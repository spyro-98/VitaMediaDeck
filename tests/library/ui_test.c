/* Synthetic provider fixtures rendered by the production seven-theme Home. */
#define VMD_REAL_LIBRARY
#define VMD_ONLINE_UI
#define main previous_iptv_main
#include "../iptv/ui_test.c"
#undef main
#include "../../src/ui/home_tabs.c"
static int dashboard_empty, dashboard_sources, dashboard_drop_resume, dashboard_cold;
static int fixture_library_query(const char *, int, VtLibraryResult *, int);
#define vt_library_query fixture_library_query
#include "../../src/ui/library_screen.c"
#undef vt_library_query
static int fixture_library_query(const char *q, int type, VtLibraryResult *out, int cap) {
    if (view == LIBRARY_HOME && (dashboard_empty || (dashboard_cold && type != VT_LIBRARY_ALL)))
        return 0;
    int count = vt_library_query(q, type, out, cap);
    if (dashboard_drop_resume && view == LIBRARY_HOME && type == VT_LIBRARY_CONTINUE &&
        frame >= 16 && count > 0) {
        memmove(out, out + 1, (size_t)(count - 1) * sizeof(*out));
        count--;
    }
    return count;
}
#include "history/playback_history.h"
static int search_calls, progressive, fixture_count, loading_fixture, loading_flag,
    publication_fixture;
