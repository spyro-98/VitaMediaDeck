/* Synthetic provider fixtures rendered by the production seven-theme Home. */
#define VMD_REAL_LIBRARY
#define VMD_ONLINE_UI
#define main previous_iptv_main
#include "../iptv/ui_test.c"
#undef main
#include "../../src/ui/home_tabs.c"
#include "../../src/ui/library_screen.c"
#include "history/playback_history.h"
static int search_calls, progressive, fixture_count;
static VtLibraryItem fixture[8];
int vt_i18n_current_language(void) {
    return language ? VT_LANG_IT : VT_LANG_EN;
}
int ui_local_media_library_tick(void) {
    return 0;
}
void vt_library_search_cancel(void) {}
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
    int n = vt_library_query(query, VT_LIBRARY_ALL, out, cap);
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
    (void)s;
    (void)cap;
    return 0;
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
    for (int theme = 0; theme < 7; theme++)
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
    /* Sources has a concrete local entry, and R1 cycles views in place. */
    view = 2;
    navigation = (typeof(navigation)){.valid = 1, .focus = 0, .filter = -1};
    script_exit();
    input_script[1] = SCE_CTRL_CROSS;
    CHECK(ui_library_screen(&selected) == UI_LIBRARY_LOCAL);
    CHECK(artwork_requests > 0 && search_calls >= 28);
    FT_Done_Face(face);
    FT_Done_FreeType(ft);
    printf("Unified Home UI: %u checks; 28 host layouts, all themes, covers, mini-player space, "
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
