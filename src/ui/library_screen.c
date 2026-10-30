#include "ui/library_screen.h"
#include "ui/home_dashboard.h"
#include "library/catalog.h"
#include "library/search.h"
#include "i18n/i18n.h"
#include "ui/home_tabs.h"
#include "ui/components.h"
#include "ui/navigation.h"
#include "ui/theme_scene.h"
#include "ui/brand.h"
#include "ui/mini_player.h"
#include "ui/runtime.h"
#include "ui/text_input.h"
#include "ui/network_sources_screen.h"
#include "ui/loading_screen.h"
#include "media/video_thumbnail.h"
#include "settings/preferences.h"
#include "ui/personal_media_screen.h"
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static const char *tr(const char *en, const char *it) {
    return vt_i18n_text(en,it);
}
static int view = 4;
static char query[129];
typedef struct {
    VtLibraryItem item;
    int selected;
} LibraryParent;
static struct {
    int selected, top, focus, filter, depth, in_folder, valid;
    VtLibraryItem folder;
    LibraryParent parents[8];
} navigation;
static const char *view_label(int v) {
    return v == 0   ? tr("Continue watching / listening", "Continua a guardare / ascoltare")
           : v == 1 ? tr("Playback history", "Cronologia di riproduzione")
           : v == 2 ? tr("Your sources", "Le tue sorgenti")
                    : tr("Global search", "Ricerca globale");
}
static const int sections[] = {-1,
                               UI_SECTION_JELLYFIN,
                               UI_SECTION_NAVIDROME,
                               UI_SECTION_IMMICH,
                               UI_SECTION_IPTV,
                               UI_SECTION_ONLINE_AUDIO,
                               UI_SECTION_ONLINE_AUDIO,
                               UI_SECTION_NETWORK};
static const char *source_name(int i) {
    static const char *names[] = {"Local files", "Jellyfin", "Navidrome", "Immich",
                                  "IPTV",        "Radio",    "Podcast",   "Network files"};
    return i == 0 ? tr(names[0], "File locali") : i == 7 ? tr(names[7], "File di rete") : names[i];
}
static void source_results(VtLibraryResult *items, int *n) {
    *n = 8;
    for (int i = 0; i < 8; i++) {
        items[i] = (VtLibraryResult){0};
        snprintf(items[i].item.id, 16, "source_%d", i);
        snprintf(items[i].item.title, sizeof(items[i].item.title), "%s", source_name(i));
        items[i].item.kind = VT_LIBRARY_FOLDER;
        items[i].item.mode = i;
        items[i].item.provider = i == 0   ? VT_LIBRARY_LOCAL
                                 : i == 1 ? VT_LIBRARY_JELLYFIN
                                 : i == 2 ? VT_LIBRARY_NAVIDROME
                                 : i == 3 ? VT_LIBRARY_IMMICH
                                 : i == 4 ? VT_LIBRARY_IPTV
                                 : i == 5 ? VT_LIBRARY_RADIO
                                 : i == 6 ? VT_LIBRARY_PODCAST
                                          : VT_LIBRARY_NETWORK;
    }
}
static const char *media_label(int kind) {
    switch (kind) {
    case VT_LIBRARY_VIDEO:
        return "VIDEO";
    case VT_LIBRARY_AUDIO:
        return "AUDIO";
    case VT_LIBRARY_IMAGE:
        return tr("PHOTO", "FOTO");
    case VT_LIBRARY_LIVE:
        return "LIVE";
    case VT_LIBRARY_FEED:
        return "FEED";
    default:
        return "DIR";
    }
}
static void describe(const VtLibraryResult *v, int searching, char *out, size_t size) {
    char time[72] = "";
    uint64_t seconds = v->position_ms / 1000, duration = v->duration_ms / 1000;
    if (v->item.kind != VT_LIBRARY_LIVE && v->item.kind != VT_LIBRARY_IMAGE) {
        if (v->started)
            snprintf(time, sizeof(time), "%llu:%02llu", (unsigned long long)(seconds / 60),
                     (unsigned long long)(seconds % 60));
        if (duration) {
            size_t used = strlen(time);
            snprintf(time + used, sizeof(time) - used, "%s%llu:%02llu%s", used ? " / " : "",
                     (unsigned long long)(duration / 60), (unsigned long long)(duration % 60),
                     v->started && seconds >= duration ? tr(" completed", " completato") : "");
        }
    }
    /* Keep useful metadata ahead of the longer connection name. Covers carry
     * the media type; the first detail field identifies the provider group. */
    snprintf(out, size, "%s%s%s%s%s%s%s%s%s%s", vt_library_provider_name(v->item.provider),
             v->item.artist[0] ? " · " : "", v->item.artist, v->item.album[0] ? " · " : "",
             v->item.album, time[0] ? " · " : "", time,
             searching && v->cached ? tr(" · saved", " · cache") : "",
             !v->item.artist[0] && !v->item.album[0] && v->item.server_name[0] ? " · " : "",
             !v->item.artist[0] && !v->item.album[0] ? v->item.server_name : "");
}
static void search_detail(const VtLibrarySearchStatus *s, int count, int filter, char *out,
                          size_t size) {
    const char *group =
        filter >= 0 ? vt_library_provider_name(filter) : tr("All sources", "Tutte le sorgenti");
    if (s->pending) {
        snprintf(
            out, size,
            tr("%s · %d results · searching (%d)", "%s · %d risultati · ricerca in corso (%d)"),
            group, count, s->pending);
        return;
    }
    for (int i = 0; i < VT_LIBRARY_PROVIDER_COUNT; i++)
        if ((filter < 0 || filter == i) &&
            (s->state[i] == VT_SEARCH_LOGIN || s->state[i] == VT_SEARCH_ERROR)) {
            snprintf(out, size, "%d · %s: %s", count, vt_library_provider_name(i),
                     s->state[i] == VT_SEARCH_LOGIN
                         ? tr("sign in from Sources", "accedi da Sorgenti")
                         : tr("unavailable · saved results", "non disponibile · cache"));
            return;
        }
    snprintf(out, size,
             tr("%s · %d results · " UI_PAD_SQUARE ": provider", "%s · %d risultati · " UI_PAD_SQUARE ": provider"),
             group, count);
}
/* One compact eight-source rail: existing theme geometry, no textures, timers,
 * allocations or percentage claims. Only pending sources animate. */
