#ifndef VMD_IPTV_PROBE_H
#define VMD_IPTV_PROBE_H
#include "network/iptv_source.h"
enum { VT_IPTV_PROBE_UNKNOWN=0, VT_IPTV_PROBE_REACHABLE=1,
    VT_IPTV_PROBE_DNS=2, VT_IPTV_PROBE_403=3, VT_IPTV_PROBE_404=4 };
static inline int vt_iptv_probe_hidden(int result) {
    return result==VT_IPTV_PROBE_DNS || result==VT_IPTV_PROBE_403 || result==VT_IPTV_PROBE_404;
}
/* One GET, no decoding, no saved payload: stop at the first body callback.
 * Unknown (including TLS/timeout/401) must never be hidden as a dead channel. */
int vt_iptv_http_probe(const VtIptvHttpOptions *options,const char *url,volatile int *cancel);
#endif
