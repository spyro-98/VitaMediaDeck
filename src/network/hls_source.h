#ifndef VMD_HLS_SOURCE_H
#define VMD_HLS_SOURCE_H
#include "network/iptv_source.h"
/* HLS demux/remux through the app's bounded HTTP transport, never FFmpeg's
 * network stack. Existing decoder stream factories and clocks are unchanged. */
void vt_hls_source_configure(VtIptvSourceConfig *config);
/* Exposed for deterministic security/selection fixtures. malloc output. */
int vt_hls_prepare_manifest(const void *data, size_t size, const char *base,
    const VtIptvSourceConfig *config, unsigned char **output, size_t *length);
#endif
