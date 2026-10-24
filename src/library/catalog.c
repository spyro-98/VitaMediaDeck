#include "library/catalog.h"
#include "history/playback_history.h"
#include "settings/preferences.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void vt_library_local(const VtLocalMediaItem *v, VtLibraryItem *o) {
    memset(o, 0, sizeof(*o));
    o->kind = v->is_directory ? VT_LIBRARY_FOLDER : (int)v->type;
    o->provider = VT_LIBRARY_LOCAL;
    snprintf(o->locator, sizeof(o->locator), "%s", v->path);
    snprintf(o->title, sizeof(o->title), "%s", v->name);
    snprintf(o->artist, sizeof(o->artist), "%s", v->artist);
    snprintf(o->album, sizeof(o->album), "%s", v->album);
    snprintf(o->artwork, sizeof(o->artwork), "%s", v->artwork_path);
    o->duration_ms = v->duration_ms;
    o->size = v->size;
    vt_playback_history_local_id(v->path, o->history_id);
    vt_library_identity(o);
}
void vt_library_remote(const VtNetworkSource *s, const VtNetworkEntry *v, const char *parent,
                       VtLibraryItem *o) {
    memset(o, 0, sizeof(*o));
    o->provider = s->protocol == VT_NETWORK_JELLYFIN    ? VT_LIBRARY_JELLYFIN
                  : s->protocol == VT_NETWORK_NAVIDROME ? VT_LIBRARY_NAVIDROME
                  : s->protocol == VT_NETWORK_IMMICH    ? VT_LIBRARY_IMMICH
                                                        : VT_LIBRARY_NETWORK;
    o->kind = v->is_directory ? VT_LIBRARY_FOLDER
              : v->is_image   ? VT_LIBRARY_IMAGE
              : v->is_audio   ? VT_LIBRARY_AUDIO
                              : VT_LIBRARY_VIDEO;
    snprintf(o->server_name, sizeof(o->server_name), "%s", s->name);
    o->mode = v->is_directory; /* 2 is a provider pagination token. */
    vt_network_media_history_id(s, "", o->server);
    vt_network_media_history_id(s, v->path, o->history_id);
    snprintf(o->locator, sizeof(o->locator), "%s", v->path);
    snprintf(o->context, sizeof(o->context), "%s", parent ? parent : "");
    snprintf(o->title, sizeof(o->title), "%s", v->name);
    snprintf(o->artist, sizeof(o->artist), "%s", v->artist);
    snprintf(o->album, sizeof(o->album), "%s", v->album);
    o->duration_ms = v->runtime_ms;
    o->size = v->size;
    vt_library_identity(o);
}
void vt_library_online(const VtOnlineItem *v, const char *feed, VtLibraryItem *o) {
    memset(o, 0, sizeof(*o));
    o->provider = v->kind == VT_ONLINE_RADIO ? VT_LIBRARY_RADIO : VT_LIBRARY_PODCAST;
    o->kind = v->kind == VT_ONLINE_RADIO     ? VT_LIBRARY_LIVE
              : v->kind == VT_ONLINE_EPISODE ? VT_LIBRARY_AUDIO
                                             : VT_LIBRARY_FEED;
    snprintf(o->locator, sizeof(o->locator), "%s", v->url);
    snprintf(o->title, sizeof(o->title), "%s", v->title);
    snprintf(o->artist, sizeof(o->artist), "%s", v->author);
    snprintf(o->album, sizeof(o->album), "%s", v->show_title);
    snprintf(o->artwork, sizeof(o->artwork), "%s", v->artwork_path);
    snprintf(o->artwork_url, sizeof(o->artwork_url), "%s", v->artwork_url);
    o->duration_ms = v->duration_ms;
    if (v->kind == VT_ONLINE_EPISODE) {
        char identity[1025];
        snprintf(identity, sizeof(identity), "%s#%s", feed ? feed : "", v->id[0] ? v->id : v->url);
        vt_playback_history_local_id(identity, o->history_id);
        memcpy(o->history_id, "podcs", 5);
        snprintf(o->context, sizeof(o->context), "%s", feed ? feed : "");
        /* GUID is stable even if the enclosure URL changes. */
        snprintf(o->server, sizeof(o->server), "%s", o->history_id);
    }
    vt_library_identity(o);
}
void vt_library_iptv(const VtIptvChannel *v, VtLibraryItem *o) {
    memset(o, 0, sizeof(*o));
    o->provider = VT_LIBRARY_IPTV;
    o->kind = v->mode == VT_IPTV_FILE   ? VT_LIBRARY_VIDEO
              : v->mode == VT_IPTV_LIST ? VT_LIBRARY_FOLDER
                                        : VT_LIBRARY_LIVE;
    o->mode = v->mode;
    snprintf(o->locator, sizeof(o->locator), "%s", v->url);
    snprintf(o->title, sizeof(o->title), "%s", v->title);
    snprintf(o->user_agent, sizeof(o->user_agent), "%s", v->user_agent);
    snprintf(o->artist, sizeof(o->artist), "%s", v->group);
    snprintf(o->artwork, sizeof(o->artwork), "%s", v->artwork_path);
    snprintf(o->artwork_url, sizeof(o->artwork_url), "%s", v->logo);
    vt_library_identity(o);
    snprintf(o->history_id, sizeof(o->history_id), "%s", o->id);
}
int vt_library_source(const VtLibraryItem *v, VtNetworkSource *out,
                      VtNetworkCredential *credential) {
    VtNetworkSource *sources = calloc(VT_NETWORK_MAX_SOURCES, sizeof(*sources));
    VtNetworkCredential *credentials = calloc(VT_NETWORK_MAX_SOURCES, sizeof(*credentials));
    if (!sources || !credentials) {
        free(sources);
        free(credentials);
        return -1;
    }
    int n = vt_network_sources_load(sources, VT_NETWORK_MAX_SOURCES);
    if (vt_preferences_remember_network_passwords())
        vt_network_credentials_load(credentials, VT_NETWORK_MAX_SOURCES);
    int found = -1;
    for (int i = 0; i < n; i++) {
        char key[16];
        vt_network_media_history_id(&sources[i], "", key);
        if (!strcmp(key, v->server)) {
            *out = sources[i];
            *credential = credentials[i];
            found = 0;
            break;
        }
    }
    memset(credentials, 0, VT_NETWORK_MAX_SOURCES * sizeof(*credentials));
    free(sources);
    free(credentials);
    return found;
}

void vt_library_cache(const VtLibraryItem *v) {
    vt_library_put(v);
    uint64_t p = 0, d = 0, order = vt_playback_history_order(v->history_id);
    if (order) {
        vt_playback_history_progress(v->history_id, &p, &d);
        vt_library_import(v, p, d, order);
    }
}
