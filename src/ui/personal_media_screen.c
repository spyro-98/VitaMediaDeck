#include "ui/personal_media_screen.h"
#include "app_paths.h"
#include "i18n/i18n.h"
#include "media/background_playback.h"
#include "media/hw_player_screen.h"
#include "media/video_thumbnail.h"
#include "network/personal_media.h"
#include "settings/preferences.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/image_viewer.h"
#include "ui/loading_screen.h"
#include "ui/mini_player.h"
#include "ui/music_player.h"
#include "ui/network_sources_screen.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/text_input.h"
#include "ui/theme.h"
#include "ui/touch.h"
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/rng.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vita2d.h>
#define PHOTO_PATH VITAMEDIADECK_NETWORK_DIR "/immich-preview"
#define COVER_PATH VITAMEDIADECK_NETWORK_DIR "/navidrome-cover"
static char g_music_cover_path[512];
static char g_music_cover_id[16];
static void wipe(void *p, size_t n) {
    volatile unsigned char *v = p;
    while (n--)
        *v++ = 0;
}
static int it(void) { return vt_i18n_current_language() == VT_LANG_IT; }
static const char *tr(const char *en, const char *italian) { return it() ? italian : en; }
static int section_for(VtNetworkProtocol p) {
    return p == VT_NETWORK_NAVIDROME ? UI_SECTION_NAVIDROME : UI_SECTION_IMMICH;
}
static void text(int x, int y, const char *s, int small) {
    unsigned size = small ? UI_FONT_SMALL : UI_FONT_BODY;
    vita2d_font *font = ui_runtime_font(size);
    if (font) {
        char fitted[512];
        ui_font_fit_text(font, size, s, fitted, sizeof(fitted), 932 - x);
        ui_font_draw_text(font, x, y, small ? VT_THEME_TEXT_MUTED : VT_THEME_TEXT, size, fitted);
    }
}
static int save_file(const char *path, const unsigned char *data, size_t size) {
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0600);
    if (fd < 0)
        return -1;
    size_t off = 0;
    while (off < size) {
        int n = sceIoWrite(fd, data + off, size - off);
        if (n <= 0)
            break;
        off += (size_t)n;
    }
    int close = sceIoClose(fd);
    if (off != size || close < 0) {
        sceIoRemove(path);
        return -1;
    }
    return 0;
}
typedef struct {
    const VtNetworkSource *s;
    const VtNetworkCredential *c;
    const char *path;
    unsigned char *data;
    size_t size;
    volatile int cancel;
    int full;
} ImageTask;
static int image_task(void *ctx) {
    ImageTask *t = ctx;
    return t->full ? vt_personal_preview(t->s, t->c, t->path, &t->data, &t->size, &t->cancel)
                   : vt_personal_artwork(t->s, t->c, t->path, &t->data, &t->size, &t->cancel);
}
static int fetch_image(const VtNetworkSource *s, const VtNetworkCredential *c, const char *path,
                       const char *dest, char out[512], int full) {
    ImageTask t = {.s = s, .c = c, .path = path, .full = full};
    int ret = ui_loading_run(tr("Loading image", "Caricamento immagine"), image_task, &t, &t.cancel,
                             NULL, NULL);
    out[0] = 0;
    if (ret == 0 && !t.cancel) {
        const char *extension = t.size >= 12 && !memcmp(t.data, "RIFF", 4) ? "webp"
                                : t.size >= 8 && t.data[0] == 0x89         ? "png"
                                                                           : "jpg";
        snprintf(out, 512, "%s.%s", dest, extension);
        ret = save_file(out, t.data, t.size);
        if (ret < 0)
            out[0] = 0;
    } else
        ret = -1;
    free(t.data);
    return ret;
}
typedef struct {
    VtNetworkSource source;
    VtNetworkCredential credential;
    VtNetworkEntry *entries;
    int count, selected;
} PersonalMusicQueue;
static PersonalMusicQueue g_music_queue;
void ui_personal_media_shutdown(void) {
    free(g_music_queue.entries);
    wipe(&g_music_queue, sizeof(g_music_queue));
}
static int run_music_queue(void);
static int music_fullscreen(uint64_t position, void *ctx) {
    (void)position;
    (void)ctx;
    int result = run_music_queue();
    if (result >= UI_NETWORK_ACTION_SECTION_BASE) {
        int section = result - UI_NETWORK_ACTION_SECTION_BASE;
        ui_sections_request(section);
        return VT_HW_PLAYER_ACTION_SECTION_BASE + section;
    }
    return 0;
}
typedef struct {
    PersonalMusicQueue *queue;
    const char *path;
    VtNetworkEntry *entries;
    int count;
    char detail[192];
} QueuePageTask;
static int queue_page_task(void *ctx) {
    QueuePageTask *t = ctx;
    t->count = vt_personal_list(&t->queue->source, &t->queue->credential, t->path, t->entries,
                                VT_NETWORK_MAX_ENTRIES, t->detail, sizeof(t->detail));
    return t->count < 0 ? t->count : 0;
}
static int run_music_queue(void) {
    PersonalMusicQueue *queue = &g_music_queue;
    if (!queue->entries || queue->selected < 0 || queue->selected >= queue->count)
        return 0;
    for (;;) {
        const VtNetworkEntry *song = &queue->entries[queue->selected];
        char id[16];
        vt_network_media_history_id(&queue->source, song->path, id);
        VtBackgroundPlaybackSnapshot snap = {0};
        int existing = vt_background_playback_snapshot(&snap) && !strcmp(snap.video_id, id);
        if (!existing) {
            vt_video_thumbnail_suspend();
            vt_background_playback_stop();
            if (strcmp(g_music_cover_id, id) || !g_music_cover_path[0]) {
                if (g_music_cover_path[0])
                    sceIoRemove(g_music_cover_path);
                int cover_result = fetch_image(&queue->source, &queue->credential, song->path,
                                               COVER_PATH, g_music_cover_path, 0);
                snprintf(g_music_cover_id, sizeof(g_music_cover_id), "%s",
                         cover_result == 0 ? id : "");
            }
            int ret = vt_background_playback_prepare_remote_audio(
                &queue->source, &queue->credential, song->path, id, song->name, song->runtime_ms);
            if (ret == 0) {
                vt_background_playback_set_audio_details(
                    song->artist[0] ? song->artist : "Navidrome", g_music_cover_path);
                ret = vt_background_playback_activate(0);
            }
            vt_video_thumbnail_resume();
            if (ret < 0) {
                vt_background_playback_stop();
                ui_personal_media_shutdown();
                ui_message_show("Navidrome", tr("Playback failed", "Riproduzione fallita"), 2800);
                return 0;
            }
            vt_background_playback_set_fullscreen_resume(music_fullscreen, NULL);
        }
        int result = ui_music_player_run(g_music_cover_path, song->album, song->audio_format,
                                         song->audio_bitrate_kbps);
        if (result >= UI_MUSIC_PLAYER_SECTION_BASE)
            return UI_NETWORK_ACTION_SECTION_BASE + result - UI_MUSIC_PLAYER_SECTION_BASE;
        if (result == UI_MUSIC_PLAYER_MINIMIZE)
            return 0;
        if (result == UI_MUSIC_PLAYER_STOP) {
            vt_background_playback_stop();
            ui_personal_media_shutdown();
            return 0;
        }
        if (result == UI_MUSIC_PLAYER_REPEAT) {
            vt_background_playback_stop();
            continue;
        }
        if (ui_music_player_shuffle_enabled()) {
            int choices[VT_NETWORK_MAX_ENTRIES], size = 0;
            for (int i = 0; i < queue->count; i++)
                if (i != queue->selected && queue->entries[i].is_audio)
                    choices[size++] = i;
            if (size) {
                uint32_t random = 0;
                sceKernelGetRandomNumber(&random, sizeof(random));
                queue->selected = choices[random % (unsigned)size];
                continue;
            }
        }
        int next = queue->selected + 1;
        while (next < queue->count && !queue->entries[next].is_audio)
            next++;
        if (next >= queue->count) {
            const char *path = NULL;
            for (int i = 0; i < queue->count; i++)
                if (queue->entries[i].is_directory == 2)
                    path = queue->entries[i].path;
            if (path) {
                VtNetworkEntry *entries = calloc(VT_NETWORK_MAX_ENTRIES, sizeof(*entries));
                QueuePageTask task = {.queue = queue, .path = path, .entries = entries};
                if (entries &&
                    ui_loading_run(tr("Loading next tracks", "Caricamento prossimi brani"),
                                   queue_page_task, &task, NULL, NULL, NULL) == 0 &&
                    task.count > 0) {
                    free(queue->entries);
                    queue->entries = entries;
                    queue->count = task.count;
                    queue->selected = 0;
                    while (queue->selected < queue->count &&
                           !queue->entries[queue->selected].is_audio)
                        queue->selected++;
                    if (queue->selected < queue->count)
                        continue;
                } else {
                    free(entries);
                    ui_message_show("Navidrome",
                                    tr("Could not load the next tracks",
                                       "Impossibile caricare i brani successivi"),
                                    2600);
                }
            }
            vt_background_playback_stop();
            ui_personal_media_shutdown();
            return 0;
        }
        queue->selected = next;
    }
}
int ui_personal_media_open(const VtNetworkSource *source, const VtNetworkCredential *credential,
                           const VtNetworkEntry *item, const VtNetworkEntry *page, int count,
                           int selected) {
    if (!source || !credential || !item || !page || count < 1 || count > VT_NETWORK_MAX_ENTRIES ||
        selected < 0 || selected >= count)
        return -1;
    if (item->is_image) {
        vt_video_thumbnail_suspend();
        char preview[512];
        int ret = fetch_image(source, credential, item->path, PHOTO_PATH, preview, 1);
        if (ret == 0)
            ret = ui_image_viewer_show(preview, item->name);
        else
            ui_message_show(
                "Immich",
                tr("Preview unavailable. Check API permissions and generated previews.",
                   "Anteprima non disponibile. Verifica permessi API e anteprime generate."),
                3000);
        if (preview[0])
            sceIoRemove(preview);
        vt_video_thumbnail_resume();
        return ret >= UI_IMAGE_VIEWER_SECTION_BASE
                   ? UI_NETWORK_ACTION_SECTION_BASE + ret - UI_IMAGE_VIEWER_SECTION_BASE
                   : 0;
    }
    if (!item->is_audio || source->protocol != VT_NETWORK_NAVIDROME)
        return 0;
    VtNetworkEntry *entries = malloc((size_t)count * sizeof(*entries));
    if (!entries)
        return -1;
    memcpy(entries, page, (size_t)count * sizeof(*entries));
    ui_personal_media_shutdown();
    g_music_queue = (PersonalMusicQueue){.source = *source,
                                         .credential = *credential,
                                         .entries = entries,
                                         .count = count,
                                         .selected = selected};
    return run_music_queue();
}
static int configure(VtNetworkSource *source, VtNetworkCredential *credential, int fresh,
                     VtNetworkProtocol protocol) {
    VtNetworkSource s = fresh ? (VtNetworkSource){0} : *source;
    VtNetworkCredential c = fresh ? (VtNetworkCredential){0} : *credential;
    s.protocol = protocol;
    char endpoint[1200] = "";
    if (!fresh && vt_personal_base_url(&s, endpoint, sizeof(endpoint)) < 0)
        return 0;
    if (fresh)
        snprintf(s.name, sizeof(s.name), "%s", vt_network_protocol_name(protocol));
    int ok = ui_text_input(tr("Server name", "Nome server"), s.name, s.name, sizeof(s.name));
    if (ok > 0)
        ok =
            ui_text_input(tr("Server URL (http:// or https://)", "URL server (http:// o https://)"),
                          endpoint, endpoint, sizeof(endpoint));
    if (ok > 0) {
        const char *scheme = strstr(endpoint, "://");
        const char *path = strchr(scheme ? scheme + 3 : endpoint, '/');
        size_t authority = path ? (size_t)(path - endpoint) : strlen(endpoint);
        if (authority >= sizeof(s.host) || (path && strlen(path) >= sizeof(s.root_path)))
            ok = 0;
        else {
            memcpy(s.host, endpoint, authority);
            s.host[authority] = 0;
            snprintf(s.root_path, sizeof(s.root_path), "%s", path ? path : "");
            s.port = 0;
        }
    }
    if (ok > 0 && protocol == VT_NETWORK_NAVIDROME)
        ok = ui_text_input(tr("Navidrome username", "Utente Navidrome"), s.username, s.username,
                           sizeof(s.username));
    if (ok > 0)
        ok = ui_text_input_secure(protocol == VT_NETWORK_IMMICH
                                      ? "Immich API key"
                                      : tr("Navidrome password", "Password Navidrome"),
                                  c.password, c.password, sizeof(c.password));
    char base[1400];
    if (ok > 0 && vt_personal_base_url(&s, base, sizeof(base)) < 0) {
        ui_message_show("URL", tr("Invalid server URL", "URL server non valido"), 2600);
        ok = 0;
    }
    if (ok > 0) {
        if (fresh || strcmp(s.host, source->host) || strcmp(s.root_path, source->root_path) ||
            s.port != source->port)
            s.tls_public_key_sha256[0] = 0;
        wipe(c.access_token, sizeof(c.access_token));
        wipe(c.user_id, sizeof(c.user_id));
        *source = s;
        *credential = c;
    }
    wipe(&c, sizeof(c));
    return ok > 0;
}
static int persist(VtNetworkSource *s, VtNetworkCredential *c, int n) {
    int ret = vt_network_sources_save(s, n);
    if (ret == 0 && vt_preferences_remember_network_passwords()) {
        int password_result = vt_network_credentials_save(c, n);
        if (password_result < 0) {
            vt_network_credentials_clear();
            ui_message_show(tr("Credentials not saved", "Credenziali non salvate"),
                            tr("Server saved. Enter credentials again next launch.",
                               "Server salvato. Reinserisci le credenziali al prossimo avvio."),
                            2800);
        }
    }
    if (ret < 0)
        ui_message_show(tr("Save failed", "Salvataggio fallito"),
                        tr("Configuration was not saved", "Configurazione non salvata"), 2800);
    return ret;
}
int ui_personal_media_screen(VtNetworkProtocol protocol) {
    VtNetworkSource *sources = calloc(VT_NETWORK_MAX_SOURCES, sizeof(*sources));
    VtNetworkCredential *credentials = calloc(VT_NETWORK_MAX_SOURCES, sizeof(*credentials));
    if (!sources || !credentials) {
        free(sources);
        free(credentials);
        return UI_SECTION_LOCAL_MEDIA;
    }
    int count = vt_network_sources_load(sources, VT_NETWORK_MAX_SOURCES);
    if (count < 0)
        count = 0;
    if (vt_preferences_remember_network_passwords() &&
        vt_network_credentials_load(credentials, VT_NETWORK_MAX_SOURCES) < 0)
        wipe(credentials, VT_NETWORK_MAX_SOURCES * sizeof(*credentials));
    int selected = 0, remove = 0, result = UI_SECTION_LOCAL_MEDIA;
    UiSectionsSidebar sidebar;
    ui_sections_sidebar_init(&sidebar, section_for(protocol));
    UiNavRepeat repeat;
    ui_nav_repeat_reset(&repeat);
    SceCtrlData prev = {0}, pad = {0};
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceCtrlPeekBufferPositive(0, &prev, 1);
    for (;;) {
        int map[VT_NETWORK_MAX_SOURCES], n = 0;
        for (int i = 0; i < count; i++)
            if (sources[i].protocol == protocol)
                map[n++] = i;
        if (selected >= n)
            selected = n ? n - 1 : 0;
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned pressed = pad.buttons & ~prev.buttons;
        prev = pad;
        UiTouchEvent touch;
        unsigned flags = ui_touch_poll(&touch);
        ui_mini_player_handle_buttons(&pressed);
        if (ui_mini_player_handle_touch(flags, &touch))
            flags = 0;
        if (ui_mini_player_input_locked()) {
            pressed = 0;
            pad.buttons = 0;
            pad.lx = pad.ly = 128;
            flags = 0;
        }
        if (ui_brand_sections_hit(flags, touch.x, touch.y)) {
            pressed |= SCE_CTRL_LTRIGGER;
            flags = 0;
        }
        int section = ui_sections_sidebar_handle_buttons(&sidebar, &pressed, pad.buttons, pad.ly);
        if (sidebar.open) {
            int hit = ui_sections_sidebar_handle_touch(&sidebar, flags, touch.x, touch.y);
            if (hit != UI_SECTION_NONE)
                section = hit;
            flags = 0;
        }
        ui_sections_sidebar_tick(&sidebar);
        if (section != UI_SECTION_NONE) {
            result = section;
            break;
        }
        if (!sidebar.open && sidebar.animation < 0.01f) {
            unsigned nav = ui_nav_repeat_update(&repeat, pressed, pad.buttons, pad.lx, pad.ly,
                                                SCE_CTRL_UP | SCE_CTRL_DOWN);
            if ((nav & SCE_CTRL_UP) && selected > 0) {
                selected--;
                remove = 0;
            }
            if ((nav & SCE_CTRL_DOWN) && selected + 1 < n) {
                selected++;
                remove = 0;
            }
            int top = selected / 5 * 5;
            if (flags & UI_TOUCH_EVENT_TAP)
                for (int row = 0; row < 5 && top + row < n; row++)
                    if (ui_touch_hit_rect(touch.x, touch.y, 28, 116 + row * 60, 904, 52)) {
                        selected = top + row;
                        pressed |= SCE_CTRL_CROSS;
                    }
            if (pressed & SCE_CTRL_CIRCLE)
                break;
            if ((pressed & SCE_CTRL_RTRIGGER) && n) {
                if (remove == selected + 1) {
                    int idx = map[selected];
                    VtNetworkSource removed_source = sources[idx];
                    VtNetworkCredential removed_credential = credentials[idx];
                    for (int k = idx; k + 1 < count; k++) {
                        sources[k] = sources[k + 1];
                        credentials[k] = credentials[k + 1];
                    }
                    count--;
                    wipe(&credentials[count], sizeof(credentials[count]));
                    if (persist(sources, credentials, count) < 0) {
                        for (int k = count; k > idx; k--) {
                            sources[k] = sources[k - 1];
                            credentials[k] = credentials[k - 1];
                        }
                        sources[idx] = removed_source;
                        credentials[idx] = removed_credential;
                        count++;
                    }
                    wipe(&removed_credential, sizeof(removed_credential));
                    remove = 0;
                } else
                    remove = selected + 1;
            }
            if ((pressed & SCE_CTRL_SQUARE) && count < VT_NETWORK_MAX_SOURCES) {
                VtNetworkSource s;
                VtNetworkCredential c = {0};
                if (configure(&s, &c, 1, protocol)) {
                    sources[count] = s;
                    credentials[count] = c;
                    count++;
                    if (persist(sources, credentials, count) < 0) {
                        count--;
                        wipe(&credentials[count], sizeof(credentials[count]));
                    } else
                        selected = n;
                }
                wipe(&c, sizeof(c));
                remove = 0;
            }
            if ((pressed & SCE_CTRL_TRIANGLE) && n) {
                int idx = map[selected];
                VtNetworkSource s = sources[idx];
                VtNetworkCredential c = credentials[idx];
                if (configure(&s, &c, 0, protocol)) {
                    VtNetworkSource old = sources[idx];
                    VtNetworkCredential oldc = credentials[idx];
                    sources[idx] = s;
                    credentials[idx] = c;
                    if (persist(sources, credentials, count) < 0) {
                        sources[idx] = old;
                        credentials[idx] = oldc;
                    }
                    wipe(&oldc, sizeof(oldc));
                }
                wipe(&c, sizeof(c));
                remove = 0;
            }
            if ((pressed & SCE_CTRL_CROSS) && n) {
                int idx = map[selected];
                ui_network_personal_resume_credential(&sources[idx], &credentials[idx]);
                if (!credentials[idx].password[0])
                    ui_text_input_secure(protocol == VT_NETWORK_IMMICH ? "Immich API key"
                                                                       : tr("Password", "Password"),
                                         "", credentials[idx].password,
                                         sizeof(credentials[idx].password));
                if (credentials[idx].password[0]) {
                    int ret = ui_network_browse_personal(&sources[idx], &credentials[idx]);
                    if (ret == VT_NETWORK_AUTH_FAILED) {
                        wipe(&credentials[idx], sizeof(credentials[idx]));
                        if (vt_preferences_remember_network_passwords() &&
                            vt_network_credentials_save(credentials, count) < 0)
                            vt_network_credentials_clear();
                    } else if (ret >= 0)
                        persist(sources, credentials, count);
                    if (ret >= UI_NETWORK_ACTION_SECTION_BASE) {
                        result = ret - UI_NETWORK_ACTION_SECTION_BASE;
                        break;
                    }
                }
                remove = 0;
            }
            if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_SQUARE | SCE_CTRL_TRIANGLE)) {
                sceCtrlPeekBufferPositive(0, &prev, 1);
                ui_touch_reset();
                ui_nav_repeat_reset(&repeat);
            }
        }
        ui_mini_player_pump();
        vita2d_start_drawing();
        vita2d_clear_screen();
        ui_chrome_background(VT_THEME_BG, VT_THEME_SIGNAL_BRIGHT);
        ui_brand_draw_header_placeholder(NULL, vt_network_protocol_name(protocol));
        text(28, 90,
             protocol == VT_NETWORK_NAVIDROME
                 ? tr("Your music servers", "I tuoi server musicali")
                 : tr("Your photo libraries", "Le tue raccolte fotografiche"),
             0);
        int top = selected / 5 * 5;
        for (int row = 0; row < 5 && top + row < n; row++) {
            int k = top + row, y = 116 + row * 60;
            ui_panel(28, y, 904, 52, VT_THEME_SURFACE,
                     k == selected ? VT_THEME_FOCUS : VT_THEME_BORDER, k == selected);
            text(46, y + 25, sources[map[k]].name, 0);
            text(46, y + 45, sources[map[k]].host, 1);
        }
        if (!n)
            text(46, 170,
                 tr("Press Square to add a server", "Premi Quadrato per aggiungere un server"), 0);
        text(28, 442,
             tr("X Connect    Square Add    Triangle Edit",
                "X Collega    Quadrato Aggiungi    Triangolo Modifica"),
             1);
        text(28, 466,
             remove
                 ? tr("R1 again: remove this connection", "Ancora R1: rimuovi questo collegamento")
                 : tr("Circle Back    R1 Remove connection    L1 Sections",
                      "Cerchio Indietro    R1 Rimuovi collegamento    L1 Sezioni"),
             1);
        ui_mini_player_draw();
        if (sidebar.open || sidebar.animation > 0.01f)
            ui_sections_sidebar_draw(&sidebar);
        vita2d_end_drawing();
        vita2d_swap_buffers();
        sceKernelDelayThread(1000);
    }
    wipe(credentials, VT_NETWORK_MAX_SOURCES * sizeof(*credentials));
    free(credentials);
    free(sources);
    return result;
}
