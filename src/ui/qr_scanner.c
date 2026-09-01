#include "ui/qr_scanner.h"

#include <stdint.h>
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

/* This is the working camera path used by cxziaho/qrdemo: the rear camera
 * writes ABGR frames directly to a CDRAM vita2d texture. */
#define QR_CAMERA_WIDTH 640
#define QR_CAMERA_HEIGHT 480
/* The displayed guide and the decoder consume the exact same centre crop.
 * It makes a QR code physically larger for quirc than a 16:9 full-frame,
 * half-resolution feed. */
#define QR_CAMERA_CROP_SIZE 480
#define QR_CAMERA_CROP_X ((QR_CAMERA_WIDTH - QR_CAMERA_CROP_SIZE) / 2)
#define QR_CAMERA_CROP_Y 0
#define QR_DECODE_WIDTH QR_CAMERA_CROP_SIZE
#define QR_DECODE_HEIGHT QR_CAMERA_CROP_SIZE
#define QR_DECODE_SMALL_WIDTH (QR_DECODE_WIDTH / 2)
#define QR_DECODE_SMALL_HEIGHT (QR_DECODE_HEIGHT / 2)
#define QR_ADAPTIVE_BLOCK 32
#define QR_URL_MAX 2048
#define QR_WORKER_PRIORITY 0x40
#define QR_WORKER_STACK (256 * 1024)
/* Keep the investigation hooks available without writing debug frames or
 * verbose per-frame telemetry in production builds. */
#define QR_DEBUG_DIAGNOSTICS 0
#define QR_DEBUG_LOG_INTERVAL 30

/* The scan view deliberately fills the screen height.  The top bar yields to
 * the live image so a QR code can occupy enough real camera pixels. */
#define QR_PREVIEW_X 260
#define QR_PREVIEW_Y 48
#define QR_PREVIEW_SIZE 444
#define QR_GUIDE_X 282
#define QR_GUIDE_Y 70
#define QR_GUIDE_SIZE 400

