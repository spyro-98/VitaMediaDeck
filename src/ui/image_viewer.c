#include "ui/image_viewer.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "common/text_log.h"
#include "i18n/i18n.h"
#include "media/image_loader.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/runtime.h"
#include "ui/loading_screen.h"
#include "ui/theme.h"
#include "ui/touch.h"

#define VIEWPORT_X 0.0f
#define VIEWPORT_Y 48.0f
#define VIEWPORT_W 960.0f
#define VIEWPORT_H 448.0f
#define VIEWPORT_CX (VIEWPORT_X + VIEWPORT_W * .5f)
#define VIEWPORT_CY (VIEWPORT_Y + VIEWPORT_H * .5f)
#define PI_F 3.14159265358979323846f

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

static int image_load_task(void *opaque) {
	ImageLoadTask *task = opaque;
	return vt_image_decode(task->path, 2048, &task->decoded,
	                       task->error, sizeof(task->error));
}

static float clamp_float(float value, float low, float high) {
	return value < low ? low : value > high ? high : value;
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

static void draw_viewer(vita2d_texture *texture, const VtImageInfo *info,
	                    const char *title, const ImageViewState *state) {
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
	vita2d_disable_clipping();
	/* A compact gallery rail keeps metadata separate from the image itself. */
	vita2d_draw_rectangle(0, 0, 960, 48, VT_THEME_GLASS_A(238));
	vita2d_draw_rectangle(0, 47, 960, 1, VT_THEME_SPECTRAL);
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (body) {
		char fitted[256];
		ui_font_fit_text(body, UI_FONT_BODY, title ? title : "", fitted,
		                 sizeof(fitted), 470);
		ui_font_draw_text(body, 28, 31, VT_THEME_TEXT, UI_FONT_BODY, fitted);
	}
	if (small && info) {
		char details[160];
		if (info->downscaled)
			snprintf(details, sizeof(details), "%s  %ux%u  >  %ux%u   %.0f%%",
			         info->format, info->source_width, info->source_height,
			         info->decoded_width, info->decoded_height, state->zoom * 100.0f);
		else
			snprintf(details, sizeof(details), "%s  %ux%u   %.0f%%",
			         info->format, info->source_width, info->source_height,
			         state->zoom * 100.0f);
		int width = ui_font_text_width(small, UI_FONT_SMALL, details);
		ui_font_draw_text(small, 932 - width, 29, VT_THEME_TEXT_MUTED,
		                  UI_FONT_SMALL, details);
	}
	vita2d_draw_rectangle(0, 496, 960, 48, VT_THEME_GLASS_A(238));
	vita2d_draw_rectangle(0, 496, 960, 1, VT_THEME_BORDER_DIM);
	if (small) {
		char touch_hint[128];
		ui_font_fit_text(small, UI_FONT_SMALL,
		                 vt_i18n_str(VT_STR_IMAGE_VIEWER_TOUCH_HINT), touch_hint,
		                 sizeof(touch_hint), 250);
		ui_font_draw_text(small, 24, 526, VT_THEME_TEXT_MUTED, UI_FONT_SMALL,
		                  touch_hint);
		const char *hardware = vt_i18n_str(VT_STR_IMAGE_VIEWER_HARDWARE_HINT);
		char hardware_hint[256];
		int width = ui_font_fit_text(small, UI_FONT_SMALL, hardware,
		                             hardware_hint, sizeof(hardware_hint), 640);
		ui_font_draw_text(small, 936 - width, 526, VT_THEME_TEXT_MUTED,
		                  UI_FONT_SMALL, hardware_hint);
	}
	vita2d_end_drawing();
	vita2d_wait_rendering_done();
	vita2d_swap_buffers();
}

int ui_image_viewer_show(const char *path, const char *title) {
	ImageLoadTask task;
	memset(&task, 0, sizeof(task));
	task.path = path;
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
	log_printf("image viewer: %s %ux%u proxy=%ux%u path=%s\n",
	           task.info.format, task.info.source_width, task.info.source_height,
	           task.info.decoded_width, task.info.decoded_height, path);
	ImageViewState state;
	memset(&state, 0, sizeof(state));
	state.zoom = 1.0f;
	ui_touch_reset();
	SceCtrlData controls, previous;
	memset(&previous, 0, sizeof(previous));
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	sceCtrlPeekBufferPositive(0, &previous, 1);
	for (;;) {
		sceCtrlPeekBufferPositive(0, &controls, 1);
		unsigned int pressed = controls.buttons & ~previous.buttons;
		previous = controls;
		if (pressed & SCE_CTRL_CIRCLE) break;
		if (pressed & SCE_CTRL_CROSS) {
			state.zoom = 1.0f;
			state.pan_x = state.pan_y = state.rotation = 0.0f;
		}
		if (pressed & SCE_CTRL_LTRIGGER) state.rotation -= PI_F * .5f;
		if (pressed & SCE_CTRL_RTRIGGER) state.rotation += PI_F * .5f;
		if (pressed & SCE_CTRL_TRIANGLE)
			change_zoom(&state, task.texture, 1.25f, VIEWPORT_CX, VIEWPORT_CY);
		if (pressed & SCE_CTRL_SQUARE)
			change_zoom(&state, task.texture, .8f, VIEWPORT_CX, VIEWPORT_CY);
		float analog_x = fabsf((float)controls.lx - 128.0f) > 20.0f
		               ? ((float)controls.lx - 128.0f) / 16.0f : 0.0f;
		float analog_y = fabsf((float)controls.ly - 128.0f) > 20.0f
		               ? ((float)controls.ly - 128.0f) / 16.0f : 0.0f;
		state.pan_x += analog_x;
		state.pan_y += analog_y;
		if (controls.buttons & SCE_CTRL_LEFT) state.pan_x -= 7.0f;
		if (controls.buttons & SCE_CTRL_RIGHT) state.pan_x += 7.0f;
		if (controls.buttons & SCE_CTRL_UP) state.pan_y -= 7.0f;
		if (controls.buttons & SCE_CTRL_DOWN) state.pan_y += 7.0f;
		float right_y = (float)controls.ry - 128.0f;
		if (fabsf(right_y) > 28.0f)
			change_zoom(&state, task.texture, 1.0f - right_y / 6400.0f,
			            VIEWPORT_CX, VIEWPORT_CY);
		float right_x = (float)controls.rx - 128.0f;
		if (fabsf(right_x) > 28.0f)
			state.rotation += right_x / 18000.0f;
		state.rotation = normalized_angle_delta(state.rotation);
		handle_touch(&state, task.texture);
		clamp_pan(&state, task.texture);
		draw_viewer(task.texture, &task.info, title, &state);
		sceKernelDelayThread(16 * 1000);
	}
	vita2d_free_texture(task.texture);
	ui_touch_reset();
	return 0;
}