static void search_activity(const VtLibrarySearchStatus *s, int filter, int x, int y, int width,
                            uint64_t now) {
    int theme = ui_theme_id();
    float phase = vt_preferences_reduce_motion() ? .375f : (now % 1200000ULL) / 1200000.0f;
    int cell = width / VT_LIBRARY_PROVIDER_COUNT;
    for (int i = 0; i < VT_LIBRARY_PROVIDER_COUNT; i++) {
        int state = s->state[i], active = state == VT_SEARCH_LOADING || s->updating[i] > 0;
        float px = x + i * cell, w = cell - 5;
        unsigned color = state == VT_SEARCH_ERROR || state == VT_SEARCH_LOGIN ? VT_THEME_WARNING
                         : state == VT_SEARCH_READY ? VT_THEME_SIGNAL_BRIGHT
                                                    : VT_THEME_BORDER;
        if (active)
            color = VT_THEME_COLD;
        if (filter >= 0 && filter != i)
            color = ui_theme_alpha(color, 70);
        float travel = active ? phase : .5f;
        if (theme == VT_UI_THEME_GHOST_MESH) {
            vita2d_draw_line(px + 13, y, px + w, y, ui_theme_alpha(color, 120));
            for (int k = 0; k < 8; k++) {
                float angle = k * .78539816f, next = angle + .5f;
                unsigned c = active && k != (int)(travel * 8) ? ui_theme_alpha(color, 65) : color;
                vita2d_draw_line(px + 5 + cosf(angle) * 4, y + sinf(angle) * 4,
                                 px + 5 + cosf(next) * 4, y + sinf(next) * 4, c);
            }
        } else if (theme == VT_UI_THEME_DECK_90) {
            for (int k = 0; k < 4; k++)
                vita2d_draw_rectangle(px + k * w / 4, y - 3, w / 4 - 2, 5,
                                      active && k != (int)(travel * 4) ? ui_theme_alpha(color, 65)
                                                                       : color);
        } else if (theme == VT_UI_THEME_SIGNAL) {
            for (int k = 0; k < 5; k++) {
                unsigned c = active && k != (int)(travel * 5) ? ui_theme_alpha(color, 65) : color;
                float at = px + k * w / 5;
                vita2d_draw_line(at, y + 3, at + 5, y - 3, c);
            }
        } else if (theme == VT_UI_THEME_APERTURE) {
            vita2d_draw_line(px, y, px + w, y, ui_theme_alpha(color, 90));
            for (int k = 0; k < 4; k++)
                vita2d_draw_rectangle(px + k * w / 4, y - 3, w / 4 - 4, 6,
                                      active && k != (int)(travel * 4) ? ui_theme_alpha(color, 65)
                                                                       : color);
        } else if (theme == VT_UI_THEME_RELAY) {
            vita2d_draw_line(px, y, px + w, y, ui_theme_alpha(color, 100));
            vita2d_draw_rectangle(px + travel * (w - 5), y - 2, 5, 5, color);
        } else if (theme == VT_UI_THEME_MOSAIC) {
            for (int k = 0; k < 3; k++)
                vita2d_draw_rectangle(px + k * w / 3, y - 3, w / 3 - 3, 6,
                                      active && k != (int)(travel * 3) ? ui_theme_alpha(color, 65)
                                                                       : color);
        } else {
            vita2d_draw_rectangle(px, y, w, 2, ui_theme_alpha(color, 90));
            vita2d_draw_rectangle(px + travel * (w - 12), y - 1, active ? 12 : w / 2, 3, color);
        }
        if (filter == i)
            vita2d_draw_rectangle(px, y + 5, w, 1, VT_THEME_TEXT);
    }
}
static UiThemeRect heading_rect(int theme, int bottom) {
    if (vt_ui_theme_legacy_layout(theme))
        return (UiThemeRect){18, 62, 924, 46};
    UiThemeHomeLayout l = ui_theme_home_layout(theme, bottom);
    int baseline = theme == VT_UI_THEME_SIGNAL       ? 162
                   : theme == VT_UI_THEME_MOSAIC     ? 151
                   : theme == VT_UI_THEME_GHOST_MESH ? 122
                                                     : 113;
    int size = theme == VT_UI_THEME_SIGNAL || theme == VT_UI_THEME_APERTURE ||
                       theme == VT_UI_THEME_GHOST_MESH
                   ? UI_FONT_HERO
                   : UI_FONT_DISPLAY;
    if (theme == VT_UI_THEME_MOSAIC)
        l.w = 912;
    if (theme == VT_UI_THEME_RELAY)
        l.w = 624;
    return (UiThemeRect){l.x, baseline - size, l.w, size + 34};
}
static int authorize_folder(const VtLibraryItem *v) {
    if (!v->server[0] || v->provider == VT_LIBRARY_PODCAST)
        return 0;
    VtNetworkSource source;
    VtNetworkCredential credential = {0};
    int ret = vt_library_source(v, &source, &credential);
    if (ret >= 0)
        ret = ui_network_library_authorize(&source, &credential);
    memset(&credential, 0, sizeof(credential));
    return ret;
}
typedef struct {
    VtNetworkSource *sources;
    VtNetworkCredential *credentials;
    char keys[VT_NETWORK_MAX_SOURCES][16];
    int count,valid;
} LibraryArtworkSources;
static LibraryArtworkSources artwork_sources(void) {
    LibraryArtworkSources a = {0};
    a.sources = calloc(VT_NETWORK_MAX_SOURCES, sizeof(*a.sources));
    a.credentials = calloc(VT_NETWORK_MAX_SOURCES, sizeof(*a.credentials));
    if (a.sources && a.credentials) {
        a.count = vt_network_sources_load(a.sources, VT_NETWORK_MAX_SOURCES);
        a.valid=a.count>=0;
        if (a.count < 0)
            a.count = 0;
        if (vt_preferences_remember_network_passwords())
            vt_network_credentials_load(a.credentials, VT_NETWORK_MAX_SOURCES);
        for (int i = 0; i < a.count; i++) {
            vt_network_media_history_id(&a.sources[i], "", a.keys[i]);
            ui_network_personal_resume_credential(&a.sources[i], &a.credentials[i]);
        }
    }
    return a;
}
static vita2d_texture *remote_artwork(LibraryArtworkSources *a, const VtLibraryItem *v,
                                      int priority) {
    if (v->provider == VT_LIBRARY_PODCAST || !v->server[0])
        return NULL;
    for (int i = 0; i < a->count; i++)
        if (!strcmp(v->server, a->keys[i])) {
            vt_library_search_get_session(&a->sources[i], &a->credentials[i]);
            return vt_video_thumbnail_get_remote_priority(&a->sources[i], &a->credentials[i],
                                                          v->locator, v->size, priority);
        }
    return NULL;
}
/* One Home composition. Full collections/search are deliberate drill-downs,
 * not alternate Home pages. No storage or transport calls in drawing. */
