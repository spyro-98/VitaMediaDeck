#ifdef VMD_ENABLE_PAF_NATIVE
#include "ui/native_vita.h"
#endif
#include "ui/loading_screen.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>

#include "app_paths.h"
#include "common/text_log.h"
#include "i18n/i18n.h"
#include "network/download_manager.h"
#include "settings/preferences.h"
#include "system/display_awake.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/format.h"
#include "ui/mini_player.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/theme.h"
#include "ui/theme_home.h"
#include "ui/touch.h"

#define SCREEN_WIDTH  960
#define SCREEN_HEIGHT 544

#define COLOR_BG       VT_THEME_BG
#define COLOR_TEXT     VT_THEME_TEXT
#define COLOR_MUTED    VT_THEME_TEXT_MUTED
#define COLOR_TRACK    VT_THEME_BORDER
#define COLOR_ACCENT   VT_THEME_SIGNAL_BRIGHT
#define COLOR_BUFFER   VT_THEME_SPECTRAL_LIGHT

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

typedef struct {
	uint64_t sample_time_us;
	uint64_t sample_bytes;
	double bytes_per_second;
	int was_paused;
} UiDownloadRateState;

static void *loading_task_pthread(void *argp) {
	UiTaskThreadArgs *thread_args = (UiTaskThreadArgs *)argp;
	UiTaskState *state = thread_args->state;
	state->result = state->task(state->ctx);
	__atomic_store_n(&state->done, 1, __ATOMIC_RELEASE);
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

static void format_eta(uint64_t seconds, char *out, size_t out_size) {
	if (!out || !out_size) return;
	if (seconds < 60ULL)
		snprintf(out, out_size, "%llus", (unsigned long long)seconds);
	else if (seconds < 3600ULL)
		snprintf(out, out_size, "%llum %02llus",
		         (unsigned long long)(seconds / 60ULL),
		         (unsigned long long)(seconds % 60ULL));
	else
		snprintf(out, out_size, "%lluh %02llum",
		         (unsigned long long)(seconds / 3600ULL),
		         (unsigned long long)((seconds % 3600ULL) / 60ULL));
}

static double update_download_rate(UiDownloadRateState *state, uint64_t current,
	                               uint64_t now_us, int paused) {
	if (!state) return 0.0;
	if (!state->sample_time_us || current < state->sample_bytes || paused ||
	    state->was_paused) {
		state->sample_time_us = now_us;
		state->sample_bytes = current;
		state->was_paused = paused;
		if (paused) state->bytes_per_second = 0.0;
		return state->bytes_per_second;
	}
	state->was_paused = 0;
	uint64_t elapsed_us = now_us - state->sample_time_us;
	if (elapsed_us >= 500000ULL) {
		double instant = (double)(current - state->sample_bytes) * 1000000.0 /
		                 (double)elapsed_us;
		state->bytes_per_second = state->bytes_per_second > 0.0
		                        ? state->bytes_per_second * 0.70 + instant * 0.30
		                        : instant;
		state->sample_time_us = now_us;
		state->sample_bytes = current;
	}
	return state->bytes_per_second;
}

static void draw_spinner_sized(float center_x, float center_y, uint64_t now_us,
	                          float radius, float active_radius,
	                          float idle_radius) {
/* Keep activity in the submitted frame while native resources/decoder open. */

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
			      : (age <= 5 ? VT_THEME_COLD : VT_THEME_BORDER);
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
	                           int cancellable, int download_controls,
	                           int paused, UiDownloadRateState *rate_state,
	                           UiDownloadProgressReadFn download_progress_read,
	                           void *download_progress_ctx,
	                           const char *download_title,
	                           int background_action,
	                           const UiSectionsSidebar *sidebar) {
	uint64_t now = sceKernelGetProcessTimeWide();
	vita2d_start_drawing();
	vita2d_clear_screen();
	/* A foreground download has its own central spinner and progress bar. Keep
	 * the top bar available for unrelated app loading, and suppress the compact
	 * background-download ring only while this page is visible. */
	ui_brand_set_loading(download_controls ? 0 : 1);
	ui_brand_set_download_progress_hidden(download_controls);
	ui_chrome_background(COLOR_BG, COLOR_ACCENT);
	if (download_controls)
		ui_brand_draw_breadcrumbs(vt_i18n_str(VT_STR_SECTIONS_DOWNLOAD), NULL, download_title);
	else ui_brand_draw_header(query);
	ui_brand_set_download_progress_hidden(0);

	/* Status, title and measured progress have independent, uncluttered rails. */
	if (download_controls && paused) {
		vita2d_draw_rectangle(465,226,10,36,VT_THEME_WARNING);
		vita2d_draw_rectangle(485,226,10,36,VT_THEME_WARNING);
	} else ui_draw_spinner(480.0f, 246.0f, now);
	draw_centered_text(download_controls ? 306 : 330, COLOR_TEXT, UI_FONT_DISPLAY,
	                   download_controls && paused
	                       ? vt_i18n_str(VT_STR_NETWORK_DOWNLOAD_PAUSED)
	                       : message ? message
	                                 : vt_i18n_str(VT_STR_LOADING_DEFAULT_MESSAGE));
	if (download_controls && download_title && download_title[0]) {
		vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
		if (small)
			ui_text_line(42, 145, 876, download_title, COLOR_TEXT, UI_FONT_BODY);
	}

	uint64_t current = 0, total = 0;
	if (download_controls && download_progress_read) {
		download_progress_read(download_progress_ctx, &current, &total);
	} else {
		long current_value = progress_current ? *progress_current : 0;
		long total_value = progress_total ? *progress_total : 0;
		current = current_value > 0 ? (uint64_t)current_value : 0ULL;
		total = total_value > 0 ? (uint64_t)total_value : 0ULL;
	}
	double bytes_per_second = download_controls
	                        ? update_download_rate(rate_state, current, now, paused)
	                        : 0.0;
	float bar_x = 42.0f, bar_y = 366.0f, bar_w = 876.0f, bar_h = 8.0f;
	if (download_controls)
		vita2d_draw_rectangle(bar_x, bar_y, bar_w, bar_h, COLOR_TRACK);
	if (total > 0) {
		float frac = (float)((double)current / (double)total);
		if (frac < 0.0f) frac = 0.0f;
		if (frac > 1.0f) frac = 1.0f;
		vita2d_draw_rectangle(bar_x, bar_y, bar_w * frac, bar_h, COLOR_ACCENT);
		if (ui_runtime_font(UI_FONT_BODY)) {
			char current_size[32], total_size[32], progress[96];
			ui_format_file_size((uint64_t)current, current_size, sizeof(current_size));
			ui_format_file_size((uint64_t)total, total_size, sizeof(total_size));
			snprintf(progress, sizeof(progress), "%d%%   %s / %s",
			         (int)(frac * 100.0f), current_size, total_size);
			draw_centered_text(418, COLOR_MUTED, UI_FONT_BODY, progress);
			char rate_size[32], eta[32], timing[96];
			if (bytes_per_second >= 1.0) {
				ui_format_file_size((uint64_t)bytes_per_second,
				                    rate_size, sizeof(rate_size));
			uint64_t remaining = current < total ? total - current : 0ULL;
				format_eta((uint64_t)((double)remaining / bytes_per_second + 0.5),
				           eta, sizeof(eta));
				snprintf(timing, sizeof(timing), "%s/s   ETA %s", rate_size, eta);
			} else {
				snprintf(timing, sizeof(timing), "--/s   ETA --");
			}
			if (!paused) draw_centered_text(442, COLOR_MUTED, UI_FONT_SMALL, timing);
		}
	} else if (download_controls) {
		/* Chunked responses and servers that reject HEAD still expose useful live
		 * byte progress. The moving segment explicitly means total size unknown. */
		float segment_w = 112.0f;
		float phase = (paused || vt_preferences_reduce_motion())
		            ? 0.5f : (float)(now % 1400000ULL) / 1400000.0f;
		float segment_x = bar_x - segment_w + phase * (bar_w + segment_w);
		float left = segment_x < bar_x ? bar_x : segment_x;
		float right = segment_x + segment_w > bar_x + bar_w
		            ? bar_x + bar_w : segment_x + segment_w;
		if (right > left)
			vita2d_draw_rectangle(left, bar_y, right - left, bar_h, COLOR_BUFFER);
		if (ui_runtime_font(UI_FONT_BODY)) {
			char current_size[32], progress[64];
			ui_format_file_size((uint64_t)current, current_size, sizeof(current_size));
			snprintf(progress, sizeof(progress),
			         vt_i18n_str(VT_STR_LOADING_DOWNLOAD_RECEIVED), current_size);
			draw_centered_text(418, COLOR_MUTED, UI_FONT_BODY, progress);
			char rate_size[32], timing[64];
			if (bytes_per_second >= 1.0) {
				ui_format_file_size((uint64_t)bytes_per_second,
				                    rate_size, sizeof(rate_size));
				snprintf(timing, sizeof(timing), "%s/s   ETA --", rate_size);
			} else {
				snprintf(timing, sizeof(timing), "--/s   ETA --");
			}
			if (!paused) draw_centered_text(442, COLOR_MUTED, UI_FONT_SMALL, timing);
		}
	}
	if (download_controls) {
		int pause_x = background_action ? 72 : 246;
		int button_w = background_action ? 240 : 216;
		ui_action_button(pause_x, 458, button_w, 42, VT_THEME_SURFACE_RAISED, "Start",
		                 vt_i18n_str(paused ? VT_STR_NETWORK_DOWNLOAD_RESUME
		                                  : VT_STR_NETWORK_DOWNLOAD_PAUSE), 0);
		if (background_action)
			ui_action_button(360, 458, 240, 42, VT_THEME_SURFACE, "Triangle",
			                 vt_i18n_str(VT_STR_NETWORK_DOWNLOAD_BACKGROUND), 0);
		ui_action_button(background_action ? 648 : 498, 458, button_w, 42,
		                 VT_THEME_SURFACE, "Circle",
		                 vt_i18n_str(VT_STR_NETWORK_DOWNLOAD_ABORT), 0);
	} else if (cancellable) {
		ui_action_button(338,458,284,42,VT_THEME_SURFACE,"Circle",vt_i18n_str(VT_STR_NETWORK_CANCEL),0);
	}
	if (sidebar && sidebar->animation > 0.01f)
		ui_sections_sidebar_draw(sidebar);

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
	const unsigned *theme_before=ui_theme_media_begin();
	const char *title = info && info->title && info->title[0] ? info->title
	                   : vt_i18n_str(VT_STR_LOADING_DEFAULT_TITLE);
	const char *channel = info && info->channel ? info->channel : "";
	const char *status = info && info->status && info->status[0]
	                       ? info->status : vt_i18n_str(VT_STR_LOADING_DEFAULT_STATUS);
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);

	if (info && info->draw_backdrop) {
		info->draw_backdrop(info->backdrop_ctx);
		/* Keep the last frame visible; acquire the new position with a compact
		 * scrim instead of replacing the canvas with a black loading page. */
		vita2d_draw_rectangle(330, 204, 300, 152, RGBA8(0, 0, 0, 170));
		ui_draw_spinner(480.0f, 252.0f, now_us);
		if (small) ui_font_draw_text_centered(small,480,324,268,COLOR_TEXT,
		    UI_FONT_SMALL,status);
		if (info->cancellable)
			ui_action_button(338,392,284,50,VT_THEME_SURFACE,"Circle",vt_i18n_str(VT_STR_NETWORK_CANCEL),0);
		ui_theme_media_end(theme_before);
		return;
	}

	vita2d_draw_rectangle(0,0,960,544,VT_THEME_MEDIA_BACKDROP);
	if (!vt_ui_theme_legacy_layout(ui_theme_id()))
		ui_theme_fx_background(VT_THEME_MEDIA_BACKDROP);
	/* Top band integrated into the player: three layers give a slight
	 * gradient toward the canvas without extra textures or shaders. */
	vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, 74, RGBA8(0, 0, 0, 220));
	vita2d_draw_rectangle(0, 74, SCREEN_WIDTH, 24, RGBA8(0, 0, 0, 132));
	vita2d_draw_rectangle(0, 98, SCREEN_WIDTH, 18, RGBA8(0, 0, 0, 62));
	vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, 3, COLOR_ACCENT);
	draw_clipped_line(body, 32, 45, 680, UI_FONT_BODY, COLOR_TEXT, title);
	draw_clipped_line(small, 32, 72, 680, UI_FONT_SMALL, COLOR_MUTED, channel);
	ui_brand_draw_status_indicators();

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
		if (info && info->start_over_requested) {
			vita2d_draw_rectangle(338, 382, 284, 42, VT_THEME_SURFACE_FOCUS);
			vita2d_draw_rectangle(338, 382, 3, 42, VT_THEME_SIGNAL_BRIGHT);
			ui_font_draw_text_centered(
			    small, SCREEN_WIDTH / 2, 409, 252, COLOR_TEXT, UI_FONT_SMALL,
			    vt_i18n_str(VT_STR_LOADING_PLAYER_START_OVER));
		}
		if(info && info->cancellable)
			ui_action_button(338,info->start_over_requested?446:392,284,50,
			    VT_THEME_SURFACE,"Circle",vt_i18n_str(VT_STR_NETWORK_CANCEL),0);
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
		                      quality_w + 30, 32, VT_THEME_SURFACE_RAISED);
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
 ui_theme_media_end(theme_before);
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
		/* Never turn an advertised cancellable operation into a synchronous UI
		 * stall. The caller can report/retry the allocation failure safely. */
		log_printf("ui_player_loading_run: pthread_create failed (%d), task not started\n",
		          create_ret);
		ui_brand_set_loading(0);
		return create_ret > 0 ? -create_ret : -1;
	}
	SceCtrlData ctrl, previous;
	memset(&ctrl, 0, sizeof(ctrl));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	int cancellation_requested=0;
	while (!__atomic_load_n(&state.done, __ATOMIC_ACQUIRE)) {
		ui_player_loading_present(&draw_info, progress_current, progress_total);
		if (draw_info.progress_tick && progress_current && progress_total) {
			draw_info.progress_tick(*progress_current, *progress_total);
		}
		if (cancel_flag && !cancellation_requested) {
			sceCtrlPeekBufferPositive(0, &ctrl, 1);
			unsigned int pressed = ctrl.buttons & ~previous.buttons;
			previous = ctrl;
			UiTouchEvent touch;
			unsigned int touch_flags = ui_touch_poll(&touch);
			int start_over_touch = draw_info.start_over_requested &&
			    (touch_flags & UI_TOUCH_EVENT_TAP) &&
			    ui_touch_hit_rect(touch.x, touch.y, 338, 382, 284, 42);
			int cancel_requested = 0;
			if (draw_info.start_over_requested &&
			    ((pressed & SCE_CTRL_CROSS) || start_over_touch)) {
				*draw_info.start_over_requested = 1;
				cancel_requested = 1;
			}
			int cancel_button_tap=(touch_flags&UI_TOUCH_EVENT_TAP) &&
			    ui_touch_hit_rect(touch.x,touch.y,338,draw_info.start_over_requested?446:392,284,50);
			if ((ctrl.buttons & SCE_CTRL_CIRCLE) || cancel_button_tap ||
			    touch_is_cancel_hold(touch_flags, &touch)) {
				cancel_requested = 1;
			}
			if (cancel_requested) {
				cancellation_requested=1;
				draw_info.status=vt_i18n_str(VT_STR_LOADING_CANCELLING);
				draw_info.cancellable=0;draw_info.start_over_requested=NULL;
				draw_info.detail_text=NULL;
				/* Interrupt the decoder-owned transport phase before publishing the
				 * caller flag. Otherwise the worker can enter rollback from that flag
				 * and a delayed callback can accidentally cancel the fresh restore. */
				if (draw_info.cancel_action)
					draw_info.cancel_action(draw_info.cancel_ctx);
				__atomic_store_n(cancel_flag,1,__ATOMIC_RELEASE);
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
	draw_loading_frame(NULL, message, NULL, NULL, 0, 0, 0, NULL, NULL, NULL,
	                   NULL, 0, NULL);
}

static int loading_run_internal(const char *query, const char *message,
	                            UiLoadingTaskFn task, void *ctx,
	                            volatile int *cancel_flag,
	                            const volatile long *progress_current,
	                            const volatile long *progress_total,
	                            volatile int *pause_flag,
	                            int download_controls,
	                            UiDownloadProgressReadFn download_progress_read,
	                            void *download_progress_ctx) {
	if (!task) return -1;

	if (!ui_runtime_is_ready()) return -1;
	int ret;

	UiTaskState state;
	memset(&state, 0, sizeof(state));
	state.task = task;
	state.ctx = ctx;
	state.result = -1;
	UiTaskThreadArgs thread_args = { &state };
	UiDownloadRateState download_rate;
	memset(&download_rate, 0, sizeof(download_rate));

	/* Network/XML callbacks may use POSIX TLS and cancellation. A raw kernel
	 * worker does not have pthread-embedded's Vita OSAL context. Match the
	 * player loader and never fall back to blocking network work on the UI. */
	pthread_attr_t attr;
	int create_ret=pthread_attr_init(&attr);
	int attr_ready=create_ret==0;
	if(!create_ret)create_ret=pthread_attr_setstacksize(&attr,TASK_THREAD_STACK);
	pthread_t thread;
	if(!create_ret)create_ret=pthread_create(&thread,&attr,loading_task_pthread,&thread_args);
	if(attr_ready)pthread_attr_destroy(&attr);
	if(create_ret) {
		log_printf("ui_loading_run: pthread setup failed (%d); task not started\n",create_ret);
		ui_brand_set_loading(0);
		return UI_LOADING_TASK_FAILED;
	}

	SceCtrlData ctrl, previous;
	memset(&ctrl, 0, sizeof(ctrl));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	int cancellation_requested=0;
	while (!__atomic_load_n(&state.done, __ATOMIC_ACQUIRE)) {
		if (download_controls) vt_display_keep_awake_tick();
		draw_loading_frame(query, cancellation_requested?vt_i18n_str(VT_STR_LOADING_CANCELLING):message, progress_current, progress_total,
		                   cancel_flag != NULL && !cancellation_requested, download_controls,
		                   pause_flag && *pause_flag,
		                   download_controls ? &download_rate : NULL,
		                   download_progress_read, download_progress_ctx, NULL, 0,
		                   NULL);

		if (cancel_flag && !cancellation_requested) {
			sceCtrlPeekBufferPositive(0, &ctrl, 1);
			unsigned int pressed = ctrl.buttons & ~previous.buttons;
			previous = ctrl;
			if (download_controls && pause_flag && (pressed & SCE_CTRL_START))
				*pause_flag = !*pause_flag;
			if (ctrl.buttons & SCE_CTRL_CIRCLE) {
				cancellation_requested=1;__atomic_store_n(cancel_flag,1,__ATOMIC_RELEASE);
			}
			UiTouchEvent touch;
			unsigned int touch_flags = ui_touch_poll(&touch);
			if (download_controls && pause_flag &&
			    (touch_flags & UI_TOUCH_EVENT_TAP) &&
			    ui_touch_hit_rect(touch.x, touch.y, 246, 458, 216, 42))
				*pause_flag = !*pause_flag;
			if ((download_controls && (touch_flags & UI_TOUCH_EVENT_TAP) &&
			     ui_touch_hit_rect(touch.x, touch.y, 498, 458, 216, 42)) ||
			    (!download_controls && (touch_flags&UI_TOUCH_EVENT_TAP) &&
			     ui_touch_hit_rect(touch.x,touch.y,338,458,284,42)) ||
			    touch_is_cancel_hold(touch_flags, &touch)) {
				cancellation_requested=1;__atomic_store_n(cancel_flag,1,__ATOMIC_RELEASE);
			}
		}
		sceKernelDelayThread(16 * 1000);
	}

	pthread_join(thread, NULL);
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
	                            progress_current, progress_total, NULL, 0,
	                            NULL, NULL);
}

int ui_loading_run_with_query(const char *query,
	                          const char *message,
	                          UiLoadingTaskFn task,
	                          void *ctx,
	                          volatile int *cancel_flag,
	                          const volatile long *progress_current,
	                          const volatile long *progress_total) {
	return loading_run_internal(query, message, task, ctx, cancel_flag,
	                            progress_current, progress_total, NULL, 0,
	                            NULL, NULL);
}

int ui_loading_run_download(const char *message,
	                        UiLoadingTaskFn task, void *ctx,
	                        volatile int *pause_flag,
	                        volatile int *cancel_flag,
	                        UiDownloadProgressReadFn progress_read,
	                        void *progress_ctx) {
	return loading_run_internal(NULL, message, task, ctx, cancel_flag,
	                            NULL, NULL, pause_flag, 1,
	                            progress_read, progress_ctx);
}

static void background_status_progress(void *opaque, uint64_t *current,
	                                   uint64_t *total) {
	const VtDownloadBackgroundStatus *status = opaque;
	if (current) *current = status ? status->current : 0;
	if (total) *total = status ? status->total : 0;
}

int ui_loading_monitor_background_download(void) {
	if (!ui_runtime_is_ready()) return -1;
	UiDownloadRateState rate;
	memset(&rate, 0, sizeof(rate));
	SceCtrlData ctrl, previous;
	memset(&ctrl, 0, sizeof(ctrl));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	UiSectionsSidebar sidebar;
	ui_sections_sidebar_init(&sidebar, UI_SECTION_NETWORK);

	for (;;) {
		VtDownloadBackgroundStatus status;
		vt_download_background_status(&status);
		if (status.state == VT_DOWNLOAD_BACKGROUND_IDLE) {
			ui_brand_set_loading(0);
			return -1;
		}
		if (status.state != VT_DOWNLOAD_BACKGROUND_RUNNING) {
			log_save(VITAMEDIADECK_SESSION_LOG_PATH);
			ui_brand_set_loading(0);
			if (status.state == VT_DOWNLOAD_BACKGROUND_COMPLETE)
				ui_message_show_success(
				    vt_i18n_str(VT_STR_NETWORK_DOWNLOAD_COMPLETE),
				    status.detail[0] ? status.detail : status.destination, 2600);
			else if (status.state == VT_DOWNLOAD_BACKGROUND_ABORTED)
				ui_message_show(vt_i18n_str(VT_STR_NETWORK_DOWNLOAD_ABORTED),
				                status.destination, 2400);
			else
				ui_message_show(vt_i18n_str(VT_STR_NETWORK_DOWNLOAD_FAILED),
				                status.detail[0] ? status.detail :
				                vt_i18n_str(VT_STR_NETWORK_READ_FAILED), 3000);
			vt_download_background_acknowledge();
			return 0;
		}

		vt_display_keep_awake_tick();
		draw_loading_frame(NULL, vt_i18n_str(VT_STR_NETWORK_DOWNLOADING),
		                   NULL, NULL, 1, 1, status.paused, &rate,
		                   background_status_progress, &status,
		                   status.title, 1, &sidebar);

		sceCtrlPeekBufferPositive(0, &ctrl, 1);
		unsigned int pressed = ctrl.buttons & ~previous.buttons;
		previous = ctrl;
		UiTouchEvent touch;
		unsigned int touch_flags = ui_touch_poll(&touch);
		if (!ui_mini_player_input_locked() &&
		    ui_brand_sections_hit(touch_flags, touch.x, touch.y)) {
			pressed |= SCE_CTRL_LTRIGGER;
			touch_flags = UI_TOUCH_EVENT_NONE;
		}
		int sidebar_was_open = sidebar.open;
		int section = ui_sections_sidebar_handle_buttons(
		    &sidebar, &pressed, ctrl.buttons, ctrl.ly);
		if (sidebar.open) {
			int touched = ui_sections_sidebar_handle_touch(
			    &sidebar, touch_flags, touch.x, touch.y);
			if (touched != UI_SECTION_NONE) section = touched;
			touch_flags = UI_TOUCH_EVENT_NONE;
		}
		ui_sections_sidebar_tick(&sidebar);
		if (section != UI_SECTION_NONE) {
			ui_brand_set_loading(0);
			ui_touch_reset();
			return UI_LOADING_DOWNLOAD_SECTION_BASE + section;
		}
		int page_owns_input = !sidebar_was_open && !sidebar.open &&
		                      sidebar.animation <= 0.01f;
		if (!page_owns_input) {
			pressed = 0;
			touch_flags = UI_TOUCH_EVENT_NONE;
		}
		int tapped_pause = (touch_flags & UI_TOUCH_EVENT_TAP) &&
		                   ui_touch_hit_rect(touch.x, touch.y, 72, 458, 240, 42);
		int tapped_background = (touch_flags & UI_TOUCH_EVENT_TAP) &&
		                        ui_touch_hit_rect(touch.x, touch.y,
		                                          360, 458, 240, 42);
		int tapped_abort = (touch_flags & UI_TOUCH_EVENT_TAP) &&
		                   ui_touch_hit_rect(touch.x, touch.y, 648, 458, 240, 42);
		if ((pressed & SCE_CTRL_START) || tapped_pause)
			vt_download_background_toggle_pause();
		if ((pressed & SCE_CTRL_TRIANGLE) || tapped_background) {
			ui_brand_set_loading(0);
			ui_touch_reset();
			return 1;
		}
		if ((pressed & SCE_CTRL_CIRCLE) || tapped_abort ||
		    touch_is_cancel_hold(touch_flags, &touch))
			vt_download_background_abort();
		sceKernelDelayThread(16 * 1000);
	}
}

static void message_show_internal(const char *message, const char *detail,
	                              int duration_ms, int success) {
	if (duration_ms < 0) duration_ms = 0;
	if (!ui_runtime_is_ready()) return;

	ui_brand_set_loading(0);
	SceCtrlData ctrl, previous;
	memset(&ctrl, 0, sizeof(ctrl));
	sceCtrlPeekBufferPositive(0, &previous, 1);
	ui_touch_reset();
	for (int elapsed=0;;elapsed+=16) {
		vita2d_start_drawing();vita2d_disable_clipping();vita2d_clear_screen();
		unsigned int state_color = success ? VT_THEME_SUCCESS : VT_THEME_DANGER;
		ui_chrome_background(COLOR_BG, state_color);ui_brand_draw_header(NULL);
		vita2d_draw_rectangle(451.0f, 151.0f, 58.0f, 58.0f, state_color);
		unsigned int badge_ink = ui_contrast_bw(state_color);
		if (success) {
			vita2d_draw_line(465,180,476,192,badge_ink);
			vita2d_draw_line(476,192,498,167,badge_ink);
			vita2d_draw_line(465,181,476,193,badge_ink);
			vita2d_draw_line(476,193,498,168,badge_ink);
		} else {
			vita2d_draw_rectangle(478,165,4,20,badge_ink);
			vita2d_draw_rectangle(478,191,4,4,badge_ink);
		}
		draw_centered_text(270,COLOR_TEXT,UI_FONT_DISPLAY,
		    message?message:vt_i18n_str(VT_STR_LOADING_DEFAULT_ERROR_MESSAGE));
		if (detail && detail[0])
			ui_text_block(64,324,832,28,5,detail,COLOR_MUTED,UI_FONT_BODY);
		vita2d_end_drawing();vita2d_wait_rendering_done();vita2d_swap_buffers();
		if (elapsed>=duration_ms) break;
		sceCtrlPeekBufferPositive(0, &ctrl, 1);
		unsigned int pressed = ctrl.buttons & ~previous.buttons;
		previous = ctrl;
		UiTouchEvent touch;
		if (pressed || (ui_touch_poll(&touch) & UI_TOUCH_EVENT_TAP)) break;
		sceKernelDelayThread(16 * 1000);
	}
	ui_touch_reset();

}

void ui_message_show(const char *message, const char *detail, int duration_ms) {
	message_show_internal(message, detail, duration_ms, 0);
}

void ui_message_show_success(const char *message, const char *detail,
	                         int duration_ms) {
	message_show_internal(message, detail, duration_ms, 1);
}
