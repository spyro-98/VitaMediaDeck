#include "ui/image_viewer.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "common/text_log.h"
#include "i18n/i18n.h"
#include "media/image_loader.h"
#include "settings/preferences.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/format.h"
#include "ui/runtime.h"
#include "ui/loading_screen.h"
#include "ui/mini_player.h"
#include "ui/sections_sidebar.h"
#include "ui/theme.h"
#include "ui/touch.h"

#define VIEWPORT_X 0.0f
#define VIEWPORT_Y 0.0f
#define VIEWPORT_W 960.0f
#define VIEWPORT_H 544.0f
#define VIEWPORT_CX (VIEWPORT_X + VIEWPORT_W * .5f)
#define VIEWPORT_CY (VIEWPORT_Y + VIEWPORT_H * .5f)
#define PI_F 3.14159265358979323846f
#define IMAGE_VIEWER_MAX_EDGE 960U
#define IMAGE_DETAIL_TILE_EDGE 1024U
#define IMAGE_DETAIL_MIN_ZOOM 1.25f
#define IMAGE_DETAIL_PATH_MAX 768U
#define IMAGE_DETAIL_THREAD_STACK (512U * 1024U)

typedef struct {
	const char *path;
	VtDecodedImage decoded;
	vita2d_texture *texture;
	VtImageInfo info;
	char error[160];
} ImageLoadTask;

typedef struct {
	float zoom;
	float pan_x;
	float pan_y;
	float rotation;
	UiTouchPoints previous_touch;
} ImageViewState;

typedef struct {
	char path[IMAGE_DETAIL_PATH_MAX];
	unsigned int source_x;
	unsigned int source_y;
	unsigned int width;
	unsigned int height;
	VtDecodedImage decoded;
	char error[160];
} ImageDetailTask;

typedef struct {
	vita2d_texture *texture;
	unsigned int source_x;
	unsigned int source_y;
	unsigned int width;
	unsigned int height;
} ImageNativeDetail;

typedef struct {
	pthread_t thread;
	volatile int running;
	volatile int done;
	int result;
	uint64_t retry_after_us;
	uint64_t started_us;
	ImageDetailTask task;
} ImageDetailWorker;

static int image_load_task(void *opaque) {
	ImageLoadTask *task = opaque;
	return vt_image_decode(task->path, IMAGE_VIEWER_MAX_EDGE, &task->decoded,
	                       task->error, sizeof(task->error));
}

static int image_detail_load_task(void *opaque) {
	ImageDetailTask *task = opaque;
	return vt_image_decode_native_region(task->path, task->source_x,
	                                     task->source_y, task->width,
	                                     task->height, &task->decoded,
	                                     task->error, sizeof(task->error));
}

static void *image_detail_worker_main(void *opaque) {
	ImageDetailWorker *worker = opaque;
	worker->result = image_detail_load_task(&worker->task);
	__sync_synchronize();
	worker->done = 1;
	__sync_synchronize();
	return NULL;
}

static float clamp_float(float value, float low, float high) {
	return value < low ? low : value > high ? high : value;
}

static unsigned fade_color(unsigned color, float opacity) {
	if (opacity <= 0.0f) return color & 0x00ffffffU;
	if (opacity >= 1.0f) return color;
	unsigned alpha = (color >> 24) & 0xffU;
	return (color & 0x00ffffffU) |
	       ((unsigned)((float)alpha * opacity + .5f) << 24);
}

static float approach(float value, float target, float rate,
	                  float delta_seconds) {
	if (delta_seconds <= 0.0f) return value;
	return value + (target - value) *
	       (1.0f - expf(-rate * delta_seconds));
}

static float normalized_angle_delta(float value) {
	while (value > PI_F) value -= PI_F * 2.0f;
	while (value < -PI_F) value += PI_F * 2.0f;
	return value;
}

