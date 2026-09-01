#include "media/video_thumbnail.h"

#include <errno.h>
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jpeglib.h>
#include <png.h>

#include <psp2/gxm.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
#include <libavutil/pixfmt.h>

#include "common/text_log.h"
#include "media/image_loader.h"
#define THUMB_WIDTH                 264
#define THUMB_HEIGHT                148
#define THUMB_PIXELS                (THUMB_WIDTH * THUMB_HEIGHT)
#define THUMB_BYTES                 (THUMB_PIXELS * sizeof(uint16_t))
#define THUMB_PATH_MAX              VT_NETWORK_PATH_MAX
#define THUMB_REQUEST_CAPACITY      18
#define THUMB_TEXTURE_CAPACITY      12
#define THUMB_FAILURE_CAPACITY      24
#define THUMB_DISK_SLOTS            128
#define THUMB_CACHE_VERSION         6u
#define THUMB_FAILURE_RETRY_US      (30ULL * 1000ULL * 1000ULL)
#define THUMB_UPLOAD_RETRY_US       (500ULL * 1000ULL)
#define THUMB_DECODE_DEADLINE_US    (5ULL * 1000ULL * 1000ULL)
#define THUMB_FRAME_LOCAL_DEADLINE_US  (8ULL * 1000ULL * 1000ULL)
#define THUMB_FRAME_REMOTE_DEADLINE_US (12ULL * 1000ULL * 1000ULL)
#define THUMB_SALVAGE_DEADLINE_US   (1ULL * 1000ULL * 1000ULL)
#define THUMB_MAX_DEMUX_PACKETS     4096
#define THUMB_MAX_VIDEO_PACKETS     240
#define THUMB_AVIO_BUFFER_SIZE      (32 * 1024)
#define THUMB_COVER_MAX_PIXELS      (1024U * 1024U)
#define THUMB_THREAD_CREATE_PRIORITY  0x10000100
#define THUMB_THREAD_RUNTIME_PRIORITY 0xB0
#define THUMB_THREAD_STACK          0x100000
#define THUMB_CACHE_DIR             "ux0:data/VitaMediaDeck/thumbs"

typedef struct ThumbnailRequest {
	uint64_t key;
	uint64_t source_size;
	uint64_t sequence;
	unsigned int generation;
	int priority;
	int remote;
	int still_image;
	VtNetworkSource source;
	VtNetworkCredential credential;
	char path[THUMB_PATH_MAX];
} ThumbnailRequest;

typedef struct ThumbnailResult {
	uint64_t key;
	uint64_t source_size;
	unsigned int generation;
	char path[THUMB_PATH_MAX];
	uint16_t *pixels;
} ThumbnailResult;

typedef struct ThumbnailTexture {
	uint64_t key;
	uint64_t source_size;
	uint64_t used_us;
	char path[THUMB_PATH_MAX];
	vita2d_texture *texture;
} ThumbnailTexture;

typedef struct ThumbnailFailure {
	uint64_t key;
	uint64_t source_size;
	uint64_t retry_after_us;
	char path[THUMB_PATH_MAX];
} ThumbnailFailure;

typedef struct ThumbnailDiskHeader {
	unsigned char magic[8];
	uint32_t version;
	uint32_t width;
	uint32_t height;
	uint32_t pixel_format;
	uint64_t key;
	uint64_t source_size;
	uint32_t payload_size;
	uint32_t checksum;
} ThumbnailDiskHeader;

typedef struct ThumbnailInterrupt {
	uint64_t deadline_us;
	unsigned int generation;
} ThumbnailInterrupt;

typedef struct ThumbnailInput {
	VtDecoderStreamHandle stream;
	AVIOContext *avio;
	AVFormatContext *format;
	ThumbnailInterrupt *interrupt;
} ThumbnailInput;

typedef struct ThumbnailState {
	void *self;
	SceUID thid;
	volatile int initialized;
	volatile int enabled;
	volatile int stop;
	volatile int done;
	volatile int lock;
	volatile int active_cancel;
	volatile unsigned int generation;
	uint64_t request_sequence;
	ThumbnailRequest requests[THUMB_REQUEST_CAPACITY];
	int request_count;
	ThumbnailRequest active;
	int active_valid;
	volatile int worker_busy;
	void (*active_abort)(void *opaque);
	void *active_abort_opaque;
	ThumbnailResult result;
	uint64_t result_retry_after_us;
	ThumbnailTexture textures[THUMB_TEXTURE_CAPACITY];
	ThumbnailFailure failures[THUMB_FAILURE_CAPACITY];
} ThumbnailState;

static ThumbnailState g_thumbnail = { .thid = -1 };

static int thumbnail_information_score(const uint16_t *pixels);

static void thumbnail_lock(void) {
	while (__sync_lock_test_and_set(&g_thumbnail.lock, 1))
		sceKernelDelayThread(100);
}

static void thumbnail_unlock(void) {
	__sync_lock_release(&g_thumbnail.lock);
}

static int same_request(uint64_t key, uint64_t source_size,
	                    unsigned int generation, const char *path,
	                    const ThumbnailRequest *request) {
	return request && request->key == key &&
	       request->source_size == source_size &&
	       request->generation == generation && path &&
	       strcmp(request->path, path) == 0;
}

static uint64_t thumbnail_hash_text(uint64_t hash, const char *text) {
	for (const unsigned char *cursor = (const unsigned char *)text;
	     cursor && *cursor; cursor++) {
		hash ^= *cursor;
		hash *= 1099511628211ULL;
	}
	hash ^= 0xffU;
	return hash * 1099511628211ULL;
}

static uint64_t thumbnail_hash_u64(uint64_t hash, uint64_t value) {
	for (unsigned int shift = 0; shift < 64; shift += 8) {
		hash ^= (unsigned char)(value >> shift);
		hash *= 1099511628211ULL;
	}
	return hash;
}

static uint64_t thumbnail_key(const char *path, uint64_t source_size,
	                          int still_image) {
	uint64_t hash = thumbnail_hash_text(14695981039346656037ULL, path);
	hash = thumbnail_hash_u64(hash, source_size);
	return thumbnail_hash_u64(hash, still_image ? 1U : 0U);
}

static uint64_t remote_thumbnail_key(const VtNetworkSource *source,
	                                 const char *path,
	                                 uint64_t source_size) {
	uint64_t hash = 14695981039346656037ULL;
	hash = thumbnail_hash_u64(hash, source ? source->protocol : 0);
	hash = thumbnail_hash_text(hash, source ? source->host : "");
	hash = thumbnail_hash_u64(hash, source ? source->port : 0);
	hash = thumbnail_hash_text(hash, source ? source->username : "");
	hash = thumbnail_hash_text(hash, source ? source->domain : "");
	hash = thumbnail_hash_text(hash, source ? source->root_path : "");
	hash = thumbnail_hash_text(hash, source ? source->share : "");
	hash = thumbnail_hash_text(hash, path);
	return thumbnail_hash_u64(hash, source_size);
}

static uint32_t payload_checksum(const uint16_t *pixels) {
	uint32_t hash = 2166136261U;
	const unsigned char *bytes = (const unsigned char *)pixels;
	for (size_t i = 0; i < THUMB_BYTES; i++) {
		hash ^= bytes[i];
		hash *= 16777619U;
	}
	return hash;
}

static int read_fully(SceUID fd, void *buffer, size_t size) {
	unsigned char *out = buffer;
	size_t done = 0;
	while (done < size) {
		int count = sceIoRead(fd, out + done, size - done);
		if (count <= 0) return count < 0 ? count : -1;
		done += (size_t)count;
	}
	return 0;
}

static int write_fully(SceUID fd, const void *buffer, size_t size) {
	const unsigned char *data = buffer;
	size_t done = 0;
	while (done < size) {
		int count = sceIoWrite(fd, data + done, size - done);
		if (count <= 0) return count < 0 ? count : -1;
		done += (size_t)count;
	}
	return 0;
}

static void cache_paths(uint64_t key, char path[96], char temporary[96]) {
	unsigned int slot = (unsigned int)((key ^ (key >> 32)) &
	                                   (THUMB_DISK_SLOTS - 1));
	snprintf(path, 96, THUMB_CACHE_DIR "/%03u.rgb", slot);
	snprintf(temporary, 96, THUMB_CACHE_DIR "/%03u.tmp", slot);
}

static int cache_load(const ThumbnailRequest *request, uint16_t *pixels) {
	char path[96], temporary[96];
	cache_paths(request->key, path, temporary);
	(void)temporary;
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0) return fd;
	ThumbnailDiskHeader header;
	int result = read_fully(fd, &header, sizeof(header));
	if (result == 0 &&
	    (memcmp(header.magic, "VTTHMB3", 8) != 0 ||
	     header.version != THUMB_CACHE_VERSION ||
	     header.width != THUMB_WIDTH || header.height != THUMB_HEIGHT ||
	     header.pixel_format != 1 || header.key != request->key ||
	     header.source_size != request->source_size ||
	     header.payload_size != THUMB_BYTES)) result = -1;
	if (result == 0) result = read_fully(fd, pixels, THUMB_BYTES);
	if (result == 0 && header.checksum != payload_checksum(pixels)) result = -1;
	sceIoClose(fd);
	return result;
}

