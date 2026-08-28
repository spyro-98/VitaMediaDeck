#include "ui/loading_screen.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>

#include "common/text_log.h"
#include "i18n/i18n.h"
#include "settings/preferences.h"
#include "ui/brand.h"
#include "ui/runtime.h"
#include "ui/theme.h"
#include "ui/touch.h"

#define SCREEN_WIDTH  960
#define SCREEN_HEIGHT 544

#define COLOR_BG       VT_THEME_BG
#define COLOR_TEXT     VT_THEME_TEXT
#define COLOR_MUTED    VT_THEME_TEXT_MUTED
#define COLOR_TRACK    VT_THEME_BORDER
#define COLOR_ACCENT   VT_THEME_BLUE_BRIGHT
#define COLOR_BUFFER   VT_THEME_BLUE_LIGHT

#define TASK_THREAD_PRIORITY 0x10000100
#define TASK_THREAD_STACK    0x40000
#define CANCEL_TOUCH_HOLD_US (800 * 1000ULL)
#define CANCEL_TOUCH_MOVE_PX 18

typedef struct {
	UiLoadingTaskFn task;
	void *ctx;
	volatile int done;
	volatile int result;
} UiTaskState;

typedef struct {
	UiTaskState *state;
} UiTaskThreadArgs;

static int loading_task_thread(SceSize args, void *argp) {
	(void)args;
	UiTaskThreadArgs *thread_args = (UiTaskThreadArgs *)argp;
	UiTaskState *state = thread_args->state;
	state->result = state->task(state->ctx);
	__sync_synchronize();
	state->done = 1;
	return sceKernelExitThread(0);
}

static void *loading_task_pthread(void *argp) {
	UiTaskThreadArgs *thread_args = (UiTaskThreadArgs *)argp;
	UiTaskState *state = thread_args->state;
	state->result = state->task(state->ctx);
	__sync_synchronize();
	state->done = 1;
	return NULL;
}

static int touch_is_cancel_hold(unsigned int flags, const UiTouchEvent *touch) {
	if (!(flags & UI_TOUCH_EVENT_UP) || !touch ||
	    touch->duration_us < CANCEL_TOUCH_HOLD_US) return 0;
	int dx = touch->x - touch->down_x;
	int dy = touch->y - touch->down_y;
	return dx * dx + dy * dy <= CANCEL_TOUCH_MOVE_PX * CANCEL_TOUCH_MOVE_PX;
}

static void draw_centered_text(int y, unsigned int color,
	                           unsigned int size, const char *text) {
	vita2d_font *font = ui_runtime_font(size);
	if (!font || !text) return;
	ui_font_draw_text_centered(font, SCREEN_WIDTH / 2, y, SCREEN_WIDTH - 96,
	                           color, size, text);
}

static void draw_spinner_sized(float center_x, float center_y, uint64_t now_us,
	                          float radius, float active_radius,
	                          float idle_radius) {
	const int dots = 10;
	const float tau = 6.28318530718f;
	int reduced_motion = vt_preferences_reduce_motion();
	int active = reduced_motion ? 0 :
	             (int)((now_us / 90000ULL) % (uint64_t)dots);

	for (int i = 0; i < dots; i++) {
		float angle = tau * (float)i / (float)dots - 1.57079632679f;
		float x = center_x + cosf(angle) * radius;
		float y = center_y + sinf(angle) * radius;
		int age = (i - active + dots) % dots;
		unsigned int color;
		float dot_radius;
		if (age == 0) {
			color = COLOR_BUFFER;
			dot_radius = active_radius;
		} else {
			color = age <= 2 ? COLOR_ACCENT
			      : (age <= 5 ? VT_THEME_BLUE : VT_THEME_BORDER);
			dot_radius = idle_radius;
		}
		if (reduced_motion && age != 0) color = VT_THEME_BORDER;
		vita2d_draw_fill_circle(x, y, dot_radius, color);
	}

	/* The center is deliberately left empty. vita2d_draw_array() does not
	 * copy the vertices into the GPU pool: passing it a local array causes
	 * a GXM fault. */
}