static float fit_scale(vita2d_texture *texture) {
	float width = (float)vita2d_texture_get_width(texture);
	float height = (float)vita2d_texture_get_height(texture);
	if (width <= 0.0f || height <= 0.0f) return 1.0f;
	float x = VIEWPORT_W / width;
	float y = VIEWPORT_H / height;
	return x < y ? x : y;
}

static void native_detail_free(ImageNativeDetail *detail) {
	if (!detail) return;
	if (detail->texture) vita2d_free_texture(detail->texture);
	memset(detail, 0, sizeof(*detail));
}

static void native_detail_center(const ImageViewState *state,
	                              vita2d_texture *proxy,
	                              const VtImageInfo *info,
	                              float *source_x, float *source_y) {
	if (!state || !proxy || !info || !source_x || !source_y) return;
	float proxy_width = (float)vita2d_texture_get_width(proxy);
	float scale = fit_scale(proxy) * state->zoom * proxy_width /
	              (float)info->source_width;
	if (scale <= 0.0001f) { *source_x = *source_y = 0.0f; return; }
	float screen_x = -state->pan_x;
	float screen_y = -state->pan_y;
	float cosine = cosf(state->rotation);
	float sine = sinf(state->rotation);
	*source_x = info->source_width * .5f +
	            (cosine * screen_x + sine * screen_y) / scale;
	*source_y = info->source_height * .5f +
	            (-sine * screen_x + cosine * screen_y) / scale;
}

static int native_detail_contains(const ImageNativeDetail *detail,
	                               const VtImageInfo *info,
	                               float source_x, float source_y) {
	if (!detail || !detail->texture) return 0;
	const float margin = 96.0f;
	float left = detail->source_x > 0 ? detail->source_x + margin
	                                      : (float)detail->source_x;
	float top = detail->source_y > 0 ? detail->source_y + margin
	                                    : (float)detail->source_y;
	float right = info && detail->source_x + detail->width < info->source_width
	            ? detail->source_x + detail->width - margin
	            : (float)(detail->source_x + detail->width);
	float bottom = info && detail->source_y + detail->height < info->source_height
	             ? detail->source_y + detail->height - margin
	             : (float)(detail->source_y + detail->height);
	return source_x >= left && source_y >= top &&
	       source_x < right && source_y < bottom;
}

static void native_detail_request(const char *path, const VtImageInfo *info,
	                               vita2d_texture *proxy,
	                               const ImageViewState *state,
	                               const ImageNativeDetail *detail,
	                               ImageDetailWorker *worker) {
	if (!path || !info || !proxy || !state || !detail || !worker ||
	    state->zoom < IMAGE_DETAIL_MIN_ZOOM ||
	    !info->downscaled || worker->running ||
	    sceKernelGetProcessTimeWide() < worker->retry_after_us) return;
	float center_x = 0.0f, center_y = 0.0f;
	native_detail_center(state, proxy, info, &center_x, &center_y);
	if (native_detail_contains(detail, info, center_x, center_y)) return;
	unsigned int width = info->source_width < IMAGE_DETAIL_TILE_EDGE
	                   ? info->source_width : IMAGE_DETAIL_TILE_EDGE;
	unsigned int height = info->source_height < IMAGE_DETAIL_TILE_EDGE
	                    ? info->source_height : IMAGE_DETAIL_TILE_EDGE;
	unsigned int source_x = center_x > width * .5f
	                      ? (unsigned int)(center_x - width * .5f) : 0;
	unsigned int source_y = center_y > height * .5f
	                      ? (unsigned int)(center_y - height * .5f) : 0;
	if (source_x + width > info->source_width) source_x = info->source_width - width;
	if (source_y + height > info->source_height) source_y = info->source_height - height;
	/* WebP crop origins are even; use that same origin for every codec so the
	 * tile remains geometrically exact when formats are switched. */
	source_x &= ~1U;
	source_y &= ~1U;
	memset(&worker->task, 0, sizeof(worker->task));
	int path_length = snprintf(worker->task.path, sizeof(worker->task.path),
	                           "%s", path);
	if (path_length < 0 || (size_t)path_length >= sizeof(worker->task.path)) {
		log_printf("image viewer native detail path too long\n");
		return;
	}
	worker->task.source_x = source_x;
	worker->task.source_y = source_y;
	worker->task.width = width;
	worker->task.height = height;
	worker->result = -1;
	worker->done = 0;
	worker->started_us = sceKernelGetProcessTimeWide();
	pthread_attr_t attr;
	int attr_ready = pthread_attr_init(&attr) == 0;
	int create_result = attr_ready
	                  ? pthread_attr_setstacksize(&attr, IMAGE_DETAIL_THREAD_STACK)
	                  : -1;
	if (create_result == 0)
		create_result = pthread_create(&worker->thread, &attr,
		                               image_detail_worker_main, worker);
	if (attr_ready) pthread_attr_destroy(&attr);
	if (create_result != 0) {
		log_printf("image viewer native detail thread failed: %d\n", create_result);
		worker->retry_after_us = sceKernelGetProcessTimeWide() + 2000000ULL;
		return;
	}
	worker->running = 1;
	log_printf("image viewer native detail queued: %ux%u at %u,%u\n",
	           width, height, source_x, source_y);
}

