#include "network/http_audio_stream.h"
#include "network/online_audio.h"
#include "common/text_log.h"
#include <stdlib.h>
#include <string.h>
#include <vita_https.h>
typedef struct { VitaHttpsClient *client; VitaHttpsStream stream; } HttpAudio;
static int audio_read(void *ctx, void *data, size_t size) {
	HttpAudio *h = ctx; return h->stream.read(h->stream.opaque, data, size);
}
static int64_t audio_seek(void *ctx, int64_t offset, int whence) {
	HttpAudio *h = ctx; return h->stream.seek(h->stream.opaque, offset, whence);
}
static void audio_close(void *ctx) {
	HttpAudio *h = ctx;
	if (h->stream.close) h->stream.close(h->stream.opaque);
	vita_https_client_destroy(h->client); free(h);
}
static int audio_open(void *ctx, VtDecoderStreamHandle *out, volatile int *cancel) {
	const char *url = ctx;
	if (!vt_online_url_valid(url)) return -1;
	HttpAudio *h = calloc(1, sizeof(*h)); if (!h) return -1;
	VitaHttpsClientConfig config = { .user_agent = "VitaMediaDeck/1.3.0",
	    .allow_http = 1, .connect_timeout_ms = 10000, .request_timeout_ms = 20000 };
	h->client = vita_https_client_create(&config);
	if (!h->client) { free(h); return -1; }
	int ret = vita_https_open_range_stream(h->client, url, cancel, &h->stream);
	log_printf("podcast HTTP cursor: result=%d size=%lld cancelled=%d\n",
	    ret, (long long)h->stream.size, cancel && *cancel);
	if (ret < 0) { audio_close(h); return ret; }
	memset(out, 0, sizeof(*out));
	out->opaque = h; out->read = audio_read; out->seek = audio_seek;
	out->close = audio_close; out->size = h->stream.size;
	return 0;
}
static int audio_open_plain(void *ctx, VtDecoderStreamHandle *out) {
	return audio_open(ctx, out, NULL);
}
void vt_http_audio_stream_factory(const char *url, VtDecoderStreamFactory *out) {
	memset(out, 0, sizeof(*out)); out->opaque = (void *)url;
	out->open = audio_open_plain; out->open_cancelable = audio_open;
}
