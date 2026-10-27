#include "ui/qr_scanner.h"
#include "ui/qr_view.h"
#include "network/iptv_manifest.h"

#include <stdint.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/camera.h>
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>

#include <quirc.h>

#include "app_paths.h"
#include "common/text_log.h"
#include "i18n/i18n.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/runtime.h"
#include "ui/theme.h"
#include "ui/touch.h"

/* This is the working camera path used by cxziaho/qrdemo: the rear camera
 * writes ABGR frames directly to a CDRAM vita2d texture. */
#define QR_CAMERA_WIDTH 640
#define QR_CAMERA_HEIGHT 480
/* The displayed guide and the decoder consume the exact same centre crop.
 * It makes a QR code physically larger for quirc than a 16:9 full-frame,
 * half-resolution feed. */
#define QR_CAMERA_CROP_SIZE UI_QR_CAMERA_SIZE
#define QR_CAMERA_CROP_X UI_QR_CAMERA_CROP_X
#define QR_CAMERA_CROP_Y 0
#define QR_DECODE_WIDTH QR_CAMERA_CROP_SIZE
#define QR_DECODE_HEIGHT QR_CAMERA_CROP_SIZE
#define QR_DECODE_SMALL_WIDTH (QR_DECODE_WIDTH / 2)
#define QR_DECODE_SMALL_HEIGHT (QR_DECODE_HEIGHT / 2)
#define QR_ADAPTIVE_BLOCK 32
#define QR_URL_MAX 2048
#define QR_WORKER_STACK (256 * 1024)
/* Keep the investigation hooks available without writing debug frames or
 * verbose per-frame telemetry in production builds. */
#define QR_DEBUG_DIAGNOSTICS 0
#define QR_DEBUG_LOG_INTERVAL 30

typedef struct QrDecodeWorker {
	struct quirc *decoder;
	struct quirc *small_decoder;
	pthread_t thread;
	int thread_started;
	volatile int stop;
	volatile int pending;
	volatile int result;
	volatile int candidates;
	volatile int decode_error;
	volatile int rejection;
	volatile unsigned int submitted_frames;
	volatile unsigned int analyzed_frames;
	volatile unsigned int frame_id;
	volatile unsigned int luma_min;
	volatile unsigned int luma_max;
	volatile unsigned int luma_mean;
	volatile unsigned int otsu_threshold;
	volatile unsigned int dark_percent;
	volatile unsigned int frame_checksum;
	volatile int debug_frame_saved;
	unsigned char frame[QR_DECODE_WIDTH * QR_DECODE_HEIGHT];
	char url[QR_URL_MAX];
} QrDecodeWorker;

static char g_last_error[128];

static void set_last_error(const char *operation, int error) {
	if (error < 0)
		snprintf(g_last_error, sizeof(g_last_error), "%s failed (0x%08X)",
		         operation, (unsigned int)error);
	else
		snprintf(g_last_error, sizeof(g_last_error), "%s failed", operation);
}

const char *ui_qr_scan_last_error(void) {
	return g_last_error[0] ? g_last_error : "Camera or QR decoder unavailable";
}

#if QR_DEBUG_DIAGNOSTICS
/* quirc's recognition stage binarizes input with Otsu's global threshold.
 * Recompute it here strictly for diagnostics, before quirc overwrites its
 * aliased input buffer during identification. */
static unsigned int qr_otsu_threshold(const unsigned char *image, size_t length) {
	unsigned int histogram[256] = {0};
	uint64_t sum = 0;
	for (size_t i = 0; i < length; i++) histogram[image[i]]++;
	for (unsigned int i = 0; i < 256; i++) sum += (uint64_t)i * histogram[i];
	double sum_background = 0.0;
	unsigned int background_count = 0;
	double best_variance = 0.0;
	unsigned int threshold = 0;
	for (unsigned int i = 0; i < 256; i++) {
		background_count += histogram[i];
		if (!background_count) continue;
		unsigned int foreground_count = (unsigned int)length - background_count;
		if (!foreground_count) break;
		sum_background += (double)i * histogram[i];
		/* This is intentionally the same Otsu variance calculation as quirc. */
		double mean_background = sum_background / background_count;
		double mean_foreground = ((double)sum - sum_background) / foreground_count;
		double delta = mean_background - mean_foreground;
		double variance = delta * delta * background_count * foreground_count;
		if (variance >= best_variance) {
			best_variance = variance;
			threshold = i;
		}
	}
	return threshold;
}