static void native_detail_pump(ImageDetailWorker *worker,
	                            ImageNativeDetail *detail) {
	if (!worker || !worker->running || !worker->done || !detail) return;
	__sync_synchronize();
	pthread_join(worker->thread, NULL);
	worker->running = 0;
	if (worker->result < 0) {
		log_printf("image viewer native detail failed: %s\n", worker->task.error);
		vt_image_decoded_free(&worker->task.decoded);
		worker->retry_after_us = sceKernelGetProcessTimeWide() + 5000000ULL;
		return;
	}
	vita2d_texture *texture = vt_image_upload(&worker->task.decoded,
	                                          worker->task.error,
	                                          sizeof(worker->task.error));
	vt_image_decoded_free(&worker->task.decoded);
	if (!texture) {
		log_printf("image viewer native detail upload failed: %s\n",
		           worker->task.error);
		return;
	}
	native_detail_free(detail);
	detail->texture = texture;
	detail->source_x = worker->task.source_x;
	detail->source_y = worker->task.source_y;
	detail->width = worker->task.width;
	detail->height = worker->task.height;
	worker->retry_after_us = 0;
	uint64_t elapsed_ms = worker->started_us
	                    ? (sceKernelGetProcessTimeWide() - worker->started_us) / 1000ULL
	                    : 0;
	log_printf("image viewer native detail ready: %ux%u at %u,%u in %llums\n",
	           detail->width, detail->height,
	           detail->source_x, detail->source_y,
	           (unsigned long long)elapsed_ms);
}

static void clamp_pan(ImageViewState *state, vita2d_texture *texture) {
	float scale = fit_scale(texture) * state->zoom;
	float width = (float)vita2d_texture_get_width(texture) * scale;
	float height = (float)vita2d_texture_get_height(texture) * scale;
	float cosine = fabsf(cosf(state->rotation));
	float sine = fabsf(sinf(state->rotation));
	float half_width = (width * cosine + height * sine) * .5f;
	float half_height = (width * sine + height * cosine) * .5f;
	/* Always keep at least a 48 px strip recoverable on screen, while allowing
	 * the user to inspect every edge of a zoomed/rotated proxy. */
	float limit_x = half_width + VIEWPORT_W * .5f - 48.0f;
	float limit_y = half_height + VIEWPORT_H * .5f - 48.0f;
	if (half_width <= VIEWPORT_W * .5f) limit_x = 0.0f;
	if (half_height <= VIEWPORT_H * .5f) limit_y = 0.0f;
	state->pan_x = clamp_float(state->pan_x, -limit_x, limit_x);
	state->pan_y = clamp_float(state->pan_y, -limit_y, limit_y);
}

