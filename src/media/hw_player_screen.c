#include "media/hw_player_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "i18n/i18n.h"
#include "media/audio_volume.h"
#include "settings/preferences.h"
#include "system/display_awake.h"
#include "system/performance.h"
#include "ui/brand.h"
#include "ui/loading_screen.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/theme.h"
#include "ui/touch.h"

#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544
#define TIMELINE_X 48
#define TIMELINE_Y 509
#define TIMELINE_W 864
#define STICK_CENTER 128
#define STICK_DEADZONE 42
#define RIGHT_PANEL_WIDTH 320.0f
#define RIGHT_PANEL_ROW_Y 82.0f
#define RIGHT_PANEL_ROW_STEP 68.0f
#define PLAYER_BACK_X 400.0f
#define PLAYER_PLAY_X 480.0f
#define PLAYER_FORWARD_X 560.0f
#define PLAYER_TRANSPORT_Y 458.0f
#define SEEK_REPEAT_DELAY_US 360000ULL
#define SEEK_REPEAT_INTERVAL_US 220000ULL

typedef struct {
	VtDecoderPlayer *player;
	VtDecoderPlayerConfig config;
} OpenTask;

static int open_task(void *opaque) {
	OpenTask *task = opaque;
	return vt_decoder_open(task->player, &task->config);
}

typedef struct {
	VtDecoderPlayer *player;
	uint64_t position_ms;
} SeekTask;

static int seek_task(void *opaque) {
	SeekTask *task = opaque;
	return vt_decoder_seek(task->player, task->position_ms);
}

static int fallback_task(void *opaque) {
	SeekTask *task = opaque;
	return vt_decoder_fallback_to_software(task->player, task->position_ms);
}

static int run_seek(VtDecoderPlayer *player, UiPlayerLoadingInfo *loading,
	                volatile int *cancel, uint64_t position_ms) {
	SeekTask task = { player, position_ms };
	if (cancel) *cancel = 0;
	return ui_player_loading_run(loading, seek_task, &task, cancel, NULL, NULL);
}

static uint64_t resync_after_blocking_action(SceCtrlData *controls,
	                                          SceCtrlData *previous,
	                                          int *stick_direction,
	                                          uint64_t *stick_repeat_at) {
	if (controls) sceCtrlPeekBufferPositive(0, controls, 1);
	if (controls && previous) *previous = *controls;
	if (stick_direction) *stick_direction = 0;
	if (stick_repeat_at) *stick_repeat_at = 0;
	ui_touch_reset();
	return sceKernelGetProcessTimeWide();
}

static unsigned fade_color(unsigned color, float opacity) {
	if (opacity < 0.0f) opacity = 0.0f;
	if (opacity > 1.0f) opacity = 1.0f;
	unsigned alpha = (unsigned)(((color >> 24) & 0xffU) * opacity);
	return (color & 0x00ffffffU) | (alpha << 24);
}

static float approach_for_frame(float current, float target,
	                            float response_per_second,
	                            float delta_seconds) {
	/* Exponential smoothing expressed in seconds keeps the same visual duration
	 * when decoding or I/O causes a frame to arrive late. */
	if (delta_seconds < 0.0f) delta_seconds = 0.0f;
	if (delta_seconds > 0.05f) delta_seconds = 0.05f;
	float alpha = 1.0f - expf(-response_per_second * delta_seconds);
	return current + (target - current) * alpha;
}

static void draw_text_clipped(vita2d_font *font, int x, int y, int width,
	                          unsigned size, unsigned color, const char *text) {
	if (!font || !text) return;
	char buffer[192];
	ui_font_fit_text(font, size, text, buffer, sizeof(buffer), width);
	ui_font_draw_text(font, x, y, color, size, buffer);
}

static void format_time(uint64_t milliseconds, char output[24]) {
	uint64_t seconds = milliseconds / 1000ULL;
	uint64_t hours = seconds / 3600ULL;
	seconds %= 3600ULL;
	if (hours) snprintf(output, 24, "%llu:%02llu:%02llu",
	                    (unsigned long long)hours,
	                    (unsigned long long)(seconds / 60ULL),
	                    (unsigned long long)(seconds % 60ULL));
	else snprintf(output, 24, "%llu:%02llu",
	              (unsigned long long)(seconds / 60ULL),
	              (unsigned long long)(seconds % 60ULL));
}

