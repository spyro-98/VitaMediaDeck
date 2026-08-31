#include "ui/qr_scanner.h"

#include <stdio.h>
#include <string.h>

#include <psp2/camera.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <vita2d.h>

#include <quirc.h>

#include "app_paths.h"
#include "common/text_log.h"
#include "i18n/i18n.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/runtime.h"
#include "ui/theme.h"

#define QR_WIDTH 640
#define QR_HEIGHT 480
/* Decode the centre guide area rather than the entire camera image.  quirc
 * uses a global threshold, so excluding the bright/dark background around the
 * guide makes phone-display QR codes substantially more reliable. */
#define QR_SCAN_WIDTH 544
#define QR_SCAN_HEIGHT 400
#define QR_SCAN_X ((QR_WIDTH - QR_SCAN_WIDTH) / 2)
#define QR_SCAN_Y ((QR_HEIGHT - QR_SCAN_HEIGHT) / 2)
/* The Vita camera's PHYCONT allocation must be a multiple of 1 MiB.  Two MiB
 * is the known-good allocation used by SDL's Vita camera backend. */
#define QR_CAMERA_BUFFER_SIZE (2 * 1024 * 1024)
#define QR_Y_SIZE (QR_WIDTH * QR_HEIGHT)
#define QR_UV_WIDTH ((QR_WIDTH + 1) / 2)
#define QR_UV_HEIGHT ((QR_HEIGHT + 1) / 2)
#define QR_UV_SIZE (QR_UV_WIDTH * QR_UV_HEIGHT)

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

static int upload_preview(vita2d_texture *preview, const unsigned char *source) {
	unsigned int stride;
	unsigned char *pixels;
	if (!preview || !source) return -1;
	stride = vita2d_texture_get_stride(preview);
	pixels = vita2d_texture_get_datap(preview);
	if (!pixels || stride < QR_WIDTH * sizeof(uint16_t)) return -1;
	for (int y = 0; y < QR_HEIGHT; y++) {
		uint16_t *destination = (uint16_t *)(pixels + y * stride);
		const unsigned char *row = source + y * QR_WIDTH;
		for (int x = 0; x < QR_WIDTH; x++) {
			unsigned char gray = row[x];
			destination[x] = (uint16_t)(((gray >> 3) << 11) |
		                             ((gray >> 2) << 5) | (gray >> 3));
		}
	}
	return 0;
}

static void draw_scanner(vita2d_texture *preview, int has_preview) {
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_chrome_background(VT_THEME_BG, VT_THEME_SIGNAL_BRIGHT);
	ui_brand_draw_header(vt_i18n_str(VT_STR_NETWORK_QR_TITLE));
	ui_scene_identity(66, 68, 540, "NET/QR", vt_i18n_str(VT_STR_NETWORK_QR_TITLE),
	                  vt_i18n_str(VT_STR_NETWORK_QR_DETAIL));
	ui_panel(252, 116, 456, 342, VT_THEME_SURFACE, VT_THEME_SIGNAL_BRIGHT, 1);
	if (has_preview && preview) {
		/* Keep the preview in the same panel, irrespective of capture size. */
		vita2d_draw_texture_scale(preview, 264, 125,
		                         432.0f / QR_WIDTH, 324.0f / QR_HEIGHT);
	} else {
		vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
		if (body) ui_font_draw_text_centered(body, 480, 278, 360, VT_THEME_TEXT,
		                                    UI_FONT_BODY, "Starting rear camera...");
	}
	vita2d_draw_rectangle(300, 153, 360, 2, VT_THEME_SIGNAL_LIGHT);
	vita2d_draw_rectangle(300, 153, 2, 270, VT_THEME_SIGNAL_LIGHT);
	vita2d_draw_rectangle(658, 153, 2, 270, VT_THEME_SIGNAL_LIGHT);
	vita2d_draw_rectangle(300, 421, 360, 2, VT_THEME_SIGNAL_LIGHT);
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (body) ui_font_draw_text_centered(body, 480, 445, 400, VT_THEME_TEXT,
	                                    UI_FONT_BODY, vt_i18n_str(VT_STR_NETWORK_QR_AIM));
	if (small) ui_font_draw_text_centered(small, 480, 469, 420, VT_THEME_TEXT_MUTED,
	                                     UI_FONT_SMALL, vt_i18n_str(VT_STR_NETWORK_QR_HTTPS));
	ui_action_button(368, 492, 224, 42, VT_THEME_SURFACE_RAISED, "Circle",
	                 vt_i18n_str(VT_STR_NETWORK_CANCEL), 0);
	vita2d_end_drawing();
	vita2d_wait_rendering_done();
	vita2d_swap_buffers();
}