static void change_zoom(ImageViewState *state, vita2d_texture *texture,
	                    float factor, float anchor_x, float anchor_y) {
	float old_zoom = state->zoom;
	state->zoom = clamp_float(state->zoom * factor, .2f, 8.0f);
	float applied = state->zoom / old_zoom;
	/* Keep the point beneath the fingers visually anchored during pinch. */
	state->pan_x = anchor_x - VIEWPORT_CX -
	               (anchor_x - VIEWPORT_CX - state->pan_x) * applied;
	state->pan_y = anchor_y - VIEWPORT_CY -
	               (anchor_y - VIEWPORT_CY - state->pan_y) * applied;
	clamp_pan(state, texture);
}

static void handle_touch(ImageViewState *state, vita2d_texture *texture) {
	UiTouchPoints touch;
	ui_touch_peek_points(&touch);
	if (touch.count == 1 && state->previous_touch.count == 1 &&
	    touch.id[0] == state->previous_touch.id[0]) {
		state->pan_x += touch.x[0] - state->previous_touch.x[0];
		state->pan_y += touch.y[0] - state->previous_touch.y[0];
		clamp_pan(state, texture);
	} else if (touch.count >= 2 && state->previous_touch.count >= 2) {
		float old_dx = (float)(state->previous_touch.x[1] - state->previous_touch.x[0]);
		float old_dy = (float)(state->previous_touch.y[1] - state->previous_touch.y[0]);
		float new_dx = (float)(touch.x[1] - touch.x[0]);
		float new_dy = (float)(touch.y[1] - touch.y[0]);
		float old_distance = sqrtf(old_dx * old_dx + old_dy * old_dy);
		float new_distance = sqrtf(new_dx * new_dx + new_dy * new_dy);
		float old_mid_x = (state->previous_touch.x[0] +
		                   state->previous_touch.x[1]) * .5f;
		float old_mid_y = (state->previous_touch.y[0] +
		                   state->previous_touch.y[1]) * .5f;
		float new_mid_x = (touch.x[0] + touch.x[1]) * .5f;
		float new_mid_y = (touch.y[0] + touch.y[1]) * .5f;
		state->pan_x += new_mid_x - old_mid_x;
		state->pan_y += new_mid_y - old_mid_y;
		if (old_distance > 8.0f && new_distance > 8.0f)
			change_zoom(state, texture, new_distance / old_distance,
			            new_mid_x, new_mid_y);
		state->rotation += normalized_angle_delta(
		    atan2f(new_dy, new_dx) - atan2f(old_dy, old_dx));
		state->rotation = normalized_angle_delta(state->rotation);
		clamp_pan(state, texture);
	}
	state->previous_touch = touch;
}