static void draw_play_icon(float x, float y, float height, unsigned color) {
	int rows = (int)height;
	for (int row = 0; row < rows; row++) {
		float half = row < rows / 2 ? (float)row + 1.0f
		                              : (float)(rows - row);
		vita2d_draw_rectangle(x, y + row, 3.0f + half * 1.3f, 1.25f,
		                      color);
	}
}

static void draw_skip_button(vita2d_font *font, float center_x,
	                         const char *label, float opacity) {
	unsigned fill = fade_color(RGBA8(8, 24, 40, 225), opacity);
	unsigned text = fade_color(VT_THEME_TEXT, opacity);
	vita2d_draw_fill_circle(center_x, PLAYER_TRANSPORT_Y, 22.0f, fill);
	vita2d_draw_fill_circle(center_x, PLAYER_TRANSPORT_Y, 21.0f,
	                        fade_color(VT_THEME_SURFACE_RAISED, opacity));
	if (font) {
		int width = ui_font_text_width(font, UI_FONT_SMALL, label);
		ui_font_draw_text(font, (int)center_x - width / 2,
		                  (int)PLAYER_TRANSPORT_Y + 7, text,
		                  UI_FONT_SMALL, label);
	}
}

static void draw_hud(const VtHwPlayerScreenSource *source,
	                 const VtDecoderPlayerStatus *status, int paused,
	                 int volume, float hud_opacity, float volume_opacity,
	                 int dragging, float drag_fraction) {
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (hud_opacity > 0.01f) {
		unsigned text = fade_color(VT_THEME_TEXT, hud_opacity);
		unsigned muted = fade_color(VT_THEME_TEXT_MUTED, hud_opacity);
		/* Lightweight stepped scrims protect type over both bright and dark video
		 * without turning the picture into a boxed-in page. */
		vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, 76,
		                      fade_color(RGBA8(0, 0, 0, 218), hud_opacity));
		vita2d_draw_rectangle(0, 76, SCREEN_WIDTH, 22,
		                      fade_color(RGBA8(0, 0, 0, 118), hud_opacity));
		vita2d_draw_rectangle(0, 98, SCREEN_WIDTH, 16,
		                      fade_color(RGBA8(0, 0, 0, 48), hud_opacity));
		draw_text_clipped(body, 30, 43, 720, UI_FONT_BODY, text,
		                  source->title ? source->title
		                                : vt_i18n_str(VT_STR_PLAYER_VIDEO));
		draw_text_clipped(small, 30, 69, 720, UI_FONT_SMALL,
		                  muted,
		                  source->location ? source->location : "");
		ui_brand_draw_status_indicators_alpha(hud_opacity);
		vita2d_draw_rectangle(0, 390, SCREEN_WIDTH, 30,
		                      fade_color(RGBA8(0, 0, 0, 46), hud_opacity));
		vita2d_draw_rectangle(0, 420, SCREEN_WIDTH, 124,
		                      fade_color(RGBA8(0, 0, 0, 190), hud_opacity));
		char decoder[16];
		snprintf(decoder, sizeof(decoder), "%s DEC",
		         status->hardware_accelerated ? "HW" : "SW");
		unsigned border = status->hardware_accelerated
		                ? RGBA8(84, 229, 132, 255) : RGBA8(70, 219, 255, 255);
		vita2d_draw_rectangle(48, 431, 92, 32,
		                      fade_color(RGBA8(0, 0, 0, 190), hud_opacity));
		vita2d_draw_rectangle(48, 431, 3, 32, fade_color(border, hud_opacity));
		if (small) ui_font_draw_text(small, 60, 454, text,
		                             UI_FONT_SMALL, decoder);
		char quality[48];
		snprintf(quality, sizeof(quality), "%up  %d fps",
		         status->height, status->fps);
		if (small) ui_font_draw_text(small, 154, 454, text,
		                             UI_FONT_SMALL, quality);
		draw_skip_button(small, PLAYER_BACK_X, "<", hud_opacity);
		draw_skip_button(small, PLAYER_FORWARD_X, ">", hud_opacity);
		vita2d_draw_fill_circle(PLAYER_PLAY_X, PLAYER_TRANSPORT_Y, 31,
		                        fade_color(VT_THEME_HALO_A(92), hud_opacity));
		vita2d_draw_fill_circle(PLAYER_PLAY_X, PLAYER_TRANSPORT_Y, 28,
		                        fade_color(VT_THEME_SURFACE_FOCUS, hud_opacity));
		if (paused) draw_play_icon(470, 442, 32, text);
		else {
			vita2d_draw_rectangle(470, 442, 8, 32, text);
			vita2d_draw_rectangle(485, 442, 8, 32, text);
		}
		if (paused && small) {
			const char *paused_text = vt_i18n_str(VT_STR_PLAYER_PAUSED);
			int width = ui_font_text_width(small, UI_FONT_SMALL, paused_text);
			vita2d_draw_rectangle(PLAYER_PLAY_X - width * .5f - 9, 391,
			                      width + 18, 25,
			                      fade_color(RGBA8(0, 0, 0, 206), hud_opacity));
			ui_font_draw_text(small, (int)PLAYER_PLAY_X - width / 2, 409,
			                  fade_color(VT_THEME_BLUE_LIGHT, hud_opacity),
			                  UI_FONT_SMALL, paused_text);
		}
		float progress = dragging ? drag_fraction
		               : status->duration_ms
		                   ? (float)status->position_ms / (float)status->duration_ms
		                   : 0.0f;
		if (progress < 0.0f) progress = 0.0f;
		if (progress > 1.0f) progress = 1.0f;
		vita2d_draw_rectangle(TIMELINE_X, TIMELINE_Y, TIMELINE_W, 6,
		                      fade_color(RGBA8(96, 125, 160, 150), hud_opacity));
		vita2d_draw_rectangle(TIMELINE_X, TIMELINE_Y, TIMELINE_W * progress, 6,
		                      fade_color(VT_THEME_BLUE_BRIGHT, hud_opacity));
		vita2d_draw_fill_circle(TIMELINE_X + TIMELINE_W * progress,
		                        TIMELINE_Y + 3.0f, dragging ? 9.0f : 6.0f, text);
		char current[24], duration[24];
		uint64_t shown_position = dragging && status->duration_ms
		                        ? (uint64_t)(drag_fraction * status->duration_ms)
		                        : status->position_ms;
		format_time(shown_position, current);
		format_time(status->duration_ms, duration);
		if (small) {
			ui_font_draw_text(small, TIMELINE_X, 499,
			                  dragging ? fade_color(VT_THEME_BLUE_LIGHT, hud_opacity)
			                           : text,
			                  UI_FONT_SMALL, current);
			int width = ui_font_text_width(small, UI_FONT_SMALL, duration);
			ui_font_draw_text(small, TIMELINE_X + TIMELINE_W - width, 499,
			                  text, UI_FONT_SMALL, duration);
		}
	}
	if (volume_opacity > 0.01f) {
		/* Volume never jumps vertically when the rest of the HUD fades. */
		int x = 812, y = 431;
		vita2d_draw_rectangle(x, y, 116, 34,
		                      fade_color(RGBA8(0, 0, 0, 216), volume_opacity));
		vita2d_draw_rectangle(x, y, 3, 34,
		                      fade_color(VT_THEME_BLUE_LIGHT, volume_opacity));
		if (small) ui_font_draw_textf(small, x + 14, y + 24,
		                             fade_color(VT_THEME_TEXT, volume_opacity),
		                             UI_FONT_SMALL,
		                             vt_i18n_str(VT_STR_PLAYER_VOLUME_FORMAT), volume);
	}
}