int ui_qr_scan_https_url(char *out, size_t out_size) {
	if (!out || out_size < 2) return -1;
	out[0] = '\0';
	g_last_error[0] = '\0';
	void *buffer = NULL;
	vita2d_texture *preview = NULL;
	struct quirc *decoder = NULL;
	int camera_open = 0;
	int camera_started = 0;
	int result = -1;
	/* Camera DMA requires a physically contiguous, non-cached buffer. */
	SceUID block = sceKernelAllocMemBlock("VitaMediaDeck QR camera",
	                                     SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_NC_RW,
	                                     QR_CAMERA_BUFFER_SIZE, NULL);
	if (block < 0 || sceKernelGetMemBlockBase(block, &buffer) < 0) {
		log_printf("qr camera: PHYCONT buffer allocation -> 0x%08X\n", (unsigned)block);
		set_last_error("Camera buffer allocation", block < 0 ? block : -1);
		if (block >= 0) sceKernelFreeMemBlock(block);
		return -1;
	}
	SceCameraInfo info;
	memset(&info, 0, sizeof(info));
	info.size = sizeof(info);
	/* Shared is the standard VitaSDK sample configuration.  Exclusive access
	 * can be rejected before a frame is ever delivered. */
	info.priority = SCE_CAMERA_PRIORITY_SHARE;
	/* RAW8 is rejected by retail SceCamera for this configuration.  The Y plane
	 * of YUV420 is already an 8-bit grayscale image suitable for quirc. */
	info.buffer = 0;
	info.range = 1;
	info.format = SCE_CAMERA_FORMAT_YUV420_PLANE;
	/* 640x480 gives quirc enough pixels to resolve the finder patterns of a
	 * QR code shown on a phone or printed at a normal distance.  Keep the live
	 * preview at the camera's 30 fps mode; decoding still happens per frame. */
	info.resolution = SCE_CAMERA_RESOLUTION_640_480;
	info.framerate = SCE_CAMERA_FRAMERATE_30_FPS;
	info.pitch = 0;
	info.sizeIBase = QR_Y_SIZE;
	info.sizeUBase = QR_UV_SIZE;
	info.sizeVBase = QR_UV_SIZE;
	info.pIBase = buffer;
	info.pUBase = (unsigned char *)buffer + QR_Y_SIZE;
	info.pVBase = (unsigned char *)buffer + QR_Y_SIZE + QR_UV_SIZE;
	result = sceCameraOpen(SCE_CAMERA_DEVICE_BACK, &info);
	if (result < 0) {
		log_printf("qr camera: open YUV420 640x480/30 -> 0x%08X\n", (unsigned)result);
		set_last_error("Rear camera open", result);
		sceKernelFreeMemBlock(block);
		return -1;
	}
	camera_open = 1;
	result = sceCameraStart(SCE_CAMERA_DEVICE_BACK);
	if (result < 0) {
		log_printf("qr camera: start -> 0x%08X\n", (unsigned)result);
		set_last_error("Rear camera start", result);
		goto done;
	}
	camera_started = 1;
	result = sceCameraIsActive(SCE_CAMERA_DEVICE_BACK);
	if (result <= 0) {
		log_printf("qr camera: active -> 0x%08X\n", (unsigned)result);
		set_last_error("Rear camera activation", result);
		goto done;
	}
	preview = vita2d_create_empty_texture_format(QR_WIDTH, QR_HEIGHT,
	                                             SCE_GXM_TEXTURE_FORMAT_R5G6B5);
	if (!preview) {
		log_printf("qr camera: preview texture allocation failed\n");
		set_last_error("Camera preview texture", -1);
		goto done;
	}
	vita2d_texture_set_filters(preview, SCE_GXM_TEXTURE_FILTER_POINT,
	                          SCE_GXM_TEXTURE_FILTER_POINT);
	decoder = quirc_new();
	if (!decoder || quirc_resize(decoder, QR_SCAN_WIDTH, QR_SCAN_HEIGHT) < 0) {
		log_printf("qr camera: quirc initialization failed\n");
		set_last_error("QR decoder initialization", -1);
		goto done;
	}
	SceCtrlData previous;
	int candidate_reported = 0;
	int decode_error_reported = 0;
	int unsupported_payload_reported = 0;
	memset(&previous, 0, sizeof(previous));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	while (1) {
		SceCtrlData controls;
		sceCtrlPeekBufferPositive(0, &controls, 1);
		unsigned pressed = controls.buttons & ~previous.buttons;
		previous = controls;
		if (pressed & SCE_CTRL_CIRCLE) { result = 0; break; }
		SceCameraRead read;
		memset(&read, 0, sizeof(read));
		read.size = sizeof(read);
		/* Match the Vita camera backend: use the most recently acquired frame
		 * instead of blocking the UI while waiting for the next one. */
		read.mode = 1;
		int read_result = sceCameraRead(SCE_CAMERA_DEVICE_BACK, &read);
		if (read_result < 0) {
			log_printf("qr camera: frame read -> 0x%08X\n", (unsigned)read_result);
			set_last_error("Rear camera frame read", read_result);
			result = -1;
			break;
		}
		if (upload_preview(preview, buffer) < 0) {
			set_last_error("Camera preview upload", -1);
			result = -1;
			break;
		}
		int width, height;
		uint8_t *image = quirc_begin(decoder, &width, &height);
		if (!image || width != QR_SCAN_WIDTH || height != QR_SCAN_HEIGHT) {
			set_last_error("QR decoder frame", -1);
			result = -1;
			break;
		}
		for (int y = 0; y < QR_SCAN_HEIGHT; y++) {
			const unsigned char *source = (const unsigned char *)buffer +
			    (y + QR_SCAN_Y) * QR_WIDTH + QR_SCAN_X;
			memcpy(image + y * QR_SCAN_WIDTH, source, QR_SCAN_WIDTH);
		}
		quirc_end(decoder);
		int candidates = quirc_count(decoder);
		if (candidates > 0 && !candidate_reported) {
			log_printf("qr camera: detected %d candidate(s)\n", candidates);
			candidate_reported = 1;
		}
		for (int i = 0; i < candidates; i++) {
			struct quirc_code code;
			struct quirc_data data;
			quirc_extract(decoder, i, &code);
			quirc_decode_error_t decode_result = quirc_decode(&code, &data);
			if (decode_result != QUIRC_SUCCESS) {
				if (!decode_error_reported) {
					log_printf("qr camera: candidate decode: %s\n",
					           quirc_strerror(decode_result));
					decode_error_reported = 1;
				}
				continue;
			}
			if (data.payload_len < 9 || (size_t)data.payload_len >= out_size) {
				if (!unsupported_payload_reported) {
					log_printf("qr camera: decoded payload has unsupported length (%d)\n",
					           data.payload_len);
					unsupported_payload_reported = 1;
				}
				continue;
			}
			memcpy(out, data.payload, (size_t)data.payload_len);
			out[data.payload_len] = '\0';
			if (!strncmp(out, "https://", 8) || !strncmp(out, "http://", 7)) {
				result = 1;
				goto done;
			}
			if (!unsupported_payload_reported) {
				log_printf("qr camera: decoded payload is not an HTTP(S) URL\n");
				unsupported_payload_reported = 1;
			}
			out[0] = '\0';
		}
		draw_scanner(preview, 1);
	}
done:
	if (decoder) quirc_destroy(decoder);
	if (preview) vita2d_free_texture(preview);
	if (camera_started) sceCameraStop(SCE_CAMERA_DEVICE_BACK);
	if (camera_open) sceCameraClose(SCE_CAMERA_DEVICE_BACK);
	sceKernelFreeMemBlock(block);
	/* Keep the QR diagnostics available on ux0 immediately after cancelling
	 * the scanner, not only when the whole application exits. */
	log_save(VITAMEDIADECK_SESSION_LOG_PATH);
	return result;
}