void ui_brand_set_loading(int loading) {
    loading_flag = loading;
}
static VtLibraryItem fixture[8];
int vt_i18n_current_language(void) {
    return language ? VT_LANG_IT : VT_LANG_EN;
}
int ui_local_media_library_tick(void) {
    return 0;
}
int vt_library_search_home_favourites(VtLibraryItem *out, int cap, VtLibrarySearchStatus *status) {
    memset(status, 0, sizeof(*status));
    if (dashboard_empty)
        return 0;
    const int saved[] = {5, 6, 4};
    int count = cap < 3 ? cap : 3;
    for (int i = 0; i < count; i++)
        out[i] = fixture[saved[i]];
    return count;
}
int vt_library_search_home_media(VtLibraryResult *out, int cap) {
    if (dashboard_empty)
        return 0;
    return vt_library_query("", VT_LIBRARY_ALL, out, cap);
}
void vt_library_search_cancel(void) {}
unsigned vt_library_search_revision(void) {
    return publication_fixture && frame >= 16 ? 1 : 0;
}
void vt_library_search_local_refresh(void) {}
void vt_library_search_cover(const VtLibraryItem *v) {
    (void)v;
}
int vt_library_search_get_session(const VtNetworkSource *s, VtNetworkCredential *c) {
    (void)s;
    (void)c;
    return 0;
}
void ui_online_audio_set_home(int home) {
    (void)home;
}
void vt_library_search_begin(const char *q, const VtLibraryItem *folder) {
    (void)q;
    (void)folder;
    search_calls++;
}
int vt_library_search_snapshot(VtLibraryResult *out, int cap, VtLibrarySearchStatus *status) {
    memset(status, 0, sizeof(*status));
    if (progressive && frame >= 16) {
        VtLibraryItem v = fixture[0];
        strcpy(v.title, "AAA newly arrived item");
        strcpy(v.locator, "ux0:new-item.mp4");
        vt_library_identity(&v);
        vt_library_put(&v);
        status->pending = 0;
    } else if (progressive)
        status->pending = 1;
    if (loading_fixture) {
        status->pending = 3;
        status->state[VT_LIBRARY_LOCAL] = VT_SEARCH_READY;
        status->state[VT_LIBRARY_NAVIDROME] = VT_SEARCH_LOADING;
        status->state[VT_LIBRARY_RADIO] = VT_SEARCH_LOADING;
        status->state[VT_LIBRARY_IMMICH] = VT_SEARCH_LOGIN;
        status->state[VT_LIBRARY_PODCAST] = VT_SEARCH_ERROR;
        if (loading_fixture == 2)
            return 0;
    }
    int n = vt_library_query(query, VT_LIBRARY_ALL, out, cap);
    status->revision = vt_library_search_revision();
    if (publication_fixture && frame >= 16)
        for (int i = 0; i < n; i++)
            strcpy(out[i].item.artwork, "late-fixture-cover");
    if (progressive == 2 && frame >= 16) {
        int k = 0;
        for (int i = 0; i < n; i++)
            if (strcmp(out[i].item.id, fixture[0].id))
                out[k++] = out[i];
        n = k;
    }
    return n;
}
int vt_network_sources_load(VtNetworkSource *s, int cap) {
    if (!dashboard_sources)
        return 0;
    CHECK(cap >= 2);
    memset(s, 0, 2 * sizeof(*s));
    s[0].protocol = VT_NETWORK_SFTP;
    strcpy(s[0].name, "NAS / sample data");
    strcpy(s[0].host, "nas.fixture");
    s[1].protocol = VT_NETWORK_JELLYFIN;
    strcpy(s[1].name, "Jellyfin / sample data");
    strcpy(s[1].host, "jellyfin.fixture");
    return 2;
}
int vt_network_credentials_load(VtNetworkCredential *s, int cap) {
    (void)s;
    (void)cap;
    return 0;
}
int vt_preferences_remember_network_passwords(void) {
    return 0;
}
int ui_network_library_authorize(VtNetworkSource *s, VtNetworkCredential *c) {
    (void)s;
    (void)c;
    return -1;
}
void vt_network_media_history_id(const VtNetworkSource *s, const char *p, char out[16]) {
    (void)s;
    (void)p;
    strcpy(out, "r-fixture");
}
void vt_playback_history_local_id(const char *p, char out[16]) {
    (void)p;
    strcpy(out, "media-fixture");
}
uint64_t vt_playback_history_order(const char *p) {
    (void)p;
    return 0;
}
int vt_playback_history_progress(const char *id, uint64_t *p, uint64_t *d) {
    (void)id;
    (void)p;
    (void)d;
    return 0;
}
vita2d_texture *vt_video_thumbnail_get(const char *p, uint64_t size) {
    (void)size;
    return vt_artwork_thumbnail_get(p);
}
static void script_exit(void) {
    memset(input_script, 0, sizeof(input_script));
    script_count = 24;
    input_script[20] = SCE_CTRL_LTRIGGER;
    input_script[22] = SCE_CTRL_CROSS;
    capture_frame = 18;
    frame = 0;
}
int main(int argc, char **argv) {
    CHECK(argc == 3);
    output = argv[2];
    CHECK(!FT_Init_FreeType(&ft));
    CHECK(!FT_New_Face(ft, argv[1], 0, &face));
    language = 1;
    fixture_count = 8;
    for (int i = 0; i < fixture_count; i++) {
        fixture[i] = (VtLibraryItem){.provider = i,
                                     .kind = i == 5   ? VT_LIBRARY_LIVE
                                             : i == 3 ? VT_LIBRARY_IMAGE
                                                      : VT_LIBRARY_AUDIO,
                                     .duration_ms = 3600000};
        snprintf(fixture[i].locator, sizeof(fixture[i].locator), "fixture:%d", i);
        snprintf(fixture[i].title, sizeof(fixture[i].title), "%s - Night City",
                 vt_library_provider_name(i));
        snprintf(fixture[i].artist, sizeof(fixture[i].artist), "Test artist / sample data");
        snprintf(fixture[i].album, sizeof(fixture[i].album), "Test album");
        strcpy(fixture[i].artwork, "fixture-cover");
        snprintf(fixture[i].history_id, sizeof(fixture[i].history_id), "fixture-%d", i);
        vt_library_identity(&fixture[i]);
        vt_library_started(&fixture[i]);
        vt_library_progress(fixture[i].history_id, 60000 + i * 90000, 3600000);
    }
    for (int theme = 0; theme < VT_UI_THEME_COUNT; theme++)
        for (int mode = 0; mode < 4; mode++) {
            ui_theme_apply(theme);
            loaded = 1;
            ui_home_tab_set(HOME_LIBRARY);
            view = mode;
            query[0] = 0;
            navigation.valid = 0;
            mini_top = theme % 2 ? 472 : 536;
            script_exit();
            VtLibraryItem selected;
            CHECK(ui_library_screen(&selected) == UI_LIBRARY_SECTION_BASE + UI_SECTION_LOCAL_MEDIA);
            CHECK(frame == 22);
            if (theme == VT_UI_THEME_SIGNAL)
                CHECK(first_pixels[120][4][0] > 150);
            memcpy(pixels, first_pixels, sizeof(pixels));
            char name[80];
            snprintf(name, sizeof(name), "library-theme-%d-view-%d", theme, mode);
            png(name);
        }
    /* Loading coexists with cached content; all shells get compact activity
     * without covering their wheel, sidebar, bottom tabs or mini-player. */
    for (int theme = 0; theme < VT_UI_THEME_COUNT; theme++) {
        ui_theme_apply(theme);
        for (loading_fixture = 1; loading_fixture <= 2; loading_fixture++) {
            view = 3;
            query[0] = 0;
            navigation.valid = 0;
            script_exit();
            VtLibraryItem chosen;
            CHECK(ui_library_screen(&chosen) == UI_LIBRARY_SECTION_BASE + UI_SECTION_LOCAL_MEDIA);
            CHECK(!loading_flag);
            memcpy(pixels, first_pixels, sizeof(pixels));
            char name[80];
            snprintf(name, sizeof(name), "search-theme-%d-%s", theme,
                     loading_fixture == 1 ? "partial" : "loading");
            png(name);
        }
        VtLibrarySearchStatus active = {.pending = 1};
        active.state[VT_LIBRARY_NAVIDROME] = VT_SEARCH_LOADING;
        host_reduced_motion = 0;
        memset(pixels, 0, sizeof(pixels));
        search_activity(&active, -1, 24, 60, 900, 0);
        memcpy(first_pixels, pixels, sizeof(pixels));
        memset(pixels, 0, sizeof(pixels));
        search_activity(&active, -1, 24, 60, 900, 600000);
        CHECK(memcmp(first_pixels, pixels, sizeof(pixels)));
        host_reduced_motion = 1;
        memset(pixels, 0, sizeof(pixels));
        search_activity(&active, -1, 24, 60, 900, 0);
        memcpy(first_pixels, pixels, sizeof(pixels));
        memset(pixels, 0, sizeof(pixels));
        search_activity(&active, -1, 24, 60, 900, 600000);
        CHECK(!memcmp(first_pixels, pixels, sizeof(pixels)));
        host_reduced_motion = 0;
    }
    loading_fixture = 0;
    /* Dashboard is one heterogeneous surface, with every group visible and
     * reachable. The backend is deliberately synthetic, including empty state. */
    const unsigned directions[] = {SCE_CTRL_UP, SCE_CTRL_DOWN, SCE_CTRL_LEFT, SCE_CTRL_RIGHT};
    for (int theme = 0; theme < VT_UI_THEME_COUNT; theme++)
        for (int bottom = 472; bottom <= 536; bottom += 64) {
            UiDashboardLayout l = ui_dashboard_layout(theme, bottom);
            int reachable[UI_DASH_COUNT] = {1};
            for (int iteration = 0; iteration < UI_DASH_COUNT; iteration++)
                for (int from = 0; from < UI_DASH_COUNT; from++)
                    if (reachable[from])
                        for (int dir = 0; dir < 4; dir++)
                            reachable[ui_dashboard_move(&l, from, directions[dir])] = 1;
            for (int i = 0; i < UI_DASH_COUNT; i++) {
                UiThemeRect a = l.cards[i];
                CHECK(a.x >= l.content.x && a.y >= l.content.y);
                CHECK(a.x + a.w <= l.content.x + l.content.w &&
                      a.y + a.h <= l.content.y + l.content.h);
                CHECK(a.h >= 36 && a.w >= 140 && reachable[i]);
                for (int j = 0; j < i; j++) {
                    UiThemeRect b = l.cards[j];
                    CHECK(a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y ||
                          b.y + b.h <= a.y);
                }
            }
        }
    /* Every collection-availability combination keeps visible targets disjoint
     * and reachable, including the mini-player's shorter viewport. */
    for (int theme = 0; theme < VT_UI_THEME_COUNT; theme++)
        for (int bottom = 472; bottom <= 536; bottom += 64)
            for (int mask = 0; mask < 16; mask++) {
                int counts[4];
                for (int i = 0; i < 4; i++) counts[i] = !!(mask & (1 << i));
                UiDashboardLayout l = ui_dashboard_compose(theme, bottom, counts);
                int reached[UI_DASH_COUNT] = {1};
                for (int n = 0; n < UI_DASH_COUNT; n++)
                    for (int i = 0; i < UI_DASH_COUNT; i++) if (reached[i])
                        for (int d = 0; d < 4; d++)
                            reached[ui_dashboard_move(&l, i, directions[d])] = 1;
                for (int i = 0; i < UI_DASH_COUNT; i++) {
                    UiThemeRect a = l.cards[i];
                    if (!a.h) continue;
                    CHECK(reached[i]);
                    for (int j = 0; j < i; j++) {
                        UiThemeRect b = l.cards[j];
                        if (!b.h) continue;
                        CHECK(a.x + a.w <= b.x || b.x + b.w <= a.x || a.y + a.h <= b.y || b.y + b.h <= a.y);
                    }
                }
            }
    loading_fixture = 0;
    progressive = 0;
    for (int theme = 0; theme < VT_UI_THEME_COUNT; theme++)
        for (dashboard_empty = 0; dashboard_empty <= 1; dashboard_empty++) {
            ui_theme_apply(theme);
            dashboard_sources = !dashboard_empty;
            mini_top = theme % 2 ? 472 : 536;
            view = LIBRARY_HOME;
            dash_selected = UI_DASH_CONTINUE;
            memset(dash_offset, 0, sizeof(dash_offset));
            memset(dash_anchor, 0, sizeof(dash_anchor));
            ui_home_tab_set(HOME_LIBRARY);
            script_exit();
            VtLibraryItem picked;
            CHECK(ui_library_screen(&picked) == UI_LIBRARY_SECTION_BASE + UI_SECTION_LOCAL_MEDIA);
            CHECK(view == LIBRARY_HOME && !loading_flag);
            memcpy(pixels, first_pixels, sizeof(pixels));
            char name[80];
            snprintf(name, sizeof(name), "home-dashboard-%d-%s", theme,
                     dashboard_empty ? "empty" : "populated");
            png(name);
        }
    /* Unplayed indexed media must populate Home without inventing history. */
    dashboard_empty = 0;
    dashboard_cold = 1;
    dashboard_sources = 0;
    for (int theme = 0; theme < VT_UI_THEME_COUNT; theme++) {
        ui_theme_apply(theme);
        view = LIBRARY_HOME;
        DashboardData *cold = calloc(1, sizeof(*cold));
        VtLibrarySearchStatus cold_status;
        dashboard_refresh(cold, &cold_status, 0);
        CHECK(cold->count[0] > 0 && !cold->resuming);
        CHECK(cold->count[3] > 0 && cold->discovering);
        free(cold);
        dash_selected = UI_DASH_CONTINUE;
        memset(dash_offset, 0, sizeof(dash_offset));
        memset(dash_anchor, 0, sizeof(dash_anchor));
        script_exit();
        VtLibraryItem picked;
        CHECK(ui_library_screen(&picked) == UI_LIBRARY_SECTION_BASE + UI_SECTION_LOCAL_MEDIA);
        memcpy(pixels, first_pixels, sizeof(pixels));
        char name[80];
        snprintf(name, sizeof(name), "home-dashboard-%d-no-history", theme);
        png(name);
    }
    dashboard_cold = 0;
    dashboard_sources = 1;
    mini_top = 536;
    ui_theme_apply(VT_UI_THEME_LEGACY);
    ui_home_tab_set(HOME_LIBRARY);
    view = LIBRARY_HOME;
    dash_selected = UI_DASH_CONNECTIONS;
    memset(dash_offset, 0, sizeof(dash_offset));
    memset(dash_anchor, 0, sizeof(dash_anchor));
    script_exit();
    input_script[2] = SCE_CTRL_CROSS;
    VtLibraryItem connection;
    CHECK(ui_library_screen(&connection) == UI_LIBRARY_CONNECT);
    CHECK(!strcmp(connection.title, "NAS / sample data") &&
          connection.provider == VT_LIBRARY_NETWORK);
    view = LIBRARY_HOME;
    dash_selected = UI_DASH_CONTINUE;
    memset(dash_offset, 0, sizeof(dash_offset));
    memset(dash_anchor, 0, sizeof(dash_anchor));
    script_exit();
    input_script[2] = SCE_CTRL_RTRIGGER;
    input_script[18] = SCE_CTRL_CROSS;
    CHECK(ui_library_screen(&connection) == UI_LIBRARY_PLAY);
    CHECK(view == LIBRARY_HOME && dash_offset[UI_DASH_CONTINUE] == 1);
    /* A background expiry cannot replace an imminent Cross with another item. */
    view = LIBRARY_HOME;
    dash_selected = UI_DASH_CONTINUE;
    dashboard_drop_resume = 1;
    memset(dash_offset, 0, sizeof(dash_offset));
    memset(dash_anchor, 0, sizeof(dash_anchor));
    VtLibraryResult expected_resume[DASH_CONTINUE_MAX];
    CHECK(vt_library_query("", VT_LIBRARY_CONTINUE, expected_resume, DASH_CONTINUE_MAX) > 0);
    script_exit();
    input_script[18] = SCE_CTRL_CROSS;
    CHECK(ui_library_screen(&connection) == UI_LIBRARY_PLAY);
    CHECK(!strcmp(connection.id, expected_resume[0].item.id));
    dashboard_drop_resume = 0;
    /* A favourite preview is its own touch target, including non-primary covers. */
    view = LIBRARY_HOME;
    dash_selected = UI_DASH_FAVOURITES;
    memset(dash_offset, 0, sizeof(dash_offset));
    memset(dash_anchor, 0, sizeof(dash_anchor));
    script_exit();
    UiThemeRect favourite = ui_dashboard_layout(VT_UI_THEME_LEGACY, 536).cards[UI_DASH_FAVOURITES];
    int cover_size = favourite.h - 42;
    if (cover_size > 64)
        cover_size = 64;
    tap_frame = 2;
    tap_x = favourite.x + 10 + (favourite.w - 20) / 3 + cover_size / 2;
    tap_y = favourite.y + 30 + cover_size / 2;
    CHECK(ui_library_screen(&connection) == UI_LIBRARY_PLAY);
    CHECK(!strcmp(connection.id, fixture[6].id));
    tap_frame = -1;
    /* History is a deliberate drill-down. Circle returns to the composition. */
    view = LIBRARY_HOME;
    dash_selected = UI_DASH_HISTORY;
    script_exit();
    input_script[2] = SCE_CTRL_CROSS;
    input_script[6] = SCE_CTRL_CIRCLE;
    CHECK(ui_library_screen(&connection) == UI_LIBRARY_SECTION_BASE + UI_SECTION_LOCAL_MEDIA);
    CHECK(view == LIBRARY_HOME);
    dashboard_sources = 0;
    /* A visible secondary server row opens that server, not the first row. */
    dashboard_sources = 1;
    view = LIBRARY_HOME;
    dash_selected = UI_DASH_CONNECTIONS;
    memset(dash_offset, 0, sizeof(dash_offset));
    memset(dash_anchor, 0, sizeof(dash_anchor));
    UiDashboardLayout server_layout = ui_dashboard_layout(VT_UI_THEME_LEGACY, 536);
    tap_frame = 2;
    tap_x = server_layout.cards[UI_DASH_CONNECTIONS].x + 100;
    tap_y = server_layout.cards[UI_DASH_CONNECTIONS].y + 28 + 48 + 12;
    script_exit();
    CHECK(ui_library_screen(&connection) == UI_LIBRARY_CONNECT);
    CHECK(connection.provider == VT_LIBRARY_JELLYFIN);
    tap_frame = -1;
    dashboard_sources = 0;
    /* Actual UI retains focused identity when a sorted progressive result arrives. */
    ui_theme_apply(VT_UI_THEME_LEGACY);
    view = 3;
    query[0] = 0;
    navigation = (typeof(navigation)){.valid = 1, .focus = 0, .filter = -1};
    progressive = 1;
    mini_top = 536;
    script_exit();
    input_script[18] = SCE_CTRL_CROSS;
    VtLibraryItem selected;
    CHECK(ui_library_screen(&selected) == UI_LIBRARY_PLAY);
    CHECK(!strcmp(selected.id, fixture[0].id));
    CHECK(frame == 18);
    progressive = 2;
    navigation = (typeof(navigation)){.valid = 1, .focus = 0, .filter = -1};
    script_exit();
    input_script[18] = SCE_CTRL_CROSS;
    CHECK(ui_library_screen(&selected) == UI_LIBRARY_PLAY);
    CHECK(!strcmp(selected.id, fixture[0].id));
    /* Re-entering after playback restores that selected identity. */
    progressive = 0;
    script_exit();
    input_script[1] = SCE_CTRL_CROSS;
    CHECK(ui_library_screen(&selected) == UI_LIBRARY_PLAY);
    CHECK(!strcmp(selected.id, fixture[0].id));
    /* A late cover can publish after all providers finish without a registry
     * revision. The screen must observe the search publication itself. */
    publication_fixture = 1;
    navigation = (typeof(navigation)){.valid = 1, .focus = 0, .filter = -1};
    script_exit();
    input_script[18] = SCE_CTRL_CROSS;
    CHECK(ui_library_screen(&selected) == UI_LIBRARY_PLAY);
    CHECK(!strcmp(selected.artwork, "late-fixture-cover"));
    publication_fixture = 0;
    /* Sources has a concrete local entry, and R1 cycles views in place. */
    view = 2;
    navigation = (typeof(navigation)){.valid = 1, .focus = 0, .filter = -1};
    script_exit();
    input_script[1] = SCE_CTRL_CROSS;
    CHECK(ui_library_screen(&selected) == UI_LIBRARY_LOCAL);
    CHECK(artwork_requests > 0 && search_calls >= 28);
    FT_Done_Face(face);
    FT_Done_FreeType(ft);
    printf("Unified Home UI: %u checks; 72 host layouts, integrated dashboard/navigation, "
           "animated/reduced-motion loading, all "
           "themes, covers, mini-player space, "
           "progressive focus and player return passed.\n",
           checks);
    return 0;
}

int ui_network_personal_resume_credential(const VtNetworkSource *s, VtNetworkCredential *c) {
    (void)s;
    (void)c;
    return 0;
}
vita2d_texture *vt_video_thumbnail_get_remote_priority(const VtNetworkSource *s,
                                                       const VtNetworkCredential *c, const char *p,
                                                       uint64_t size, int priority) {
    (void)s;
    (void)c;
    (void)p;
    (void)size;
    (void)priority;
    return NULL;
}