static void qr_log_processing(const QrDecodeWorker *worker, int candidates) {
	unsigned int frame = worker->frame_id;
	if (frame > 3 && frame % QR_DEBUG_LOG_INTERVAL != 0 && candidates == 0) return;
	log_printf("qr debug: frame=%u luma=%u..%u mean=%u otsu=%u dark=%u%% hash=%08X candidates=%d\n",
	           frame, worker->luma_min, worker->luma_max, worker->luma_mean,
	           worker->otsu_threshold, worker->dark_percent,
	           worker->frame_checksum, candidates);
}
#endif

static int publish_qr_data(QrDecodeWorker *worker, const struct quirc_data *data) {
	int result = data->payload_len > 0
	    ? vt_iptv_input_url(data->payload, (size_t)data->payload_len,
	                        worker->url, sizeof(worker->url)) : VT_IPTV_INVALID;
	if (result) {
		__atomic_store_n(&worker->rejection,result == VT_IPTV_LIMIT ? 1 : 2,__ATOMIC_RELEASE);
		return 0;
	}
	__atomic_store_n(&worker->rejection,0,__ATOMIC_RELEASE);
	__atomic_store_n(&worker->decode_error,0,__ATOMIC_RELEASE);
	log_printf("qr camera: URL accepted bytes=%u normalized=%u\n",
	           (unsigned)data->payload_len, (unsigned)strlen(worker->url));
	__atomic_store_n(&worker->result, 1, __ATOMIC_RELEASE);
	return 1;
}

static int decode_candidates(QrDecodeWorker *worker, struct quirc *decoder,
	                         const char *pipeline) {
	int candidates = quirc_count(decoder);
	for (int i = 0; i < candidates && !__atomic_load_n(&worker->result,__ATOMIC_ACQUIRE); i++) {
		struct quirc_code code;
		struct quirc_data data;
		quirc_extract(decoder, i, &code);
		quirc_decode_error_t direct_result = quirc_decode(&code, &data);
		quirc_decode_error_t decode_result = direct_result;
		#if QR_DEBUG_DIAGNOSTICS
		int flipped = 0;
		#endif
		if (decode_result != QUIRC_SUCCESS) {
			/* Rear-camera and texture paths can mirror the image.  quirc exposes
			 * an explicit mirrored-code retry for this case. */
			quirc_flip(&code);
			decode_result = quirc_decode(&code, &data);
			#if QR_DEBUG_DIAGNOSTICS
			flipped = 1;
			#endif
		}
		#if QR_DEBUG_DIAGNOSTICS
		log_printf("qr debug: frame=%u pipeline=%s candidate=%d/%d corners=(%d,%d)(%d,%d)(%d,%d)(%d,%d) direct=%s final=%s flipped=%d\n",
		           worker->frame_id, pipeline, i + 1, candidates,
		           code.corners[0].x, code.corners[0].y,
		           code.corners[1].x, code.corners[1].y,
		           code.corners[2].x, code.corners[2].y,
		           code.corners[3].x, code.corners[3].y,
		           quirc_strerror(direct_result), quirc_strerror(decode_result), flipped);
		#endif
		if (decode_result != QUIRC_SUCCESS) {
			__atomic_store_n(&worker->decode_error,decode_result,__ATOMIC_RELEASE);
			continue;
		}
		publish_qr_data(worker, &data);
	}
	return candidates;
}

static int process_full_frame(QrDecodeWorker *worker) {
	int width = 0;
	int height = 0;
	uint8_t *image = quirc_begin(worker->decoder, &width, &height);
	if (!image || width != QR_DECODE_WIDTH || height != QR_DECODE_HEIGHT) return -1;
	memcpy(image, worker->frame, sizeof(worker->frame));
	quirc_end(worker->decoder);
	return decode_candidates(worker, worker->decoder, "full");
}