static void cache_store(const ThumbnailRequest *request,
	                    const uint16_t *pixels) {
	if (g_thumbnail.stop || !g_thumbnail.enabled || g_thumbnail.active_cancel ||
	    request->generation != g_thumbnail.generation) return;
	sceIoMkdir("ux0:data/VitaMediaDeck", 0777);
	sceIoMkdir(THUMB_CACHE_DIR, 0777);
	char path[96], temporary[96];
	cache_paths(request->key, path, temporary);
	ThumbnailDiskHeader header;
	memset(&header, 0, sizeof(header));
	memcpy(header.magic, "VTTHMB3", 8);
	header.version = THUMB_CACHE_VERSION;
	header.width = THUMB_WIDTH;
	header.height = THUMB_HEIGHT;
	header.pixel_format = 1;
	header.key = request->key;
	header.source_size = request->source_size;
	header.payload_size = THUMB_BYTES;
	header.checksum = payload_checksum(pixels);
	sceIoRemove(temporary);
	SceUID fd = sceIoOpen(temporary,
	                      SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (fd < 0) return;
	int result = write_fully(fd, &header, sizeof(header));
	if (result == 0) result = write_fully(fd, pixels, THUMB_BYTES);
	/* This is a disposable, checksummed cache. A synchronous flash flush delayed
	 * the first visible cover and could overlap player startup for no durability
	 * benefit; close + verified-on-read publication is sufficient. */
	sceIoClose(fd);
	if (result == 0 && !g_thumbnail.stop && g_thumbnail.enabled &&
	    !g_thumbnail.active_cancel &&
	    request->generation == g_thumbnail.generation) {
		/* These files are disposable cache entries. The verified header prevents
		 * a power-loss window from ever being treated as a valid thumbnail. */
		sceIoRemove(path);
		if (sceIoRename(temporary, path) < 0) sceIoRemove(temporary);
	} else {
		sceIoRemove(temporary);
	}
}

static int thumbnail_cancelled(const ThumbnailInterrupt *interrupt) {
	return g_thumbnail.stop || !g_thumbnail.enabled || g_thumbnail.active_cancel ||
	       (interrupt && interrupt->generation != g_thumbnail.generation);
}

static int thumbnail_interrupted(void *opaque) {
	const ThumbnailInterrupt *interrupt = opaque;
	return thumbnail_cancelled(interrupt) ||
	       (interrupt && interrupt->deadline_us &&
	        sceKernelGetProcessTimeWide() >= interrupt->deadline_us);
}

static int thumbnail_stream_read(void *opaque, uint8_t *buffer, int size) {
	ThumbnailInput *input = opaque;
	if (!input || !input->stream.read ||
	    thumbnail_interrupted(input->interrupt)) return AVERROR_EXIT;
	int result = input->stream.read(input->stream.opaque, buffer, (size_t)size);
	if (result == 0) return AVERROR_EOF;
	return result < 0 ? AVERROR(EIO) : result;
}

static int64_t thumbnail_stream_seek(void *opaque, int64_t offset, int whence) {
	ThumbnailInput *input = opaque;
	if (!input || thumbnail_interrupted(input->interrupt)) return AVERROR_EXIT;
	if (whence == AVSEEK_SIZE) return input->stream.size;
	if (!input->stream.seek) return AVERROR(ENOSYS);
	return input->stream.seek(input->stream.opaque, offset,
	                          whence & ~AVSEEK_FORCE);
}

static void thumbnail_input_close(ThumbnailInput *input) {
	if (!input) return;
	/* Withdraw the non-owning abort target before releasing cursor ownership. */
	thumbnail_lock();
	if (g_thumbnail.active_abort_opaque == input->stream.opaque) {
		g_thumbnail.active_abort = NULL;
		g_thumbnail.active_abort_opaque = NULL;
	}
	thumbnail_unlock();
	if (input->format) avformat_close_input(&input->format);
	if (input->avio) {
		av_freep(&input->avio->buffer);
		avio_context_free(&input->avio);
	}
	if (input->stream.close && input->stream.opaque)
		input->stream.close(input->stream.opaque);
	memset(input, 0, sizeof(*input));
}

static int thumbnail_input_open(ThumbnailInput *input,
	                            const VtDecoderStreamFactory *factory,
	                            ThumbnailInterrupt *interrupt) {
	if (!input || !factory || (!factory->open && !factory->open_cancelable))
		return AVERROR(EINVAL);
	memset(input, 0, sizeof(*input));
	input->interrupt = interrupt;
	int result = factory->open_cancelable
	           ? factory->open_cancelable(factory->opaque, &input->stream,
	                                      &g_thumbnail.active_cancel)
	           : factory->open(factory->opaque, &input->stream);
	if (result < 0 || !input->stream.read || !input->stream.seek) {
		thumbnail_input_close(input);
		return result < 0 ? result : AVERROR(EINVAL);
	}
	thumbnail_lock();
	g_thumbnail.active_abort = input->stream.abort;
	g_thumbnail.active_abort_opaque = input->stream.opaque;
	thumbnail_unlock();
	/* Connection/authentication has its own cooperative cancellation. Bound all
	 * container parsing and decoding from the first byte after that point. */
	if (interrupt && !interrupt->deadline_us)
		interrupt->deadline_us = sceKernelGetProcessTimeWide() +
		                         THUMB_DECODE_DEADLINE_US;
	unsigned char *buffer = av_malloc(THUMB_AVIO_BUFFER_SIZE);
	if (!buffer) {
		thumbnail_input_close(input);
		return AVERROR(ENOMEM);
	}
	input->avio = avio_alloc_context(buffer, THUMB_AVIO_BUFFER_SIZE, 0,
	                                input, thumbnail_stream_read, NULL,
	                                thumbnail_stream_seek);
	if (!input->avio) {
		av_free(buffer);
		thumbnail_input_close(input);
		return AVERROR(ENOMEM);
	}
	input->avio->seekable = AVIO_SEEKABLE_NORMAL;
	input->format = avformat_alloc_context();
	if (!input->format) {
		thumbnail_input_close(input);
		return AVERROR(ENOMEM);
	}
	input->format->pb = input->avio;
	input->format->flags |= AVFMT_FLAG_CUSTOM_IO | AVFMT_FLAG_FAST_SEEK;
	input->format->interrupt_callback.callback = thumbnail_interrupted;
	input->format->interrupt_callback.opaque = interrupt;
	input->format->probesize = 1024 * 1024;
	input->format->max_analyze_duration = 2 * AV_TIME_BASE;
	result = avformat_open_input(&input->format, NULL, NULL, NULL);
	if (result < 0) thumbnail_input_close(input);
	return result;
}

static int clamp_byte(int value) {
	return value < 0 ? 0 : value > 255 ? 255 : value;
}

static int sample_plane(const uint8_t *data, int stride, int width, int height,
	                    int64_t x_fp, int64_t y_fp, int component_step,
	                    int component_offset) {
	if (!data || width <= 0 || height <= 0) return 128;
	int64_t max_x = (int64_t)(width - 1) << 16;
	int64_t max_y = (int64_t)(height - 1) << 16;
	if (x_fp < 0) x_fp = 0;
	if (y_fp < 0) y_fp = 0;
	if (x_fp > max_x) x_fp = max_x;
	if (y_fp > max_y) y_fp = max_y;
	int x0 = (int)(x_fp >> 16), y0 = (int)(y_fp >> 16);
	int x1 = x0 + 1 < width ? x0 + 1 : x0;
	int y1 = y0 + 1 < height ? y0 + 1 : y0;
	uint32_t fx = (uint32_t)x_fp & 0xffffU;
	uint32_t fy = (uint32_t)y_fp & 0xffffU;
	int p00 = data[y0 * stride + x0 * component_step + component_offset];
	int p10 = data[y0 * stride + x1 * component_step + component_offset];
	int p01 = data[y1 * stride + x0 * component_step + component_offset];
	int p11 = data[y1 * stride + x1 * component_step + component_offset];
	uint32_t top = (uint32_t)p00 * (65536U - fx) + (uint32_t)p10 * fx;
	uint32_t bottom = (uint32_t)p01 * (65536U - fx) + (uint32_t)p11 * fx;
	uint32_t top8 = (top + 32768U) >> 16;
	uint32_t bottom8 = (bottom + 32768U) >> 16;
	return (int)((top8 * (65536U - fy) + bottom8 * fy + 32768U) >> 16);
}

static uint16_t yuv_to_rgb565(int y, int u, int v, int full_range,
	                          int bt709) {
	int d = u - 128, e = v - 128;
	int r, g, b;
	if (full_range) {
		if (bt709) {
			r = y + ((403 * e + 128) >> 8);
			g = y - ((48 * d + 120 * e + 128) >> 8);
			b = y + ((475 * d + 128) >> 8);
		} else {
			r = y + ((359 * e + 128) >> 8);
			g = y - ((88 * d + 183 * e + 128) >> 8);
			b = y + ((454 * d + 128) >> 8);
		}
	} else {
		int c = y - 16;
		if (c < 0) c = 0;
		if (bt709) {
			r = (298 * c + 459 * e + 128) >> 8;
			g = (298 * c - 55 * d - 136 * e + 128) >> 8;
			b = (298 * c + 541 * d + 128) >> 8;
		} else {
			r = (298 * c + 409 * e + 128) >> 8;
			g = (298 * c - 100 * d - 208 * e + 128) >> 8;
			b = (298 * c + 516 * d + 128) >> 8;
		}
	}
	r = clamp_byte(r); g = clamp_byte(g); b = clamp_byte(b);
	return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

static int frame_to_rgb565(const AVFrame *frame, uint16_t *pixels) {
	if (!frame || !pixels || frame->width <= 0 || frame->height <= 0)
		return AVERROR(EINVAL);
	int planar = frame->format == AV_PIX_FMT_YUV420P ||
	             frame->format == AV_PIX_FMT_YUVJ420P;
	int nv12 = frame->format == AV_PIX_FMT_NV12;
	int packed_step = 0, red_offset = 0, green_offset = 1, blue_offset = 2;
	if (frame->format == AV_PIX_FMT_RGB24) packed_step = 3;
	else if (frame->format == AV_PIX_FMT_BGR24) {
		packed_step = 3; red_offset = 2; blue_offset = 0;
	} else if (frame->format == AV_PIX_FMT_RGBA) packed_step = 4;
	else if (frame->format == AV_PIX_FMT_BGRA) {
		packed_step = 4; red_offset = 2; blue_offset = 0;
	} else if (frame->format == AV_PIX_FMT_ARGB) {
		packed_step = 4; red_offset = 1; green_offset = 2; blue_offset = 3;
	} else if (frame->format == AV_PIX_FMT_ABGR) {
		packed_step = 4; red_offset = 3; green_offset = 2; blue_offset = 1;
	}
	if (!planar && !nv12 && !packed_step) return AVERROR(ENOSYS);
	int source_w = frame->width, source_h = frame->height;
	int crop_x = 0, crop_y = 0, crop_w = source_w, crop_h = source_h;
	if ((int64_t)source_w * THUMB_HEIGHT > (int64_t)source_h * THUMB_WIDTH) {
		crop_w = (int)((int64_t)source_h * THUMB_WIDTH / THUMB_HEIGHT);
		crop_x = (source_w - crop_w) / 2;
	} else {
		crop_h = (int)((int64_t)source_w * THUMB_HEIGHT / THUMB_WIDTH);
		crop_y = (source_h - crop_h) / 2;
	}
	int chroma_w = (source_w + 1) / 2;
	int chroma_h = (source_h + 1) / 2;
	int full_range = frame->color_range == AVCOL_RANGE_JPEG ||
	                 frame->format == AV_PIX_FMT_YUVJ420P;
	int bt709 = frame->colorspace == AVCOL_SPC_BT709 ||
	            (frame->colorspace == AVCOL_SPC_UNSPECIFIED && source_w >= 720);
	for (int out_y = 0; out_y < THUMB_HEIGHT; out_y++) {
		int64_t sy = ((int64_t)crop_y << 16) - 32768 +
		             (((int64_t)(out_y * 2 + 1) * crop_h) << 15) /
		             THUMB_HEIGHT;
		for (int out_x = 0; out_x < THUMB_WIDTH; out_x++) {
			int64_t sx = ((int64_t)crop_x << 16) - 32768 +
			             (((int64_t)(out_x * 2 + 1) * crop_w) << 15) /
			             THUMB_WIDTH;
			if (packed_step) {
				int r = sample_plane(frame->data[0], frame->linesize[0],
				                     source_w, source_h, sx, sy,
				                     packed_step, red_offset);
				int g = sample_plane(frame->data[0], frame->linesize[0],
				                     source_w, source_h, sx, sy,
				                     packed_step, green_offset);
				int b = sample_plane(frame->data[0], frame->linesize[0],
				                     source_w, source_h, sx, sy,
				                     packed_step, blue_offset);
				pixels[out_y * THUMB_WIDTH + out_x] =
					(uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
				continue;
			}
			int y = sample_plane(frame->data[0], frame->linesize[0],
			                     source_w, source_h, sx, sy, 1, 0);
			int u, v;
			if (planar) {
				u = sample_plane(frame->data[1], frame->linesize[1],
				                 chroma_w, chroma_h, sx / 2, sy / 2, 1, 0);
				v = sample_plane(frame->data[2], frame->linesize[2],
				                 chroma_w, chroma_h, sx / 2, sy / 2, 1, 0);
			} else {
				u = sample_plane(frame->data[1], frame->linesize[1],
				                 chroma_w, chroma_h, sx / 2, sy / 2, 2, 0);
				v = sample_plane(frame->data[1], frame->linesize[1],
				                 chroma_w, chroma_h, sx / 2, sy / 2, 2, 1);
			}
			pixels[out_y * THUMB_WIDTH + out_x] =
				yuv_to_rgb565(y, u, v, full_range, bt709);
		}
	}
	return 0;
}

static int rgb24_to_rgb565(const unsigned char *data, int stride,
	                       int source_w, int source_h, uint16_t *pixels) {
	if (!data || !pixels || source_w <= 0 || source_h <= 0 || stride < source_w * 3)
		return AVERROR(EINVAL);
	int crop_x = 0, crop_y = 0, crop_w = source_w, crop_h = source_h;
	if ((int64_t)source_w * THUMB_HEIGHT > (int64_t)source_h * THUMB_WIDTH) {
		crop_w = (int)((int64_t)source_h * THUMB_WIDTH / THUMB_HEIGHT);
		crop_x = (source_w - crop_w) / 2;
	} else {
		crop_h = (int)((int64_t)source_w * THUMB_HEIGHT / THUMB_WIDTH);
		crop_y = (source_h - crop_h) / 2;
	}
	for (int out_y = 0; out_y < THUMB_HEIGHT; out_y++) {
		int64_t sy = ((int64_t)crop_y << 16) - 32768 +
		             (((int64_t)(out_y * 2 + 1) * crop_h) << 15) /
		             THUMB_HEIGHT;
		for (int out_x = 0; out_x < THUMB_WIDTH; out_x++) {
			int64_t sx = ((int64_t)crop_x << 16) - 32768 +
			             (((int64_t)(out_x * 2 + 1) * crop_w) << 15) /
			             THUMB_WIDTH;
			int r = sample_plane(data, stride, source_w, source_h, sx, sy, 3, 0);
			int g = sample_plane(data, stride, source_w, source_h, sx, sy, 3, 1);
			int b = sample_plane(data, stride, source_w, source_h, sx, sy, 3, 2);
			pixels[out_y * THUMB_WIDTH + out_x] =
				(uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
		}
	}
	return 0;
}

static int rgba_to_rgb565(const unsigned char *data, int stride,
	                       int source_w, int source_h, uint16_t *pixels) {
	if (!data || !pixels || source_w <= 0 || source_h <= 0 ||
	    stride < source_w * 4) return AVERROR(EINVAL);
	int crop_x = 0, crop_y = 0, crop_w = source_w, crop_h = source_h;
	if ((int64_t)source_w * THUMB_HEIGHT > (int64_t)source_h * THUMB_WIDTH) {
		crop_w = (int)((int64_t)source_h * THUMB_WIDTH / THUMB_HEIGHT);
		crop_x = (source_w - crop_w) / 2;
	} else {
		crop_h = (int)((int64_t)source_w * THUMB_HEIGHT / THUMB_WIDTH);
		crop_y = (source_h - crop_h) / 2;
	}
	for (int out_y = 0; out_y < THUMB_HEIGHT; out_y++) {
		int64_t sy = ((int64_t)crop_y << 16) - 32768 +
		             (((int64_t)(out_y * 2 + 1) * crop_h) << 15) /
		             THUMB_HEIGHT;
		for (int out_x = 0; out_x < THUMB_WIDTH; out_x++) {
			int64_t sx = ((int64_t)crop_x << 16) - 32768 +
			             (((int64_t)(out_x * 2 + 1) * crop_w) << 15) /
			             THUMB_WIDTH;
			int r = sample_plane(data, stride, source_w, source_h, sx, sy, 4, 0);
			int g = sample_plane(data, stride, source_w, source_h, sx, sy, 4, 1);
			int b = sample_plane(data, stride, source_w, source_h, sx, sy, 4, 2);
			int a = sample_plane(data, stride, source_w, source_h, sx, sy, 4, 3);
			/* Composite transparency against the media backdrop instead of leaving
			 * uninitialized RGB under fully transparent pixels. */
			r = (r * a + 8 * (255 - a)) / 255;
			g = (g * a + 16 * (255 - a)) / 255;
			b = (b * a + 24 * (255 - a)) / 255;
			pixels[out_y * THUMB_WIDTH + out_x] =
				(uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
		}
	}
	return 0;
}

static int decode_still_thumbnail(const ThumbnailRequest *request,
	                              uint16_t *pixels) {
	if (!request || !pixels || request->remote) return AVERROR(EINVAL);
	VtDecodedImage decoded;
	char error[128];
	int result = vt_image_decode(request->path, 512, &decoded,
	                             error, sizeof(error));
	if (result < 0) {
		log_printf("image thumbnail decode failed: %s: %s\n",
		           request->path, error);
		return AVERROR_INVALIDDATA;
	}
	result = rgba_to_rgb565(decoded.pixels, (int)decoded.stride,
	                       (int)decoded.width, (int)decoded.height, pixels);
	vt_image_decoded_free(&decoded);
	return result;
}

/* A syntactically valid cover can still be a fade-to-black frame. On an OLED
 * background that is indistinguishable from a missing texture and, more
 * importantly, prevents the useful representative-frame fallback from ever
 * running. Keep this deliberately conservative: only almost-uniform or
 * overwhelmingly black images are rejected. */
static int thumbnail_information_score(const uint16_t *pixels) {
	if (!pixels) return 0;
	uint64_t sum = 0, squared = 0;
	unsigned int visible = 0;
	int minimum = 255, maximum = 0;
	for (unsigned int i = 0; i < THUMB_PIXELS; i++) {
		uint16_t pixel = pixels[i];
		int red = ((pixel >> 11) & 0x1f) * 255 / 31;
		int green = ((pixel >> 5) & 0x3f) * 255 / 63;
		int blue = (pixel & 0x1f) * 255 / 31;
		int luma = (77 * red + 150 * green + 29 * blue) >> 8;
		if (luma < minimum) minimum = luma;
		if (luma > maximum) maximum = luma;
		if (luma >= 18) visible++;
		sum += (uint64_t)luma;
		squared += (uint64_t)luma * (uint64_t)luma;
	}
	if (maximum <= 12 || visible < THUMB_PIXELS / 200U) return 0;
	uint64_t mean = sum / THUMB_PIXELS;
	uint64_t variance = squared / THUMB_PIXELS - mean * mean;
	int range = maximum - minimum;
	if (range < 8 && variance < 4) return 0;
	uint64_t score = variance + (uint64_t)range * 4U +
	                 (uint64_t)visible * 100U / THUMB_PIXELS;
	return score > 0x7fffffffULL ? 0x7fffffff : (int)score;
}

typedef struct ThumbnailJpegError {
	struct jpeg_error_mgr base;
	jmp_buf jump;
	unsigned char *buffer;
	int decoder_created;
} ThumbnailJpegError;

static void thumbnail_jpeg_error_exit(j_common_ptr common) {
	ThumbnailJpegError *error = (ThumbnailJpegError *)common->err;
	longjmp(error->jump, 1);
}

static int decode_jpeg_cover(const unsigned char *data, size_t size,
	                         uint16_t *pixels) {
	if (!data || size < 4 || !pixels) return AVERROR(EINVAL);
	struct jpeg_decompress_struct decoder;
	ThumbnailJpegError error;
	memset(&decoder, 0, sizeof(decoder));
	memset(&error, 0, sizeof(error));
	decoder.err = jpeg_std_error(&error.base);
	error.base.error_exit = thumbnail_jpeg_error_exit;
	if (setjmp(error.jump)) {
		free(error.buffer);
		if (error.decoder_created) jpeg_destroy_decompress(&decoder);
		return AVERROR_INVALIDDATA;
	}
	jpeg_create_decompress(&decoder);
	error.decoder_created = 1;
	jpeg_mem_src(&decoder, data, (unsigned long)size);
	if (jpeg_read_header(&decoder, TRUE) != JPEG_HEADER_OK)
		thumbnail_jpeg_error_exit((j_common_ptr)&decoder);
	decoder.out_color_space = JCS_RGB;
	static const unsigned int denominators[] = { 8, 4, 2, 1 };
	for (unsigned int i = 0; i < sizeof(denominators) / sizeof(denominators[0]); i++) {
		unsigned int denominator = denominators[i];
		if (decoder.image_width >= THUMB_WIDTH * denominator &&
		    decoder.image_height >= THUMB_HEIGHT * denominator) {
			decoder.scale_num = 1;
			decoder.scale_denom = denominator;
			break;
		}
	}
	jpeg_start_decompress(&decoder);
	if (decoder.output_components != 3 || decoder.output_width == 0 ||
	    decoder.output_height == 0 ||
	    (uint64_t)decoder.output_width * decoder.output_height >
	        THUMB_COVER_MAX_PIXELS) {
		jpeg_abort_decompress(&decoder);
		jpeg_destroy_decompress(&decoder);
		error.decoder_created = 0;
		return AVERROR(EINVAL);
	}
	size_t stride = (size_t)decoder.output_width * 3U;
	size_t bytes = stride * decoder.output_height;
	error.buffer = malloc(bytes);
	if (!error.buffer)
		thumbnail_jpeg_error_exit((j_common_ptr)&decoder);
	while (decoder.output_scanline < decoder.output_height) {
		JSAMPROW row = error.buffer + (size_t)decoder.output_scanline * stride;
		jpeg_read_scanlines(&decoder, &row, 1);
	}
	jpeg_finish_decompress(&decoder);
	int result = rgb24_to_rgb565(error.buffer, (int)stride,
	                             (int)decoder.output_width,
	                             (int)decoder.output_height, pixels);
	free(error.buffer);
	error.buffer = NULL;
	jpeg_destroy_decompress(&decoder);
	error.decoder_created = 0;
	return result;
}

static int decode_png_cover(const unsigned char *data, size_t size,
	                        uint16_t *pixels) {
	if (!data || size < 8 || !pixels) return AVERROR(EINVAL);
	png_image image;
	memset(&image, 0, sizeof(image));
	image.version = PNG_IMAGE_VERSION;
	if (!png_image_begin_read_from_memory(&image, data, size))
		return AVERROR_INVALIDDATA;
	if (!image.width || !image.height ||
	    (uint64_t)image.width * image.height > THUMB_COVER_MAX_PIXELS) {
		png_image_free(&image);
		return AVERROR(EINVAL);
	}
	image.format = PNG_FORMAT_RGB;
	size_t bytes = PNG_IMAGE_SIZE(image);
	unsigned char *buffer = malloc(bytes);
	if (!buffer) {
		png_image_free(&image);
		return AVERROR(ENOMEM);
	}
	int decoded = png_image_finish_read(&image, NULL, buffer, 0, NULL);
	int result = decoded
	           ? rgb24_to_rgb565(buffer, (int)PNG_IMAGE_ROW_STRIDE(image),
	                             (int)image.width, (int)image.height, pixels)
	           : AVERROR_INVALIDDATA;
	free(buffer);
	png_image_free(&image);
	return result;
}

static int choose_target_and_seek(AVFormatContext *format, int stream_index) {
	AVStream *stream = format->streams[stream_index];
	int64_t duration_us = 0;
	if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0)
		duration_us = av_rescale_q(stream->duration, stream->time_base,
		                           AV_TIME_BASE_Q);
	else if (format->duration != AV_NOPTS_VALUE && format->duration > 0)
		duration_us = format->duration;
	int64_t target_us = 2 * AV_TIME_BASE;
	if (duration_us > 0) {
		target_us = duration_us < 6 * AV_TIME_BASE
		          ? duration_us / 3 : duration_us / 10;
		if (target_us < 2 * AV_TIME_BASE && duration_us >= 3 * AV_TIME_BASE)
			target_us = 2 * AV_TIME_BASE;
		if (target_us > 10 * AV_TIME_BASE) target_us = 10 * AV_TIME_BASE;
		if (target_us >= duration_us - AV_TIME_BASE / 2)
			target_us = duration_us / 2;
	}
	int64_t target = av_rescale_q(target_us, AV_TIME_BASE_Q, stream->time_base);
	if (stream->start_time != AV_NOPTS_VALUE) target += stream->start_time;
	/* A bounded probe can leave AVIOContext.error latched to AVERROR_EXIT.
	 * FFmpeg's seek resets EOF but not that error field, so explicitly recover
	 * the reusable cursor before either indexed seek. */
	if (format->pb) {
		format->pb->error = 0;
		format->pb->eof_reached = 0;
	}
	int result = av_seek_frame(format, stream_index, target, AVSEEK_FLAG_BACKWARD);
	if (result < 0) {
		if (format->pb) {
			format->pb->error = 0;
			format->pb->eof_reached = 0;
		}
		result = av_seek_frame(format, stream_index, 0, AVSEEK_FLAG_BACKWARD);
	}
	if (result >= 0) avformat_flush(format);
	return result;
}

static int decode_attached_cover(AVFormatContext *format, int stream_index,
	                             uint16_t *pixels,
	                             ThumbnailInterrupt *interrupt) {
	if (!format || stream_index < 0 || !pixels) return AVERROR(EINVAL);
	AVStream *stream = format->streams[stream_index];
	const unsigned char *data = stream->attached_pic.data;
	size_t size = stream->attached_pic.size > 0
	            ? (size_t)stream->attached_pic.size : 0;
	if ((!data || !size) && stream->codecpar->extradata &&
	    stream->codecpar->extradata_size > 0) {
		data = stream->codecpar->extradata;
		size = (size_t)stream->codecpar->extradata_size;
	}
	if (!data || !size) return AVERROR_INVALIDDATA;
	if (thumbnail_interrupted(interrupt)) return AVERROR_EXIT;
	if (size >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff)
		return decode_jpeg_cover(data, size, pixels);
	static const unsigned char png_signature[8] = {
		0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'
	};
	if (size >= sizeof(png_signature) &&
	    memcmp(data, png_signature, sizeof(png_signature)) == 0)
		return decode_png_cover(data, size, pixels);
	return AVERROR_DECODER_NOT_FOUND;
}

static int receive_thumbnail_frames(AVCodecContext *decoder, AVFrame *frame,
	                                uint16_t *pixels,
	                                ThumbnailInterrupt *interrupt,
	                                int *converted, int *frames_received) {
	if (frames_received) *frames_received = 0;
	for (;;) {
		if (thumbnail_interrupted(interrupt)) return AVERROR_EXIT;
		int receive_result = avcodec_receive_frame(decoder, frame);
		if (receive_result == AVERROR(EAGAIN) || receive_result == AVERROR_EOF)
			return 0;
		if (receive_result < 0) return receive_result;
		if (frames_received) (*frames_received)++;
		int result = frame_to_rgb565(frame, pixels);
		av_frame_unref(frame);
		if (result < 0) return result;
		/* Continue draining black/fade frames already buffered by the codec. A
		 * later picture in the same reorder window may be the first useful cover. */
		if (thumbnail_information_score(pixels) > 0) {
			*converted = 1;
			return 0;
		}
	}
}

static int send_thumbnail_packet(AVCodecContext *decoder,
	                             const AVPacket *packet, AVFrame *frame,
	                             uint16_t *pixels,
	                             ThumbnailInterrupt *interrupt,
	                             int *converted) {
	for (;;) {
		if (thumbnail_interrupted(interrupt)) return AVERROR_EXIT;
		int send_result = avcodec_send_packet(decoder, packet);
		if (send_result < 0 && send_result != AVERROR(EAGAIN))
			return send_result;
		int received = 0;
		int result = receive_thumbnail_frames(decoder, frame, pixels, interrupt,
		                                      converted, &received);
		if (result < 0 || *converted) return result;
		if (send_result >= 0) return 0;
		/* EAGAIN means the packet was not consumed. Draining must make progress
		 * before the exact same packet is retried; dropping it corrupts long-GOP
		 * and B-frame fallback decoding. */
		if (!received) return AVERROR(EIO);
	}
}

static void find_thumbnail_streams(AVFormatContext *format, int *cover_index,
	                                int *video_index) {
	if (cover_index) *cover_index = -1;
	if (video_index) *video_index = -1;
	if (!format) return;
	for (unsigned int i = 0; i < format->nb_streams; i++) {
		AVStream *candidate = format->streams[i];
		int image_attachment =
		    candidate->codecpar->codec_type == AVMEDIA_TYPE_ATTACHMENT &&
		    (candidate->codecpar->codec_id == AV_CODEC_ID_MJPEG ||
		     candidate->codecpar->codec_id == AV_CODEC_ID_PNG);
		if (((candidate->disposition & AV_DISPOSITION_ATTACHED_PIC) ||
		     image_attachment) &&
		    cover_index && *cover_index < 0) {
			*cover_index = (int)i;
			continue;
		}
		if (candidate->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) continue;
		/* The pinned Vita FFmpeg build contains the CPU H.264 decoder used by
		 * the representative-frame fallback. Do not select an advertised codec
		 * whose decoder was deliberately omitted from the Vita build. */
		if (video_index && *video_index < 0 &&
		    candidate->codecpar->codec_id == AV_CODEC_ID_H264 &&
		    avcodec_find_decoder_by_name("h264"))
			*video_index = (int)i;
	}
}

static int cover_payload_available(const AVStream *stream) {
	if (!stream) return 0;
	return (stream->attached_pic.data && stream->attached_pic.size > 0) ||
	       (stream->codecpar->extradata && stream->codecpar->extradata_size > 0);
}

enum {
	THUMB_ORIGIN_NONE = 0,
	THUMB_ORIGIN_EMBEDDED,
	THUMB_ORIGIN_PROVIDER,
	THUMB_ORIGIN_FRAME
};

static int decode_thumbnail(const ThumbnailRequest *request,
	                        const VtDecoderStreamFactory *factory,
	                        uint16_t *pixels, int *origin_out) {
	if (origin_out) *origin_out = THUMB_ORIGIN_NONE;
	ThumbnailInterrupt interrupt = {
		/* Network cursor setup observes active_cancel. Start the separate CPU/demux
		 * budget after a TLS/SSH/SMB cursor is open so a legitimate handshake does
		 * not make an otherwise cheap embedded-cover decode fail immediately. */
		.deadline_us = 0,
		.generation = request->generation
	};
	ThumbnailInput input;
	/* Local Vita paths such as ux0:/ and uma0:/ are not ordinary FFmpeg URLs.
	 * Use the same seekable custom AVIO contract for every source; the factory
	 * maps local paths through sceIo and remote paths through their transports. */
	int result = thumbnail_input_open(&input, factory, &interrupt);
	AVFormatContext *format = result >= 0 ? input.format : NULL;
	AVCodecContext *decoder = NULL;
	AVPacket *packet = NULL;
	AVFrame *frame = NULL;
	int cover_index = -1;
	int stream_index = -1;
	if (result >= 0) find_thumbnail_streams(format, &cover_index, &stream_index);
	int converted = 0;
	/* Read artwork already materialized by the demuxer before doing any probing. */
	if (result >= 0 && cover_index >= 0 && !thumbnail_interrupted(&interrupt)) {
		int cover_result = decode_attached_cover(format, cover_index, pixels,
		                                         &interrupt);
		if (cover_result >= 0 && thumbnail_information_score(pixels) > 0) {
			converted = 1;
			if (origin_out) *origin_out = THUMB_ORIGIN_EMBEDDED;
		} else if (cover_result >= 0) {
			/* A valid but fully black/uniform attachment is visually equivalent to
			 * missing artwork in the grid. Continue to the representative H.264 frame. */
			log_printf("video thumbnail: embedded cover unusable, using frame: %s\n",
			           request->path);
		}
	}
	/* Matroska may enumerate cover.jpg during open while materializing its packet
	 * only during stream discovery; older FFmpeg builds can also expose it as an
	 * image attachment backed by codec extradata. Probe whenever artwork is absent
	 * or incomplete, not only when H.264 dimensions happen to be incomplete. */
	if (result >= 0 && !converted &&
	    (cover_index < 0 ||
	     !cover_payload_available(format->streams[cover_index]) ||
	     stream_index < 0 ||
	     format->streams[stream_index]->codecpar->width <= 0 ||
	     format->streams[stream_index]->codecpar->height <= 0) &&
	    !thumbnail_interrupted(&interrupt)) {
		result = avformat_find_stream_info(format, NULL);
		uint64_t probe_finished = sceKernelGetProcessTimeWide();
		if (result < 0 && interrupt.deadline_us &&
		    probe_finished >= interrupt.deadline_us &&
		    !thumbnail_cancelled(&interrupt)) {
			find_thumbnail_streams(format, &cover_index, &stream_index);
			int cover_usable = cover_index >= 0 &&
			    cover_payload_available(format->streams[cover_index]);
			int video_usable = stream_index >= 0 &&
			    format->streams[stream_index]->codecpar->width > 0 &&
			    format->streams[stream_index]->codecpar->height > 0;
			if (cover_usable || video_usable) {
				/* Permit only a short verified salvage. Frame fallback must seek,
				 * which also resets any AVIO error latched by the interrupted probe. */
				interrupt.deadline_us = probe_finished +
				                        THUMB_SALVAGE_DEADLINE_US;
				result = 0;
				log_printf("video thumbnail: probe deadline salvaged cover=%d video=%d: %s\n",
				           cover_usable, video_usable, request->path);
			}
		}
		if (result >= 0) {
			find_thumbnail_streams(format, &cover_index, &stream_index);
			if (cover_index >= 0 && !thumbnail_interrupted(&interrupt)) {
				int cover_result = decode_attached_cover(format, cover_index, pixels,
				                                         &interrupt);
				if (cover_result >= 0 &&
				    thumbnail_information_score(pixels) > 0) {
					converted = 1;
					if (origin_out) *origin_out = THUMB_ORIGIN_EMBEDDED;
				} else if (cover_result >= 0) {
					log_printf("video thumbnail: probed cover unusable, using frame: %s\n",
					           request->path);
				}
			}
		}
	}
	if (result >= 0 && !converted && stream_index < 0)
		result = AVERROR_STREAM_NOT_FOUND;
	AVStream *stream = result >= 0 && !converted
	                 ? format->streams[stream_index] : NULL;
	/* Opening the container and inspecting embedded artwork have their own bounded
	 * cost. Missing or invalid artwork must not consume the representative-frame
	 * decoder's entire budget before the H.264 fallback begins. */
	if (result >= 0 && !converted) {
		interrupt.deadline_us = sceKernelGetProcessTimeWide() +
		    (request->remote ? THUMB_FRAME_REMOTE_DEADLINE_US
		                     : THUMB_FRAME_LOCAL_DEADLINE_US);
		if (format->pb) {
			format->pb->error = 0;
			format->pb->eof_reached = 0;
		}
	}
	/* H.264 explicitly uses the CPU decoder: thumbnail work must never claim
	 * SceVideodec or compete for its CDRAM surfaces. The single-threaded worker
	 * stays at low priority and remains wall-bounded even while a video mini-player
	 * is active, so uncached cells eventually receive their required frame cover. */
	const AVCodec *codec = result >= 0 && !converted
	                       ? avcodec_find_decoder_by_name("h264") : NULL;
	if (result >= 0 && !converted && !codec) result = AVERROR_DECODER_NOT_FOUND;
	if (result >= 0 && !converted) decoder = avcodec_alloc_context3(codec);
	if (result >= 0 && !converted && !decoder) result = AVERROR(ENOMEM);
	if (result >= 0 && !converted)
		result = avcodec_parameters_to_context(decoder, stream->codecpar);
	if (result >= 0 && !converted) {
		decoder->thread_count = 1;
		decoder->thread_type = 0;
		decoder->skip_loop_filter = AVDISCARD_ALL;
		decoder->flags2 |= AV_CODEC_FLAG2_FAST;
		result = avcodec_open2(decoder, codec, NULL);
	}
	if (result >= 0 && !converted && !thumbnail_interrupted(&interrupt)) {
		result = choose_target_and_seek(format, stream_index);
		if (result >= 0) {
			avcodec_flush_buffers(decoder);
			packet = av_packet_alloc();
			frame = av_frame_alloc();
			if (!packet || !frame) result = AVERROR(ENOMEM);
		}
	}
	int packets_read = 0, video_packets = 0;
	while (result >= 0 && !converted && packets_read < THUMB_MAX_DEMUX_PACKETS &&
	       video_packets < THUMB_MAX_VIDEO_PACKETS &&
	       !thumbnail_interrupted(&interrupt)) {
		int read_result = av_read_frame(format, packet);
		if (read_result < 0) {
			result = send_thumbnail_packet(decoder, NULL, frame, pixels,
			                               &interrupt, &converted);
			if (!converted && result >= 0) result = read_result;
			break;
		}
		packets_read++;
		if (packet->stream_index == stream_index) {
			video_packets++;
			result = send_thumbnail_packet(decoder, packet, frame, pixels,
			                               &interrupt, &converted);
		}
		av_packet_unref(packet);
	}
	if (thumbnail_interrupted(&interrupt)) result = AVERROR_EXIT;
	else if (!converted && result >= 0) result = AVERROR(EIO);
	else if (converted && origin_out && *origin_out == THUMB_ORIGIN_NONE)
		*origin_out = THUMB_ORIGIN_FRAME;
	av_frame_free(&frame);
	av_packet_free(&packet);
	avcodec_free_context(&decoder);
	thumbnail_input_close(&input);
	return result;
}

static void remember_failure(const ThumbnailRequest *request,
	                         uint64_t retry_delay_us) {
	uint64_t now = sceKernelGetProcessTimeWide();
	int slot = 0;
	for (int i = 0; i < THUMB_FAILURE_CAPACITY; i++) {
		if (!g_thumbnail.failures[i].path[0]) { slot = i; break; }
		if (g_thumbnail.failures[i].retry_after_us <
		    g_thumbnail.failures[slot].retry_after_us) slot = i;
	}
	ThumbnailFailure *failure = &g_thumbnail.failures[slot];
	failure->key = request->key;
	failure->source_size = request->source_size;
	failure->retry_after_us = now + retry_delay_us;
	snprintf(failure->path, sizeof(failure->path), "%s", request->path);
}

static int thumbnail_worker(SceSize args, void *argp) {
	(void)args;
	ThumbnailState *state = *(ThumbnailState **)argp;
	int priority_before = sceKernelGetThreadCurrentPriority();
	int priority_ret = sceKernelChangeThreadPriority(
		sceKernelGetThreadId(), THUMB_THREAD_RUNTIME_PRIORITY);
	log_printf("video thumbnail: thread priority %d -> %d ret=0x%08X\n",
	           priority_before, sceKernelGetThreadCurrentPriority(),
	           (unsigned)priority_ret);
	for (;;) {
		ThumbnailRequest request;
		int have_request = 0;
		thumbnail_lock();
		if (!state->stop && state->enabled && !state->result.pixels &&
		    state->request_count > 0) {
			/* The selected cell has a higher priority than its neighbours. Within
			 * the same class, prefer the newest viewport so rapid scrolling cannot
			 * strand the user behind stale off-screen five-second jobs. */
			int request_index = 0;
			for (int i = 1; i < state->request_count; i++) {
				if (state->requests[i].priority >
				        state->requests[request_index].priority ||
				    (state->requests[i].priority ==
				         state->requests[request_index].priority &&
				     state->requests[i].sequence >
				         state->requests[request_index].sequence))
					request_index = i;
			}
			request = state->requests[request_index];
			state->request_count--;
			if (request_index < state->request_count)
				memmove(state->requests + request_index,
				        state->requests + request_index + 1,
				        sizeof(state->requests[0]) *
				            (state->request_count - request_index));
			memset(&state->requests[state->request_count], 0,
			       sizeof(state->requests[state->request_count]));
			state->active = request;
			state->active_valid = 1;
			state->worker_busy = 1;
			state->active_cancel = 0;
			have_request = 1;
		}
		int should_stop = state->stop;
		thumbnail_unlock();
		if (should_stop) break;
		if (!have_request) {
			sceKernelDelayThread(8 * 1000);
			continue;
		}
		uint64_t started_us = sceKernelGetProcessTimeWide();
		uint16_t *pixels = malloc(THUMB_BYTES);
		int result = pixels ? cache_load(&request, pixels) : AVERROR(ENOMEM);
		int from_cache = result == 0;
		int origin = THUMB_ORIGIN_NONE;
		if (result < 0 && pixels && state->enabled && !state->stop &&
		    request.still_image) {
			result = decode_still_thumbnail(&request, pixels);
			if (result == 0) origin = THUMB_ORIGIN_PROVIDER;
		}
		if (result < 0 && pixels && state->enabled && !state->stop &&
		    !request.still_image) {
			if (request.remote &&
			    request.source.protocol == VT_NETWORK_JELLYFIN) {
				unsigned char *artwork = NULL;
				size_t artwork_size = 0;
				result = vt_network_fetch_artwork(
				    &request.source, &request.credential, request.path,
				    &artwork, &artwork_size, &state->active_cancel);
				if (result == 0 && artwork) {
					if (artwork_size >= 3 && artwork[0] == 0xff &&
					    artwork[1] == 0xd8 && artwork[2] == 0xff)
						result = decode_jpeg_cover(artwork, artwork_size, pixels);
					else if (artwork_size >= 8 && artwork[0] == 0x89 &&
					         !memcmp(artwork + 1, "PNG\r\n\x1a\n", 7))
						result = decode_png_cover(artwork, artwork_size, pixels);
					else result = AVERROR_INVALIDDATA;
					if (result == 0 && thumbnail_information_score(pixels) > 0)
						origin = THUMB_ORIGIN_PROVIDER;
					else if (result == 0) result = AVERROR_INVALIDDATA;
				}
				free(artwork);
			}
		}
		if (result < 0 && pixels && state->enabled && !state->stop &&
		    !state->active_cancel && !request.still_image) {
			VtDecoderStreamFactory factory;
			VtNetworkStreamFactory remote;
			memset(&factory, 0, sizeof(factory));
			memset(&remote, 0, sizeof(remote));
			if (request.remote)
				result = vt_network_stream_factory_init(
				    &remote, &request.source, &request.credential, request.path);
			else {
				vt_decoder_file_stream_factory(request.path, &factory);
				result = 0;
			}
			if (result >= 0)
				result = decode_thumbnail(&request,
				                          request.remote ? &remote.factory : &factory,
				                          pixels, &origin);
			memset(&remote.credential, 0, sizeof(remote.credential));
		}
		uint16_t *cache_pixels = NULL;
		if (result == 0 && !from_cache && pixels) {
			cache_pixels = malloc(THUMB_BYTES);
			if (cache_pixels) memcpy(cache_pixels, pixels, THUMB_BYTES);
		}
		uint64_t elapsed_ms =
		    (sceKernelGetProcessTimeWide() - started_us) / 1000ULL;
		int report_ready = 0;
		int report_failure = 0;
		int report_preempted = 0;
		thumbnail_lock();
		int was_preempted = state->active_cancel;
		state->active_cancel = 0;
		state->active_valid = 0;
		memset(&state->active, 0, sizeof(state->active));
		if (result == 0 && !was_preempted && state->enabled && !state->stop &&
		    request.generation == state->generation &&
		    !state->result.pixels) {
			state->result.key = request.key;
			state->result.source_size = request.source_size;
				state->result.generation = request.generation;
			snprintf(state->result.path, sizeof(state->result.path), "%s",
			         request.path);
				state->result.pixels = pixels;
				state->result_retry_after_us = 0;
			pixels = NULL;
			report_ready = 1;
		} else if (result < 0 && state->enabled && !state->stop &&
		           request.generation == state->generation) {
			if (was_preempted) report_preempted = 1;
			else {
				remember_failure(&request,
				                 result == AVERROR(EAGAIN)
				                     ? 2ULL * 1000ULL * 1000ULL
				                     : THUMB_FAILURE_RETRY_US);
				report_failure = 1;
			}
		} else if (was_preempted && state->enabled && !state->stop &&
		           request.generation == state->generation) {
			report_preempted = 1;
		}
		thumbnail_unlock();
		/* Publish to the UI before optional flash persistence. The UI owns `pixels`;
		 * the worker writes an independent copy so upload can happen immediately. */
		if (report_ready && cache_pixels) cache_store(&request, cache_pixels);
		free(cache_pixels);
		thumbnail_lock();
		state->worker_busy = 0;
		thumbnail_unlock();
		if (report_ready) {
			const char *method = from_cache ? "cache" :
			                     origin == THUMB_ORIGIN_EMBEDDED ? "embedded" :
			                     origin == THUMB_ORIGIN_PROVIDER ? "provider" :
			                     origin == THUMB_ORIGIN_FRAME ? "frame" : "unknown";
			log_printf("video thumbnail ready [%s] %llums: %s\n", method,
			           (unsigned long long)elapsed_ms, request.path);
		} else if (report_failure) {
			char error[96];
			if (av_strerror(result, error, sizeof(error)) < 0)
				snprintf(error, sizeof(error), "code %d", result);
			log_printf("video thumbnail failed %llums: %s -> %d (%s)\n",
			           (unsigned long long)elapsed_ms, request.path, result, error);
		} else if (report_preempted) {
			log_printf("video thumbnail preempted %llums: %s\n",
			           (unsigned long long)elapsed_ms, request.path);
		}
		free(pixels);
		memset(&request.credential, 0, sizeof(request.credential));
	}
	state->done = 1;
	__sync_synchronize();
	return sceKernelExitThread(0);
}

int vt_video_thumbnail_init(void) {
	if (g_thumbnail.initialized) return 0;
	memset(&g_thumbnail, 0, sizeof(g_thumbnail));
	g_thumbnail.thid = -1;
	g_thumbnail.self = &g_thumbnail;
	g_thumbnail.thid = sceKernelCreateThread(
		"VitaMediaDeckThumbnail", thumbnail_worker,
		THUMB_THREAD_CREATE_PRIORITY,
		THUMB_THREAD_STACK, 0, 0, NULL);
	if (g_thumbnail.thid < 0) return g_thumbnail.thid;
	int result = sceKernelStartThread(g_thumbnail.thid,
	                                  sizeof(g_thumbnail.self),
	                                  &g_thumbnail.self);
	if (result < 0) {
		sceKernelDeleteThread(g_thumbnail.thid);
		g_thumbnail.thid = -1;
		return result;
	}
	g_thumbnail.initialized = 1;
	log_printf("video thumbnail worker ready\n");
	return 0;
}

void vt_video_thumbnail_resume(void) {
	if (!g_thumbnail.initialized) return;
	thumbnail_lock();
	g_thumbnail.enabled = 1;
	/* suspend() increments generation before raising active_cancel. Therefore an
	 * old in-flight request remains cancelled by its generation mismatch even
	 * after this shared flag is cleared. Waiting for worker_busy here created a
	 * race on mini-player/full-player return: the new grid inherited cancel=1 and
	 * could not request any cover until another scene transition. */
	g_thumbnail.active_cancel = 0;
	thumbnail_unlock();
}

void vt_video_thumbnail_suspend(void) {
	if (!g_thumbnail.initialized) return;
	uint16_t *pixels = NULL;
	thumbnail_lock();
	g_thumbnail.enabled = 0;
	g_thumbnail.active_cancel = 1;
	if (g_thumbnail.active_abort && g_thumbnail.active_abort_opaque)
		g_thumbnail.active_abort(g_thumbnail.active_abort_opaque);
	g_thumbnail.generation++;
	g_thumbnail.request_count = 0;
	memset(g_thumbnail.requests, 0, sizeof(g_thumbnail.requests));
	/* The worker owns its private request copy. Drop the shared active copy so a
	 * suspended browser never keeps a remote password resident unnecessarily. */
	g_thumbnail.active_valid = 0;
	memset(&g_thumbnail.active, 0, sizeof(g_thumbnail.active));
	pixels = g_thumbnail.result.pixels;
	memset(&g_thumbnail.result, 0, sizeof(g_thumbnail.result));
	g_thumbnail.result_retry_after_us = 0;
	thumbnail_unlock();
	free(pixels);
}

void vt_video_thumbnail_prepare_playback(void) {
	if (!g_thumbnail.initialized) return;
	vt_video_thumbnail_suspend();
	/* All browser scenes fence their last frame before returning, but keep this
	 * API self-contained because it is also called by mini-player restoration. */
	vita2d_wait_rendering_done();
	int released = 0;
	for (int i = 0; i < THUMB_TEXTURE_CAPACITY; i++) {
		if (g_thumbnail.textures[i].texture) {
			vita2d_free_texture(g_thumbnail.textures[i].texture);
			released++;
		}
		memset(&g_thumbnail.textures[i], 0,
		       sizeof(g_thumbnail.textures[i]));
	}
	uint64_t deadline = sceKernelGetProcessTimeWide() + 250 * 1000ULL;
	while (g_thumbnail.worker_busy &&
	       sceKernelGetProcessTimeWide() < deadline)
		sceKernelDelayThread(1000);
	log_printf("video thumbnail playback handoff: textures=%d worker=%s\n",
	           released, g_thumbnail.worker_busy ? "still cancelling" : "idle");
}

void vt_video_thumbnail_pump(void) {
	if (!g_thumbnail.initialized || !g_thumbnail.enabled) return;
	ThumbnailResult result;
	memset(&result, 0, sizeof(result));
	uint64_t now = sceKernelGetProcessTimeWide();
	thumbnail_lock();
	if (g_thumbnail.result.pixels &&
	    (!g_thumbnail.result_retry_after_us ||
	     now >= g_thumbnail.result_retry_after_us)) {
		result = g_thumbnail.result;
		memset(&g_thumbnail.result, 0, sizeof(g_thumbnail.result));
		g_thumbnail.result_retry_after_us = 0;
	}
	thumbnail_unlock();
	if (!result.pixels) return;
	int slot = 0;
	for (int i = 0; i < THUMB_TEXTURE_CAPACITY; i++) {
		if (!g_thumbnail.textures[i].texture) { slot = i; break; }
		if (g_thumbnail.textures[i].used_us <
		    g_thumbnail.textures[slot].used_us) slot = i;
	}
	ThumbnailTexture *cached = &g_thumbnail.textures[slot];
	/* Free an evictable texture before allocating its replacement. Requiring a
	 * transient 13th CDRAM allocation made successful decodes disappear under
	 * normal decoder/UI pressure. */
	if (cached->texture) {
		vita2d_wait_rendering_done();
		vita2d_free_texture(cached->texture);
		memset(cached, 0, sizeof(*cached));
	}
	vita2d_texture *texture = vita2d_create_empty_texture_format(
		THUMB_WIDTH, THUMB_HEIGHT, SCE_GXM_TEXTURE_FORMAT_R5G6B5);
	if (!texture) {
		log_printf("video thumbnail GPU upload allocation failed: %s\n",
		           result.path);
		thumbnail_lock();
		if (g_thumbnail.enabled && !g_thumbnail.stop &&
		    result.generation == g_thumbnail.generation &&
		    !g_thumbnail.result.pixels) {
			g_thumbnail.result = result;
			g_thumbnail.result_retry_after_us = now + THUMB_UPLOAD_RETRY_US;
			result.pixels = NULL;
		}
		thumbnail_unlock();
		free(result.pixels);
		return;
	}
	unsigned int stride = vita2d_texture_get_stride(texture);
	unsigned char *destination = vita2d_texture_get_datap(texture);
	if (!destination || stride < THUMB_WIDTH * sizeof(uint16_t)) {
		log_printf("video thumbnail GPU upload layout invalid: %s\n",
		           result.path);
		vita2d_free_texture(texture);
		thumbnail_lock();
		if (g_thumbnail.enabled && !g_thumbnail.stop &&
		    result.generation == g_thumbnail.generation &&
		    !g_thumbnail.result.pixels) {
			g_thumbnail.result = result;
			g_thumbnail.result_retry_after_us = now + THUMB_UPLOAD_RETRY_US;
			result.pixels = NULL;
		}
		thumbnail_unlock();
		free(result.pixels);
		return;
	}
	for (int row = 0; row < THUMB_HEIGHT; row++)
		memcpy(destination + row * stride,
		       result.pixels + row * THUMB_WIDTH,
		       THUMB_WIDTH * sizeof(uint16_t));
	vita2d_texture_set_filters(texture, SCE_GXM_TEXTURE_FILTER_LINEAR,
	                           SCE_GXM_TEXTURE_FILTER_LINEAR);
	free(result.pixels);
	memset(cached, 0, sizeof(*cached));
	cached->key = result.key;
	cached->source_size = result.source_size;
	cached->used_us = now;
	cached->texture = texture;
	snprintf(cached->path, sizeof(cached->path), "%s", result.path);
	log_printf("video thumbnail uploaded: slot=%d %s\n", slot, result.path);
}

static vita2d_texture *thumbnail_get(
	const VtNetworkSource *source,
	const VtNetworkCredential *credential,
	const char *path, uint64_t source_size, int priority, int still_image) {
	if (!g_thumbnail.initialized || !g_thumbnail.enabled || !path || !path[0] ||
	    strlen(path) >= THUMB_PATH_MAX) return NULL;
	uint64_t key = source ? remote_thumbnail_key(source, path, source_size)
	                      : thumbnail_key(path, source_size, still_image);
	uint64_t now = sceKernelGetProcessTimeWide();
	for (int i = 0; i < THUMB_TEXTURE_CAPACITY; i++) {
		ThumbnailTexture *cached = &g_thumbnail.textures[i];
		if (cached->texture && cached->key == key &&
		    cached->source_size == source_size &&
		    strcmp(cached->path, path) == 0) {
			cached->used_us = now;
			return cached->texture;
		}
	}
	thumbnail_lock();
	unsigned int generation = g_thumbnail.generation;
	for (int i = 0; i < THUMB_FAILURE_CAPACITY; i++) {
		ThumbnailFailure *failure = &g_thumbnail.failures[i];
		if (failure->key == key && failure->source_size == source_size &&
		    strcmp(failure->path, path) == 0) {
			if (now < failure->retry_after_us) {
				thumbnail_unlock();
				return NULL;
			}
			memset(failure, 0, sizeof(*failure));
			break;
		}
	}
	if (g_thumbnail.active_valid &&
	    same_request(key, source_size, generation, path, &g_thumbnail.active)) {
		if (priority > g_thumbnail.active.priority)
			g_thumbnail.active.priority = priority;
		thumbnail_unlock();
		return NULL;
	}
	if (g_thumbnail.result.pixels && g_thumbnail.result.key == key &&
	    g_thumbnail.result.source_size == source_size &&
	    g_thumbnail.result.generation == generation &&
	    strcmp(g_thumbnail.result.path, path) == 0) {
		thumbnail_unlock();
		return NULL;
	}
	/* A newly selected cell must not wait behind an off-screen decode. Protocol
	 * setup and every later stream read/seek observe this cancellation flag. */
	if (g_thumbnail.active_valid &&
	    (priority > g_thumbnail.active.priority ||
	     (priority >= 100 && priority == g_thumbnail.active.priority))) {
		g_thumbnail.active_cancel = 1;
		if (g_thumbnail.active_abort && g_thumbnail.active_abort_opaque)
			g_thumbnail.active_abort(g_thumbnail.active_abort_opaque);
	}
	/* There is only one selected cell. Demote an older selected request so it
	 * cannot remain ahead of the current viewport after rapid navigation. */
	if (priority >= 100) {
		for (int i = 0; i < g_thumbnail.request_count; i++)
			if (g_thumbnail.requests[i].priority >= 100)
				g_thumbnail.requests[i].priority = 10;
	}
	for (int i = 0; i < g_thumbnail.request_count; i++) {
		if (same_request(key, source_size, generation, path,
		                 &g_thumbnail.requests[i])) {
			if (priority > g_thumbnail.requests[i].priority)
				g_thumbnail.requests[i].priority = priority;
			g_thumbnail.requests[i].sequence = ++g_thumbnail.request_sequence;
			thumbnail_unlock();
			return NULL;
		}
	}
	ThumbnailRequest *request;
	if (g_thumbnail.request_count == THUMB_REQUEST_CAPACITY) {
		int weakest = 0;
		for (int i = 1; i < g_thumbnail.request_count; i++) {
			if (g_thumbnail.requests[i].priority <
			        g_thumbnail.requests[weakest].priority ||
			    (g_thumbnail.requests[i].priority ==
			         g_thumbnail.requests[weakest].priority &&
			     g_thumbnail.requests[i].sequence <
			         g_thumbnail.requests[weakest].sequence))
				weakest = i;
		}
		request = &g_thumbnail.requests[weakest];
		memset(&request->credential, 0, sizeof(request->credential));
	} else {
		request = &g_thumbnail.requests[g_thumbnail.request_count++];
	}
	memset(request, 0, sizeof(*request));
	request->key = key;
	request->source_size = source_size;
	request->sequence = ++g_thumbnail.request_sequence;
	request->generation = generation;
	request->priority = priority;
	request->remote = source != NULL;
	request->still_image = still_image;
	if (source) request->source = *source;
	if (credential) request->credential = *credential;
	snprintf(request->path, sizeof(request->path), "%s", path);
	thumbnail_unlock();
	return NULL;
}

vita2d_texture *vt_video_thumbnail_get(const char *path, uint64_t source_size) {
	return thumbnail_get(NULL, NULL, path, source_size, 0, 0);
}

vita2d_texture *vt_video_thumbnail_get_priority(
	const char *path, uint64_t source_size, int priority) {
	return thumbnail_get(NULL, NULL, path, source_size, priority, 0);
}

vita2d_texture *vt_image_thumbnail_get(const char *path, uint64_t source_size) {
	return thumbnail_get(NULL, NULL, path, source_size, 0, 1);
}

vita2d_texture *vt_image_thumbnail_get_priority(
	const char *path, uint64_t source_size, int priority) {
	return thumbnail_get(NULL, NULL, path, source_size, priority, 1);
}

vita2d_texture *vt_video_thumbnail_get_remote(
	const VtNetworkSource *source,
	const VtNetworkCredential *credential,
	const char *path, uint64_t source_size) {
	if (!source || !credential) return NULL;
	return thumbnail_get(source, credential, path, source_size, 0, 0);
}

vita2d_texture *vt_video_thumbnail_get_remote_priority(
	const VtNetworkSource *source,
	const VtNetworkCredential *credential,
	const char *path, uint64_t source_size, int priority) {
	if (!source || !credential) return NULL;
	return thumbnail_get(source, credential, path, source_size, priority, 0);
}

void vt_video_thumbnail_shutdown(void) {
	if (!g_thumbnail.initialized) return;
	vt_video_thumbnail_suspend();
	thumbnail_lock();
	g_thumbnail.stop = 1;
	thumbnail_unlock();
	if (g_thumbnail.thid >= 0) {
		sceKernelWaitThreadEnd(g_thumbnail.thid, NULL, NULL);
		sceKernelDeleteThread(g_thumbnail.thid);
		g_thumbnail.thid = -1;
	}
	vita2d_wait_rendering_done();
	for (int i = 0; i < THUMB_TEXTURE_CAPACITY; i++) {
		if (g_thumbnail.textures[i].texture)
			vita2d_free_texture(g_thumbnail.textures[i].texture);
	}
	memset(&g_thumbnail, 0, sizeof(g_thumbnail));
	g_thumbnail.thid = -1;
}
