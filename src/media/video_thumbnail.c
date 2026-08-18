#include "media/video_thumbnail.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define THUMB_WIDTH                 264
#define THUMB_HEIGHT                148
#define THUMB_PIXELS                (THUMB_WIDTH * THUMB_HEIGHT)
#define THUMB_BYTES                 (THUMB_PIXELS * sizeof(uint16_t))
#define THUMB_PATH_MAX              512
#define THUMB_REQUEST_CAPACITY      18
#define THUMB_TEXTURE_CAPACITY      12
#define THUMB_FAILURE_CAPACITY      24
#define THUMB_DISK_SLOTS            128
#define THUMB_CACHE_VERSION         1u
#define THUMB_FAILURE_RETRY_US      (30ULL * 1000ULL * 1000ULL)
#define THUMB_DECODE_DEADLINE_US    (5ULL * 1000ULL * 1000ULL)
#define THUMB_MAX_DEMUX_PACKETS     320
#define THUMB_THREAD_PRIORITY       0x10000180
#define THUMB_THREAD_STACK          0x100000
#define THUMB_CACHE_DIR             "ux0:data/VitaTube/thumbs"

typedef struct ThumbnailRequest {
	uint64_t key;
	uint64_t source_size;
	unsigned int generation;
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

typedef struct ThumbnailState {
	void *self;
	SceUID thid;
	volatile int initialized;
	volatile int enabled;
	volatile int stop;
	volatile int done;
	volatile int lock;
	volatile unsigned int generation;
	ThumbnailRequest requests[THUMB_REQUEST_CAPACITY];
	int request_count;
	ThumbnailRequest active;
	int active_valid;
	ThumbnailResult result;
	ThumbnailTexture textures[THUMB_TEXTURE_CAPACITY];
	ThumbnailFailure failures[THUMB_FAILURE_CAPACITY];
} ThumbnailState;

static ThumbnailState g_thumbnail = { .thid = -1 };

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
	       request->generation == generation &&
	       strcmp(request->path, path) == 0;
}