static int process_inverted_frame(QrDecodeWorker *worker) {
	int width = 0;
	int height = 0;
	uint8_t *image = quirc_begin(worker->decoder, &width, &height);
	if (!image || width != QR_DECODE_WIDTH || height != QR_DECODE_HEIGHT) return -1;
	for (size_t i = 0; i < sizeof(worker->frame); i++)
		image[i] = (unsigned char)(255u - worker->frame[i]);
	quirc_end(worker->decoder);
	return decode_candidates(worker, worker->decoder, "inverted");
}

static int process_adaptive_frame(QrDecodeWorker *worker) {
	int width = 0;
	int height = 0;
	uint8_t *image = quirc_begin(worker->decoder, &width, &height);
	if (!image || width != QR_DECODE_WIDTH || height != QR_DECODE_HEIGHT) return -1;
	for (int block_y = 0; block_y < height; block_y += QR_ADAPTIVE_BLOCK) {
		int block_h = height - block_y;
		if (block_h > QR_ADAPTIVE_BLOCK) block_h = QR_ADAPTIVE_BLOCK;
		for (int block_x = 0; block_x < width; block_x += QR_ADAPTIVE_BLOCK) {
			int block_w = width - block_x;
			if (block_w > QR_ADAPTIVE_BLOCK) block_w = QR_ADAPTIVE_BLOCK;
			unsigned int sum = 0;
			for (int y = 0; y < block_h; y++) {
				const unsigned char *source = worker->frame +
				    (block_y + y) * width + block_x;
				for (int x = 0; x < block_w; x++) sum += source[x];
			}
			unsigned int mean = sum / (unsigned int)(block_w * block_h);
			unsigned int threshold = mean > 6 ? mean - 6 : 0;
			for (int y = 0; y < block_h; y++) {
				const unsigned char *source = worker->frame +
				    (block_y + y) * width + block_x;
				unsigned char *destination = image +
				    (block_y + y) * width + block_x;
				for (int x = 0; x < block_w; x++)
					destination[x] = source[x] < threshold ? 0 : 255;
			}
		}
	}
	quirc_end(worker->decoder);
	return decode_candidates(worker, worker->decoder, "adaptive32");
}

static int process_small_frame(QrDecodeWorker *worker) {
	int width = 0;
	int height = 0;
	uint8_t *image = quirc_begin(worker->small_decoder, &width, &height);
	if (!image || width != QR_DECODE_SMALL_WIDTH || height != QR_DECODE_SMALL_HEIGHT)
		return -1;
	for (int y = 0; y < height; y++) {
		const unsigned char *row0 = worker->frame + (y * 2) * QR_DECODE_WIDTH;
		const unsigned char *row1 = row0 + QR_DECODE_WIDTH;
		for (int x = 0; x < width; x++) {
			int sx = x * 2;
			image[y * width + x] = (unsigned char)((row0[sx] + row0[sx + 1] +
			                                  row1[sx] + row1[sx + 1]) / 4);
		}
	}
	quirc_end(worker->small_decoder);
	return decode_candidates(worker, worker->small_decoder, "downsample2");
}