enum { LIBRARY_HOME = 4, LIBRARY_OPEN_DETAIL = -10, LIBRARY_RETURN_HOME = -11 };
#define DASH_CONTINUE_MAX 8
#define DASH_RECENT_MAX 16
typedef struct {
    VtLibraryResult resume[DASH_CONTINUE_MAX], recent[DASH_RECENT_MAX];
    VtLibraryItem favourites[VT_LIBRARY_HOME_FAVOURITES], connections[VT_NETWORK_MAX_SOURCES];
    int count[4], resuming, discovering;
} DashboardData;
static int dash_selected, dash_offset[4];
static char dash_anchor[4][16];
static int dashboard_folder;
static const char *dashboard_label(int role) {
    switch (role) {
    case UI_DASH_CONTINUE:
        return tr("Pick up where you left off", "Riprendi da dove eri");
    case UI_DASH_CONNECTIONS:
        return tr("Saved connections", "Connessioni salvate");
    case UI_DASH_FAVOURITES:
        return tr("Favourites", "Preferiti");
    case UI_DASH_RECENT:
        return tr("Recently played", "Riprodotti di recente");
    case UI_DASH_SOURCES:
        return tr("Explore sources", "Esplora le sorgenti");
    default:
        return tr("Playback history", "Cronologia");
    }
}
static int dashboard_item(const DashboardData *d, int role, int offset, VtLibraryResult *out) {
    memset(out, 0, sizeof(*out));
    if (role >= 4 || d->count[role] <= 0)
        return 0;
    int i = offset % d->count[role];
    if (i < 0)
        i += d->count[role];
    if (role == UI_DASH_CONTINUE)
        *out = d->resume[i];
    else if (role == UI_DASH_RECENT)
        *out = d->recent[i];
    else
        out->item = role == UI_DASH_CONNECTIONS ? d->connections[i] : d->favourites[i];
    return 1;
}
static void dashboard_refresh(DashboardData *d, VtLibrarySearchStatus *status, int keep_focus) {
    d->count[0] = vt_library_query("", VT_LIBRARY_CONTINUE, d->resume, DASH_CONTINUE_MAX);
    d->resuming = d->count[0] > 0;
    d->discovering = 0;
    int recent = vt_library_query("", VT_LIBRARY_RECENT, d->recent, DASH_RECENT_MAX);
    d->count[3]=recent;
    d->count[2] =
        vt_library_search_home_favourites(d->favourites, VT_LIBRARY_HOME_FAVOURITES, status);
    /* A new install or an empty resume queue must still show real media.
     * Use cached worker metadata, never a synchronous provider fetch. */
    VtLibraryResult picks[DASH_RECENT_MAX];
    int picks_count = vt_library_search_home_media(picks, DASH_RECENT_MAX);
    if (!d->count[0]) {
        for (int i = 0; i < recent && d->count[0] < DASH_CONTINUE_MAX; i++)
            d->resume[d->count[0]++] = d->recent[i];
        for (int i = 0; i < picks_count && d->count[0] < DASH_CONTINUE_MAX; i++) {
            int duplicate = 0;
            for (int k = 0; k < d->count[0]; k++)
                duplicate |= !strcmp(picks[i].item.id, d->resume[k].item.id);
            if (!duplicate)
                d->resume[d->count[0]++] = picks[i];
        }
    }
    if (!d->count[3]) {
        d->discovering = 1;
        for (int i = 0; i < picks_count && d->count[3] < DASH_RECENT_MAX; i++)
            if (!d->count[0] || strcmp(picks[i].item.id, d->resume[0].item.id))
                d->recent[d->count[3]++] = picks[i];
    }
    for (int role = 0; role < 4; role++) {
        int selected=0;
        if(keep_focus&&dash_anchor[role][0])for(int i=0;i<d->count[role];i++){
            VtLibraryResult item;dashboard_item(d,role,i,&item);
            if(!strcmp(item.item.id,dash_anchor[role])){selected=i;break;}
        }
        dash_offset[role]=selected;
        VtLibraryResult item;
        if (dashboard_item(d, role, dash_offset[role], &item))
            snprintf(dash_anchor[role], sizeof(dash_anchor[role]), "%s", item.item.id);
        else
            dash_anchor[role][0] = 0;
    }
}
static vita2d_texture *dashboard_cover(LibraryArtworkSources *sources, const VtLibraryItem *item,
                                       int priority) {
    if (item->artwork[0] && strcmp(item->artwork, "-"))
        return vt_artwork_thumbnail_get(item->artwork);
    if (item->artwork_url[0])
        vt_library_search_cover(item);
    if (item->provider == VT_LIBRARY_LOCAL && item->kind == VT_LIBRARY_IMAGE)
        return vt_image_thumbnail_get(item->locator, item->size);
    if (item->provider == VT_LIBRARY_LOCAL && item->kind == VT_LIBRARY_VIDEO)
        return vt_video_thumbnail_get(item->locator, item->size);
    if (item->kind == VT_LIBRARY_VIDEO || item->kind == VT_LIBRARY_IMAGE)
        return remote_artwork(sources, item, priority);
    return NULL;
}
static void dashboard_backdrop(UiThemeRect r, vita2d_texture *art) {
    vita2d_draw_rectangle(r.x, r.y, r.w, r.h, VT_THEME_SURFACE_RAISED);
    if (art) {
        float w = vita2d_texture_get_width(art), h = vita2d_texture_get_height(art);
        if (w > 0 && h > 0) {
            float scale = fmaxf(r.w / w, r.h / h);
            vita2d_set_clip_rectangle(r.x, r.y, r.x + r.w, r.y + r.h);
            vita2d_enable_clipping();
            vita2d_draw_texture_scale(art, r.x + (r.w - w * scale) * .5f,
                                     r.y + (r.h - h * scale) * .5f, scale, scale);
            vita2d_disable_clipping();
        }
    } else {
        /* Original vector media motif; useful even before artwork is cached. */
        for (int i = 0; i < 12; i++) {
            int h = (int)(r.h * (.15f + (i % 5) * .08f));
            vita2d_draw_rectangle(r.x + r.w * .5f + i * r.w / 26, r.y + (r.h - h) / 2,
                                  r.w / 40, h, VT_THEME_COLD_DIM);
        }
    }
    /* Bounded, static contrast veil: 18 quads, no offscreen texture/blur. */
    for (int i = 0; i < 18; i++) {
        int y = r.y + r.h * i / 18, next = r.y + r.h * (i + 1) / 18;
        unsigned alpha = 50 + (unsigned)(i * 11);
        vita2d_draw_rectangle(r.x, y, r.w, next - y, RGBA8(0, 0, 0, alpha));
    }
}
static int dashboard_favourite_size(UiThemeRect r, int count) {
    int previews = count < 3 ? count : 3;
    int size = r.h >= 150 ? r.h - 78 : r.h - 42;
    int max = r.h >= 150 ? 96 : 64;
    if (size > max) size = max;
    if (previews && size > (r.w - 20) / previews - 12)
        size = (r.w - 20) / previews - 12;
    return size;
}
static void dashboard_card(UiThemeRect r, int role, const VtLibraryResult *v, int available,
                           int count, int offset, int selected, LibraryArtworkSources *sources,
                           const DashboardData *data) {
    if (r.h <= 0)
        return;
    const char *heading = dashboard_label(role);
    if (role == UI_DASH_CONTINUE && !data->resuming)
        heading = available ? tr("From your library", "Dalla tua libreria")
                            : tr("Make yourself at home", "La tua libreria inizia qui");
    if (role == UI_DASH_RECENT && data->discovering)
        heading = tr("Explore your library", "Scopri la tua libreria");
    if (role >= 4) {
        ui_action_button(r.x, r.y, r.w, r.h, VT_THEME_SURFACE, "", heading, selected);
        return;
    }
    if (role == UI_DASH_CONTINUE) {
        vita2d_texture *art = available ? dashboard_cover(sources, &v->item, 100) : NULL;
        ui_theme_scene_card(UI_SCENE_HOME, r, selected, role);
        UiThemeRect image = {r.x + 4, r.y + 4, r.w - 8, r.h - 8};
        dashboard_backdrop(image, art);
        int compact = r.h < 125;
        ui_text_line(r.x + 14, r.y + 22, r.w - 28, heading,
                     RGBA8(208, 233, 238, 255), UI_FONT_SMALL);
        const char *title = available ? v->item.title
                                      : tr("What would you like to play?", "Cosa vuoi guardare o ascoltare?");
        int title_y = compact ? r.y + 47 : r.y + r.h -
            (ui_theme_id()==VT_UI_THEME_LEGACY_VITA?94:83);
        ui_text_line(r.x + 14, title_y, r.w - 28, title, RGBA8(255, 255, 255, 255),
                     r.w > 440 && !compact ? UI_FONT_DISPLAY : UI_FONT_BODY);
        if (!compact) {
            char detail[512];
            if (available)
                describe(v, 0, detail, sizeof(detail));
            else
                snprintf(detail, sizeof(detail), "%s",
                         tr("Open local files or connect a server. Your media will appear here.",
                            "Apri i file locali o collega un server: qui troverai i tuoi contenuti."));
            ui_text_line(r.x + 14, title_y + 23, r.w - 28, detail,
                         RGBA8(213, 223, 232, 255), UI_FONT_SMALL);
        }
        int native_home=ui_theme_id()==VT_UI_THEME_LEGACY_VITA;
        int button_height=native_home?36:28;
        int button_y = r.y + r.h - button_height - 11;
        const char *action = !available ? tr("Browse sources", "Esplora sorgenti")
                             : data->resuming ? tr("Continue", "Riprendi")
                                              : tr("Open", "Apri");
        int button_width = !available ? (r.w > 320 ? 224 : r.w - 24) : (r.w > 320 ? 160 : 130);
        ui_action_button(r.x + 12, button_y, button_width, button_height, VT_THEME_SURFACE, UI_PAD_CROSS, action, selected);
        if (count > 1) {
            char more[64];
            snprintf(more, sizeof(more), UI_PAD_SQUARE "  %d/%d  " UI_PAD_R1, offset + 1, count);
            ui_text_line(r.x + r.w - 160, r.y + r.h - 20, 146, more,
                         RGBA8(232, 240, 246, 255), UI_FONT_SMALL);
        }
        if (available && data->resuming && v->duration_ms && v->position_ms) {
            float fraction = fminf(1, (float)v->position_ms / (float)v->duration_ms);
            vita2d_draw_rectangle(r.x, r.y + r.h - 3, r.w, 3, VT_THEME_BORDER_DIM);
            vita2d_draw_rectangle(r.x, r.y + r.h - 3, r.w * fraction, 3, VT_THEME_SIGNAL_BRIGHT);
        }
        return;
    }
    /* Collections have different visual roles: server rows, cover shelf and
     * activity rows. No repeated giant placeholders for empty categories. */
    int compact = r.h < 62;
    ui_text_line(r.x + 8, r.y + 20, compact ? 68 : r.w - 16,
                 compact ? tr("Library", "Libreria") : heading, VT_THEME_COLD_LIGHT, UI_FONT_SMALL);
    if (role == UI_DASH_FAVOURITES && available && r.w >= 300 && r.h >= 66) {
        int size = dashboard_favourite_size(r, count);
        int previews = count < 3 ? count : 3;
        int cell = (r.w - 20) / previews;
        for (int k = 0; k < previews; k++) {
            const VtLibraryItem *item = &data->favourites[(offset + k) % count];
            vita2d_texture *cover = dashboard_cover(sources, item, selected ? 90 : 40);
            int x = r.x + 10 + cell * k;
            ui_artwork_tile(x, r.y + 30, size, cover, media_label(item->kind));
            if (!k)
                vita2d_draw_rectangle(x, r.y + 30 + size + 2, size, 2,
                                      selected ? VT_THEME_SIGNAL_BRIGHT : VT_THEME_COLD_DIM);
            if (r.h >= 150) {
                ui_text_line(x, r.y + 30 + size + 19, cell - 8, item->title,
                             VT_THEME_TEXT, UI_FONT_SMALL);
                ui_text_line(x, r.y + 30 + size + 39, cell - 8,
                             vt_library_provider_name(item->provider), VT_THEME_TEXT_MUTED,
                             UI_FONT_SMALL);
            } else if (cell > size + 70) {
                ui_text_line(x + size + 8, r.y + 49, cell - size - 14, item->title,
                             VT_THEME_TEXT, UI_FONT_SMALL);
                ui_text_line(x + size + 8, r.y + 69, cell - size - 14,
                             vt_library_provider_name(item->provider), VT_THEME_TEXT_MUTED,
                             UI_FONT_SMALL);
            }
        }
        return;
    }
    if (!available) {
        ui_text_line(r.x + 8, r.y + (compact ? 40 : 49), r.w - 16,
                     role == UI_DASH_CONNECTIONS ? tr("Add a server", "Collega un server")
                     : role == UI_DASH_FAVOURITES ? tr("Explore and save favourites", "Esplora e salva preferiti")
                                                  : tr("Open playback history", "Apri la cronologia"),
                     VT_THEME_TEXT_MUTED, UI_FONT_SMALL);
        return;
    }
    int rows = (r.h - 27) / 48;
    if (rows < 1)
        rows = 1;
    if (rows > 3)
        rows = 3;
    if (rows > count)
        rows = count;
    for (int k = 0; k < rows; k++) {
        VtLibraryResult item;
        dashboard_item(data, role, offset + k, &item);
        int y = compact ? r.y + 4 : r.y + 28 + k * 48;
        int size = compact ? r.h - 8 : 40;
        if (size > 40)
            size = 40;
        int x = r.x + 8, tx = x + size + 10, tw = r.w - size - 26;
        if (compact) {
            tx = x + 82 + size;
            tw = r.x + r.w - tx - 8;
            x += 76;
        }
        if (selected && !k)
            vita2d_draw_rectangle(r.x + 3, y, 2, size, VT_THEME_SIGNAL_BRIGHT);
        if (role == UI_DASH_CONNECTIONS)
            ui_theme_service_icon(x, y, size, size,
                                   item.item.provider == VT_LIBRARY_IMMICH ? 2 : 1);
        else
            ui_artwork_tile(x, y, size, dashboard_cover(sources, &item.item, selected ? 90 : 40),
                             media_label(item.item.kind));
        ui_text_line(tx, y + 16, tw, item.item.title, VT_THEME_TEXT, UI_FONT_SMALL);
        ui_text_line(tx, y + 35, tw, vt_library_provider_name(item.item.provider),
                     VT_THEME_TEXT_MUTED, UI_FONT_SMALL);
    }
}
static int dashboard_search(void) {
    char text[129] = "";
    if (ui_text_input(tr("Search all your media", "Cerca in tutti i tuoi contenuti"), text, text,
                      sizeof(text)) <= 0)
        return 0;
    snprintf(query, sizeof(query), "%s", text);
    view = 3;
    navigation.valid = 0;
    dashboard_folder = 0;
    return 1;
}
static void dashboard_connections(DashboardData *data,const LibraryArtworkSources *artwork){
 data->count[UI_DASH_CONNECTIONS]=0;
 for(int i=artwork->count-1;i>=0;i--){
  VtNetworkEntry entry={.is_directory=1};
  snprintf(entry.name,sizeof(entry.name),"%s",artwork->sources[i].name[0]?artwork->sources[i].name:artwork->sources[i].host);
  snprintf(entry.path,sizeof(entry.path),"%s",artwork->sources[i].root_path[0]?artwork->sources[i].root_path:"/");
  vt_library_remote(&artwork->sources[i],&entry,"",&data->connections[data->count[UI_DASH_CONNECTIONS]++]);
 }
}
static void artwork_sources_free(LibraryArtworkSources *a){
 if(a->credentials){memset(a->credentials,0,VT_NETWORK_MAX_SOURCES*sizeof(*a->credentials));free(a->credentials);}
 free(a->sources);memset(a,0,sizeof(*a));
}
static int dashboard_screen(VtLibraryItem *chosen) {
    DashboardData *data = calloc(1, sizeof(*data));
    if (!data)
        return UI_LIBRARY_BACK;
    unsigned sources_revision=vt_network_sources_revision();
    LibraryArtworkSources artwork = artwork_sources();
    dashboard_connections(data,&artwork);
    /* A new visit shows the latest leading items, not yesterday's carousel
     * anchors. Navigation within this visit still preserves visible identities. */
    memset(dash_offset,0,sizeof(dash_offset));memset(dash_anchor,0,sizeof(dash_anchor));
    UiSectionsSidebar sidebar;
    ui_sections_sidebar_init(&sidebar, UI_SECTION_LOCAL_MEDIA);
    UiFocusMotion tabs = {0}, focus_motion = {0};
    ui_home_controls_reset();
    ui_touch_reset();
    vt_video_thumbnail_resume();
    vt_library_search_begin("", NULL);
    SceCtrlData previous = {0}, ctrl = {0};
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceCtrlPeekBufferPositive(0, &previous, 1);
    VtLibrarySearchStatus status = {0};
    unsigned revision = ~0U, search_revision = ~0U;
    uint64_t refreshed = 0,sources_retry_after=0;
    int first_refresh=1;
    int dirty = 1, focus_sections = 0, in_collection=0, result = UI_LIBRARY_BACK;
    for (;;) {
        uint64_t now = sceKernelGetProcessTimeWide();
        ui_mini_player_pump();
        vt_video_thumbnail_pump();
        vt_library_search_cover_view_begin();
        if (ui_local_media_library_tick()) {
            vt_library_search_local_refresh();
            dirty = 1;
        }
        int sources_changed=0;
        unsigned live_sources_revision=vt_network_sources_revision();
        if(live_sources_revision!=sources_revision&&now>=sources_retry_after){
            LibraryArtworkSources next=artwork_sources();
            if(next.valid){
                artwork_sources_free(&artwork);artwork=next;dashboard_connections(data,&artwork);
                sources_revision=live_sources_revision;sources_changed=1;sources_retry_after=0;
            }else{artwork_sources_free(&next);sources_retry_after=now+5000000ULL;}
        }
        unsigned live_revision=vt_library_revision(),live_search_revision=vt_library_search_revision();
        int changed=revision!=live_revision||search_revision!=live_search_revision||sources_changed;
        if(dirty||changed||(status.pending&&now-refreshed>250000ULL)){
            if(changed)for(int role=0;role<4;role++)if(first_refresh||!in_collection||focus_sections||role!=dash_selected){
                dash_anchor[role][0]=0;dash_offset[role]=0;
            }
            dashboard_refresh(data,&status,!first_refresh);
            revision=live_revision;search_revision=status.revision;refreshed=now;
            dirty=0;first_refresh=0;
        }
        int bottom =
            ui_mini_player_top() < UI_CONTENT_BOTTOM ? ui_mini_player_top() : UI_CONTENT_BOTTOM;
        UiDashboardLayout layout = ui_dashboard_compose(ui_theme_id(), bottom, data->count);
        if(dash_selected<0 || dash_selected>=UI_DASH_COUNT || layout.cards[dash_selected].h<=0)
            dash_selected=UI_DASH_CONTINUE;
        sceCtrlPeekBufferPositive(0, &ctrl, 1);
        unsigned pressed = ctrl.buttons & ~previous.buttons;
        previous = ctrl;
        UiTouchEvent touch;
        unsigned flags = ui_touch_poll(&touch);
        ui_mini_player_handle_buttons(&pressed);
        if (ui_mini_player_input_locked()) {
            pressed = flags = 0;
            ctrl.buttons = 0;
            ctrl.lx = ctrl.ly = ctrl.rx = ctrl.ry = 128;
        }
        if (ui_brand_sections_hit(flags, touch.x, touch.y)) {
            pressed |= SCE_CTRL_LTRIGGER;
            flags = 0;
        }
        int was_open = sidebar.open;
        int section = ui_sections_sidebar_handle_buttons(&sidebar, &pressed, ctrl.buttons, ctrl.ly);
        int blocked = was_open || sidebar.open || sidebar.animation > .01f;
        if (blocked) {
            int s = ui_sections_sidebar_handle_touch(&sidebar, flags, touch.x, touch.y);
            if (s != UI_SECTION_NONE)
                section = s;
            flags = 0;
        } else if (ui_mini_player_handle_touch(flags, &touch))
            flags = 0;
        ui_sections_sidebar_tick(&sidebar);
        if (section != UI_SECTION_NONE) {
            result = UI_LIBRARY_SECTION_BASE + section;
            break;
        }
        UiHomeControlSample sample = {.pressed = pressed,
                                      .held = ctrl.buttons,
                                      .touch_flags = flags,
                                      .lx = ctrl.lx,
                                      .ly = ctrl.ly,
                                      .rx = ctrl.rx,
                                      .ry = ctrl.ry,
                                      .selected = dash_selected,
                                      .count = UI_DASH_COUNT,
                                      .bottom = bottom,
                                      .focus_sections = focus_sections,
                                      .blocked = blocked,
                                      .geometric_content = 1,
                                      .touch = &touch};
        UiHomeControlResult controls = ui_home_controls_update(&sample);
        pressed = controls.pressed;
        if(sample.focus_sections&&!controls.focus_sections)dash_selected=UI_DASH_CONTINUE;
        focus_sections = controls.focus_sections;
        if (controls.sections_changed) {
            result = UI_LIBRARY_TAB;
            break;
        }
        unsigned nav = controls.content_nav;
        if (blocked) {
            pressed = nav = flags = 0;
        }
        if(ui_brand_home_search_hit(flags,touch.x,touch.y)){pressed|=SCE_CTRL_TRIANGLE;flags=0;}
        if (pressed & SCE_CTRL_TRIANGLE) {
            if (dashboard_search()) {
                result = LIBRARY_OPEN_DETAIL;
                break;
            }
            sceCtrlPeekBufferPositive(0, &previous, 1);
            ui_home_controls_reset();
            ui_touch_reset();
            continue;
        }
        if (pressed & SCE_CTRL_CIRCLE) {
            if(in_collection)in_collection=0;
            else focus_sections = 1;
        }
        if(focus_sections || (dash_selected<4&&!data->count[dash_selected]))in_collection=0;
        if(!focus_sections && in_collection && nav && dash_selected<4) {
            int count=data->count[dash_selected];
            int previous=!!(nav&(SCE_CTRL_LEFT|SCE_CTRL_UP));
            if(count>1) {
                dash_offset[dash_selected]=(dash_offset[dash_selected]+count+(previous?-1:1))%count;
                VtLibraryResult item;dashboard_item(data,dash_selected,dash_offset[dash_selected],&item);
                snprintf(dash_anchor[dash_selected],16,"%s",item.item.id);
                dirty=1;
            }
            nav=0;
        }
        if (!focus_sections && nav) {
            int next = ui_dashboard_move(&layout, dash_selected, nav);
            if (next == dash_selected && ui_theme_id() != VT_UI_THEME_GHOST_MESH) {
                int leave =
                    ui_theme_id() == VT_UI_THEME_DECK_90 || ui_theme_id() == VT_UI_THEME_RELAY
                        ? !!(nav & SCE_CTRL_LEFT)
                    : ui_theme_id() == VT_UI_THEME_APERTURE ? !!(nav & SCE_CTRL_DOWN)
                                                            : !!(nav & SCE_CTRL_UP);
                if (leave)
                    focus_sections = 1;
            }
            if (dash_selected != next)
                dirty = 1;
            dash_selected = next;
        }
        if (flags & UI_TOUCH_EVENT_TAP) {
            for (int i = 0; i < UI_DASH_COUNT; i++) {
                UiThemeRect r = layout.cards[i];
                if (r.h > 0 && ui_touch_hit_rect(touch.x, touch.y, r.x, r.y, r.w, r.h)) {
                    dash_selected = i;
                    focus_sections = 0;
                    if (i == UI_DASH_FAVOURITES && data->count[i] && r.w >= 300 && r.h >= 66) {
                        int previews_count = data->count[i] < 3 ? data->count[i] : 3;
                        int cell = (r.w - 20) / previews_count;
                        int k = (touch.x - r.x - 10) / cell,
                            previews = data->count[i] < 3 ? data->count[i] : 3;
                        if (touch.x >= r.x + 10 && k >= 0 && k < previews && touch.y >= r.y + 30 &&
                            touch.y < r.y + r.h - 6) {
                            dash_offset[i] = (dash_offset[i] + k) % data->count[i];
                            snprintf(dash_anchor[i], 16, "%s", data->favourites[dash_offset[i]].id);
                        }
                    }
                    if (i < 4 && i != UI_DASH_CONTINUE && data->count[i] > 1 &&
                        !(i == UI_DASH_FAVOURITES && r.w >= 300 && r.h >= 66) && r.h >= 62) {
                        int row = (touch.y - r.y - 28) / 48;
                        int rows = (r.h - 27) / 48;
                        if (rows > 3) rows = 3;
                        if (rows > data->count[i]) rows = data->count[i];
                        if (touch.y >= r.y + 28 && row >= 0 && row < rows) {
                            dash_offset[i] = (dash_offset[i] + row) % data->count[i];
                            VtLibraryResult hit;
                            dashboard_item(data, i, dash_offset[i], &hit);
                            snprintf(dash_anchor[i], 16, "%s", hit.item.id);
                        }
                    }
                    if(i!=UI_DASH_CONTINUE && i<4)in_collection=1;
                    pressed |= SCE_CTRL_CROSS;
                    break;
                }
            }
        }
        if (!focus_sections && (pressed & (SCE_CTRL_RTRIGGER | SCE_CTRL_SQUARE)))
            dirty = 1;
        if (!focus_sections && dash_selected < 4 && data->count[dash_selected] > 1 &&
            (pressed & (SCE_CTRL_RTRIGGER | SCE_CTRL_SQUARE))) {
            int count = data->count[dash_selected], step = pressed & SCE_CTRL_SQUARE ? -1 : 1;
            dash_offset[dash_selected] = (dash_offset[dash_selected] + count + step) % count;
            VtLibraryResult item;
            dashboard_item(data, dash_selected, dash_offset[dash_selected], &item);
            snprintf(dash_anchor[dash_selected], 16, "%s", item.item.id);
        }
        if ((pressed & SCE_CTRL_CROSS) && !focus_sections && !in_collection &&
            dash_selected>UI_DASH_CONTINUE && dash_selected<4 && data->count[dash_selected]) {
            in_collection=1;pressed&=~SCE_CTRL_CROSS;
        }
        if ((pressed & SCE_CTRL_CROSS) && !focus_sections) {
            VtLibraryResult selected;
            int exists = dashboard_item(
                data, dash_selected, dash_selected < 4 ? dash_offset[dash_selected] : 0, &selected);
            if (dash_selected == UI_DASH_SOURCES ||
                (!exists && dash_selected != UI_DASH_CONTINUE && dash_selected != UI_DASH_RECENT)) {
                view = 2;
                navigation.valid = 0;
                query[0] = 0;
                result = LIBRARY_OPEN_DETAIL;
                break;
            }
            if (dash_selected == UI_DASH_HISTORY || (!exists && dash_selected == UI_DASH_RECENT)) {
                view = 1;
                navigation.valid = 0;
                query[0] = 0;
                result = LIBRARY_OPEN_DETAIL;
                break;
            }
            if (!exists && dash_selected == UI_DASH_CONTINUE) {
                view = 2;
                navigation.valid = 0;
                query[0] = 0;
                result = LIBRARY_OPEN_DETAIL;
                break;
            }
            if (!exists) {
                if (dashboard_search()) {
                    result = LIBRARY_OPEN_DETAIL;
                    break;
                }
                sceCtrlPeekBufferPositive(0, &previous, 1);
                ui_home_controls_reset();
                continue;
            }
            *chosen = selected.item;
            if (dash_selected == UI_DASH_CONNECTIONS) {
                result = UI_LIBRARY_CONNECT;
                break;
            }
            if (chosen->kind >= VT_LIBRARY_FOLDER && chosen->provider != VT_LIBRARY_LOCAL) {
                if (authorize_folder(chosen) < 0)
                    continue;
                view = 3;
                query[0] = 0;
                dashboard_folder = 1;
                navigation = (typeof(navigation)){
                    .valid = 1, .focus = 0, .filter = -1, .in_folder = 1, .folder = *chosen};
                result = LIBRARY_OPEN_DETAIL;
                break;
            }
            result = UI_LIBRARY_PLAY;
            break;
        }
        ui_home_tabs_set_expanded(1);
        vita2d_start_drawing();
        vita2d_clear_screen();
        ui_chrome_background(VT_THEME_BG, VT_THEME_COLD);
        ui_brand_set_loading(status.pending != 0);
        ui_brand_draw_home_header();
        ui_brand_draw_home_search();
        ui_home_tabs_draw_with_focus(focus_sections, &tabs);
        ui_theme_scene_begin(UI_SCENE_HOME, "integrated-home");
        ui_text_line(layout.content.x, layout.title_y, layout.content.w,
                     tr("Your media, together", "I tuoi contenuti, insieme"), VT_THEME_TEXT,
                     vt_ui_theme_legacy_layout(ui_theme_id()) ? UI_FONT_BODY : UI_FONT_DISPLAY);
        if (layout.subtitle_y)
            ui_text_line(layout.content.x, layout.subtitle_y, layout.content.w,
                         tr("Continue · saved connections · favourites",
                            "Riprendi · connessioni salvate · preferiti"),
                         VT_THEME_TEXT_MUTED, UI_FONT_SMALL);
        for (int i = 0; i < UI_DASH_COUNT; i++) {
            VtLibraryResult item;
            int count = i < 4 ? data->count[i] : 0, offset = i < 4 ? dash_offset[i] : 0;
            int available = dashboard_item(data, i, offset, &item);
            dashboard_card(layout.cards[i], i, &item, available, count, offset,
                           !focus_sections && dash_selected == i, &artwork, data);
        }
        if (!focus_sections) {
            UiThemeRect r = layout.cards[dash_selected];
            if(in_collection && dash_selected!=UI_DASH_CONTINUE) {
                ui_text_line(r.x+r.w-90,r.y+20,80,UI_PAD_CROSS "  " UI_PAD_CIRCLE,VT_THEME_SIGNAL_BRIGHT,UI_FONT_SMALL);
                r.y+=26;r.h-=30;
                if(dash_selected==UI_DASH_FAVOURITES)r.w=(r.w-20)/(data->count[dash_selected]<3?data->count[dash_selected]:3);
                else if(r.h>44)r.h=44;
            }
            ui_focus_motion_tick(&focus_motion, r.x, r.y, r.w, r.h);
            ui_focus_outline_draw(&focus_motion);
        }
        ui_mini_player_draw();
        ui_sections_sidebar_draw(&sidebar);
        vt_library_search_cover_view_end();vt_video_thumbnail_view_end();vita2d_end_drawing();
        vita2d_wait_rendering_done();
        vita2d_swap_buffers();
    }
    ui_brand_set_loading(0);
    vt_library_search_cancel();
    ui_touch_reset();
    if (artwork.credentials) {
        memset(artwork.credentials, 0, VT_NETWORK_MAX_SOURCES * sizeof(*artwork.credentials));
        free(artwork.credentials);
    }
    free(artwork.sources);
    free(data);
    return result;
}

