#ifndef VMD_HTTP_AUDIO_STREAM_H
#define VMD_HTTP_AUDIO_STREAM_H
#include "media/vita_decoder.h"
/* URL must outlive the decoder. Each cursor owns its client and bounded cache. */
void vt_http_audio_stream_factory(const char *url, VtDecoderStreamFactory *out);
#endif