static uint64_t thumbnail_key(const char *path, uint64_t source_size) {
	uint64_t hash = 14695981039346656037ULL;
	for (const unsigned char *cursor = (const unsigned char *)path;
	     cursor && *cursor; cursor++) {
		hash ^= *cursor;
		hash *= 1099511628211ULL;
	}
	for (unsigned int shift = 0; shift < 64; shift += 8) {
		hash ^= (unsigned char)(source_size >> shift);
		hash *= 1099511628211ULL;
	}
	return hash;
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
	    (memcmp(header.magic, "VTTHMB1", 8) != 0 ||
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
	if (g_thumbnail.stop || !g_thumbnail.enabled ||
	    request->generation != g_thumbnail.generation) return;
	sceIoMkdir("ux0:data/VitaTube", 0777);
	sceIoMkdir(THUMB_CACHE_DIR, 0777);
	char path[96], temporary[96];
	cache_paths(request->key, path, temporary);
	ThumbnailDiskHeader header;
	memset(&header, 0, sizeof(header));
	memcpy(header.magic, "VTTHMB1", 8);
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
	if (result == 0) result = sceIoSyncByFd(fd, 0);
	sceIoClose(fd);
	if (result == 0 && !g_thumbnail.stop && g_thumbnail.enabled &&
	    request->generation == g_thumbnail.generation) {
		/* These files are disposable cache entries. The verified header prevents
		 * a power-loss window from ever being treated as a valid thumbnail. */
		sceIoRemove(path);
		if (sceIoRename(temporary, path) < 0) sceIoRemove(temporary);
	} else {
		sceIoRemove(temporary);
	}
}

static int thumbnail_interrupted(void *opaque) {
	const ThumbnailInterrupt *interrupt = opaque;
	return g_thumbnail.stop || !g_thumbnail.enabled ||
	       (interrupt && interrupt->generation != g_thumbnail.generation) ||
	       (interrupt && sceKernelGetProcessTimeWide() >= interrupt->deadline_us);
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
	if (!planar && !nv12) return AVERROR(ENOSYS);
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
	int result = av_seek_frame(format, stream_index, target, AVSEEK_FLAG_BACKWARD);
	if (result < 0)
		result = av_seek_frame(format, stream_index, 0, AVSEEK_FLAG_BACKWARD);
	return result;
}

static int decode_thumbnail(const ThumbnailRequest *request, uint16_t *pixels) {
	ThumbnailInterrupt interrupt = {
		.deadline_us = sceKernelGetProcessTimeWide() + THUMB_DECODE_DEADLINE_US,
		.generation = request->generation
	};
	AVFormatContext *format = avformat_alloc_context();
	AVCodecContext *decoder = NULL;
	AVPacket *packet = NULL;
	AVFrame *frame = NULL;
	int result = format ? 0 : AVERROR(ENOMEM);
	if (format) {
		format->interrupt_callback.callback = thumbnail_interrupted;
		format->interrupt_callback.opaque = &interrupt;
		/* Stream probing is allowed to open a decoder before our explicit
		 * avcodec_open2() below. Restrict it to the named CPU implementation so
		 * avformat_find_stream_info() cannot select h264_vita/SceVideodec. The
		 * AVFormatContext owns and releases this av_strdup() allocation. */
		format->codec_whitelist = av_strdup("h264");
		if (!format->codec_whitelist) result = AVERROR(ENOMEM);
		if (result >= 0)
			result = avformat_open_input(&format, request->path, NULL, NULL);
	}
	if (result >= 0) result = avformat_find_stream_info(format, NULL);
	int stream_index = -1;
	if (result >= 0) {
		for (unsigned int i = 0; i < format->nb_streams; i++) {
			AVStream *candidate = format->streams[i];
			if (candidate->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
			    candidate->codecpar->codec_id == AV_CODEC_ID_H264 &&
			    !(candidate->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
				stream_index = (int)i;
				break;
			}
		}
		if (stream_index < 0) result = AVERROR_STREAM_NOT_FOUND;
	}
	AVStream *stream = result >= 0 ? format->streams[stream_index] : NULL;
	/* Request the named CPU decoder explicitly. avcodec_find_decoder() may
	 * select the separately registered h264_vita implementation. Thumbnail
	 * work must never claim SceVideodec or compete for its CDRAM surfaces. */
	const AVCodec *codec = result >= 0
	                       ? avcodec_find_decoder_by_name("h264") : NULL;
	if (result >= 0 && !codec) result = AVERROR_DECODER_NOT_FOUND;
	if (result >= 0) decoder = avcodec_alloc_context3(codec);
	if (result >= 0 && !decoder) result = AVERROR(ENOMEM);
	if (result >= 0) result = avcodec_parameters_to_context(decoder, stream->codecpar);
	if (result >= 0) {
		decoder->thread_count = 1;
		decoder->thread_type = 0;
		result = avcodec_open2(decoder, codec, NULL);
	}
	if (result >= 0 && !thumbnail_interrupted(&interrupt)) {
		choose_target_and_seek(format, stream_index);
		avcodec_flush_buffers(decoder);
		packet = av_packet_alloc();
		frame = av_frame_alloc();
		if (!packet || !frame) result = AVERROR(ENOMEM);
	}
	int packets_read = 0;
	int converted = 0;
	while (result >= 0 && !converted && packets_read < THUMB_MAX_DEMUX_PACKETS &&
	       !thumbnail_interrupted(&interrupt)) {
		int read_result = av_read_frame(format, packet);
		if (read_result < 0) {
			avcodec_send_packet(decoder, NULL);
			while (!thumbnail_interrupted(&interrupt) &&
			       avcodec_receive_frame(decoder, frame) >= 0) {
				result = frame_to_rgb565(frame, pixels);
				av_frame_unref(frame);
				if (result >= 0) converted = 1;
				break;
			}
			if (!converted && result >= 0) result = read_result;
			break;
		}
		packets_read++;
		if (packet->stream_index == stream_index) {
			int send_result = avcodec_send_packet(decoder, packet);
			if (send_result >= 0 || send_result == AVERROR(EAGAIN)) {
				for (;;) {
					int receive_result = avcodec_receive_frame(decoder, frame);
					if (receive_result == AVERROR(EAGAIN) ||
					    receive_result == AVERROR_EOF) break;
					if (receive_result < 0) { result = receive_result; break; }
					result = frame_to_rgb565(frame, pixels);
					av_frame_unref(frame);
					if (result >= 0) converted = 1;
					break;
				}
			} else if (send_result < 0) {
				result = send_result;
			}
		}
		av_packet_unref(packet);
	}
	if (thumbnail_interrupted(&interrupt)) result = AVERROR_EXIT;
	else if (!converted && result >= 0) result = AVERROR(EIO);
	av_frame_free(&frame);
	av_packet_free(&packet);
	avcodec_free_context(&decoder);
	if (format) avformat_close_input(&format);
	return result;
}

static void remember_failure(const ThumbnailRequest *request) {
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
	failure->retry_after_us = now + THUMB_FAILURE_RETRY_US;
	snprintf(failure->path, sizeof(failure->path), "%s", request->path);
}

static int thumbnail_worker(SceSize args, void *argp) {
	(void)args;
	ThumbnailState *state = *(ThumbnailState **)argp;
	for (;;) {
		ThumbnailRequest request;
		int have_request = 0;
		thumbnail_lock();
		if (!state->stop && state->enabled && !state->result.pixels &&
		    state->request_count > 0) {
			request = state->requests[--state->request_count];
			state->active = request;
			state->active_valid = 1;
			have_request = 1;
		}
		int should_stop = state->stop;
		thumbnail_unlock();
		if (should_stop) break;
		if (!have_request) {
			sceKernelDelayThread(8 * 1000);
			continue;
		}
		uint16_t *pixels = malloc(THUMB_BYTES);
		int result = pixels ? cache_load(&request, pixels) : AVERROR(ENOMEM);
		int from_cache = result == 0;
		if (result < 0 && pixels && state->enabled && !state->stop) {
			result = decode_thumbnail(&request, pixels);
			if (result == 0) cache_store(&request, pixels);
		}
		thumbnail_lock();
		state->active_valid = 0;
		if (result == 0 && state->enabled && !state->stop &&
		    request.generation == state->generation &&
		    !state->result.pixels) {
			state->result.key = request.key;
			state->result.source_size = request.source_size;
			state->result.generation = request.generation;
			snprintf(state->result.path, sizeof(state->result.path), "%s",
			         request.path);
			state->result.pixels = pixels;
			pixels = NULL;
			if (!from_cache)
				log_printf("video thumbnail generated: %s", request.path);
		} else if (result < 0 && state->enabled && !state->stop &&
		           request.generation == state->generation) {
			remember_failure(&request);
			log_printf("video thumbnail unavailable: %s -> %d",
			           request.path, result);
		}
		thumbnail_unlock();
		free(pixels);
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
		"VitaTubeThumbnail", thumbnail_worker, THUMB_THREAD_PRIORITY,
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
	return 0;
}

void vt_video_thumbnail_resume(void) {
	if (!g_thumbnail.initialized) return;
	thumbnail_lock();
	g_thumbnail.enabled = 1;
	thumbnail_unlock();
}

void vt_video_thumbnail_suspend(void) {
	if (!g_thumbnail.initialized) return;
	uint16_t *pixels = NULL;
	thumbnail_lock();
	g_thumbnail.enabled = 0;
	g_thumbnail.generation++;
	g_thumbnail.request_count = 0;
	pixels = g_thumbnail.result.pixels;
	memset(&g_thumbnail.result, 0, sizeof(g_thumbnail.result));
	thumbnail_unlock();
	free(pixels);
}

void vt_video_thumbnail_pump(void) {
	if (!g_thumbnail.initialized || !g_thumbnail.enabled) return;
	ThumbnailResult result;
	memset(&result, 0, sizeof(result));
	thumbnail_lock();
	if (g_thumbnail.result.pixels) {
		result = g_thumbnail.result;
		memset(&g_thumbnail.result, 0, sizeof(g_thumbnail.result));
	}
	thumbnail_unlock();
	if (!result.pixels) return;
	vita2d_texture *texture = vita2d_create_empty_texture_format(
		THUMB_WIDTH, THUMB_HEIGHT, SCE_GXM_TEXTURE_FORMAT_R5G6B5);
	if (!texture) {
		free(result.pixels);
		return;
	}
	unsigned int stride = vita2d_texture_get_stride(texture);
	unsigned char *destination = vita2d_texture_get_datap(texture);
	for (int row = 0; row < THUMB_HEIGHT; row++)
		memcpy(destination + row * stride,
		       result.pixels + row * THUMB_WIDTH,
		       THUMB_WIDTH * sizeof(uint16_t));
	vita2d_texture_set_filters(texture, SCE_GXM_TEXTURE_FILTER_LINEAR,
	                           SCE_GXM_TEXTURE_FILTER_LINEAR);
	free(result.pixels);
	uint64_t now = sceKernelGetProcessTimeWide();
	int slot = 0;
	for (int i = 0; i < THUMB_TEXTURE_CAPACITY; i++) {
		if (!g_thumbnail.textures[i].texture) { slot = i; break; }
		if (g_thumbnail.textures[i].used_us <
		    g_thumbnail.textures[slot].used_us) slot = i;
	}
	ThumbnailTexture *cached = &g_thumbnail.textures[slot];
	if (cached->texture) vita2d_free_texture(cached->texture);
	memset(cached, 0, sizeof(*cached));
	cached->key = result.key;
	cached->source_size = result.source_size;
	cached->used_us = now;
	cached->texture = texture;
	snprintf(cached->path, sizeof(cached->path), "%s", result.path);
}

vita2d_texture *vt_video_thumbnail_get(const char *path, uint64_t source_size) {
	if (!g_thumbnail.initialized || !g_thumbnail.enabled || !path || !path[0] ||
	    strlen(path) >= THUMB_PATH_MAX) return NULL;
	uint64_t key = thumbnail_key(path, source_size);
	uint64_t now = sceKernelGetProcessTimeWide();
	for (int i = 0; i < THUMB_TEXTURE_CAPACITY; i++) {
		ThumbnailTexture *cached = &g_thumbnail.textures[i];
		if (cached->texture && cached->key == key &&
		    cached->source_size == source_size && strcmp(cached->path, path) == 0) {
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
	if ((g_thumbnail.active_valid &&
	     same_request(key, source_size, generation, path, &g_thumbnail.active)) ||
	    (g_thumbnail.result.pixels && g_thumbnail.result.key == key &&
	     g_thumbnail.result.source_size == source_size &&
	     g_thumbnail.result.generation == generation &&
	     strcmp(g_thumbnail.result.path, path) == 0)) {
		thumbnail_unlock();
		return NULL;
	}
	for (int i = 0; i < g_thumbnail.request_count; i++) {
		if (same_request(key, source_size, generation, path,
		                 &g_thumbnail.requests[i])) {
			thumbnail_unlock();
			return NULL;
		}
	}
	if (g_thumbnail.request_count == THUMB_REQUEST_CAPACITY) {
		memmove(g_thumbnail.requests, g_thumbnail.requests + 1,
		        sizeof(g_thumbnail.requests[0]) * (THUMB_REQUEST_CAPACITY - 1));
		g_thumbnail.request_count--;
	}
	ThumbnailRequest *request =
		&g_thumbnail.requests[g_thumbnail.request_count++];
	request->key = key;
	request->source_size = source_size;
	request->generation = generation;
	snprintf(request->path, sizeof(request->path), "%s", path);
	thumbnail_unlock();
	return NULL;
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