static void *qr_decode_thread(void *argp) {
	QrDecodeWorker *worker = argp;
	while (!__atomic_load_n(&worker->stop, __ATOMIC_ACQUIRE)) {
		if (!__atomic_load_n(&worker->pending, __ATOMIC_ACQUIRE)) {
			sceKernelDelayThread(1000);
			continue;
		}
		int candidates = process_full_frame(worker);
		if (candidates < 0) {
			__atomic_store_n(&worker->decode_error,QUIRC_ERROR_DATA_OVERFLOW,__ATOMIC_RELEASE);
			__atomic_store_n(&worker->pending, 0, __ATOMIC_RELEASE);
			continue;
		}
		if (!__atomic_load_n(&worker->result,__ATOMIC_ACQUIRE)) {
			int inverted_candidates = process_inverted_frame(worker);
			if (inverted_candidates > 0) candidates += inverted_candidates;
		}
		if (!__atomic_load_n(&worker->result,__ATOMIC_ACQUIRE)) {
			int adaptive_candidates = process_adaptive_frame(worker);
			if (adaptive_candidates > 0) candidates += adaptive_candidates;
		}
		if (!__atomic_load_n(&worker->result,__ATOMIC_ACQUIRE)) {
			int small_candidates = process_small_frame(worker);
			if (small_candidates > 0) candidates += small_candidates;
		}
		__atomic_store_n(&worker->analyzed_frames, worker->frame_id, __ATOMIC_RELEASE);
		#if QR_DEBUG_DIAGNOSTICS
		qr_log_processing(worker, candidates);
		#endif
		if (candidates > 0) __atomic_store_n(&worker->candidates,candidates,__ATOMIC_RELEASE);
		__atomic_store_n(&worker->pending, 0, __ATOMIC_RELEASE);
	}
	return NULL;
}

#if QR_DEBUG_DIAGNOSTICS
static void save_debug_frame(QrDecodeWorker *worker) {
	if (!worker || worker->debug_frame_saved) return;
	const char *path = "ux0:data/VitaMediaDeck/qr-debug-frame.pgm";
	SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (fd < 0) {
		log_printf("qr debug: frame dump open -> 0x%08X\n", (unsigned int)fd);
		worker->debug_frame_saved = -1;
		return;
	}
	char header[32];
	int header_length = snprintf(header, sizeof(header), "P5\n%d %d\n255\n",
	                             QR_DECODE_WIDTH, QR_DECODE_HEIGHT);
	int result = sceIoWrite(fd, header, (SceSize)header_length);
	if (result == header_length) {
		size_t written = 0;
		while (written < sizeof(worker->frame)) {
			result = sceIoWrite(fd, worker->frame + written,
			                    sizeof(worker->frame) - written);
			if (result <= 0) break;
			written += (size_t)result;
		}
		if (written == sizeof(worker->frame)) result = 0;
	}
	sceIoClose(fd);
	worker->debug_frame_saved = result < 0 ? -1 : 1;
	log_printf("qr debug: frame dump %s -> %s\n", path,
	           worker->debug_frame_saved > 0 ? "saved" : "failed");
}
#endif

static void submit_camera_frame(QrDecodeWorker *worker, vita2d_texture *preview) {
	if (!worker || !preview || __atomic_load_n(&worker->pending,__ATOMIC_ACQUIRE) || __atomic_load_n(&worker->result,__ATOMIC_ACQUIRE)) return;
	unsigned int stride = vita2d_texture_get_stride(preview);
	unsigned char *pixels = vita2d_texture_get_datap(preview);
	if (!pixels || stride < QR_CAMERA_WIDTH * sizeof(uint32_t)) return;
	unsigned int min_luma = 255;
	unsigned int max_luma = 0;
	uint64_t sum_luma = 0;
	unsigned int checksum = 2166136261u;
	for (int y = 0; y < QR_DECODE_HEIGHT; y++) {
		const uint32_t *source = (const uint32_t *)(pixels +
		    (y + QR_CAMERA_CROP_Y) * stride) + QR_CAMERA_CROP_X;
		unsigned char *destination = worker->frame + y * QR_DECODE_WIDTH;
		for (int x = 0; x < QR_DECODE_WIDTH; x++) {
			/* The rear camera is explicitly placed in BLACKWHITE mode.  The
			 * known-good Vita scanner feeds quirc the first ABGR byte directly;
			 * preserving that path avoids colour conversion artefacts. */
			unsigned int luma = ((const unsigned char *)source)[x * sizeof(uint32_t)];
			destination[x] = (unsigned char)luma;
			if (luma < min_luma) min_luma = luma;
			if (luma > max_luma) max_luma = luma;
			sum_luma += luma;
			checksum = (checksum ^ luma) * 16777619u;
		}
	}
	#if QR_DEBUG_DIAGNOSTICS
	unsigned int threshold = qr_otsu_threshold(worker->frame, sizeof(worker->frame));
	unsigned int dark_pixels = 0;
	for (size_t i = 0; i < sizeof(worker->frame); i++) {
		if (worker->frame[i] < threshold) dark_pixels++;
	}
	worker->luma_min = min_luma;
	worker->luma_max = max_luma;
	worker->luma_mean = (unsigned int)(sum_luma / sizeof(worker->frame));
	worker->otsu_threshold = threshold;
	worker->dark_percent = (dark_pixels * 100u) / sizeof(worker->frame);
	worker->frame_checksum = checksum;
	#endif
	__sync_synchronize();
	worker->frame_id = worker->submitted_frames + 1;
	worker->submitted_frames = worker->frame_id;
	#if QR_DEBUG_DIAGNOSTICS
	if (worker->frame_id == QR_DEBUG_LOG_INTERVAL) save_debug_frame(worker);
	#endif
	__atomic_store_n(&worker->pending, 1, __ATOMIC_RELEASE);
}