static UiThemeRect library_row_rect(int slot,int bottom) {
    if(ui_theme_id()!=VT_UI_THEME_LEGACY_VITA)
        return ui_theme_home_item_rect(ui_theme_id(),slot,bottom);
    UiThemeRect r={18,128+slot*72,924,64};
    return ui_theme_page_rect(UI_SCENE_HOME,r);
}
static void library_native_row(UiThemeRect r,const VtLibraryResult *v,const char *detail,
                               vita2d_texture *art,int selected) {
    ui_panel(r.x,r.y,r.w,r.h,selected?VT_THEME_SURFACE_RAISED:VT_THEME_SURFACE,VT_THEME_BORDER_DIM,0);
    ui_artwork_tile(r.x+8,r.y+8,48,art,media_label(v->item.kind));
    int width=r.w-88;
    ui_text_line(r.x+72,r.y+27,width,v->item.title,VT_THEME_TEXT,UI_FONT_BODY);
    ui_text_line(r.x+72,r.y+51,width,detail,VT_THEME_TEXT_MUTED,UI_FONT_SMALL);
}
static int library_browser_screen(VtLibraryItem *selected_out) {
    VtLibraryResult *items = calloc(VT_LIBRARY_RESULTS, sizeof(*items));
    if (!items)
        return UI_LIBRARY_BACK;
    LibraryArtworkSources artwork = artwork_sources();
    UiSectionsSidebar sidebar;
    ui_sections_sidebar_init(&sidebar, UI_SECTION_LOCAL_MEDIA);
    UiFocusMotion tab_motion = {0}, result_motion={0};
    UiNavRepeat repeat;
    ui_nav_repeat_reset(&repeat);
    ui_home_controls_reset();
    SceCtrlData ctrl = {0}, previous = {0};
    sceCtrlPeekBufferPositive(0, &previous, 1);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    int selected = 0, top = 0, n = 0, focus = view==3?0:1, filter = -1, result = UI_LIBRARY_BACK, depth = 0,
        dirty = 1;
    LibraryParent parents[8] = {0};
    VtLibraryItem folder = {0};
    int in_folder = 0;
    uint64_t refreshed = 0;
    unsigned seen_revision = ~0U, seen_search_revision = ~0U;
    VtLibrarySearchStatus status = {0};
    char identity[192], detail[256];
    if (navigation.valid) {
        selected = navigation.selected;
        top = navigation.top;
        focus = navigation.focus;
        filter = navigation.filter;
        depth = navigation.depth;
        in_folder = navigation.in_folder;
        folder = navigation.folder;
        memcpy(parents, navigation.parents, sizeof(parents));
    }
    vt_library_search_begin(query, in_folder ? &folder : NULL);
    vt_video_thumbnail_resume();
    ui_touch_reset();
    for (;;) {
        uint64_t now = sceKernelGetProcessTimeWide();
        ui_mini_player_pump();
        vt_video_thumbnail_pump();
        vt_library_search_cover_view_begin();
        if (ui_local_media_library_tick() && !in_folder) {
            vt_library_search_local_refresh();
            dirty = 1;
        }
        if (dirty || (now - refreshed > 250000ULL &&
                      (status.pending || seen_revision != vt_library_revision() ||
                       ((view == 3 || in_folder) &&
                        seen_search_revision != vt_library_search_revision())))) {
            int keep = !dirty && !focus && n && selected < n;
            VtLibraryResult prior = {0};
            if (keep)
                prior = items[selected];
            char focused[16] = "";
            if (n && selected < n)
                snprintf(focused, sizeof(focused), "%s", items[selected].item.id);
            if (view == 2 && !in_folder)
                source_results(items, &n);
            else if (view == 3 || in_folder)
                n = vt_library_search_snapshot(items, VT_LIBRARY_RESULTS, &status);
            else
                n = vt_library_query("", view == 0 ? VT_LIBRARY_CONTINUE : VT_LIBRARY_RECENT, items,
                                     VT_LIBRARY_RESULTS);
            if (filter >= 0 && view == 3 && !in_folder) {
                int k = 0;
                for (int i = 0; i < n; i++)
                    if (items[i].item.provider == filter)
                        items[k++] = items[i];
                n = k;
            }
            int found_focus = 0;
            for (int i = 0; i < n; i++)
                if (!strcmp(focused, items[i].item.id)) {
                    selected = i;
                    found_focus = 1;
                    break;
                }
            /* Arrival/expiry must never turn an imminent X press into a
             * different media choice. Retain that cached row until navigation. */
            if (keep && !found_focus) {
                int at = selected < n ? selected : n;
                if (n < VT_LIBRARY_RESULTS)
                    n++;
                else if (at >= n)
                    at = n - 1;
                if (at + 1 < n)
                    memmove(items + at + 1, items + at, (size_t)(n - at - 1) * sizeof(*items));
                items[at] = prior;
                items[at].cached = 1;
                selected = at;
            }

            if (selected >= n)
                selected = n ? n - 1 : 0;
            refreshed = now;
            seen_revision = vt_library_revision();
            seen_search_revision = status.revision;
            dirty = 0;
        }
        int bottom =
            ui_mini_player_top() < UI_CONTENT_BOTTOM ? ui_mini_player_top() : UI_CONTENT_BOTTOM;
        UiThemeHomeLayout layout = ui_theme_home_layout(ui_theme_id(), bottom);
        int native_rows=ui_theme_id()==VT_UI_THEME_LEGACY_VITA;
        if(native_rows)layout=(UiThemeHomeLayout){18,128,924,72,(bottom-52-128+8)/72,1};
        int legacy = vt_ui_theme_legacy_layout(ui_theme_id()), offset = legacy && !native_rows ? 1 : 0,
            visible = layout.rows - offset;
        if (visible < 1)
            visible = 1;
        sceCtrlPeekBufferPositive(0, &ctrl, 1);
        unsigned pressed = ctrl.buttons & ~previous.buttons;
        previous = ctrl;
        UiTouchEvent touch;
        unsigned flags = ui_touch_poll(&touch);
        ui_mini_player_handle_buttons(&pressed);
        if (ui_mini_player_input_locked()) {
            pressed = flags = 0;
            ctrl.buttons = 0;
            ctrl.lx = ctrl.ly = ctrl.rx = ctrl.ry = 128;
        }
        if (ui_brand_sections_hit(flags, touch.x, touch.y)) {
            pressed |= SCE_CTRL_LTRIGGER;
            flags = 0;
        }
        int was_open = sidebar.open;
        int section = ui_sections_sidebar_handle_buttons(&sidebar, &pressed, ctrl.buttons, ctrl.ly);
        int blocked = was_open || sidebar.open || sidebar.animation > .01f;
        if (blocked) {
            int s = ui_sections_sidebar_handle_touch(&sidebar, flags, touch.x, touch.y);
            if (s != UI_SECTION_NONE)
                section = s;
            flags = 0;
        } else if (ui_mini_player_handle_touch(flags, &touch))
            flags = 0;
        ui_sections_sidebar_tick(&sidebar);
        if (section != UI_SECTION_NONE) {
            result = UI_LIBRARY_SECTION_BASE + section;
            break;
        }
        unsigned nav = 0;
        int changed = 0;
        if (legacy) {
            if (!blocked) {
                nav = ui_nav_repeat_update(&repeat, pressed, ctrl.buttons, ctrl.lx, focus?ctrl.ly:128,
                                           SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT |
                                               SCE_CTRL_RIGHT);
                unsigned tabs = ui_home_tabs_navigation(pressed, focus ? ctrl.buttons : 0,
                                                        focus ? ctrl.lx : 128, 128);
                changed = ui_home_tabs_input(tabs, flags, &touch, focus);
                if (focus && (nav & SCE_CTRL_DOWN) && n) {
                    focus = 0;selected=0;
                    nav &= ~SCE_CTRL_DOWN;
                } else if (!focus && (nav & SCE_CTRL_UP) && selected == 0) {
                    focus = 1;
                    nav = 0;
                }
            }
        } else {
            UiHomeControlSample input = {
                .pressed = pressed,
                .held = ctrl.buttons,
                .touch_flags = flags,
                .lx = ctrl.lx,
                .ly = focus?ctrl.ly:128,
                .rx = ctrl.rx,
                .ry = ctrl.ry,
                .selected = selected,
                .count = n ? n : ((view == 3 || in_folder) && status.pending ? 1 : 0),
                .bottom = bottom,
                .focus_sections = focus,
                .blocked = blocked,
                .touch = &touch};
            UiHomeControlResult controls = ui_home_controls_update(&input);
            pressed = controls.pressed;
            nav = controls.content_nav;
            if(input.focus_sections&&!controls.focus_sections)selected=0;
            focus = controls.focus_sections;
            changed = controls.sections_changed;
        }
        if (changed) {
            result = UI_LIBRARY_TAB;
            break;
        }
        if (blocked) {
            pressed = nav = 0;
            flags = 0;
        }
        if (pressed & SCE_CTRL_TRIANGLE) {
            char text[129];
            snprintf(text, sizeof(text), "%s", query);
            if (ui_text_input(tr("Global search: title, artist, album, podcast or channel",
                                 "Ricerca globale: titolo, artista, album, podcast o canale"),
                              text, text, sizeof(text)) > 0) {
                snprintf(query, sizeof(query), "%s", text);
                view = 3;
                selected = top = 0;
                focus = 0;
                filter = -1;
                in_folder = depth = 0;
                folder = (VtLibraryItem){0};
                vt_library_search_begin(query, NULL);
                dirty = 1;
            }
            sceCtrlPeekBufferPositive(0, &previous, 1);
            ui_touch_reset();
            continue;
        }
        UiThemeRect heading = heading_rect(ui_theme_id(), bottom);
        if ((pressed & SCE_CTRL_RTRIGGER) ||
            ((flags & UI_TOUCH_EVENT_TAP) &&
             ui_touch_hit_rect(touch.x, touch.y, heading.x, heading.y, heading.w, heading.h))) {
            result = LIBRARY_RETURN_HOME;
            break;
        }
        if ((pressed & SCE_CTRL_SQUARE) && view == 3 && !in_folder) {
            filter++;
            if (filter >= VT_LIBRARY_PROVIDER_COUNT)
                filter = -1;
            selected = top = 0;
            dirty = 1;
        }
        if (pressed & SCE_CTRL_CIRCLE) {
            if (in_folder) {
                if (!depth && dashboard_folder) {
                    result = LIBRARY_RETURN_HOME;
                    break;
                }
                if (depth > 0) {
                    depth--;
                    folder = parents[depth].item;
                    selected = parents[depth].selected;
                    in_folder = folder.locator[0] != 0;
                } else
                    in_folder = 0;
                vt_library_search_begin(query, in_folder ? &folder : NULL);
                dirty = 1;
                top = 0;
                continue;
            }
            result = LIBRARY_RETURN_HOME;
            break;
        }
        if (!focus) {
            selected=ui_grid_navigation(selected,n,layout.columns,nav);
        }
        if (selected < top)
            top = selected / layout.columns * layout.columns;
        if (selected >= top + visible)
            top = (selected / layout.columns - visible / layout.columns + 1) * layout.columns;
        if (top < 0)
            top = 0;
        snprintf(identity,sizeof(identity),"library:%d:%.128s:%.15s",view,query,folder.id);
        UiPageView hit_page=ui_theme_page(UI_SCENE_HOME,"unified-library",identity,top,n,
            layout.columns,visible,layout.step);
        if(!blocked&&(flags&UI_TOUCH_EVENT_DOWN)&&touch.down_x>=layout.x&&
            touch.down_y>=layout.y+offset*layout.step&&touch.down_y<ui_theme_home_content_bottom(ui_theme_id(),bottom))focus=0;
        if(!focus&&!blocked){
            int scroll_y=layout.y+offset*layout.step;
            ui_theme_page_input(UI_SCENE_HOME,"unified-library",layout.x,scroll_y,
                legacy?924:ui_theme_id()==VT_UI_THEME_GHOST_MESH?layout.w:942-layout.x,
                ui_theme_home_content_bottom(ui_theme_id(),bottom)-scroll_y,n,0,&top,&selected,ctrl.buttons,ctrl.lx,ctrl.ly,&flags,&touch);
            hit_page=ui_theme_page(UI_SCENE_HOME,"unified-library",identity,top,n,
                layout.columns,visible,layout.step);
        }
        if (flags & UI_TOUCH_EVENT_TAP) {
            for (int i=hit_page.first;i<hit_page.end;i++) {
                UiThemeRect r = library_row_rect(i-top+offset,bottom);
                if (touch.y>=layout.y+offset*layout.step&&touch.y<ui_theme_home_content_bottom(ui_theme_id(),bottom)&&ui_touch_hit_rect(touch.x, touch.y, r.x, r.y, r.w, r.h)) {
                    selected = i;
                    focus = 0;
                    pressed |= SCE_CTRL_CROSS;
                    break;
                }
            }
        }
        if((pressed&SCE_CTRL_CROSS) && focus && ui_home_tab()==HOME_LIBRARY) {
            result=LIBRARY_RETURN_HOME;break;
        }
        if ((pressed & SCE_CTRL_CROSS) && !focus && n) {
            VtLibraryItem chosen = items[selected].item;
            if (view == 2 && !in_folder) {
                int source = chosen.mode;
                if (source == 0) {
                    result = UI_LIBRARY_LOCAL;
                    break;
                }
                if (source == 5 || source == 6)
                    ui_home_tab_set(source == 5 ? HOME_RADIO : HOME_PODCAST);
                result = UI_LIBRARY_SECTION_BASE + sections[source];
                break;
            }
            if (chosen.kind >= VT_LIBRARY_FOLDER && chosen.provider != VT_LIBRARY_LOCAL) {
                if (depth >= 8) {
                    ui_message_show(tr("Library", "Libreria"),
                                    tr("Close a folder before opening another level",
                                       "Chiudi una cartella prima di aprire un altro livello"),
                                    2400);
                    continue;
                }
                if (authorize_folder(&chosen) < 0) {
                    ui_message_show(
                        tr("Source unavailable", "Sorgente non disponibile"),
                        tr("Check the server in Sources", "Controlla il server in Sorgenti"), 2400);
                    continue;
                }
                parents[depth].item = folder;
                parents[depth++].selected = selected;
                folder = chosen;
                in_folder = 1;
                selected = top = 0;
                vt_library_search_begin("", &folder);
                dirty = 1;
                continue;
            }
            *selected_out = chosen;
            result = UI_LIBRARY_PLAY;
            break;
        }
        snprintf(identity, sizeof(identity), "library:%d:%.128s:%.15s", view, query, folder.id);
        snprintf(detail, sizeof(detail), "%s",
                 query[0] ? query
                          : tr(UI_PAD_R1 ": Home · " UI_PAD_TRIANGLE ": search",
                               UI_PAD_R1 ": Home · " UI_PAD_TRIANGLE ": cerca"));
        if (view == 3 || in_folder)
            search_detail(&status, n, in_folder ? -1 : filter, detail, sizeof(detail));
        ui_home_tabs_set_expanded(1);
        vita2d_start_drawing();
        vita2d_clear_screen();
        ui_chrome_background(VT_THEME_BG, VT_THEME_BLUE_BRIGHT);
        ui_brand_set_loading((view == 3 || in_folder) && status.pending);
        ui_brand_draw_home_header();
        ui_home_tabs_draw_with_focus(focus, &tab_motion);
        if (!legacy)
            ui_theme_home_heading(in_folder  ? folder.title
                                  : query[0] ? query
                                             : view_label(view),
                                  detail, bottom);
        else {
            ui_text_line(18, 82, 924,
                         in_folder  ? folder.title
                         : query[0] ? query
                                    : view_label(view),
                         VT_THEME_TEXT, UI_FONT_BODY);
            ui_text_line(18, 103, 924, detail, VT_THEME_TEXT_MUTED, UI_FONT_SMALL);
        }
        UiPageView page = ui_theme_page(UI_SCENE_HOME, "unified-library", identity, top, n,
                                        layout.columns, visible, layout.step);
        int viewport = layout.y + offset * layout.step;
        if (view == 3 || in_folder)
            search_activity(&status, in_folder ? -1 : filter, layout.x, viewport - 7,
                            legacy                                    ? 924
                            : ui_theme_id() == VT_UI_THEME_GHOST_MESH ? layout.w
                                                                      : 942 - layout.x,
                            now);
        vita2d_set_clip_rectangle(0, viewport - 3, 960, ui_theme_home_content_bottom(ui_theme_id(),bottom));
        vita2d_enable_clipping();
        for (int i = page.first; i < page.end; i++) {
            VtLibraryResult *v = &items[i];
            char line[512];
            describe(v, view == 3, line, sizeof(line));
            if (view == 2)
                snprintf(line, sizeof(line), "%s",
                         tr("Open provider and manage connections",
                            "Apri il provider e gestisci i collegamenti"));
            const char *path = v->item.artwork;
            vita2d_texture *art = NULL;
            if (path[0] && strcmp(path, "-"))
                art = vt_artwork_thumbnail_get(path);
            else if (v->item.artwork_url[0])
                vt_library_search_cover(&v->item);
            if (!art && v->item.provider == VT_LIBRARY_LOCAL && v->item.kind == VT_LIBRARY_IMAGE)
                art = vt_image_thumbnail_get(v->item.locator, v->item.size);
            else if (!art && v->item.provider == VT_LIBRARY_LOCAL &&
                     v->item.kind == VT_LIBRARY_VIDEO)
                art = vt_video_thumbnail_get(v->item.locator, v->item.size);
            if (!art)
                art = remote_artwork(&artwork, &v->item, i == selected ? 100 : 50);
            if(native_rows)library_native_row(library_row_rect(i-top,bottom),v,line,art,!focus&&i==selected);
            else ui_theme_home_row_art(i - top + offset, i, v->item.title, line, i == selected, !focus,
                                  bottom, art, media_label(v->item.kind));
            if (v->duration_ms && v->position_ms && v->item.kind != VT_LIBRARY_LIVE) {
                UiThemeRect r = library_row_rect(i-top+offset,bottom);
                float fraction = (float)v->position_ms / (float)v->duration_ms;
                if (fraction > 1)
                    fraction = 1;
                vita2d_draw_rectangle(r.x + 8, r.y + r.h - 4, (r.w - 16) * fraction, 2,
                                      VT_THEME_SIGNAL_BRIGHT);
            }
        }
        if(native_rows && !focus && n) {
            UiThemeRect r=library_row_rect(selected-top,bottom);
            ui_focus_motion_tick(&result_motion,r.x,r.y,r.w,r.h);
            ui_focus_outline_draw(&result_motion);
        }
        vita2d_disable_clipping();
        if (!n) {
            int empty_width = ui_theme_id() == VT_UI_THEME_GHOST_MESH ? layout.w : 942 - layout.x;
            ui_text_line(
                layout.x, viewport + 34, empty_width,
                (status.pending && (view == 3 || in_folder))
                    ? tr("Searching available sources...", "Ricerca nelle sorgenti...")
                    : tr("No items in this view yet", "Ancora nessun contenuto in questa vista"),
                VT_THEME_TEXT, UI_FONT_BODY);
            ui_text_line(layout.x, viewport + 62, empty_width,
                         tr(UI_PAD_R1 ": Home · " UI_PAD_TRIANGLE ": search all sources",
                            UI_PAD_R1 ": Home · " UI_PAD_TRIANGLE ": cerca ovunque"),
                         VT_THEME_TEXT_MUTED, UI_FONT_SMALL);
        }
        ui_theme_page_finish(UI_SCENE_HOME, layout.x, viewport, legacy ? 924 : 942 - layout.x,
                             bottom - 38);
        const char *actions = tr(UI_PAD_R1 ": Home · " UI_PAD_TRIANGLE ": search", UI_PAD_R1 ": Home · " UI_PAD_TRIANGLE ": cerca");
        int footer_x = layout.x,
            footer_w = ui_theme_id() == VT_UI_THEME_GHOST_MESH ? layout.w : 942 - layout.x;
        if(ui_theme_id()==VT_UI_THEME_LEGACY_VITA)
            ui_panel(footer_x-2,bottom-51,footer_w+4,49,VT_THEME_SURFACE,VT_THEME_BORDER_DIM,0);
        if (ui_theme_id() == VT_UI_THEME_APERTURE) {
            char hint[512];
            snprintf(hint, sizeof(hint), "%s · %s", actions, ui_theme_home_controls_hint(focus));
            ui_text_line(layout.x, 408, layout.w, hint, VT_THEME_TEXT_MUTED, UI_FONT_SMALL);
        } else {
            ui_text_line(footer_x, bottom - 26, footer_w, actions, VT_THEME_TEXT_MUTED,
                         UI_FONT_SMALL);
            ui_text_line(footer_x, bottom - 8, footer_w,
                         view==3 ? tr("D-pad: select · " UI_PAD_CROSS ": open · " UI_PAD_SQUARE ": provider",
                                       "Frecce: seleziona · " UI_PAD_CROSS ": apri · " UI_PAD_SQUARE ": sorgente")
                         : filter >= 0 ? vt_library_provider_name(filter)
                                       : ui_theme_home_controls_hint(focus),
                         VT_THEME_TEXT_MUTED, UI_FONT_SMALL);
        }
        ui_mini_player_draw();
        ui_sections_sidebar_draw(&sidebar);
        vt_library_search_cover_view_end();vt_video_thumbnail_view_end();vita2d_end_drawing();
        vita2d_wait_rendering_done();
        vita2d_swap_buffers();
    }
    ui_brand_set_loading(0);
    navigation.selected = selected;
    navigation.top = top;
    navigation.focus = focus;
    navigation.filter = filter;
    navigation.depth = depth;
    navigation.in_folder = in_folder;
    navigation.folder = folder;
    navigation.valid = 1;
    memcpy(navigation.parents, parents, sizeof(parents));
    vt_library_search_cancel();
    ui_touch_reset();
    if (artwork.credentials) {
        memset(artwork.credentials, 0, VT_NETWORK_MAX_SOURCES * sizeof(*artwork.credentials));
        free(artwork.credentials);
    }
    free(artwork.sources);
    free(items);
    return result;
}

void ui_library_reset_home(void) {
    view=LIBRARY_HOME;query[0]=0;navigation.valid=0;dashboard_folder=0;
}
int ui_library_screen(VtLibraryItem *selected_out) {
    for (;;) {
        int result = view == LIBRARY_HOME ? dashboard_screen(selected_out)
                                          : library_browser_screen(selected_out);
        if (result == LIBRARY_OPEN_DETAIL)
            continue;
        if (result == LIBRARY_RETURN_HOME) {
            view = LIBRARY_HOME;
            query[0] = 0;
            navigation.valid = 0;
            dashboard_folder = 0;
            continue;
        }
        return result;
    }
}