typedef struct QrDecodeWorker {
	struct quirc *decoder;
	struct quirc *small_decoder;
	SceUID thid;
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

static int is_http_url(const char *value, size_t length) {
	return (length >= 8 && !memcmp(value, "https://", 8)) ||
	       (length >= 7 && !memcmp(value, "http://", 7));
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
	if (data->payload_len <= 0 || data->payload_len >= QR_URL_MAX) {
		worker->rejection = 1;
		return 0;
	}
	memcpy(worker->url, data->payload, (size_t)data->payload_len);
	worker->url[data->payload_len] = '\0';
	if (!is_http_url(worker->url, (size_t)data->payload_len)) {
		worker->url[0] = '\0';
		worker->rejection = 2;
		return 0;
	}
	__sync_synchronize();
	worker->result = 1;
	return 1;
}

static int decode_candidates(QrDecodeWorker *worker, struct quirc *decoder,
	                         const char *pipeline) {
	int candidates = quirc_count(decoder);
	for (int i = 0; i < candidates && !worker->result; i++) {
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
			worker->decode_error = decode_result;
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

static int qr_decode_thread(SceSize args, void *argp) {
	(void)args;
	QrDecodeWorker *worker = *(QrDecodeWorker **)argp;
	while (!worker->stop) {
		if (!worker->pending) {
			sceKernelDelayThread(1000);
			continue;
		}
		int candidates = process_full_frame(worker);
		if (candidates < 0) {
			worker->decode_error = QUIRC_ERROR_DATA_OVERFLOW;
			worker->pending = 0;
			continue;
		}
		if (!worker->result) {
			int inverted_candidates = process_inverted_frame(worker);
			if (inverted_candidates > 0) candidates += inverted_candidates;
		}
		if (!worker->result) {
			int adaptive_candidates = process_adaptive_frame(worker);
			if (adaptive_candidates > 0) candidates += adaptive_candidates;
		}
		if (!worker->result) {
			int small_candidates = process_small_frame(worker);
			if (small_candidates > 0) candidates += small_candidates;
		}
		worker->analyzed_frames = worker->frame_id;
		#if QR_DEBUG_DIAGNOSTICS
		qr_log_processing(worker, candidates);
		#endif
		if (candidates > 0) worker->candidates = candidates;
		worker->pending = 0;
	}
	return 0;
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
	if (!worker || !preview || worker->pending || worker->result) return;
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
	worker->pending = 1;
}

static void report_decoder_diagnostics(const QrDecodeWorker *worker,
	                                     int *candidate_reported,
	                                     int *decode_error_reported,
	                                     int *rejection_reported) {
	if (worker->candidates > 0 && !*candidate_reported) {
		log_printf("qr camera: detected %d candidate(s)\n", worker->candidates);
		*candidate_reported = 1;
	}
	if (worker->decode_error && !*decode_error_reported) {
		log_printf("qr camera: candidate decode: %s\n",
		           quirc_strerror((quirc_decode_error_t)worker->decode_error));
		*decode_error_reported = 1;
	}
	if (worker->rejection && !*rejection_reported) {
		if (worker->rejection == 1)
			log_printf("qr camera: decoded payload has unsupported length\n");
		else
			log_printf("qr camera: decoded payload is not an HTTP(S) URL\n");
		*rejection_reported = 1;
	}
}

static const char *scanner_status(const QrDecodeWorker *worker) {
	if (!worker) return "Starting QR decoder...";
	if (worker->result) return "QR link recognized";
	if (worker->rejection == 2) return "QR read: link is not HTTP or HTTPS";
	if (worker->rejection == 1) return "QR read: unsupported link length";
	if (worker->decode_error) return "QR candidate found: decoding...";
	if (worker->candidates > 0) return "QR candidate found";
	return "Searching for QR code...";
}

static void draw_scanner(vita2d_texture *preview, int has_preview,
	                     const QrDecodeWorker *worker) {
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_chrome_background(VT_THEME_BG, VT_THEME_SIGNAL_BRIGHT);
	/* The large square is intentionally above the normal header area. */
	ui_panel(248, 36, 468, 468, VT_THEME_SURFACE, VT_THEME_SIGNAL_BRIGHT, 1);
	if (has_preview && preview) {
		vita2d_draw_texture_part_scale(preview, QR_PREVIEW_X, QR_PREVIEW_Y,
		                              QR_CAMERA_CROP_X, QR_CAMERA_CROP_Y,
		                              QR_CAMERA_CROP_SIZE, QR_CAMERA_CROP_SIZE,
		                              (float)QR_PREVIEW_SIZE / QR_CAMERA_CROP_SIZE,
		                              (float)QR_PREVIEW_SIZE / QR_CAMERA_CROP_SIZE);
	} else {
		vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
		if (body) ui_font_draw_text_centered(body, 480, 278, 420, VT_THEME_TEXT,
		                                    UI_FONT_BODY, "Starting rear camera...");
	}
	vita2d_draw_rectangle(QR_GUIDE_X, QR_GUIDE_Y, QR_GUIDE_SIZE, 2, VT_THEME_SIGNAL_LIGHT);
	vita2d_draw_rectangle(QR_GUIDE_X, QR_GUIDE_Y, 2, QR_GUIDE_SIZE, VT_THEME_SIGNAL_LIGHT);
	vita2d_draw_rectangle(QR_GUIDE_X + QR_GUIDE_SIZE - 2, QR_GUIDE_Y, 2, QR_GUIDE_SIZE,
	                     VT_THEME_SIGNAL_LIGHT);
	vita2d_draw_rectangle(QR_GUIDE_X, QR_GUIDE_Y + QR_GUIDE_SIZE - 2, QR_GUIDE_SIZE, 2,
	                     VT_THEME_SIGNAL_LIGHT);
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (body) ui_font_draw_text_centered(body, 132, 132, 228, VT_THEME_TEXT,
	                                    UI_FONT_BODY, scanner_status(worker));
	if (small && worker) {
		char progress[96];
		snprintf(progress, sizeof(progress), "Frames %u submitted / %u analyzed",
		         worker->submitted_frames, worker->analyzed_frames);
		ui_font_draw_text_centered(small, 132, 155, 228, VT_THEME_TEXT_MUTED,
		                           UI_FONT_SMALL, progress);
	}
	ui_action_button(24, 484, 212, 42, VT_THEME_SURFACE_RAISED, "Circle",
	                 vt_i18n_str(VT_STR_NETWORK_CANCEL), 0);
	vita2d_end_drawing();
	vita2d_wait_rendering_done();
	vita2d_swap_buffers();
}

int ui_qr_scan_https_url(char *out, size_t out_size) {
	if (!out || out_size < 2) return -1;
	out[0] = '\0';
	g_last_error[0] = '\0';
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
	worker->thid = -1;
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
	worker->thid = sceKernelCreateThread("VitaMediaDeckQrDecode", qr_decode_thread,
	                                     QR_WORKER_PRIORITY, QR_WORKER_STACK, 0, 0, NULL);
	if (worker->thid < 0 ||
	    sceKernelStartThread(worker->thid, sizeof(worker), &worker) < 0) {
		log_printf("qr camera: decoder thread start failed\n");
		set_last_error("QR decoder worker", worker->thid);
		if (worker->thid >= 0) sceKernelDeleteThread(worker->thid);
		worker->thid = -1;
		goto done;
	}

	SceCtrlData previous;
	int candidate_reported = 0;
	int decode_error_reported = 0;
	int rejection_reported = 0;
	memset(&previous, 0, sizeof(previous));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	log_printf("qr camera: active ABGR 640x480/30, BLACKWHITE, decoder centre 480x480\n");
	while (1) {
		SceCtrlData controls;
		sceCtrlPeekBufferPositive(0, &controls, 1);
		unsigned pressed = controls.buttons & ~previous.buttons;
		previous = controls;
		if (pressed & SCE_CTRL_CIRCLE) { result = 0; break; }
		/* The worker publishes result with a barrier. Check it before another
		 * blocking camera read, so a successful decode cannot be delayed or
		 * lost behind the preview loop. */
		if (worker->result) {
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
	if (worker) {
		worker->stop = 1;
		if (worker->thid >= 0) {
			sceKernelWaitThreadEnd(worker->thid, NULL, NULL);
			sceKernelDeleteThread(worker->thid);
		}
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