static void draw_loading_frame(const char *query, const char *message,
	                           const volatile long *progress_current,
	                           const volatile long *progress_total,
	                           int cancellable) {
	uint64_t now = sceKernelGetProcessTimeWide();
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_brand_set_loading(1);
	ui_brand_draw_header(query);

	/* Loading is part of the page, not an overlaid dialog. */
	ui_draw_spinner(480.0f, 246.0f, now);
	draw_centered_text(330, COLOR_TEXT, UI_FONT_DISPLAY,
	                   message ? message : vt_i18n_str(VT_STR_LOADING_DEFAULT_MESSAGE));

	long current = progress_current ? *progress_current : 0;
	long total = progress_total ? *progress_total : 0;
	if (total > 0) {
		float frac = (float)current / (float)total;
		if (frac < 0.0f) frac = 0.0f;
		if (frac > 1.0f) frac = 1.0f;
		float bar_x = 180.0f, bar_y = 390.0f, bar_w = 600.0f, bar_h = 6.0f;
		vita2d_draw_rectangle(bar_x, bar_y, bar_w, bar_h, COLOR_TRACK);
		vita2d_draw_rectangle(bar_x, bar_y, bar_w * frac, bar_h, COLOR_ACCENT);
		if (ui_runtime_font(UI_FONT_BODY)) {
			char percent[32];
			snprintf(percent, sizeof(percent), vt_i18n_str(VT_STR_LOADING_PROGRESS_PERCENT),
			         (int)(frac * 100.0f));
			draw_centered_text(430, COLOR_MUTED, UI_FONT_BODY, percent);
		}
	}
	if (cancellable) {
		draw_centered_text(total > 0 ? 470 : 402, COLOR_MUTED, UI_FONT_SMALL,
		                   vt_i18n_str(VT_STR_LOADING_CANCEL_HINT));
	}

	vita2d_end_drawing();
	vita2d_wait_rendering_done();
	vita2d_swap_buffers();
}

static void draw_clipped_line(vita2d_font *font, int x, int y, int max_width,
	                          unsigned int size, unsigned int color,
	                          const char *text) {
	if (!font || !text || !text[0]) return;
	char clipped[256];
	ui_font_fit_text(font, size, text, clipped, sizeof(clipped), max_width);
	ui_font_draw_text(font, x, y, color, size, clipped);
}

void ui_player_loading_draw(const UiPlayerLoadingInfo *info,
	                        const volatile long *progress_current,
	                        const volatile long *progress_total,
	                        uint64_t now_us) {
	const char *title = info && info->title && info->title[0] ? info->title
	                   : vt_i18n_str(VT_STR_LOADING_DEFAULT_TITLE);
	const char *channel = info && info->channel ? info->channel : "";
	const char *status = info && info->status && info->status[0]
	                       ? info->status : vt_i18n_str(VT_STR_LOADING_DEFAULT_STATUS);
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);

	/* Top band integrated into the player: three layers give a slight
	 * gradient toward the canvas without extra textures or shaders. */
	vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, 74, RGBA8(0, 0, 0, 220));
	vita2d_draw_rectangle(0, 74, SCREEN_WIDTH, 24, RGBA8(0, 0, 0, 132));
	vita2d_draw_rectangle(0, 98, SCREEN_WIDTH, 18, RGBA8(0, 0, 0, 62));
	vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, 3, COLOR_ACCENT);
	draw_clipped_line(body, 32, 45, 760, UI_FONT_BODY, COLOR_TEXT, title);
	draw_clipped_line(small, 32, 72, 760, UI_FONT_SMALL, COLOR_MUTED, channel);

	ui_draw_spinner(480.0f, 252.0f, now_us);
	if (small) {
		ui_font_draw_text_centered(small, SCREEN_WIDTH / 2, 324,
		                           SCREEN_WIDTH - 96, COLOR_TEXT,
		                           UI_FONT_SMALL, status);
		if (info && info->detail_text) {
			char detail[160];
			detail[0] = '\0';
			info->detail_text(detail, sizeof(detail), info->detail_ctx, now_us);
			if (detail[0]) {
				ui_font_draw_text_centered(small, SCREEN_WIDTH / 2, 358,
				                           SCREEN_WIDTH - 96, COLOR_BUFFER,
				                           UI_FONT_SMALL, detail);
			}
		}
		if (info && info->cancellable)
			ui_font_draw_text_centered(
				small, SCREEN_WIDTH / 2, 426, SCREEN_WIDTH - 96, COLOR_MUTED,
				UI_FONT_SMALL, vt_i18n_str(VT_STR_LOADING_PLAYER_CANCEL_HINT));
	}

	long current = progress_current ? *progress_current : 0;
	long total = progress_total ? *progress_total : 0;
	float frac = 0.0f;
	if (total > 0) {
		frac = (float)current / (float)total;
		if (frac < 0.0f) frac = 0.0f;
		if (frac > 1.0f) frac = 1.0f;
	}
	vita2d_draw_rectangle(40, 508, SCREEN_WIDTH - 80, 7, COLOR_TRACK);
	if (total > 0) {
		vita2d_draw_rectangle(40, 508, (SCREEN_WIDTH - 80) * frac, 7, COLOR_BUFFER);
	}

	if (small) {
		char quality[24];
		if (info && info->quality_height > 0) {
			snprintf(quality, sizeof(quality), vt_i18n_str(VT_STR_LOADING_QUALITY_LABEL),
			         info->quality_height);
		} else {
			strncpy(quality, vt_i18n_str(VT_STR_LOADING_QUALITY_AUTO), sizeof(quality));
			quality[sizeof(quality) - 1] = '\0';
		}
		int quality_w = ui_font_text_width(small, UI_FONT_SMALL, quality);
		vita2d_draw_rectangle(SCREEN_WIDTH - quality_w - 54, 462,
		                      quality_w + 30, 32, RGBA8(8, 18, 32, 224));
		vita2d_draw_rectangle(SCREEN_WIDTH - quality_w - 54, 462, 3, 32,
		                      COLOR_ACCENT);
		ui_font_draw_text(small, SCREEN_WIDTH - quality_w - 38, 484,
		                       COLOR_TEXT, UI_FONT_SMALL, quality);
		if (total > 0) {
			ui_font_draw_textf(small, 40, 492, COLOR_BUFFER, UI_FONT_SMALL,
			                       vt_i18n_str(VT_STR_LOADING_DOWNLOAD_PERCENT),
			                       (int)(frac * 100.0f));
		}
	}
}