static void draw_image_info_panel(float animation, const VtImageInfo *info,
	                              const ImageViewState *state) {
	if (animation <= .01f || !info || !state) return;
	const float width = 340.0f;
	float x = 960.0f - width * animation;
	vita2d_draw_rectangle(x, 0, width, 544, VT_THEME_BG_SOFT);
	vita2d_draw_rectangle(x, 0, 3, 544, VT_THEME_SPECTRAL);
	vita2d_draw_rectangle(x + 4, 0, 1, 544, VT_THEME_SIGNAL_BRIGHT);
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (body)
		ui_font_draw_text(body, x + 28, 48, VT_THEME_TEXT, UI_FONT_BODY,
		                  vt_i18n_str(VT_STR_IMAGE_VIEWER_PANEL_TITLE));
	if (small) {
		vita2d_draw_rectangle(x + width - 65, 18, 42, 26,
		                      VT_THEME_SURFACE_RAISED);
		int key_width = ui_font_text_width(small, UI_FONT_SMALL, "R1");
		ui_font_draw_text(small, x + width - 44 - key_width / 2, 38,
		                  VT_THEME_TEXT, UI_FONT_SMALL, "R1");
	}
	char dimensions[64], file_size[32], zoom[32];
	snprintf(dimensions, sizeof(dimensions), "%ux%u", info->source_width,
	         info->source_height);
	ui_format_file_size(info->file_size, file_size, sizeof(file_size));
	snprintf(zoom, sizeof(zoom), "%.0f%%", state->zoom * 100.0f);
	const char *labels[4] = {
		vt_i18n_str(VT_STR_IMAGE_VIEWER_PANEL_FORMAT),
		vt_i18n_str(VT_STR_IMAGE_VIEWER_PANEL_DIMENSIONS),
		vt_i18n_str(VT_STR_IMAGE_VIEWER_PANEL_FILE_SIZE),
		vt_i18n_str(VT_STR_IMAGE_VIEWER_PANEL_ZOOM)
	};
	const char *values[4] = { info->format, dimensions, file_size, zoom };
	for (int i = 0; i < 4; i++) {
		float y = 82.0f + i * 72.0f;
		ui_panel(x + 20, y, width - 40, 60, VT_THEME_SURFACE,
		         VT_THEME_BORDER_DIM, 0);
		if (!small) continue;
		ui_font_draw_text(small, x + 38, y + 24, VT_THEME_TEXT_MUTED,
		                  UI_FONT_SMALL, labels[i]);
		int value_width = ui_font_text_width(small, UI_FONT_SMALL, values[i]);
		ui_font_draw_text(small, x + width - value_width - 38, y + 49,
		                  VT_THEME_COLD_LIGHT, UI_FONT_SMALL, values[i]);
	}
}

static void draw_native_detail(const ImageNativeDetail *detail,
	                            vita2d_texture *proxy,
	                            const VtImageInfo *info,
	                            const ImageViewState *state) {
	if (!detail || !detail->texture || !proxy || !info || !state ||
	    !info->source_width || !info->source_height) return;
	float source_scale = fit_scale(proxy) * state->zoom *
	                     (float)vita2d_texture_get_width(proxy) /
	                     (float)info->source_width;
	float image_x = ((float)detail->source_x + detail->width * .5f -
	                 info->source_width * .5f) * source_scale;
	float image_y = ((float)detail->source_y + detail->height * .5f -
	                 info->source_height * .5f) * source_scale;
	float cosine = cosf(state->rotation);
	float sine = sinf(state->rotation);
	float x = VIEWPORT_CX + state->pan_x + cosine * image_x - sine * image_y;
	float y = VIEWPORT_CY + state->pan_y + sine * image_x + cosine * image_y;
	vita2d_draw_texture_scale_rotate_hotspot(
	    detail->texture, x, y, source_scale, source_scale, state->rotation,
	    detail->width * .5f, detail->height * .5f);
}