static void report_decoder_diagnostics(const QrDecodeWorker *worker,
	                                     int *candidate_reported,
	                                     int *decode_error_reported,
	                                     int *rejection_reported) {
	if (__atomic_load_n(&worker->candidates,__ATOMIC_ACQUIRE) > 0 && !*candidate_reported) {
		log_printf("qr camera: detected %d candidate(s)\n", __atomic_load_n(&worker->candidates,__ATOMIC_ACQUIRE));
		*candidate_reported = 1;
	}
	if (__atomic_load_n(&worker->decode_error,__ATOMIC_ACQUIRE) && !*decode_error_reported) {
		log_printf("qr camera: candidate decode: %s\n",
		           quirc_strerror((quirc_decode_error_t)__atomic_load_n(&worker->decode_error,__ATOMIC_ACQUIRE)));
		*decode_error_reported = 1;
	}
	if (__atomic_load_n(&worker->rejection,__ATOMIC_ACQUIRE) && !*rejection_reported) {
		if (__atomic_load_n(&worker->rejection,__ATOMIC_ACQUIRE) == 1)
			log_printf("qr camera: decoded payload has unsupported length\n");
		else
			log_printf("qr camera: decoded payload is not an HTTP(S) URL\n");
		*rejection_reported = 1;
	}
}

static UiQrState scanner_state(const QrDecodeWorker *worker) {
 if (!worker) return UI_QR_STARTING;
 if (__atomic_load_n(&worker->result,__ATOMIC_ACQUIRE)) return UI_QR_RECOGNIZED;
 int rejection=__atomic_load_n(&worker->rejection,__ATOMIC_ACQUIRE);
 if (rejection==2) return UI_QR_INVALID_PROTOCOL;
 if (rejection==1) return UI_QR_INVALID_LENGTH;
 if (__atomic_load_n(&worker->decode_error,__ATOMIC_ACQUIRE)) return UI_QR_DECODING;
 if (__atomic_load_n(&worker->candidates,__ATOMIC_ACQUIRE)>0) return UI_QR_CANDIDATE;
 return UI_QR_SEARCHING;
}

static void draw_scanner(vita2d_texture *preview, int has_preview,
                         const QrDecodeWorker *worker) {
 UiQrStatus status={.state=scanner_state(worker)};
 if(worker) {
  status.submitted=worker->submitted_frames;
  status.analyzed=__atomic_load_n(&worker->analyzed_frames,__ATOMIC_ACQUIRE);
 }
 vita2d_start_drawing();
 vita2d_clear_screen();
 ui_qr_view_draw(has_preview?preview:NULL,&status);
 vita2d_end_drawing();
 vita2d_wait_rendering_done();
 vita2d_swap_buffers();
}