void ui_draw_spinner(float center_x, float center_y, uint64_t now_us) {
	draw_spinner_sized(center_x, center_y, now_us, 30.0f, 5.5f, 3.5f);
}

void ui_draw_spinner_compact(float center_x, float center_y, uint64_t now_us) {
	draw_spinner_sized(center_x, center_y, now_us, 10.0f, 2.8f, 1.8f);
}

void ui_player_loading_present(const UiPlayerLoadingInfo *info,
	                           const volatile long *progress_current,
	                           const volatile long *progress_total) {
	if (!ui_runtime_is_ready()) return;
	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_player_loading_draw(info, progress_current, progress_total,
	                       sceKernelGetProcessTimeWide());
	vita2d_end_drawing();
	vita2d_wait_rendering_done();
	vita2d_swap_buffers();
}

int ui_player_loading_run(const UiPlayerLoadingInfo *info,
	                      UiLoadingTaskFn task, void *ctx,
	                      volatile int *cancel_flag,
	                      const volatile long *progress_current,
	                      const volatile long *progress_total) {
	if (!task || !ui_runtime_is_ready()) return -1;
	UiPlayerLoadingInfo draw_info;
	memset(&draw_info, 0, sizeof(draw_info));
	if (info) draw_info = *info;
	draw_info.cancellable = cancel_flag != NULL;
	UiTaskState state;
	memset(&state, 0, sizeof(state));
	state.task = task;
	state.ctx = ctx;
	state.result = -1;
	UiTaskThreadArgs thread_args = { &state };
	pthread_attr_t attr;
	int attr_ret = pthread_attr_init(&attr);
	int attr_ready = attr_ret == 0;
	if (attr_ret == 0)
		attr_ret = pthread_attr_setstacksize(&attr, TASK_THREAD_STACK);
	pthread_t thread;
	int create_ret = attr_ret == 0
	               ? pthread_create(&thread, &attr, loading_task_pthread,
	                                &thread_args)
	               : attr_ret;
	if (attr_ready) pthread_attr_destroy(&attr);
	if (create_ret != 0) {
		/* No separate thread: the task runs on the caller and no input is
		 * read anymore until it returns on its own, `cancel_flag` included.
		 * If this ever shows up in the log, it is the direct cause of a
		 * cancel button that seems unresponsive during this task. */
		log_printf("ui_player_loading_run: pthread_create failed (%d), synchronous task without cancellation",
		          create_ret);
		ui_player_loading_present(&draw_info, progress_current, progress_total);
		return task(ctx);
	}
	SceCtrlData ctrl, previous;
	memset(&ctrl, 0, sizeof(ctrl));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	while (!state.done) {
		ui_player_loading_present(&draw_info, progress_current, progress_total);
		if (draw_info.progress_tick && progress_current && progress_total) {
			draw_info.progress_tick(*progress_current, *progress_total);
		}
		if (cancel_flag) {
			sceCtrlPeekBufferPositive(0, &ctrl, 1);
			unsigned int pressed = ctrl.buttons & ~previous.buttons;
			previous = ctrl;
			UiTouchEvent touch;
			unsigned int touch_flags = ui_touch_poll(&touch);
			if ((pressed & SCE_CTRL_CIRCLE) ||
			    touch_is_cancel_hold(touch_flags, &touch)) {
				*cancel_flag = 1;
			}
		}
		sceKernelDelayThread(16 * 1000);
	}
	int join_ret = pthread_join(thread, NULL);
	if (join_ret != 0)
		log_printf("ui_player_loading_run: pthread_join -> %d", join_ret);
	ui_brand_set_loading(0);
	return state.result;
}