static void draw_buffering_overlay(float opacity, uint64_t now) {
	if (opacity <= 0.01f) return;
	const float x = 330.0f, y = 238.0f, width = 300.0f, height = 68.0f;
	vita2d_draw_rectangle(x - 4, y - 4, width + 8, height + 8,
	                      fade_color(RGBA8(0, 0, 0, 92), opacity));
	vita2d_draw_rectangle(x, y, width, height,
	                      fade_color(RGBA8(2, 10, 19, 232), opacity));
	vita2d_draw_rectangle(x, y, 4, height,
	                      fade_color(VT_THEME_BLUE_LIGHT, opacity));
	if (opacity > 0.16f) ui_draw_spinner_compact(x + 38, y + 34, now);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (small)
		ui_font_draw_text(small, (int)x + 70, (int)y + 41,
		                  fade_color(VT_THEME_TEXT, opacity), UI_FONT_SMALL,
		                  vt_i18n_str(VT_STR_PLAYER_BUFFERING));
}

static void draw_player_right_sidebar(float animation, float focus_position,
	                                  int cursor,
	                                  const VtDecoderPlayerStatus *status) {
	if (animation <= 0.01f || !status) return;
	const float width = RIGHT_PANEL_WIDTH;
	float x = SCREEN_WIDTH - width * animation;
	vita2d_draw_rectangle(x, 0, width, SCREEN_HEIGHT, RGBA8(2, 8, 17, 250));
	vita2d_draw_rectangle(x, 0, 4, SCREEN_HEIGHT, VT_THEME_BLUE_LIGHT);
	vita2d_draw_rectangle(x + 4, 0, width - 4, 1, VT_THEME_BORDER);
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (body)
		ui_font_draw_text(body, (int)x + 28, 47, VT_THEME_TEXT,
		                  UI_FONT_BODY, vt_i18n_str(VT_STR_PLAYER_PANEL_TITLE));
	if (small) {
		vita2d_draw_rectangle(x + width - 65, 18, 42, 26, VT_THEME_SURFACE_RAISED);
		int key_width = ui_font_text_width(small, UI_FONT_SMALL, "R1");
		ui_font_draw_text(small,
		                  (int)(x + width - 44.0f - key_width * 0.5f), 38,
		                  VT_THEME_TEXT,
		                  UI_FONT_SMALL, "R1");
	}
	const char *labels[2] = {
		vt_i18n_str(VT_STR_PLAYER_FILL_SCREEN),
		vt_i18n_str(VT_STR_PLAYER_LOOP)
	};
	int values[2] = {
		vt_preferences_fill_screen(), vt_preferences_loop_enabled()
	};
	for (int i = 0; i < 2; i++) {
		float y = RIGHT_PANEL_ROW_Y + i * RIGHT_PANEL_ROW_STEP;
		vita2d_draw_rectangle(x + 18, y, width - 36, 56, VT_THEME_SURFACE);
	}
	float focus_y = RIGHT_PANEL_ROW_Y + focus_position * RIGHT_PANEL_ROW_STEP;
	vita2d_draw_rectangle(x + 14, focus_y - 4, width - 28, 64,
	                      VT_THEME_HALO_A(76));
	vita2d_draw_rectangle(x + 18, focus_y, width - 36, 56,
	                      VT_THEME_SURFACE_FOCUS);
	vita2d_draw_rectangle(x + 18, focus_y, 4, 56, VT_THEME_BLUE_LIGHT);
	for (int i = 0; i < 2; i++) {
		float y = RIGHT_PANEL_ROW_Y + i * RIGHT_PANEL_ROW_STEP;
		if (small) {
			ui_font_draw_text(small, (int)x + 38, (int)y + 35,
			                  i == cursor ? VT_THEME_TEXT : VT_THEME_TEXT_MUTED,
			                  UI_FONT_SMALL, labels[i]);
			const char *value = vt_i18n_str(values[i] ? VT_STR_PLAYER_ON
			                                                : VT_STR_PLAYER_OFF);
			int value_w = ui_font_text_width(small, UI_FONT_SMALL, value);
			ui_font_draw_text(small, (int)x + (int)width - value_w - 34,
			                  (int)y + 35, values[i] ? VT_THEME_BLUE_LIGHT
			                                     : VT_THEME_TEXT_MUTED,
			                  UI_FONT_SMALL, value);
		}
	}
	vita2d_draw_rectangle(x + 18, 236, width - 36, 1, VT_THEME_BORDER);
	if (small) {
		char decoder[96];
		snprintf(decoder, sizeof(decoder), vt_i18n_str(VT_STR_PLAYER_DECODER_INFO),
		         status->hardware_accelerated ? "HW" : "SW",
		         status->height, status->fps);
		ui_font_draw_text(small, (int)x + 28, 276, VT_THEME_TEXT,
		                  UI_FONT_SMALL, decoder);
		ui_font_draw_text(small, (int)x + 28, 518, VT_THEME_TEXT_MUTED,
		                  UI_FONT_SMALL, vt_i18n_str(VT_STR_PLAYER_PANEL_HINT));
	}
}