int ui_qr_scan_https_url(char *out, size_t out_size) {
	if (!out || out_size < 2) return -1;
	out[0] = '\0';
	g_last_error[0] = '\0';
	draw_scanner(NULL, 0, NULL);
	vita2d_texture *preview = NULL;
	QrDecodeWorker *worker = NULL;
	int camera_open = 0;
	int camera_started = 0;
	int result = -1;
	SceKernelMemBlockType previous_texture_memory = vita2d_texture_get_alloc_memblock_type();
	vita2d_texture_set_alloc_memblock_type(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW);
	preview = vita2d_create_empty_texture(QR_CAMERA_WIDTH, QR_CAMERA_HEIGHT);
	vita2d_texture_set_alloc_memblock_type(previous_texture_memory);
	if (!preview || !vita2d_texture_get_datap(preview) ||
	    vita2d_texture_get_stride(preview) < QR_CAMERA_WIDTH * sizeof(uint32_t)) {
		log_printf("qr camera: ABGR CDRAM texture allocation failed\n");
		set_last_error("Camera preview texture", -1);
		goto done;
	}
	vita2d_texture_set_filters(preview, SCE_GXM_TEXTURE_FILTER_POINT,
	                          SCE_GXM_TEXTURE_FILTER_POINT);

	SceCameraInfo info;
	memset(&info, 0, sizeof(info));
	info.size = sizeof(info);
	info.priority = SCE_CAMERA_PRIORITY_SHARE;
	info.format = SCE_CAMERA_FORMAT_ABGR;
	info.resolution = SCE_CAMERA_RESOLUTION_640_480;
	info.framerate = SCE_CAMERA_FRAMERATE_30_FPS;
	info.pitch = vita2d_texture_get_stride(preview) - (QR_CAMERA_WIDTH << 2);
	info.sizeIBase = QR_CAMERA_WIDTH * QR_CAMERA_HEIGHT * sizeof(uint32_t);
	info.pIBase = vita2d_texture_get_datap(preview);
	result = sceCameraOpen(SCE_CAMERA_DEVICE_BACK, &info);
	if (result < 0) {
		log_printf("qr camera: open ABGR 640x480/30 -> 0x%08X\n", (unsigned)result);
		set_last_error("Rear camera open", result);
		goto done;
	}
	camera_open = 1;
	result = sceCameraStart(SCE_CAMERA_DEVICE_BACK);
	if (result < 0) {
		log_printf("qr camera: start -> 0x%08X\n", (unsigned)result);
		set_last_error("Rear camera start", result);
		goto done;
	}
	camera_started = 1;
	result = sceCameraSetEffect(SCE_CAMERA_DEVICE_BACK, SCE_CAMERA_EFFECT_BLACKWHITE);
	if (result < 0) {
		log_printf("qr camera: set black-and-white effect -> 0x%08X\n", (unsigned)result);
		set_last_error("Rear camera black-and-white effect", result);
		goto done;
	}
	if (sceCameraIsActive(SCE_CAMERA_DEVICE_BACK) <= 0) {
		set_last_error("Rear camera activation", -1);
		goto done;
	}

	worker = calloc(1, sizeof(*worker));
	if (!worker) {
		set_last_error("QR decoder worker", -1);
		goto done;
	}
	worker->decoder = quirc_new();
	worker->small_decoder = quirc_new();
	if (!worker->decoder || !worker->small_decoder ||
	    quirc_resize(worker->decoder, QR_DECODE_WIDTH, QR_DECODE_HEIGHT) < 0 ||
	    quirc_resize(worker->small_decoder, QR_DECODE_SMALL_WIDTH,
	                 QR_DECODE_SMALL_HEIGHT) < 0) {
		log_printf("qr camera: quirc initialization failed\n");
		set_last_error("QR decoder initialization", -1);
		goto done;
	}
	#if QR_DEBUG_DIAGNOSTICS
	log_printf("qr debug: quirc=%s full=%dx%d small=%dx%d pipelines=full,inverted,adaptive32,downsample2 regions=4096 input=ABGR.byte0 BLACKWHITE\n",
	           quirc_version(), QR_DECODE_WIDTH, QR_DECODE_HEIGHT,
	           QR_DECODE_SMALL_WIDTH, QR_DECODE_SMALL_HEIGHT);
	#endif
	pthread_attr_t attr;
	int thread_result=pthread_attr_init(&attr);
	int attr_ready=thread_result==0;
	if(!thread_result)thread_result=pthread_attr_setstacksize(&attr,QR_WORKER_STACK);
	if(!thread_result)thread_result=pthread_create(&worker->thread,&attr,qr_decode_thread,worker);
	if(attr_ready)pthread_attr_destroy(&attr);
	if(thread_result) {
		log_printf("qr camera: decoder pthread start failed: %d\n",thread_result);
		set_last_error("QR decoder worker",-thread_result);
		result=-1;
		goto done;
	}
	worker->thread_started=1;

	SceCtrlData previous;
	int candidate_reported = 0;
	int decode_error_reported = 0;
	int rejection_reported = 0;
	memset(&previous, 0, sizeof(previous));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	log_printf("qr camera: active ABGR 640x480/30, BLACKWHITE, decoder centre 480x480\n");
	ui_touch_reset();
	while (1) {
		SceCtrlData controls;
		sceCtrlPeekBufferPositive(0, &controls, 1);
		unsigned pressed = controls.buttons & ~previous.buttons;
		previous = controls;
		UiTouchEvent touch;
		unsigned touch_flags=ui_touch_poll(&touch);
		if((touch_flags&UI_TOUCH_EVENT_TAP) &&
		   ui_touch_hit_rect(touch.x,touch.y,UI_QR_CANCEL_X,UI_QR_CANCEL_Y,UI_QR_CANCEL_W,UI_QR_CANCEL_H))pressed|=SCE_CTRL_CIRCLE;
		if (pressed & SCE_CTRL_CIRCLE) { result = 0; break; }
		/* The worker publishes result with a barrier. Check it before another
		 * blocking camera read, so a successful decode cannot be delayed or
		 * lost behind the preview loop. */
		if (__atomic_load_n(&worker->result, __ATOMIC_ACQUIRE)) {
			__sync_synchronize();
			size_t length = strlen(worker->url);
			if (length > 0 && length < out_size) {
				memcpy(out, worker->url, length + 1);
				result = 1;
				break;
			}
			set_last_error("QR URL", -1);
			result = -1;
			break;
		}

		SceCameraRead read;
		memset(&read, 0, sizeof(read));
		read.size = sizeof(read);
		/* qrdemo's blocking mode guarantees a completed fresh camera frame. */
		read.mode = 0;
		int read_result = sceCameraRead(SCE_CAMERA_DEVICE_BACK, &read);
		if (read_result < 0) {
			log_printf("qr camera: frame read -> 0x%08X\n", (unsigned)read_result);
			set_last_error("Rear camera frame read", read_result);
			result = -1;
			break;
		}
		submit_camera_frame(worker, preview);
		report_decoder_diagnostics(worker, &candidate_reported,
		                           &decode_error_reported, &rejection_reported);
		draw_scanner(preview, 1, worker);
	}
done:
	log_printf("qr camera: finished result=%d bytes=%u analyzed=%u rejection=%d\n",result,
	           result>0?(unsigned)strlen(out):0,worker?__atomic_load_n(&worker->analyzed_frames,__ATOMIC_ACQUIRE):0,
	           worker?__atomic_load_n(&worker->rejection,__ATOMIC_ACQUIRE):0);
	ui_touch_reset();
	if (worker) {
		__atomic_store_n(&worker->stop,1,__ATOMIC_RELEASE);
		if(worker->thread_started)pthread_join(worker->thread,NULL);
		if (worker->decoder) quirc_destroy(worker->decoder);
		if (worker->small_decoder) quirc_destroy(worker->small_decoder);
		free(worker);
	}
	if (camera_started) sceCameraStop(SCE_CAMERA_DEVICE_BACK);
	if (camera_open) sceCameraClose(SCE_CAMERA_DEVICE_BACK);
	if (preview) vita2d_free_texture(preview);
	log_save(VITAMEDIADECK_SESSION_LOG_PATH);
	return result;
}