void ui_loading_present(const char *message) {
	if (!ui_runtime_is_ready()) return;
	draw_loading_frame(NULL, message, NULL, NULL, 0);
}

static int loading_run_internal(const char *query, const char *message,
	                            UiLoadingTaskFn task, void *ctx,
	                            volatile int *cancel_flag,
	                            const volatile long *progress_current,
	                            const volatile long *progress_total) {
	if (!task) return -1;

	if (!ui_runtime_is_ready()) return -1;
	int ret;

	UiTaskState state;
	memset(&state, 0, sizeof(state));
	state.task = task;
	state.ctx = ctx;
	state.result = -1;
	UiTaskThreadArgs thread_args = { &state };

	SceUID thid = sceKernelCreateThread("VitaMediaDeckUiTask", loading_task_thread,
	                                    TASK_THREAD_PRIORITY, TASK_THREAD_STACK,
	                                    0, 0, NULL);
	if (thid < 0) {
		draw_loading_frame(query, message, progress_current, progress_total,
		                   cancel_flag != NULL);
		return task(ctx);
	}
	ret = sceKernelStartThread(thid, sizeof(thread_args), &thread_args);
	if (ret < 0) {
		sceKernelDeleteThread(thid);
		draw_loading_frame(query, message, progress_current, progress_total,
		                   cancel_flag != NULL);
		return task(ctx);
	}

	SceCtrlData ctrl, previous;
	memset(&ctrl, 0, sizeof(ctrl));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	while (!state.done) {
		draw_loading_frame(query, message, progress_current, progress_total,
		                   cancel_flag != NULL);

		if (cancel_flag) {
			sceCtrlPeekBufferPositive(0, &ctrl, 1);
			unsigned int pressed = ctrl.buttons & ~previous.buttons;
			previous = ctrl;
			if (pressed & SCE_CTRL_CIRCLE) {
				*cancel_flag = 1;
			}
			UiTouchEvent touch;
			unsigned int touch_flags = ui_touch_poll(&touch);
			if (touch_is_cancel_hold(touch_flags, &touch)) {
				*cancel_flag = 1;
			}
		}
		sceKernelDelayThread(16 * 1000);
	}

	sceKernelWaitThreadEnd(thid, NULL, NULL);
	sceKernelDeleteThread(thid);
	ret = state.result;

	ui_brand_set_loading(0);
	return ret;
}

int ui_loading_run(const char *message,
	               UiLoadingTaskFn task,
	               void *ctx,
	               volatile int *cancel_flag,
	               const volatile long *progress_current,
	               const volatile long *progress_total) {
	return loading_run_internal(NULL, message, task, ctx, cancel_flag,
	                            progress_current, progress_total);
}

int ui_loading_run_with_query(const char *query,
	                          const char *message,
	                          UiLoadingTaskFn task,
	                          void *ctx,
	                          volatile int *cancel_flag,
	                          const volatile long *progress_current,
	                          const volatile long *progress_total) {
	return loading_run_internal(query, message, task, ctx, cancel_flag,
	                            progress_current, progress_total);
}

void ui_message_show(const char *message, const char *detail, int duration_ms) {
	if (duration_ms < 0) duration_ms = 0;
	if (!ui_runtime_is_ready()) return;

	vita2d_start_drawing();
	vita2d_clear_screen();
	ui_brand_set_loading(0);
	ui_brand_draw_header(NULL);
	/* Errors stay a full page: no modal box breaking
	 * the app's visual continuity. */
	vita2d_draw_fill_circle(480.0f, 226.0f, 25.0f, VT_THEME_DANGER);
	vita2d_draw_rectangle(478.0f, 211.0f, 4.0f, 20.0f, COLOR_TEXT);
	vita2d_draw_fill_circle(480.0f, 237.0f, 2.5f, COLOR_TEXT);
	draw_centered_text(312, COLOR_TEXT, UI_FONT_DISPLAY,
	                   message ? message : vt_i18n_str(VT_STR_LOADING_DEFAULT_ERROR_MESSAGE));
	if (detail && detail[0]) {
		draw_centered_text(354, COLOR_MUTED, UI_FONT_SMALL, detail);
	}
	vita2d_end_drawing();
	vita2d_wait_rendering_done();
	vita2d_swap_buffers();
	SceCtrlData ctrl, previous;
	memset(&ctrl, 0, sizeof(ctrl));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	int elapsed = 0;
	while (elapsed < duration_ms) {
		sceCtrlPeekBufferPositive(0, &ctrl, 1);
		unsigned int pressed = ctrl.buttons & ~previous.buttons;
		previous = ctrl;
		UiTouchEvent touch;
		if (pressed || (ui_touch_poll(&touch) & UI_TOUCH_EVENT_TAP)) break;
		sceKernelDelayThread(16 * 1000);
		elapsed += 16;
	}

}
