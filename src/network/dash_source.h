#ifndef VMD_DASH_SOURCE_H
#define VMD_DASH_SOURCE_H
#include "network/iptv_source.h"
/* Select the app-side DASH -> MPEG-TS packet adapter. Existing decoder ABI,
 * HLS transport, shared session and two-cursor contracts are unchanged. */
void vt_dash_source_configure(VtIptvSourceConfig *config);
#endif
