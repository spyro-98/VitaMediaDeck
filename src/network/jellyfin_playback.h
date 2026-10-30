#ifndef VMD_JELLYFIN_PLAYBACK_H
#define VMD_JELLYFIN_PLAYBACK_H
#include "network/network_source.h"
#include "network/iptv_source.h"
typedef struct {
 char url[VT_IPTV_URL_MAX],item_id[64],media_source_id[64],session_id[64];
 uint64_t duration_ms,position_ms;
 int transcoding;
} VtJellyfinPlayback;
/* Worker only. Negotiates a Vita H.264/AAC profile; no transcoder starts until
 * the returned HLS URL is requested. force disables direct play for one retry. */
int vt_jellyfin_playback_prepare(const VtNetworkSource *,const VtNetworkCredential *,
 const char *path,uint64_t position_ms,int force,VtJellyfinPlayback *,volatile int *cancel);
typedef struct {
 const VtNetworkSource *source;
 const VtNetworkCredential *credential;
} VtJellyfinHlsTransport;
int vt_jellyfin_hls_fetch(void *,const char *,size_t,unsigned char **,size_t *,char *,size_t,volatile int *);
int vt_jellyfin_hls_fetch_range(void *,const char *,uint64_t,size_t,unsigned char **,size_t *,char *,size_t,volatile int *);
/* Decoder and HLS producer must have stopped before this worker-only call.
 * Session IDs, credentials and signed media URLs are never logged or stored. */
int vt_jellyfin_playback_stop(const VtNetworkSource *,const VtNetworkCredential *,VtJellyfinPlayback *,uint64_t position_ms);
#endif