int vt_hw_player_screen_run(const VtHwPlayerScreenSource *source,
	                        uint64_t *last_position_ms) {
	if (!source || !source->stream.open) return -1;
	VtPerformanceClockGuard clock_guard;
	vt_performance_begin_video(&clock_guard, (int)source->expected_height,
	                           source->expected_fps);
	VtDecoderPlayer *player = vt_decoder_create();
	if (!player) {
		vt_performance_end_video(&clock_guard);
		return -1;
	}
	volatile int cancel = 0;
	OpenTask open;
	memset(&open, 0, sizeof(open));
	open.player = player;
	open.config.stream = source->stream;
	open.config.expected_width = source->expected_width;
	open.config.expected_height = source->expected_height;
	open.config.expected_fps = source->expected_fps;
	open.config.start_position_ms = source->start_position_ms;
	open.config.volume_percent = vt_audio_volume_percent();
	open.config.cancel_flag = &cancel;
	UiPlayerLoadingInfo loading = {
		.title = source->title,
		.channel = source->location,
		.status = source->authenticated_remote
		        ? vt_i18n_str(VT_STR_PLAYER_OPEN_REMOTE)
		        : vt_i18n_str(VT_STR_PLAYER_OPEN_LOCAL),
		.quality_height = source->expected_height
	};
	int ret = ui_player_loading_run(&loading, open_task, &open, &cancel, NULL, NULL);
	if (ret < 0) {
		vt_decoder_destroy(player);
		vt_performance_end_video(&clock_guard);
		return cancel ? 0 : ret;
	}
	SceCtrlData controls, previous;
	memset(&controls, 0, sizeof(controls));
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	sceCtrlPeekBufferPositive(0, &previous, 1);
	ui_touch_reset();
	int paused = 0;
	int hud_visible = 1;
	float hud_opacity = 1.0f;
	int dragging = 0;
	float drag_fraction = 0.0f;
	UiSectionsSidebar sidebar;
	ui_sections_sidebar_init(&sidebar, source->authenticated_remote
	                                    ? UI_SECTION_NETWORK
	                                    : UI_SECTION_LOCAL_MEDIA);
	int right_open = 0;
	int right_cursor = 0;
	float right_animation = 0.0f;
	float right_focus_position = 0.0f;
	float buffering_opacity = 0.0f;
	uint64_t hud_deadline = sceKernelGetProcessTimeWide() + 3500000ULL;
	uint64_t volume_deadline = 0;
	uint64_t last_volume_input = 0;
	uint64_t previous_frame_us = sceKernelGetProcessTimeWide();
	int left_seek_direction = 0;
	uint64_t left_seek_repeat_at = 0;
	UiNavRepeat right_nav_repeat;
	ui_nav_repeat_reset(&right_nav_repeat);
	for (;;) {
		uint64_t now = sceKernelGetProcessTimeWide();
		float delta_seconds = (float)(now - previous_frame_us) / 1000000.0f;
		previous_frame_us = now;
		sceCtrlPeekBufferPositive(0, &controls, 1);
		unsigned pressed = controls.buttons & ~previous.buttons;
		previous = controls;
		UiTouchEvent touch;
		unsigned touch_flags = ui_touch_poll(&touch);
		VtDecoderPlayerStatus status;
		vt_decoder_get_status(player, &status);
		if (status.error) {
			if (vt_decoder_backend(player) == VT_DECODER_BACKEND_HARDWARE) {
				SeekTask task = { player, status.position_ms };
				cancel = 0;
				loading.status = vt_i18n_str(VT_STR_PLAYER_FALLBACK_SOFTWARE);
				ret = ui_player_loading_run(&loading, fallback_task, &task,
				                            &cancel, NULL, NULL);
				loading.status = source->authenticated_remote
				               ? vt_i18n_str(VT_STR_PLAYER_OPEN_REMOTE)
				               : vt_i18n_str(VT_STR_PLAYER_OPEN_LOCAL);
				if (ret >= 0) {
					paused = 0;
					hud_visible = 1;
					now = resync_after_blocking_action(
					    &controls, &previous, &left_seek_direction,
					    &left_seek_repeat_at);
					hud_deadline = now + 3500000ULL;
					continue;
				}
			}
			ret = -1;
			break;
		}
		if (status.eof) {
			if (!vt_preferences_loop_enabled()) { ret = 0; break; }
			ret = run_seek(player, &loading, &cancel, 0);
			if (ret < 0) break;
			paused = 0;
			vt_decoder_set_paused(player, 0);
			hud_visible = 1;
			now = resync_after_blocking_action(
			    &controls, &previous, &left_seek_direction,
			    &left_seek_repeat_at);
			hud_deadline = now + 3500000ULL;
			continue;
		}
		int swapped = vt_preferences_player_swap_shoulders();
		unsigned transport_pressed = pressed;
		if (swapped) pressed &= ~(SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER);
		if (swapped && !sidebar.open && !right_open &&
		    (pressed & SCE_CTRL_LEFT)) {
			pressed &= ~SCE_CTRL_LEFT;
			pressed |= SCE_CTRL_LTRIGGER;
		}
		unsigned right_toggle = swapped ? SCE_CTRL_RIGHT : SCE_CTRL_RTRIGGER;
		if (!sidebar.open && sidebar.animation <= 0.01f &&
		    (pressed & right_toggle)) {
			right_open = !right_open;
			right_cursor = 0;
			right_focus_position = 0.0f;
			ui_nav_repeat_reset(&right_nav_repeat);
			dragging = 0;
			pressed &= ~right_toggle;
		}
		if (!right_open && right_animation <= 0.01f) {
			int section = ui_sections_sidebar_handle_buttons(
			    &sidebar, &pressed, controls.buttons, controls.ly);
			if (section != UI_SECTION_NONE) {
				ret = VT_HW_PLAYER_ACTION_SECTION_BASE + section;
				break;
			}
		} else if (right_open) {
			unsigned right_navigation = ui_nav_repeat_update(
			    &right_nav_repeat, pressed, controls.buttons, controls.lx,
			    controls.ly, SCE_CTRL_UP | SCE_CTRL_DOWN);
			if ((right_navigation & SCE_CTRL_UP) && right_cursor > 0) right_cursor--;
			if ((right_navigation & SCE_CTRL_DOWN) && right_cursor < 1) right_cursor++;
			if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) {
				if (right_cursor == 0)
					vt_preferences_set_fill_screen(!vt_preferences_fill_screen());
				else vt_preferences_set_loop_enabled(!vt_preferences_loop_enabled());
			}
			if (pressed & SCE_CTRL_CIRCLE) right_open = 0;
			pressed = 0;
		} else {
			/* Closing right drawer: keep the frame modal until it clears. */
			pressed = 0;
		}
		/* A drawer remains modal until its closing animation is fully gone.
		 * This prevents a second tap or shoulder press from reaching playback
		 * controls through a still-visible panel, and prevents opposite drawers
		 * from crossing over each other. */
		int panels_open = sidebar.open || right_open ||
		                  sidebar.animation > 0.01f || right_animation > 0.01f;
		int panels_closing = (!sidebar.open && sidebar.animation > 0.01f) ||
		                     (!right_open && right_animation > 0.01f);
		if (panels_closing) {
			ui_touch_reset();
			touch_flags = UI_TOUCH_EVENT_NONE;
		}
		if (panels_open) {
			dragging = 0;
			left_seek_direction = 0;
			left_seek_repeat_at = 0;
		}
		if (!panels_open && (pressed & SCE_CTRL_CIRCLE)) { ret = 0; break; }
		if (!panels_open && (pressed & SCE_CTRL_CROSS)) {
			paused = !paused;
			vt_decoder_set_paused(player, paused);
			hud_visible = 1;
			hud_deadline = now + 3500000ULL;
		}
		unsigned transport = swapped ? transport_pressed : pressed;
		int seek_direction = !panels_open &&
		    (transport & (swapped ? SCE_CTRL_RTRIGGER : SCE_CTRL_RIGHT)) ? 1
		                   : !panels_open &&
		    (transport & (swapped ? SCE_CTRL_LTRIGGER : SCE_CTRL_LEFT)) ? -1 : 0;
		int left_x = (int)controls.lx - STICK_CENTER;
		int left_y = (int)controls.ly - STICK_CENTER;
		int abs_left_x = left_x < 0 ? -left_x : left_x;
		int abs_left_y = left_y < 0 ? -left_y : left_y;
		int analog_seek_direction = !panels_open && abs_left_x > STICK_DEADZONE &&
		                            abs_left_x > abs_left_y
		                          ? (left_x < 0 ? -1 : 1) : 0;
		if (!analog_seek_direction) {
			left_seek_direction = 0;
			left_seek_repeat_at = 0;
		} else if (analog_seek_direction != left_seek_direction) {
			left_seek_direction = analog_seek_direction;
			left_seek_repeat_at = now + SEEK_REPEAT_DELAY_US;
			if (!seek_direction) seek_direction = analog_seek_direction;
		} else if (now >= left_seek_repeat_at) {
			left_seek_repeat_at = now + SEEK_REPEAT_INTERVAL_US;
			if (!seek_direction) seek_direction = analog_seek_direction;
		}
		if (seek_direction && status.duration_ms) {
			int64_t target = (int64_t)status.position_ms + seek_direction * 10000LL;
			if (target < 0) target = 0;
			if ((uint64_t)target > status.duration_ms) target = status.duration_ms;
			ret = run_seek(player, &loading, &cancel, (uint64_t)target);
			if (ret < 0) break;
			hud_visible = 1;
			now = resync_after_blocking_action(
			    &controls, &previous, &left_seek_direction,
			    &left_seek_repeat_at);
			hud_deadline = now + 3500000ULL;
			continue;
		}
		int right_y = (int)controls.ry - STICK_CENTER;
		if (!panels_open &&
		    (right_y > STICK_DEADZONE || right_y < -STICK_DEADZONE) &&
		    now - last_volume_input >= 80000ULL) {
			int volume = vt_audio_volume_percent() + (right_y < 0 ? 5 : -5);
			if (volume < 0) volume = 0;
			if (volume > 300) volume = 300;
			vt_audio_volume_set_percent(volume);
			vt_decoder_set_volume(player, volume);
			last_volume_input = now;
			volume_deadline = now + 1200000ULL;
		}
		if (sidebar.open) {
			int section = ui_sections_sidebar_handle_touch(
			    &sidebar, touch_flags, touch.x, touch.y);
			if (section != UI_SECTION_NONE) {
				ret = VT_HW_PLAYER_ACTION_SECTION_BASE + section;
				break;
			}
			touch_flags = UI_TOUCH_EVENT_NONE;
		} else if (right_open && (touch_flags & UI_TOUCH_EVENT_TAP)) {
			int panel_x = (int)(SCREEN_WIDTH - RIGHT_PANEL_WIDTH * right_animation);
			int handled = 0;
			for (int i = 0; i < 2; i++) {
				if (!ui_touch_hit_rect(touch.x, touch.y, panel_x + 18,
				                       (int)(RIGHT_PANEL_ROW_Y + i * RIGHT_PANEL_ROW_STEP),
				                       (int)RIGHT_PANEL_WIDTH - 36, 56)) continue;
				right_cursor = i;
				handled = 1;
				if (i == 0)
					vt_preferences_set_fill_screen(!vt_preferences_fill_screen());
				else vt_preferences_set_loop_enabled(!vt_preferences_loop_enabled());
			}
			if (!handled && touch.x < panel_x) right_open = 0;
			touch_flags = UI_TOUCH_EVENT_NONE;
		}
		int hud_interactive = hud_visible || hud_opacity > 0.65f;
		if (!panels_open && hud_interactive && status.duration_ms &&
		    (touch_flags & UI_TOUCH_EVENT_DOWN) &&
		    ui_touch_hit_rect(touch.x, touch.y, TIMELINE_X, TIMELINE_Y - 16,
		                      TIMELINE_W, 42)) {
			dragging = 1;
			drag_fraction = (float)(touch.x - TIMELINE_X) / (float)TIMELINE_W;
			if (drag_fraction < 0.0f) drag_fraction = 0.0f;
			if (drag_fraction > 1.0f) drag_fraction = 1.0f;
		}
		if (!panels_open && dragging &&
		    (touch_flags & (UI_TOUCH_EVENT_MOVE | UI_TOUCH_EVENT_HOLD |
		                    UI_TOUCH_EVENT_UP))) {
			drag_fraction = (float)(touch.x - TIMELINE_X) / (float)TIMELINE_W;
			if (drag_fraction < 0.0f) drag_fraction = 0.0f;
			if (drag_fraction > 1.0f) drag_fraction = 1.0f;
			if (touch_flags & UI_TOUCH_EVENT_UP) {
				ret = run_seek(player, &loading, &cancel,
				               (uint64_t)(drag_fraction * status.duration_ms));
				dragging = 0;
				if (ret < 0) break;
				hud_visible = 1;
				now = resync_after_blocking_action(
				    &controls, &previous, &left_seek_direction,
				    &left_seek_repeat_at);
				hud_deadline = now + 3500000ULL;
				continue;
			}
		} else if (!panels_open && (touch_flags & UI_TOUCH_EVENT_TAP)) {
			if (!hud_interactive) {
				hud_visible = 1;
				hud_deadline = now + 3500000ULL;
			} else if (ui_touch_hit_rect(touch.x, touch.y,
			                             (int)PLAYER_PLAY_X - 34,
			                             (int)PLAYER_TRANSPORT_Y - 34, 68, 68)) {
				paused = !paused;
				vt_decoder_set_paused(player, paused);
				hud_visible = 1;
				hud_deadline = now + 3500000ULL;
			} else if (status.duration_ms &&
			           ui_touch_hit_rect(touch.x, touch.y,
			                             (int)PLAYER_BACK_X - 28,
			                             (int)PLAYER_TRANSPORT_Y - 28, 56, 56)) {
				uint64_t target = status.position_ms > 10000ULL
				                ? status.position_ms - 10000ULL : 0;
				ret = run_seek(player, &loading, &cancel, target);
				if (ret < 0) break;
				now = resync_after_blocking_action(
				    &controls, &previous, &left_seek_direction,
				    &left_seek_repeat_at);
				hud_visible = 1;
				hud_deadline = now + 3500000ULL;
				continue;
			} else if (status.duration_ms &&
			           ui_touch_hit_rect(touch.x, touch.y,
			                             (int)PLAYER_FORWARD_X - 28,
			                             (int)PLAYER_TRANSPORT_Y - 28, 56, 56)) {
				uint64_t target = status.position_ms + 10000ULL;
				if (target > status.duration_ms) target = status.duration_ms;
				ret = run_seek(player, &loading, &cancel, target);
				if (ret < 0) break;
				now = resync_after_blocking_action(
				    &controls, &previous, &left_seek_direction,
				    &left_seek_repeat_at);
				hud_visible = 1;
				hud_deadline = now + 3500000ULL;
				continue;
			} else if (touch.y < 390) {
				hud_visible = 0;
			}
		}
		if (!paused && now > hud_deadline) hud_visible = 0;
		ui_sections_sidebar_tick(&sidebar);
		if (vt_preferences_reduce_motion()) {
			right_animation = right_open ? 1.0f : 0.0f;
			right_focus_position = (float)right_cursor;
			hud_opacity = hud_visible ? 1.0f : 0.0f;
			buffering_opacity = status.ready_frames == 0 && !paused ? 1.0f : 0.0f;
		} else {
			right_animation = approach_for_frame(
			    right_animation, right_open ? 1.0f : 0.0f, 20.0f, delta_seconds);
			right_focus_position = approach_for_frame(
			    right_focus_position, (float)right_cursor, 23.0f, delta_seconds);
			hud_opacity = approach_for_frame(
			    hud_opacity, hud_visible ? 1.0f : 0.0f, 13.5f, delta_seconds);
			buffering_opacity = approach_for_frame(
			    buffering_opacity,
			    status.ready_frames == 0 && !paused ? 1.0f : 0.0f,
			    12.0f, delta_seconds);
		}
		if (right_animation < 0.001f && !right_open) right_animation = 0.0f;
		if (hud_opacity < 0.001f && !hud_visible) hud_opacity = 0.0f;
		if (buffering_opacity < 0.001f && status.ready_frames > 0)
			buffering_opacity = 0.0f;
		float volume_opacity = 0.0f;
		if (now < volume_deadline) {
			uint64_t remaining = volume_deadline - now;
			volume_opacity = remaining >= 280000ULL
			               ? 1.0f : (float)remaining / 280000.0f;
		}
		vt_display_keep_awake_tick();
		vita2d_start_drawing();
		vita2d_clear_screen();
		vt_decoder_present(player, vt_preferences_fill_screen());
		vt_decoder_get_status(player, &status);
		if (hud_opacity > 0.01f || volume_opacity > 0.01f)
			draw_hud(source, &status, paused, vt_audio_volume_percent(),
			         hud_opacity, volume_opacity, dragging, drag_fraction);
		else if (vt_preferences_player_status_always_visible())
			ui_brand_draw_status_indicators_alpha(1.0f);
		draw_buffering_overlay(buffering_opacity, now);
		if (sidebar.animation > 0.01f)
			ui_sections_sidebar_draw(sidebar.cursor, sidebar.animation,
			                         sidebar.open ? sidebar.focus_cursor : -1.0f);
		draw_player_right_sidebar(right_animation, right_focus_position,
		                          right_cursor, &status);
		vita2d_end_drawing();
		vita2d_wait_rendering_done();
		vt_decoder_render_complete(player);
		vita2d_swap_buffers();
		sceKernelDelayThread(1000);
	}
	VtDecoderPlayerStatus final_status;
	vt_decoder_get_status(player, &final_status);
	if (last_position_ms) *last_position_ms = final_status.position_ms;
	vt_decoder_destroy(player);
	vt_performance_end_video(&clock_guard);
	if (ret < 0 && cancel) ret = 0;
	return ret;
}