static void draw_viewer(vita2d_texture *texture, const VtImageInfo *info,
	                    const char *title, const ImageViewState *state,
	                    const ImageNativeDetail *detail,
	                    float hud_opacity, const UiSectionsSidebar *sidebar,
	                    float right_animation) {
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_chrome_background(VT_THEME_BG, VT_THEME_SPECTRAL);
	vita2d_draw_rectangle(VIEWPORT_X, VIEWPORT_Y, VIEWPORT_W, VIEWPORT_H,
	                      VT_THEME_MEDIA_BACKDROP);
	vita2d_set_clip_rectangle((int)VIEWPORT_X, (int)VIEWPORT_Y,
	                          (int)(VIEWPORT_X + VIEWPORT_W),
	                          (int)(VIEWPORT_Y + VIEWPORT_H));
	vita2d_enable_clipping();
	float scale = fit_scale(texture) * state->zoom;
	vita2d_draw_texture_scale_rotate_hotspot(
	    texture, VIEWPORT_CX + state->pan_x, VIEWPORT_CY + state->pan_y,
	    scale, scale, state->rotation,
	    vita2d_texture_get_width(texture) * .5f,
	    vita2d_texture_get_height(texture) * .5f);
	draw_native_detail(detail, texture, info, state);
	vita2d_disable_clipping();
	/* A compact gallery rail keeps metadata separate from the image itself and
	 * fades completely for an unobstructed image view. */
	vita2d_draw_rectangle(0, 0, 960, 48,
	                      fade_color(VT_THEME_GLASS_A(238), hud_opacity));
	vita2d_draw_rectangle(0, 47, 960, 1,
	                      fade_color(VT_THEME_SPECTRAL, hud_opacity));
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (body) {
		char fitted[256];
		ui_font_fit_text(body, UI_FONT_BODY, title ? title : "", fitted,
		                 sizeof(fitted), 470);
		ui_font_draw_text(body, 28, 31, fade_color(VT_THEME_TEXT, hud_opacity),
		                  UI_FONT_BODY, fitted);
	}
	if (small && info) {
		char details[192];
		char file_size[32];
		ui_format_file_size(info->file_size, file_size, sizeof(file_size));
		snprintf(details, sizeof(details), "%s  %ux%u  %s  %.0f%%",
		         info->format, info->source_width, info->source_height,
		         file_size, state->zoom * 100.0f);
		int width = ui_font_text_width(small, UI_FONT_SMALL, details);
		ui_font_draw_text(small, 932 - width, 29,
		                  fade_color(VT_THEME_TEXT_MUTED, hud_opacity),
		                  UI_FONT_SMALL, details);
	}
	vita2d_draw_rectangle(0, 496, 960, 48,
	                      fade_color(VT_THEME_GLASS_A(238), hud_opacity));
	vita2d_draw_rectangle(0, 496, 960, 1,
	                      fade_color(VT_THEME_BORDER_DIM, hud_opacity));
	if (small) {
		char touch_hint[128];
		ui_font_fit_text(small, UI_FONT_SMALL,
		                 vt_i18n_str(VT_STR_IMAGE_VIEWER_TOUCH_HINT), touch_hint,
		                 sizeof(touch_hint), 250);
		ui_font_draw_text(small, 24, 526,
		                  fade_color(VT_THEME_TEXT_MUTED, hud_opacity), UI_FONT_SMALL,
		                  touch_hint);
		const char *hardware = vt_i18n_str(VT_STR_IMAGE_VIEWER_HARDWARE_HINT);
		char hardware_hint[256];
		int width = ui_font_fit_text(small, UI_FONT_SMALL, hardware,
		                             hardware_hint, sizeof(hardware_hint), 640);
		ui_font_draw_text(small, 936 - width, 526,
		                  fade_color(VT_THEME_TEXT_MUTED, hud_opacity),
		                  UI_FONT_SMALL, hardware_hint);
	}
	draw_image_info_panel(right_animation, info, state);
	if (sidebar && sidebar->animation > .01f)
		ui_sections_sidebar_draw(sidebar);
	vita2d_end_drawing();
	vita2d_wait_rendering_done();
	vita2d_swap_buffers();
}

