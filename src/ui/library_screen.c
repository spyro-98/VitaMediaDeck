#include "ui/library_screen.h"
#include "ui/home_dashboard.h"
#include "library/catalog.h"
#include "library/search.h"
#include "i18n/i18n.h"
#include "ui/home_tabs.h"
#include "ui/components.h"
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
    return vt_i18n_current_language() == VT_LANG_IT ? it : en;
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
             tr("%s · %d results · Square: provider", "%s · %d risultati · Quadrato: provider"),
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
    int count;
} LibraryArtworkSources;
static LibraryArtworkSources artwork_sources(void) {
    LibraryArtworkSources a = {0};
    a.sources = calloc(VT_NETWORK_MAX_SOURCES, sizeof(*a.sources));
    a.credentials = calloc(VT_NETWORK_MAX_SOURCES, sizeof(*a.credentials));
    if (a.sources && a.credentials) {
        a.count = vt_network_sources_load(a.sources, VT_NETWORK_MAX_SOURCES);
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
    int count[4];
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
    VtLibraryResult prior;
    int role = dash_selected;
    int had_prior = keep_focus && role < 4 && dashboard_item(d, role, dash_offset[role], &prior);
    d->count[0] = vt_library_query("", VT_LIBRARY_CONTINUE, d->resume, DASH_CONTINUE_MAX);
    int recent = vt_library_query("", VT_LIBRARY_RECENT, d->recent, DASH_RECENT_MAX), used = 0;
    for (int i = 0; i < recent; i++) {
        int duplicate = 0;
        for (int k = 0; k < d->count[0]; k++)
            if (!strcmp(d->recent[i].item.id, d->resume[k].item.id))
                duplicate = 1;
        if (!duplicate)
            d->recent[used++] = d->recent[i];
    }
    d->count[3] = used;
    d->count[2] =
        vt_library_search_home_favourites(d->favourites, VT_LIBRARY_HOME_FAVOURITES, status);
    if (had_prior) {
        int found = 0;
        for (int i = 0; i < d->count[role]; i++) {
            VtLibraryResult current;
            dashboard_item(d, role, i, &current);
            if (!strcmp(current.item.id, prior.item.id))
                found = 1;
        }
        if (!found) {
            int capacity = role == UI_DASH_CONTINUE     ? DASH_CONTINUE_MAX
                           : role == UI_DASH_RECENT     ? DASH_RECENT_MAX
                           : role == UI_DASH_FAVOURITES ? VT_LIBRARY_HOME_FAVOURITES
                                                        : VT_NETWORK_MAX_SOURCES;
            int at = d->count[role] < capacity ? d->count[role]++ : capacity - 1;
            prior.cached = 1;
            if (role == UI_DASH_CONTINUE)
                d->resume[at] = prior;
            else if (role == UI_DASH_RECENT)
                d->recent[at] = prior;
            else if (role == UI_DASH_FAVOURITES)
                d->favourites[at] = prior.item;
            else
                d->connections[at] = prior.item;
        }
    }
    for (int role = 0; role < 4; role++) {
        for (int i = 0; i < d->count[role]; i++) {
            VtLibraryResult item;
            dashboard_item(d, role, i, &item);
            if (!strcmp(item.item.id, dash_anchor[role])) {
                dash_offset[role] = i;
                break;
            }
        }
        if (dash_offset[role] >= d->count[role])
            dash_offset[role] = 0;
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
static void dashboard_card(UiThemeRect r, int role, const VtLibraryResult *v, int available,
                           int count, int offset, int selected, LibraryArtworkSources *sources,
                           const DashboardData *data) {
    int theme = ui_theme_id(), action = role >= 4, hero = role == UI_DASH_CONTINUE;
    ui_theme_scene_card(UI_SCENE_HOME, r, selected, role);
    ui_theme_fx_surface(r.x + 2, r.y + 2, r.w - 4, r.h - 4,
                        selected ? VT_THEME_SURFACE_RAISED : VT_THEME_SURFACE, selected);
    if (action) {
        ui_text_line(r.x + 12, r.y + r.h / 2 + 6, r.w - 30, dashboard_label(role),
                     selected ? VT_THEME_SIGNAL_BRIGHT : VT_THEME_TEXT, UI_FONT_SMALL);
        return;
    }
    int compact = r.h < 62;
    char heading[128];
    snprintf(heading, sizeof(heading), "%s",
             role == UI_DASH_CONNECTIONS && r.w < 260 ? tr("Connections", "Connessioni")
                                                      : dashboard_label(role));
    if (!compact)
        ui_text_line(r.x + 12, r.y + 21, r.w - 24 - (count > 1 ? 62 : 0), heading,
                     VT_THEME_COLD_LIGHT, UI_FONT_SMALL);
    const char *empty =
        role == UI_DASH_CONTINUE ? tr("Find something to play", "Scegli cosa guardare o ascoltare")
        : role == UI_DASH_CONNECTIONS ? tr("Connect your library", "Collega la tua libreria")
        : role == UI_DASH_FAVOURITES
            ? tr("Save your favourite media", "Salva i tuoi contenuti preferiti")
            : tr("Your recent media appears here", "Qui troverai gli ultimi contenuti");
    int art_size = compact ? r.h - 12 : r.h - 40;
    if (art_size > 130)
        art_size = 130;
    if (art_size > r.w / 3)
        art_size = r.w / 3;
    if (art_size < 26)
        art_size = 26;
    int ay = compact ? r.y + 6 : r.y + 30, tx = r.x + art_size + 24, tw = r.w - art_size - 36;
    vita2d_texture *art = available && role != UI_DASH_CONNECTIONS
                              ? dashboard_cover(sources, &v->item, selected ? 100 : 40)
                              : NULL;
    int film = role == UI_DASH_FAVOURITES && available && r.w >= 300 && r.h >= 66;
    if (film) {
        art_size = r.h - 42;
        if (art_size > 64)
            art_size = 64;
        int previews = count < 3 ? count : 3;
        for (int k = 0; k < previews; k++) {
            const VtLibraryItem *item = &data->favourites[(offset + k) % count];
            vita2d_texture *cover = dashboard_cover(sources, item, selected ? 90 : 40);
            ui_artwork_tile(r.x + 10 + k * (art_size + 8), ay, art_size, cover,
                            media_label(item->kind));
            if (!k)
                vita2d_draw_rectangle(r.x + 10, ay + art_size + 2, art_size, 2,
                                      VT_THEME_SIGNAL_BRIGHT);
        }
        tx = r.x + previews * (art_size + 8) + 18;
        tw = r.x + r.w - tx - 12;
    } else {
        if (compact && role == UI_DASH_RECENT) {
            ui_text_line(r.x + 10, r.y + 20, 68, tr("Recent", "Recenti"), VT_THEME_COLD_LIGHT,
                         UI_FONT_SMALL);
            ui_artwork_tile(r.x + 82, ay, art_size, art,
                            available ? media_label(v->item.kind) : "+");
            tx = r.x + art_size + 94;
            tw = r.x + r.w - tx - 12;
        } else
            ui_artwork_tile(r.x + 10, ay, art_size, art,
                            role == UI_DASH_CONNECTIONS ? "NET"
                            : available                 ? media_label(v->item.kind)
                                                        : "+");
    }
    unsigned title_size = hero && r.w > 440 && r.h > 112 ? UI_FONT_BODY : UI_FONT_SMALL;
    int title_y = compact ? r.y + 21 : r.y + 49;
    ui_text_line(tx, title_y, tw, available ? v->item.title : empty, VT_THEME_TEXT, title_size);
    if (available) {
        char line[512];
        describe(v, 0, line, sizeof(line));
        if (role == UI_DASH_CONNECTIONS)
            snprintf(line, sizeof(line), "%s", vt_library_provider_name(v->item.provider));
        if (compact || r.h >= 76)
            ui_text_line(tx, title_y + (compact ? 18 : 24), tw, line, VT_THEME_TEXT_MUTED,
                         UI_FONT_SMALL);
        if (hero && v->duration_ms && v->position_ms) {
            float fraction = fminf(1, (float)v->position_ms / (float)v->duration_ms);
            vita2d_draw_rectangle(tx, r.y + r.h - 14, tw, 3, VT_THEME_BORDER_DIM);
            vita2d_draw_rectangle(tx, r.y + r.h - 14, tw * fraction, 3, VT_THEME_SIGNAL_BRIGHT);
        }
    } else if (r.h > 90) {
        ui_text_line(tx, title_y + 25, tw,
                     role == UI_DASH_CONTINUE
                         ? tr("Triangle: search", "Triangolo: cerca")
                         : tr("Cross: explore sources", "Croce: esplora le sorgenti"),
                     VT_THEME_TEXT_MUTED, UI_FONT_SMALL);
    }
    if (count > 1 && !compact) {
        char counter[32];
        snprintf(counter, sizeof(counter), "%d / %d", offset + 1, count);
        ui_text_line(r.x + r.w - 68, r.y + 21, 56, counter, VT_THEME_TEXT_MUTED, UI_FONT_SMALL);
    }
    if (theme == VT_UI_THEME_APERTURE) {
        for (int x = r.x + 8; x < r.x + r.w - 8; x += 24)
            vita2d_draw_rectangle(x, r.y + r.h - 4, 10, 2, VT_THEME_COLD_DIM);
    } else if (theme == VT_UI_THEME_DECK_90 && hero) {
        for (int y = r.y + 28; y < r.y + r.h - 6; y += 6)
            vita2d_draw_rectangle(r.x + 4, y, r.w - 8, 1, RGBA8(0, 0, 0, 12));
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
static int dashboard_screen(VtLibraryItem *chosen) {
    DashboardData *data = calloc(1, sizeof(*data));
    if (!data)
        return UI_LIBRARY_BACK;
    LibraryArtworkSources artwork = artwork_sources();
    for (int i = 0; i < artwork.count; i++) {
        VtNetworkEntry entry = {.is_directory = 1};
        snprintf(entry.name, sizeof(entry.name), "%s",
                 artwork.sources[i].name[0] ? artwork.sources[i].name : artwork.sources[i].host);
        snprintf(entry.path, sizeof(entry.path), "%s",
                 artwork.sources[i].root_path[0] ? artwork.sources[i].root_path : "/");
        vt_library_remote(&artwork.sources[i], &entry, "", &data->connections[data->count[1]++]);
    }
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
    uint64_t refreshed = 0;
    int dirty = 1, focus_sections = 0, result = UI_LIBRARY_BACK;
    for (;;) {
        uint64_t now = sceKernelGetProcessTimeWide();
        ui_mini_player_pump();
        vt_video_thumbnail_pump();
        if (ui_local_media_library_tick()) {
            vt_library_search_local_refresh();
            dirty = 1;
        }
        if (dirty || (now - refreshed > 250000ULL &&
                      (revision != vt_library_revision() ||
                       search_revision != vt_library_search_revision() || status.pending))) {
            dashboard_refresh(data, &status, !dirty && !focus_sections);
            revision = vt_library_revision();
            search_revision = status.revision;
            refreshed = now;
            dirty = 0;
        }
        int bottom =
            ui_mini_player_top() < UI_CONTENT_BOTTOM ? ui_mini_player_top() : UI_CONTENT_BOTTOM;
        UiDashboardLayout layout = ui_dashboard_layout(ui_theme_id(), bottom);
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
        focus_sections = controls.focus_sections;
        if (controls.sections_changed) {
            result = UI_LIBRARY_TAB;
            break;
        }
        unsigned nav = controls.content_nav;
        if (blocked) {
            pressed = nav = flags = 0;
        }
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
        if (pressed & SCE_CTRL_CIRCLE)
            focus_sections = 1;
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
                if (ui_touch_hit_rect(touch.x, touch.y, r.x, r.y, r.w, r.h)) {
                    dash_selected = i;
                    focus_sections = 0;
                    if (i == UI_DASH_FAVOURITES && data->count[i] && r.w >= 300 && r.h >= 66) {
                        int size = r.h - 42;
                        if (size > 64)
                            size = 64;
                        int k = (touch.x - r.x - 10) / (size + 8),
                            previews = data->count[i] < 3 ? data->count[i] : 3;
                        if (touch.x >= r.x + 10 && k >= 0 && k < previews && touch.y >= r.y + 30 &&
                            touch.y < r.y + 30 + size) {
                            dash_offset[i] = (dash_offset[i] + k) % data->count[i];
                            snprintf(dash_anchor[i], 16, "%s", data->favourites[dash_offset[i]].id);
                        }
                    }
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
            ui_focus_motion_tick(&focus_motion, r.x, r.y, r.w, r.h);
            ui_focus_outline_draw(&focus_motion);
        }
        ui_text_line(layout.content.x, layout.footer_y, layout.content.w,
                     focus_sections ? ui_theme_home_controls_hint(1)
                                    : tr("Triangle: search · R1/Square: next/previous",
                                         "Triangolo: cerca · R1/Quadrato: altri"),
                     VT_THEME_TEXT_MUTED, UI_FONT_SMALL);
        ui_mini_player_draw();
        ui_sections_sidebar_draw(&sidebar);
        vita2d_end_drawing();
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

static int library_browser_screen(VtLibraryItem *selected_out) {
    VtLibraryResult *items = calloc(VT_LIBRARY_RESULTS, sizeof(*items));
    if (!items)
        return UI_LIBRARY_BACK;
    LibraryArtworkSources artwork = artwork_sources();
    UiSectionsSidebar sidebar;
    ui_sections_sidebar_init(&sidebar, UI_SECTION_LOCAL_MEDIA);
    UiFocusMotion tab_motion = {0};
    UiNavRepeat repeat;
    ui_nav_repeat_reset(&repeat);
    ui_home_controls_reset();
    SceCtrlData ctrl = {0}, previous = {0};
    sceCtrlPeekBufferPositive(0, &previous, 1);
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    int selected = 0, top = 0, n = 0, focus = 1, filter = -1, result = UI_LIBRARY_BACK, depth = 0,
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
        int legacy = vt_ui_theme_legacy_layout(ui_theme_id()), offset = legacy ? 1 : 0,
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
                nav = ui_nav_repeat_update(&repeat, pressed, ctrl.buttons, ctrl.lx, ctrl.ly,
                                           SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT |
                                               SCE_CTRL_RIGHT);
                unsigned tabs = ui_home_tabs_navigation(pressed, focus ? ctrl.buttons : 0,
                                                        focus ? ctrl.lx : 128, 128);
                changed = ui_home_tabs_input(tabs, flags, &touch, focus);
                if (focus && (nav & SCE_CTRL_DOWN) && n) {
                    focus = 0;
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
                .ly = ctrl.ly,
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
            if ((nav & SCE_CTRL_UP) && selected >= layout.columns)
                selected -= layout.columns;
            if ((nav & SCE_CTRL_DOWN) && selected + layout.columns < n)
                selected += layout.columns;
            if ((nav & SCE_CTRL_LEFT) && selected > 0)
                selected--;
            if ((nav & SCE_CTRL_RIGHT) && selected + 1 < n)
                selected++;
        }
        if (selected < top)
            top = selected / layout.columns * layout.columns;
        if (selected >= top + visible)
            top = (selected / layout.columns - visible / layout.columns + 1) * layout.columns;
        if (top < 0)
            top = 0;
        if (flags & UI_TOUCH_EVENT_TAP) {
            for (int i = top; i < top + visible && i < n; i++) {
                UiThemeRect r = ui_theme_home_item_rect(ui_theme_id(), i - top + offset, bottom);
                if (ui_touch_hit_rect(touch.x, touch.y, r.x, r.y, r.w, r.h)) {
                    selected = i;
                    focus = 0;
                    pressed |= SCE_CTRL_CROSS;
                    break;
                }
            }
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
                          : tr("R1: Home · Triangle: search",
                               "R1: Home · Triangolo: cerca"));
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
        vita2d_set_clip_rectangle(0, viewport - 3, 960, bottom - 38);
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
            ui_theme_home_row_art(i - top + offset, i, v->item.title, line, i == selected, !focus,
                                  bottom, art, media_label(v->item.kind));
            if (v->duration_ms && v->position_ms && v->item.kind != VT_LIBRARY_LIVE) {
                UiThemeRect r = ui_theme_home_item_rect(ui_theme_id(), i - top + offset, bottom);
                float fraction = (float)v->position_ms / (float)v->duration_ms;
                if (fraction > 1)
                    fraction = 1;
                vita2d_draw_rectangle(r.x + 8, r.y + r.h - 4, (r.w - 16) * fraction, 2,
                                      VT_THEME_SIGNAL_BRIGHT);
            }
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
                         tr("R1: Home · Triangle: search all sources",
                            "R1: Home · Triangolo: cerca ovunque"),
                         VT_THEME_TEXT_MUTED, UI_FONT_SMALL);
        }
        ui_theme_page_finish(UI_SCENE_HOME, layout.x, viewport, legacy ? 924 : 942 - layout.x,
                             bottom - 38);
        const char *actions = tr("R1: Home · Triangle: search", "R1: Home · Triangolo: cerca");
        int footer_x = layout.x,
            footer_w = ui_theme_id() == VT_UI_THEME_GHOST_MESH ? layout.w : 942 - layout.x;
        if (ui_theme_id() == VT_UI_THEME_APERTURE) {
            char hint[512];
            snprintf(hint, sizeof(hint), "%s · %s", actions, ui_theme_home_controls_hint(focus));
            ui_text_line(layout.x, 408, layout.w, hint, VT_THEME_TEXT_MUTED, UI_FONT_SMALL);
        } else {
            ui_text_line(footer_x, bottom - 26, footer_w, actions, VT_THEME_TEXT_MUTED,
                         UI_FONT_SMALL);
            ui_text_line(footer_x, bottom - 8, footer_w,
                         filter >= 0 ? vt_library_provider_name(filter)
                                     : ui_theme_home_controls_hint(focus),
                         VT_THEME_TEXT_MUTED, UI_FONT_SMALL);
        }
        ui_mini_player_draw();
        ui_sections_sidebar_draw(&sidebar);
        vita2d_end_drawing();
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
