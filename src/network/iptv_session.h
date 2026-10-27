#ifndef VMD_IPTV_SESSION_H
#define VMD_IPTV_SESSION_H
#include "network/iptv_source.h"
#include "media/vita_decoder.h"

typedef struct VtIptvSession VtIptvSession;
/* One worker, one bounded source and at most two independent A/V cursors.
 * Config may be NULL for production HTTP; injection is for source tests.
 * Session must outlive ALL decoder cursors. Close decoder before destroy. */
VtIptvSession *vt_iptv_session_create(const char *url, const VtIptvSourceConfig *config);
void vt_iptv_session_factory(VtIptvSession *session, VtDecoderStreamFactory *out);
void vt_iptv_session_cancel(VtIptvSession *session);
/* Cancel and join the producer on a loading/cleanup worker. Readers/source
 * stay valid until destroy; caller serializes this with destroy. Idempotent. */
void vt_iptv_session_stop_and_wait(VtIptvSession *session);
/* Nonblocking: after cancel, UI may animate until the producer has exited. */
int vt_iptv_session_stopped(VtIptvSession *session);
void vt_iptv_session_destroy(VtIptvSession *session);
/* Returns a source error / DISCONTINUITY / LAGGED, or zero while usable.
 * END is only observed by readers after draining cached bytes. */
int vt_iptv_session_event(VtIptvSession *session);
void vt_iptv_session_status(VtIptvSession *session, VtIptvSourceStatus *out);
#endif
