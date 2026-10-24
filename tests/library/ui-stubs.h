#ifndef VMD_LIBRARY_UI_STUBS_H
#define VMD_LIBRARY_UI_STUBS_H
#include "library/catalog.h"
#ifndef VMD_REAL_LIBRARY
void vt_library_cache(const VtLibraryItem *v) {
    (void)v;
}
void vt_library_started(const VtLibraryItem *v) {
    (void)v;
}
uint64_t vt_library_resume(const VtLibraryItem *v) {
    (void)v;
    return 0;
}
void vt_library_iptv(const VtIptvChannel *v, VtLibraryItem *out) {
    memset(out, 0, sizeof(*out));
    snprintf(out->id, 16, "u-test");
    snprintf(out->history_id, sizeof(out->history_id), "u-test");
    snprintf(out->locator, sizeof(out->locator), "%s", v->url);
}
void vt_library_remote(const VtNetworkSource *s, const VtNetworkEntry *v, const char *p,
                       VtLibraryItem *out) {
    (void)s;
    (void)v;
    (void)p;
    memset(out, 0, sizeof(*out));
}
void vt_library_online(const VtOnlineItem *v, const char *p, VtLibraryItem *out) {
    (void)p;
    memset(out, 0, sizeof(*out));
    snprintf(out->locator, sizeof(out->locator), "%s", v->url);
    out->provider = v->kind == VT_ONLINE_RADIO ? VT_LIBRARY_RADIO : VT_LIBRARY_PODCAST;
}
#endif
int vt_background_playback_library_matches(const char *id) {
    (void)id;
    return 0;
}
void vt_background_playback_set_library_id(const char *id) {
    (void)id;
}
uint64_t vt_background_playback_restore_podcast_history(const char *id, uint64_t duration) {
    (void)id;
    (void)duration;
    return 0;
}
#endif