int ui_image_viewer_show(const char *path, const char *title) {
	ImageLoadTask task;
	memset(&task, 0, sizeof(task));
	task.path = path;
	uint64_t preview_started_us = sceKernelGetProcessTimeWide();
	int load_result = ui_loading_run(vt_i18n_str(VT_STR_IMAGE_VIEWER_LOADING),
	                                 image_load_task, &task, NULL, NULL, NULL);
	if (load_result >= 0) {
		task.info = task.decoded.info;
		task.texture = vt_image_upload(&task.decoded,
		                               task.error, sizeof(task.error));
	}
	vt_image_decoded_free(&task.decoded);
	if (load_result < 0 || !task.texture) {
		ui_message_show(vt_i18n_str(VT_STR_IMAGE_VIEWER_FAILED),
		                task.error[0] ? task.error
		                              : vt_i18n_str(VT_STR_IMAGE_VIEWER_FAILED_DETAIL),
		                3200);
		log_printf("image viewer: load failed path=%s detail=%s\n",
		           path ? path : "", task.error);
		return -1;
	}
	uint64_t preview_elapsed_ms =
	    (sceKernelGetProcessTimeWide() - preview_started_us) / 1000ULL;
	log_printf("image viewer: %s %ux%u proxy=%ux%u load=%llums path=%s\n",
	           task.info.format, task.info.source_width, task.info.source_height,
	           task.info.decoded_width, task.info.decoded_height,
	           (unsigned long long)preview_elapsed_ms, path);
	ImageViewState state;
	memset(&state, 0, sizeof(state));
	state.zoom = 1.0f;
	ImageNativeDetail detail;
	memset(&detail, 0, sizeof(detail));
	ImageDetailWorker detail_worker;
	memset(&detail_worker, 0, sizeof(detail_worker));
	int hud_visible = 1;
	float hud_opacity = 1.0f;
	int right_open = 0;
	float right_animation = 0.0f;
	uint64_t animation_last_us = sceKernelGetProcessTimeWide();
	UiSectionsSidebar sidebar;
	ui_sections_sidebar_init(&sidebar, UI_SECTION_LOCAL_MEDIA);
	int result = 0;
	ui_touch_reset();
	SceCtrlData controls, previous;
	memset(&previous, 0, sizeof(previous));
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	sceCtrlPeekBufferPositive(0, &previous, 1);
	for (;;) {
		sceCtrlPeekBufferPositive(0, &controls, 1);
		unsigned int pressed = controls.buttons & ~previous.buttons;
		previous = controls;
		UiTouchEvent touch;
		unsigned touch_flags = ui_touch_poll(&touch);
		if (hud_visible && !ui_mini_player_input_locked() &&
		    ui_brand_sections_hit(touch_flags, touch.x, touch.y)) {
			pressed |= SCE_CTRL_LTRIGGER;
			touch_flags = UI_TOUCH_EVENT_NONE;
		}
		UiTouchPoints touch_points;
		ui_touch_peek_points(&touch_points);
		uint64_t now = sceKernelGetProcessTimeWide();
		native_detail_pump(&detail_worker, &detail);
		float delta_seconds = animation_last_us && now > animation_last_us
		                    ? (float)(now - animation_last_us) / 1000000.0f
		                    : 1.0f / 60.0f;
		if (delta_seconds > .05f) delta_seconds = .05f;
		animation_last_us = now;
		if ((pressed & SCE_CTRL_LTRIGGER) &&
		    (right_open || right_animation > .01f)) {
			right_open = 0;
			right_animation = 0.0f;
		}
		if (pressed & SCE_CTRL_RTRIGGER) {
			if (sidebar.open || sidebar.animation > .01f) {
				sidebar.open = 0;
				sidebar.animation = 0.0f;
			}
			right_open = !right_open;
			pressed &= ~SCE_CTRL_RTRIGGER;
			ui_touch_reset();
		}
		int section = UI_SECTION_NONE;
		if (!right_open && right_animation <= .01f)
			section = ui_sections_sidebar_handle_buttons(
			    &sidebar, &pressed, controls.buttons, controls.ly);
		if (sidebar.open) {
			int touched = ui_sections_sidebar_handle_touch(
			    &sidebar, touch_flags, touch.x, touch.y);
			if (touched != UI_SECTION_NONE) section = touched;
			touch_flags = UI_TOUCH_EVENT_NONE;
		} else if (right_open) {
			if ((touch_flags & UI_TOUCH_EVENT_TAP) && touch.x < 620)
				right_open = 0;
			if (pressed & SCE_CTRL_CIRCLE) right_open = 0;
			pressed = 0;
			touch_flags = UI_TOUCH_EVENT_NONE;
		}
		ui_sections_sidebar_tick(&sidebar);
		if (section != UI_SECTION_NONE) {
			result = UI_IMAGE_VIEWER_SECTION_BASE + section;
			break;
		}
		int panels_open = sidebar.open || sidebar.animation > .01f ||
		                  right_open || right_animation > .01f;
		if (panels_open) {
			state.previous_touch.count = 0;
		} else if (pressed & SCE_CTRL_CIRCLE) break;
		int reset_view = !panels_open && (pressed & SCE_CTRL_START);
		if (reset_view) {
			state.zoom = 1.0f;
			state.pan_x = state.pan_y = state.rotation = 0.0f;
		}
		if (!panels_open && (pressed & SCE_CTRL_TRIANGLE)) {
			change_zoom(&state, task.texture, 1.25f, VIEWPORT_CX, VIEWPORT_CY);
		}
		if (!panels_open && (pressed & SCE_CTRL_CROSS)) {
			change_zoom(&state, task.texture, .8f, VIEWPORT_CX, VIEWPORT_CY);
		}
		if (!panels_open && (pressed & SCE_CTRL_SQUARE)) {
			state.rotation = normalized_angle_delta(state.rotation + PI_F * .5f);
			clamp_pan(&state, task.texture);
		}
		float analog_x = fabsf((float)controls.lx - 128.0f) > 20.0f
		               ? ((float)controls.lx - 128.0f) / 16.0f : 0.0f;
		float analog_y = fabsf((float)controls.ly - 128.0f) > 20.0f
		               ? ((float)controls.ly - 128.0f) / 16.0f : 0.0f;
		int detail_motion = analog_x != 0.0f || analog_y != 0.0f ||
		                    touch_points.count > 0 || (controls.buttons &
		                    (SCE_CTRL_LEFT | SCE_CTRL_RIGHT |
		                     SCE_CTRL_UP | SCE_CTRL_DOWN));
		/* Navigation is intentionally inverted: the left controls move the
		 * viewport across the image, as if dragging the canvas beneath it. */
		if (!panels_open) {
			state.pan_x -= analog_x;
			state.pan_y -= analog_y;
			if (controls.buttons & SCE_CTRL_LEFT) state.pan_x += 7.0f;
			if (controls.buttons & SCE_CTRL_RIGHT) state.pan_x -= 7.0f;
			if (controls.buttons & SCE_CTRL_UP) state.pan_y += 7.0f;
			if (controls.buttons & SCE_CTRL_DOWN) state.pan_y -= 7.0f;
		}
		float right_x = (float)controls.rx - 128.0f;
		if (fabsf(right_x) > 28.0f) detail_motion = 1;
		if (!panels_open && fabsf(right_x) > 28.0f)
			state.rotation += right_x / 18000.0f;
		state.rotation = normalized_angle_delta(state.rotation);
		if (!panels_open) {
			handle_touch(&state, task.texture);
			if (touch_flags & UI_TOUCH_EVENT_TAP) hud_visible = !hud_visible;
		}
		clamp_pan(&state, task.texture);
		/* After a pinch or after panning comes to rest, refresh only when the
		 * current source point has left the cached native tile. */
		if (!panels_open && !detail_motion)
			native_detail_request(path, &task.info, task.texture, &state,
			                      &detail, &detail_worker);
		if (vt_preferences_reduce_motion()) {
			hud_opacity = hud_visible ? 1.0f : 0.0f;
			right_animation = right_open ? 1.0f : 0.0f;
		} else {
			hud_opacity = approach(hud_opacity, hud_visible ? 1.0f : 0.0f,
			                       16.0f, delta_seconds);
			right_animation = approach(right_animation, right_open ? 1.0f : 0.0f,
			                           18.0f, delta_seconds);
		}
		draw_viewer(task.texture, &task.info, title, &state, &detail, hud_opacity,
		            &sidebar, right_animation);
		sceKernelDelayThread(16 * 1000);
	}
	if (detail_worker.running) {
		pthread_join(detail_worker.thread, NULL);
		vt_image_decoded_free(&detail_worker.task.decoded);
	}
	native_detail_free(&detail);
	vita2d_free_texture(task.texture);
	ui_touch_reset();
	return result;
}
