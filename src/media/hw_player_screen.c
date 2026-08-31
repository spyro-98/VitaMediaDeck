#include "media/hw_player_screen.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include <libavutil/error.h>

#include "history/playback_history.h"
#include "i18n/i18n.h"
#include "media/audio_volume.h"
#include "media/player_input_lock.h"
#include "media/player_power_save.h"
#include "settings/preferences.h"
#include "system/display_awake.h"
#include "system/performance.h"
#include "ui/brand.h"
#include "ui/components.h"
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

typedef struct {
	VtDecoderPlayer *player;
	uint64_t position_ms;
	int track;
	volatile int *operation_cancel;
} TrackTask;

static int seek_task(void *opaque) {
	SeekTask *task = opaque;
	return vt_decoder_seek(task->player, task->position_ms);
}

static int fallback_task(void *opaque) {
	SeekTask *task = opaque;
	return vt_decoder_fallback_to_software(task->player, task->position_ms);
}

static int audio_track_task(void *opaque) {
	TrackTask *task = opaque;
	return vt_decoder_select_audio_track_with_cancel(
	    task->player, task->track, task->position_ms, task->operation_cancel);
}

static void cancel_player_operation(void *opaque) {
	vt_decoder_request_stop((VtDecoderPlayer *)opaque);
}

static void cancel_audio_operation(void *opaque) {
	vt_decoder_interrupt_audio_operation((VtDecoderPlayer *)opaque);
}

static void fence_decoder_frame(VtDecoderPlayer *player) {
	/* Seek/fallback runs on the cancellable loading worker. Retire the preceding
	 * texture on the UI thread first so decoder teardown never calls vita2d/GXM
	 * while the loading scene is drawing. */
	vita2d_wait_rendering_done();
	vt_decoder_render_complete(player);
	vt_decoder_prepare_background_restart(player);
}

static int run_seek(VtDecoderPlayer *player, UiPlayerLoadingInfo *loading,
	                volatile int *cancel, uint64_t position_ms) {
	SeekTask task = { player, position_ms };
	if (cancel) *cancel = 0;
	fence_decoder_frame(player);
	return ui_player_loading_run(loading, seek_task, &task, cancel, NULL, NULL);
}

static int run_track_change(VtDecoderPlayer *player,
	                        UiPlayerLoadingInfo *loading,
	                        volatile int *cancel, uint64_t position_ms,
	                        int subtitles, int track) {
	(void)cancel;
	int count = subtitles ? vt_decoder_subtitle_track_count(player)
	                      : vt_decoder_audio_track_count(player);
	int choices = subtitles ? count + 1 : count;
	if (track < 0 || track >= choices) return -1;
	int current = subtitles ? vt_decoder_active_subtitle_track(player)
	                        : vt_decoder_active_audio_track(player);
	if (track == current) {
		if (!subtitles) return 0;
		VtDecoderSubtitleState subtitle_state =
		    vt_decoder_subtitle_state(player);
		if (subtitle_state != VT_DECODER_SUBTITLE_PENDING &&
		    subtitle_state != VT_DECODER_SUBTITLE_FAILED) return 0;
	}
	/* Subtitle open/probe/seek is owned by the persistent subtitle worker. Post
	 * the serial request directly so video, input, and the right drawer remain
	 * live; importantly, do not reset or pass the decoder session cancel flag. */
	if (subtitles)
		return vt_decoder_select_subtitle_track(player, track, position_ms);
	volatile int operation_cancel = 0;
	TrackTask task = { player, position_ms, track, &operation_cancel };
	loading->status = vt_i18n_str(VT_STR_PLAYER_CHANGE_AUDIO);
	UiPlayerLoadingInfo audio_loading = *loading;
	audio_loading.cancel_action = cancel_audio_operation;
	audio_loading.cancel_ctx = player;
	int result = ui_player_loading_run(&audio_loading, audio_track_task, &task,
	                                   &operation_cancel, NULL, NULL);
	/* The decoder restores the previous audio track after an operation-only
	 * cancellation. Only swallow the matching EXIT result: a failed rollback is
	 * deliberately reported as a real error so the player cannot continue with
	 * its audio cursor stopped while the UI claims the action was dismissed. */
	return operation_cancel && result == AVERROR_EXIT ? 0 : result;
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
	unsigned fill = fade_color(VT_THEME_GLASS_A(225), opacity);
	unsigned text = fade_color(VT_THEME_TEXT, opacity);
	float left = center_x - 23.0f, top = PLAYER_TRANSPORT_Y - 23.0f;
	vita2d_draw_rectangle(left, top, 46.0f, 46.0f, fill);
	vita2d_draw_rectangle(left, top, 46.0f, 1.0f,
	                      fade_color(VT_THEME_SIGNAL_LIGHT, opacity));
	vita2d_draw_rectangle(left, top, 10.0f, 2.0f, text);
	vita2d_draw_rectangle(left, top, 2.0f, 10.0f, text);
	vita2d_draw_rectangle(left + 36.0f, top + 44.0f, 10.0f, 2.0f, text);
	vita2d_draw_rectangle(left + 44.0f, top + 36.0f, 2.0f, 10.0f, text);
	if (font) {
		int width = ui_font_text_width(font, UI_FONT_SMALL, label);
		ui_font_draw_text(font, (int)center_x - width / 2,
		                  (int)PLAYER_TRANSPORT_Y + 7, text,
		                  UI_FONT_SMALL, label);
	}
}

static void draw_player_input_lock_at(int x, int y, float opacity) {
	unsigned color = fade_color(VT_THEME_TEXT, opacity);
	unsigned panel = fade_color(RGBA8(7, 14, 17, 230), opacity);
	vita2d_draw_rectangle(x, y, 54, 44, panel);
	vita2d_draw_rectangle(x, y, 3, 44,
	                      fade_color(VT_THEME_COLD_LIGHT, opacity));
	vita2d_draw_line(x + 20, y + 19, x + 20, y + 12, color);
	vita2d_draw_line(x + 20, y + 12, x + 27, y + 6, color);
	vita2d_draw_line(x + 27, y + 6, x + 34, y + 12, color);
	vita2d_draw_line(x + 34, y + 12, x + 34, y + 19, color);
	vita2d_draw_rectangle(x + 16, y + 18, 22, 18, color);
	vita2d_draw_fill_circle((float)x + 27.0f, (float)y + 26.0f, 2.0f, panel);
	vita2d_draw_rectangle(x + 26, y + 26, 3, 6, panel);
}

static void draw_player_power_save_status_at(int x, int y) {
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	int text_x = x > 660 ? x - 218 : x + 18;
	int baseline = y > 470 ? y - 5 : y + 16;
	vita2d_draw_rectangle(x, y + 5, 12, 2, VT_THEME_COLD_DIM);
	vita2d_draw_rectangle(x + 4, y + 1, 4, 10, VT_THEME_COLD_A(86));
	if (small)
		ui_font_draw_text(small, text_x, baseline, VT_THEME_COLD_DIM,
		                  UI_FONT_SMALL,
		                  vt_i18n_str(VT_STR_PLAYER_POWER_SAVE_ACTIVE));
}

static void format_video_bitrate(uint64_t bitrate_bps, char out[32]) {
	if (!bitrate_bps) {
		snprintf(out, 32, "-- Mbps");
		return;
	}
	uint64_t tenths = (bitrate_bps + 50000ULL) / 100000ULL;
	if (tenths < 10ULL)
		snprintf(out, 32, "%llu kbps",
		         (unsigned long long)((bitrate_bps + 500ULL) / 1000ULL));
	else
		snprintf(out, 32, "%llu.%llu Mbps",
		         (unsigned long long)(tenths / 10ULL),
		         (unsigned long long)(tenths % 10ULL));
}

static void format_track_value(const VtDecoderPlayer *player, int subtitles,
	                           int selection, char out[128]) {
	int count = subtitles ? vt_decoder_subtitle_track_count(player)
	                      : vt_decoder_audio_track_count(player);
	int active = selection >= 0 ? selection
	           : subtitles ? vt_decoder_active_subtitle_track(player)
	                       : vt_decoder_active_audio_track(player);
	if (!count || (subtitles && active == 0)) {
		snprintf(out, 128, "%s", vt_i18n_str(VT_STR_PLAYER_OFF));
		return;
	}
	int index = subtitles ? active - 1 : active;
	VtDecoderTrackInfo info;
	int result = subtitles
	           ? vt_decoder_subtitle_track_info(player, index, &info)
	           : vt_decoder_audio_track_info(player, index, &info);
	if (result < 0) {
		snprintf(out, 128, "--");
		return;
	}
	char fallback[48];
	const char *name = info.title[0] ? info.title
	                 : info.language[0] ? info.language
	                 : info.codec[0] ? info.codec : NULL;
	if (!name) {
		snprintf(fallback, sizeof(fallback),
		         vt_i18n_str(VT_STR_PLAYER_TRACK_UNNAMED), index + 1);
		name = fallback;
	}
	snprintf(out, 128, vt_i18n_str(VT_STR_PLAYER_TRACK_FORMAT),
	         index + 1, count, name);
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
			if (small) {
				char shortcuts[192];
				ui_font_fit_text(small, UI_FONT_SMALL,
				                 vt_i18n_str(source->allow_minimize
				                     ? VT_STR_PLAYER_SHORTCUTS_HINT
				                     : VT_STR_PLAYER_REMOTE_SHORTCUTS_HINT), shortcuts,
				                 sizeof(shortcuts), 400);
				ui_font_draw_text(small, 292, 91,
				                  fade_color(VT_THEME_COLD_LIGHT, hud_opacity),
				                  UI_FONT_SMALL, shortcuts);
			}
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
		char bitrate[32], quality[80];
		format_video_bitrate(status->video_bitrate_bps, bitrate);
		snprintf(quality, sizeof(quality), "%up  %d fps  %s",
		         status->height, status->fps, bitrate);
		if (small) ui_font_draw_text(small, 154, 454, text,
		                             UI_FONT_SMALL, quality);
		draw_skip_button(small, PLAYER_BACK_X, "<", hud_opacity);
		draw_skip_button(small, PLAYER_FORWARD_X, ">", hud_opacity);
		vita2d_draw_rectangle(PLAYER_PLAY_X - 32, PLAYER_TRANSPORT_Y - 32, 64, 64,
		                      fade_color(VT_THEME_HALO_A(92), hud_opacity));
		vita2d_draw_rectangle(PLAYER_PLAY_X - 28, PLAYER_TRANSPORT_Y - 28, 56, 56,
		                      fade_color(VT_THEME_SURFACE_FOCUS, hud_opacity));
		vita2d_draw_rectangle(PLAYER_PLAY_X - 28, PLAYER_TRANSPORT_Y - 28, 56, 2,
		                      fade_color(VT_THEME_SIGNAL_LIGHT, hud_opacity));
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
		                      fade_color(VT_THEME_BORDER, hud_opacity));
		VtDecoderBufferStatus buffer = {0};
		int has_buffer = source->stream.buffer_status &&
		    source->stream.buffer_status(source->stream.opaque, &buffer) == 0 &&
		    buffer.capacity_bytes > 0;
		if (has_buffer &&
		    buffer.source_size > 0 && buffer.range_end > buffer.range_start) {
			float buffer_start = (float)((double)buffer.range_start /
			                                    (double)buffer.source_size);
			float buffer_end = (float)((double)buffer.range_end /
			                                  (double)buffer.source_size);
			if (buffer_start < 0.0f) buffer_start = 0.0f;
			if (buffer_end > 1.0f) buffer_end = 1.0f;
			if (buffer_end > buffer_start) {
				float buffer_width = TIMELINE_W * (buffer_end - buffer_start);
				/* A bounded network window is intentionally small relative to a
				 * feature film; preserve a visible diagnostic trace on the Vita. */
				if (buffer_width < 3.0f) buffer_width = 3.0f;
				if (TIMELINE_W * buffer_start + buffer_width > TIMELINE_W)
					buffer_width = TIMELINE_W * (1.0f - buffer_start);
				vita2d_draw_rectangle(TIMELINE_X + TIMELINE_W * buffer_start,
				                      TIMELINE_Y, buffer_width,
				                      6, fade_color(VT_THEME_SIGNAL_LIGHT, hud_opacity));
			}
		}
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
			int duration_width =
			    ui_font_text_width(small, UI_FONT_SMALL, duration);
			ui_font_draw_text(small,
			                  TIMELINE_X + TIMELINE_W - duration_width, 499,
			                  text, UI_FONT_SMALL, duration);
			if (has_buffer) {
				char reserve[64];
				snprintf(reserve, sizeof(reserve),
				         vt_i18n_str(VT_STR_PLAYER_BUFFER_RESERVE),
				         (double)buffer.resident_bytes / (1024.0 * 1024.0),
				         (double)buffer.capacity_bytes / (1024.0 * 1024.0));
				int reserve_width =
				    ui_font_text_width(small, UI_FONT_SMALL, reserve);
				int reserve_x = TIMELINE_X + TIMELINE_W - duration_width -
				                reserve_width - 20;
				ui_font_draw_text(small, reserve_x, 499,
				                  fade_color(VT_THEME_SIGNAL_LIGHT, hud_opacity),
				                  UI_FONT_SMALL, reserve);
			}
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
	                      fade_color(VT_THEME_GLASS_A(232), opacity));
	vita2d_draw_rectangle(x, y, 4, height,
	                      fade_color(VT_THEME_BLUE_LIGHT, opacity));
	if (opacity > 0.16f) ui_draw_spinner_compact(x + 38, y + 34, now);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (small)
		ui_font_draw_text(small, (int)x + 70, (int)y + 41,
		                  fade_color(VT_THEME_TEXT, opacity), UI_FONT_SMALL,
		                  vt_i18n_str(VT_STR_PLAYER_BUFFERING));
}

static unsigned subtitle_text_color(void) {
	switch (vt_preferences_subtitle_text_color()) {
		case VT_SUBTITLE_TEXT_YELLOW: return RGBA8(255, 232, 96, 255);
		case VT_SUBTITLE_TEXT_CYAN: return RGBA8(108, 235, 255, 255);
		case VT_SUBTITLE_TEXT_GREEN: return RGBA8(124, 244, 148, 255);
		case VT_SUBTITLE_TEXT_ORANGE: return RGBA8(255, 174, 72, 255);
		case VT_SUBTITLE_TEXT_MAGENTA: return RGBA8(255, 120, 218, 255);
		case VT_SUBTITLE_TEXT_BLUE: return RGBA8(130, 174, 255, 255);
		case VT_SUBTITLE_TEXT_GRAY: return RGBA8(196, 204, 212, 255);
		default: return RGBA8(255, 255, 255, 255);
	}
}

static unsigned subtitle_border_color(void) {
	switch (vt_preferences_subtitle_border_color()) {
		case VT_SUBTITLE_BORDER_MIDNIGHT: return RGBA8(3, 18, 34, 255);
		case VT_SUBTITLE_BORDER_WHITE: return RGBA8(255, 255, 255, 255);
		case VT_SUBTITLE_BORDER_YELLOW: return RGBA8(255, 210, 48, 255);
		case VT_SUBTITLE_BORDER_CYAN: return RGBA8(28, 218, 235, 255);
		case VT_SUBTITLE_BORDER_ORANGE: return RGBA8(242, 116, 44, 255);
		case VT_SUBTITLE_BORDER_BLUE: return RGBA8(46, 112, 232, 255);
		case VT_SUBTITLE_BORDER_GRAY: return RGBA8(90, 102, 112, 255);
		default: return RGBA8(0, 0, 0, 255);
	}
}

static unsigned subtitle_background_color(void) {
	switch (vt_preferences_subtitle_background_color()) {
		case VT_SUBTITLE_BACKGROUND_BLACK: return RGBA8(0, 0, 0, 198);
		case VT_SUBTITLE_BACKGROUND_MIDNIGHT: return RGBA8(3, 18, 34, 210);
		case VT_SUBTITLE_BACKGROUND_WHITE: return RGBA8(255, 255, 255, 205);
		case VT_SUBTITLE_BACKGROUND_NAVY: return RGBA8(3, 26, 52, 214);
		case VT_SUBTITLE_BACKGROUND_BURGUNDY: return RGBA8(50, 5, 18, 214);
		case VT_SUBTITLE_BACKGROUND_FOREST: return RGBA8(4, 38, 28, 214);
		case VT_SUBTITLE_BACKGROUND_SMOKE: return RGBA8(38, 45, 50, 205);
		default: return 0;
	}
}

static int subtitle_max_width(void) {
	switch (vt_preferences_subtitle_max_width()) {
		case VT_SUBTITLE_WIDTH_60: return SCREEN_WIDTH * 60 / 100;
		case VT_SUBTITLE_WIDTH_75: return SCREEN_WIDTH * 75 / 100;
		case VT_SUBTITLE_WIDTH_96: return SCREEN_WIDTH * 96 / 100;
		default: return SCREEN_WIDTH * 88 / 100;
	}
}

static unsigned subtitle_font_size(void) {
	return vt_preferences_subtitle_size() == VT_SUBTITLE_SIZE_SMALL
	     ? UI_FONT_SMALL
	     : vt_preferences_subtitle_size() == VT_SUBTITLE_SIZE_LARGE
	     ? UI_FONT_SUBTITLE_LARGE
	     : vt_preferences_subtitle_size() == VT_SUBTITLE_SIZE_EXTRA_LARGE
	     ? UI_FONT_SUBTITLE_EXTRA_LARGE : UI_FONT_BODY;
}

static size_t subtitle_utf8_bytes(const char *text) {
	const unsigned char *s = (const unsigned char *)text;
	if (!s || !s[0]) return 0;
	if (s[0] < 0x80U) return 1;
	size_t bytes = s[0] >= 0xC2U && s[0] <= 0xDFU ? 2
	             : s[0] >= 0xE0U && s[0] <= 0xEFU ? 3
	             : s[0] >= 0xF0U && s[0] <= 0xF4U ? 4 : 1;
	for (size_t i = 1; i < bytes; i++)
		if (!s[i] || (s[i] & 0xC0U) != 0x80U) return 1;
	return bytes;
}

static size_t subtitle_trim_last_utf8(char *text, size_t length) {
	if (!text || !length) return 0;
	length--;
	while (length && ((unsigned char)text[length] & 0xC0U) == 0x80U)
		length--;
	text[length] = '\0';
	return length;
}

static void subtitle_add_ellipsis(vita2d_font *font, unsigned size,
	                              char line[256], int max_width) {
	static const char ellipsis[] = "\xE2\x80\xA6";
	size_t length = strlen(line);
	char candidate[256];
	for (;;) {
		snprintf(candidate, sizeof(candidate), "%s%s", line, ellipsis);
		if (!line[0] || ui_font_text_width(font, size, candidate) <= max_width)
			break;
		length = subtitle_trim_last_utf8(line, length);
		while (length && line[length - 1] == ' ')
			line[--length] = '\0';
	}
	snprintf(line, 256, "%s", candidate);
}

static int wrap_subtitle(vita2d_font *font, unsigned size, const char *text,
	                     int max_width, int max_lines, char lines[4][256]) {
	if (max_lines < 1) max_lines = 1;
	if (max_lines > 4) max_lines = 4;
	const char *cursor = text ? text : "";
	int count = 0;
	while (*cursor && count < max_lines) {
		while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r') cursor++;
		if (*cursor == '\n') { cursor++; continue; }
		char *line = lines[count];
		size_t used = 0;
		size_t last_break = 0;
		const char *after_break = NULL;
		while (*cursor) {
			if (*cursor == '\r') { cursor++; continue; }
			if (*cursor == '\n') { cursor++; break; }
			size_t bytes = subtitle_utf8_bytes(cursor);
			if (!bytes) break;
			int is_space = *cursor == ' ' || *cursor == '\t';
			if (is_space && used == 0) { cursor += bytes; continue; }
			size_t before = used;
			if (is_space) {
				if (used + 1 >= 256) break;
				line[used++] = ' ';
				last_break = before;
				after_break = cursor + bytes;
			} else {
				if (used + bytes >= 256) break;
				memcpy(line + used, cursor, bytes);
				used += bytes;
			}
			line[used] = '\0';
			if (ui_font_text_width(font, size, line) > max_width && before > 0) {
				if (after_break && last_break > 0) {
					used = last_break;
					cursor = after_break;
				} else {
					used = before;
				}
				line[used] = '\0';
				break;
			}
			cursor += bytes;
		}
		while (used && line[used - 1] == ' ') line[--used] = '\0';
		if (used) count++;
	}
	while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
	       *cursor == '\n') cursor++;
	if (*cursor && count > 0) subtitle_add_ellipsis(font, size,
	                                               lines[count - 1], max_width);
	return count;
}

static void draw_subtitle_text(const char *text) {
	if (!text || !text[0]) return;
	unsigned size = subtitle_font_size();
	vita2d_font *font = ui_runtime_subtitle_font(
	    vt_preferences_subtitle_font(), size);
	char lines[4][256];
	memset(lines, 0, sizeof(lines));
	int max_width = subtitle_max_width();
	int count = wrap_subtitle(font, size, text, max_width,
	                          vt_preferences_subtitle_max_rows(), lines);
	if (!count) return;
	int line_height = (int)size + 7;
	int anchor;
	switch (vt_preferences_subtitle_position()) {
		case VT_SUBTITLE_POSITION_LOW:
			anchor = SCREEN_HEIGHT - 92;
			break;
		case VT_SUBTITLE_POSITION_CENTER:
			anchor = SCREEN_HEIGHT / 2 + (count - 1) * line_height / 2;
			break;
		case VT_SUBTITLE_POSITION_TOP:
			anchor = 28 + (count - 1) * line_height;
			break;
		default:
			anchor = SCREEN_HEIGHT - 22;
			break;
	}
	int y = anchor - (count - 1) * line_height;
	int outline = vt_preferences_subtitle_outline_thickness();
	unsigned border = subtitle_border_color();
	unsigned foreground = subtitle_text_color();
	unsigned background = subtitle_background_color();
	char fitted[4][256];
	int widths[4] = {0};
	int widest = 0;
	for (int line = 0; line < count; line++) {
		ui_font_fit_text(font, size, lines[line], fitted[line],
		                 sizeof(fitted[line]), max_width);
		widths[line] = ui_font_text_width(font, size, fitted[line]);
		if (widths[line] > widest) widest = widths[line];
	}
	if (background) {
		int reserved = vt_preferences_subtitle_min_rows();
		if (reserved < count) reserved = count;
		int block_top = anchor - reserved * line_height + 7;
		vita2d_draw_rectangle((SCREEN_WIDTH - widest) / 2 - 12, block_top,
		                      widest + 24, reserved * line_height + 5,
		                      background);
	}
	for (int line = 0; line < count; line++, y += line_height) {
		int width = widths[line];
		int x = (SCREEN_WIDTH - width) / 2;
		for (int radius = 1; radius <= outline; radius++) {
			ui_font_draw_text(font, x - radius, y, border, size, fitted[line]);
			ui_font_draw_text(font, x + radius, y, border, size, fitted[line]);
			ui_font_draw_text(font, x, y - radius, border, size, fitted[line]);
			ui_font_draw_text(font, x, y + radius, border, size, fitted[line]);
			ui_font_draw_text(font, x - radius, y - radius, border, size, fitted[line]);
			ui_font_draw_text(font, x + radius, y - radius, border, size, fitted[line]);
			ui_font_draw_text(font, x - radius, y + radius, border, size, fitted[line]);
			ui_font_draw_text(font, x + radius, y + radius, border, size, fitted[line]);
		}
		ui_font_draw_text(font, x, y, foreground, size, fitted[line]);
	}
}

static void draw_player_right_sidebar(float animation, float focus_position,
	                                  int cursor,
	                                  const VtDecoderPlayer *player,
	                                  const VtDecoderPlayerStatus *status,
	                                  int resume_available,
	                                  int pending_audio_track,
	                                  int pending_subtitle_track) {
	if (animation <= 0.01f || !status) return;
	const float width = RIGHT_PANEL_WIDTH;
	float x = SCREEN_WIDTH - width * animation;
	vita2d_draw_rectangle(x, 0, width, SCREEN_HEIGHT, VT_THEME_BG_SOFT);
	vita2d_draw_rectangle(x, 0, 3, SCREEN_HEIGHT, VT_THEME_SPECTRAL);
	vita2d_draw_rectangle(x + 4, 0, 1, SCREEN_HEIGHT, VT_THEME_SIGNAL_BRIGHT);
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
	const char *labels[5] = {
		vt_i18n_str(VT_STR_PLAYER_FILL_SCREEN),
		vt_i18n_str(VT_STR_PLAYER_LOOP),
		vt_i18n_str(VT_STR_PLAYER_AUDIO_TRACK),
		vt_i18n_str(VT_STR_PLAYER_SUBTITLE_TRACK),
		vt_i18n_str(VT_STR_PLAYER_RESTART_FROM_BEGINNING)
	};
	char audio_value[128], subtitle_value[128];
	format_track_value(player, 0, pending_audio_track, audio_value);
	format_track_value(player, 1, pending_subtitle_track, subtitle_value);
	VtDecoderSubtitleState subtitle_state = vt_decoder_subtitle_state(player);
	int subtitle_request_visible = pending_subtitle_track > 0 &&
	    pending_subtitle_track == vt_decoder_active_subtitle_track(player);
	if (subtitle_request_visible &&
	    subtitle_state == VT_DECODER_SUBTITLE_PENDING)
		snprintf(subtitle_value, sizeof(subtitle_value), "%s",
		         vt_i18n_str(VT_STR_PLAYER_SUBTITLE_PENDING));
	else if (subtitle_request_visible &&
	         subtitle_state == VT_DECODER_SUBTITLE_FAILED)
		snprintf(subtitle_value, sizeof(subtitle_value), "%s",
		         vt_i18n_str(VT_STR_PLAYER_SUBTITLE_FAILED));
	const char *values[5] = {
		vt_i18n_str(vt_preferences_fill_screen() ? VT_STR_PLAYER_ON
		                                             : VT_STR_PLAYER_OFF),
		vt_i18n_str(vt_preferences_loop_enabled() ? VT_STR_PLAYER_ON
		                                             : VT_STR_PLAYER_OFF),
		audio_value, subtitle_value, "00:00"
	};
	int row_count = resume_available ? 5 : 4;
	for (int i = 0; i < row_count; i++) {
		float y = RIGHT_PANEL_ROW_Y + i * RIGHT_PANEL_ROW_STEP;
		ui_panel(x + 18, y, width - 36, 56, VT_THEME_SURFACE,
		         VT_THEME_BORDER_DIM, 0);
	}
	float focus_y = RIGHT_PANEL_ROW_Y + focus_position * RIGHT_PANEL_ROW_STEP;
	vita2d_draw_rectangle(x + 14, focus_y - 4, width - 28, 64,
	                      VT_THEME_HALO_A(76));
	ui_panel(x + 18, focus_y, width - 36, 56,
	         VT_THEME_SURFACE_FOCUS, VT_THEME_SIGNAL_LIGHT, 0);
	for (int i = 0; i < row_count; i++) {
		float y = RIGHT_PANEL_ROW_Y + i * RIGHT_PANEL_ROW_STEP;
		if (small) {
			ui_font_draw_text(small, (int)x + 38, (int)y + 35,
			                  i == cursor ? VT_THEME_TEXT : VT_THEME_TEXT_MUTED,
			                  UI_FONT_SMALL, labels[i]);
			const char *value = values[i];
			int value_w = ui_font_text_width(small, UI_FONT_SMALL, value);
			if (value_w > 118) {
				static char fitted_values[5][96];
				ui_font_fit_text(small, UI_FONT_SMALL, value, fitted_values[i],
				                 sizeof(fitted_values[i]), 118);
				value = fitted_values[i];
				value_w = ui_font_text_width(small, UI_FONT_SMALL, value);
			}
			unsigned value_color =
			    (i < 2 && !strcmp(value, vt_i18n_str(VT_STR_PLAYER_OFF))) ||
			    (i == 3 && pending_subtitle_track == 0)
			        ? VT_THEME_TEXT_MUTED
			        : i == 4 ? VT_THEME_COLD_LIGHT : VT_THEME_BLUE_LIGHT;
			if (i == 3 && subtitle_request_visible &&
			    subtitle_state == VT_DECODER_SUBTITLE_PENDING)
				value_color = VT_THEME_SPECTRAL_LIGHT;
			else if (i == 3 && subtitle_request_visible &&
			         subtitle_state == VT_DECODER_SUBTITLE_FAILED)
				value_color = VT_THEME_DANGER;
			ui_font_draw_text(small, (int)x + (int)width - value_w - 34,
			                  (int)y + 35,
			                  value_color,
			                  UI_FONT_SMALL, value);
		}
	}
	int divider_y = resume_available ? 426 : 372;
	vita2d_draw_rectangle(x + 18, divider_y, width - 36, 1, VT_THEME_BORDER);
	if (small) {
		char bitrate[32], decoder[128];
		format_video_bitrate(status->video_bitrate_bps, bitrate);
		snprintf(decoder, sizeof(decoder), vt_i18n_str(VT_STR_PLAYER_DECODER_INFO),
		         status->hardware_accelerated ? "HW" : "SW",
		         status->height, status->fps, bitrate);
		ui_font_draw_text(small, (int)x + 28,
		                  resume_available ? 462 : 410, VT_THEME_TEXT,
		                  UI_FONT_SMALL, decoder);
		ui_font_draw_text(small, (int)x + 28, 518, VT_THEME_TEXT_MUTED,
		                  UI_FONT_SMALL, vt_i18n_str(VT_STR_PLAYER_PANEL_HINT));
	}
}

int vt_hw_player_screen_run(const VtHwPlayerScreenSource *source,
	                        uint64_t *last_position_ms,
	                        uint64_t *last_duration_ms,
	                        int *last_audio_track,
	                        int *last_subtitle_track) {
	if (!source || (!source->stream.open && !source->stream.open_cancelable))
		return -1;
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
	open.config.external_subtitle_count = source->external_subtitle_count;
	open.config.external_subtitles_replace_embedded =
	    source->external_subtitles_replace_embedded;
	if (open.config.external_subtitle_count < 0)
		open.config.external_subtitle_count = 0;
	if (open.config.external_subtitle_count > VT_DECODER_MAX_SUBTITLE_TRACKS)
		open.config.external_subtitle_count = VT_DECODER_MAX_SUBTITLE_TRACKS;
	memcpy(open.config.external_subtitles, source->external_subtitles,
	       (size_t)open.config.external_subtitle_count *
	           sizeof(open.config.external_subtitles[0]));
	int decoder_preference = vt_preferences_video_decoder();
	open.config.preferred_backend =
	    decoder_preference == VT_VIDEO_DECODER_HW_H264
	        ? VT_DECODER_BACKEND_HARDWARE
	    : decoder_preference == VT_VIDEO_DECODER_SW_FFMPEG
	        ? VT_DECODER_BACKEND_SOFTWARE
	        : VT_DECODER_BACKEND_NONE;
	open.config.expected_width = source->expected_width;
	open.config.expected_height = source->expected_height;
	open.config.expected_fps = source->expected_fps;
	open.config.start_position_ms = source->start_position_ms;
	open.config.audio_track = source->start_audio_track;
	open.config.subtitle_track = source->start_subtitle_track;
	open.config.volume_percent = vt_audio_volume_percent();
	open.config.cancel_flag = &cancel;
	const char *opening_status = source->authenticated_remote
	                           ? vt_i18n_str(VT_STR_PLAYER_OPEN_REMOTE)
	                           : vt_i18n_str(VT_STR_PLAYER_OPEN_LOCAL);
	UiPlayerLoadingInfo loading = {
		.title = source->title,
		.channel = source->location,
		.status = opening_status,
		.quality_height = source->expected_height,
		.cancel_action = cancel_player_operation,
		.cancel_ctx = player
	};
	volatile int start_over_requested = 0;
	if (open.config.start_position_ms > 0)
		loading.start_over_requested = &start_over_requested;
	int ret = ui_player_loading_run(&loading, open_task, &open, &cancel, NULL, NULL);
	if (start_over_requested) {
		/* The first worker exits through the decoder interrupt callback. Reuse the
		 * source only after it has joined, then perform a clean open at zero with a
		 * fresh player so no cancelled backend survives into the retry. */
		vt_decoder_destroy(player);
		player = vt_decoder_create();
		if (!player) {
			vt_performance_end_video(&clock_guard);
			return -1;
		}
		open.player = player;
		loading.cancel_ctx = player;
		cancel = 0;
		open.config.start_position_ms = 0;
		loading.start_over_requested = NULL;
		ret = ui_player_loading_run(&loading, open_task, &open, &cancel, NULL, NULL);
	}
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
	PlayerInputLock input_lock;
	memset(&input_lock, 0, sizeof(input_lock));
	input_lock.armed = (previous.buttons & SCE_CTRL_SELECT) == 0;
	PlayerPowerSaveInput power_save_input;
	memset(&power_save_input, 0, sizeof(power_save_input));
	power_save_input.armed = (previous.buttons & SCE_CTRL_START) == 0;
	int energy_saving = 0;
	int energy_lock_position = 0;
	uint64_t energy_lock_move_us = 0;
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
	int pending_audio_track = vt_decoder_active_audio_track(player);
	int pending_subtitle_track = vt_decoder_active_subtitle_track(player);
	int resume_available = open.config.start_position_ms > 0;
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
		int lock_changed = player_input_lock_update(&input_lock, controls.buttons,
		                                            now);
		pressed &= ~SCE_CTRL_SELECT;
		if (lock_changed) {
			dragging = 0;
			sidebar.open = 0;
			right_open = 0;
			hud_visible = 1;
			hud_deadline = now + 3500000ULL;
			ui_touch_reset();
		}
		if (input_lock.locked) {
			pressed = 0;
			touch_flags = UI_TOUCH_EVENT_NONE;
			dragging = 0;
		}
		int power_event = player_power_save_update(
		    &power_save_input, controls.buttons, now, !input_lock.locked);
		pressed &= ~SCE_CTRL_START;
		if (power_event == PLAYER_POWER_SAVE_EVENT_TOGGLE) {
			energy_saving = !energy_saving;
			energy_lock_position = 0;
			energy_lock_move_us = now + PLAYER_POWER_SAVE_LOCK_MOVE_US;
			dragging = 0;
			sidebar.open = 0;
			right_open = 0;
			ui_touch_reset();
		}
		if (power_event == PLAYER_POWER_SAVE_EVENT_SHORT_PRESS &&
		    source->allow_minimize) {
			ret = VT_HW_PLAYER_ACTION_MINIMIZE;
			break;
		}
		if (energy_saving) {
			pressed &= SCE_CTRL_CIRCLE;
			touch_flags = UI_TOUCH_EVENT_NONE;
			dragging = 0;
		}
		VtDecoderPlayerStatus status;
		vt_decoder_get_status(player, &status);
		if (status.error) {
			if (decoder_preference == VT_VIDEO_DECODER_AUTO &&
			    vt_decoder_backend(player) == VT_DECODER_BACKEND_HARDWARE) {
				SeekTask task = { player, status.position_ms };
				cancel = 0;
				loading.status = vt_i18n_str(VT_STR_PLAYER_FALLBACK_SOFTWARE);
				fence_decoder_frame(player);
				ret = ui_player_loading_run(&loading, fallback_task, &task,
				                            &cancel, NULL, NULL);
				loading.status = opening_status;
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
			pending_audio_track = vt_decoder_active_audio_track(player);
			pending_subtitle_track = vt_decoder_active_subtitle_track(player);
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
			int right_row_count = resume_available ? 5 : 4;
			if ((right_navigation & SCE_CTRL_DOWN) &&
			    right_cursor + 1 < right_row_count) right_cursor++;
			if (right_cursor == 2 &&
			    (pressed & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT))) {
				int count = vt_decoder_audio_track_count(player);
				if (count > 1) {
					int direction = (pressed & SCE_CTRL_LEFT) ? -1 : 1;
					pending_audio_track =
					    (pending_audio_track + (direction < 0 ? count - 1 : 1)) % count;
				}
			} else if (right_cursor == 3 &&
			           (pressed & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT))) {
				int choices = vt_decoder_subtitle_track_count(player) + 1;
				if (choices > 1) {
					int direction = (pressed & SCE_CTRL_LEFT) ? -1 : 1;
					pending_subtitle_track =
					    (pending_subtitle_track + (direction < 0 ? choices - 1 : 1)) % choices;
				}
			}
			if (pressed & SCE_CTRL_CROSS) {
				if (right_cursor == 0)
					vt_preferences_set_fill_screen(!vt_preferences_fill_screen());
				else if (right_cursor == 1)
					vt_preferences_set_loop_enabled(!vt_preferences_loop_enabled());
				else if (right_cursor < 4) {
					int selected_track = right_cursor == 3
					                   ? pending_subtitle_track : pending_audio_track;
					ret = run_track_change(player, &loading, &cancel,
					                       status.position_ms,
					                       right_cursor == 3, selected_track);
					loading.status = opening_status;
					if (right_cursor == 2)
						pending_audio_track =
						    vt_decoder_active_audio_track(player);
					if (ret < 0) {
						if (right_cursor == 3) {
							pending_subtitle_track =
							    vt_decoder_active_subtitle_track(player);
							ret = 0;
						} else break;
					}
					now = resync_after_blocking_action(
					    &controls, &previous, &left_seek_direction,
					    &left_seek_repeat_at);
					hud_deadline = now + 3500000ULL;
					continue;
				} else {
					ret = run_seek(player, &loading, &cancel, 0);
					loading.status = opening_status;
					if (ret < 0) break;
					if (source->history_id)
						vt_playback_history_update(source->history_id, 0,
						                           status.duration_ms);
					paused = 0;
					vt_decoder_set_paused(player, 0);
					resume_available = 0;
					right_open = 0;
					right_cursor = 0;
					right_focus_position = 0.0f;
					now = resync_after_blocking_action(
					    &controls, &previous, &left_seek_direction,
					    &left_seek_repeat_at);
					hud_deadline = now + 3500000ULL;
					continue;
				}
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
			int track_change_failed = 0;
			int track_change_completed = 0;
			int right_row_count = resume_available ? 5 : 4;
			for (int i = 0; i < right_row_count; i++) {
				if (!ui_touch_hit_rect(touch.x, touch.y, panel_x + 18,
				                       (int)(RIGHT_PANEL_ROW_Y + i * RIGHT_PANEL_ROW_STEP),
				                       (int)RIGHT_PANEL_WIDTH - 36, 56)) continue;
				right_cursor = i;
				handled = 1;
				if (i == 0)
					vt_preferences_set_fill_screen(!vt_preferences_fill_screen());
				else if (i == 1)
					vt_preferences_set_loop_enabled(!vt_preferences_loop_enabled());
				else if (i < 4) {
					int count = i == 3
					          ? vt_decoder_subtitle_track_count(player) + 1
					          : vt_decoder_audio_track_count(player);
					int *pending = i == 3 ? &pending_subtitle_track
					                      : &pending_audio_track;
					if (count > 1) *pending = (*pending + 1) % count;
					ret = run_track_change(player, &loading, &cancel,
					                       status.position_ms, i == 3, *pending);
					loading.status = opening_status;
					if (i == 2)
						pending_audio_track =
						    vt_decoder_active_audio_track(player);
					if (ret < 0 && i == 3) {
						pending_subtitle_track =
						    vt_decoder_active_subtitle_track(player);
						ret = 0;
					}
					track_change_failed = ret < 0;
					track_change_completed = ret >= 0;
				} else {
					ret = run_seek(player, &loading, &cancel, 0);
					loading.status = opening_status;
					track_change_failed = ret < 0;
					track_change_completed = ret >= 0;
					if (ret >= 0) {
						if (source->history_id)
							vt_playback_history_update(source->history_id, 0,
							                           status.duration_ms);
						paused = 0;
						vt_decoder_set_paused(player, 0);
						resume_available = 0;
						right_open = 0;
						right_cursor = 0;
						right_focus_position = 0.0f;
					}
				}
			}
			if (track_change_failed) break;
			if (track_change_completed) {
				now = resync_after_blocking_action(
				    &controls, &previous, &left_seek_direction,
				    &left_seek_repeat_at);
				hud_deadline = now + 3500000ULL;
				continue;
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
		if (energy_saving)
			vt_decoder_discard_video_to_clock(player);
		else
			vt_decoder_present(player, vt_preferences_fill_screen());
		vt_decoder_get_status(player, &status);
		if (energy_saving) {
			if (now >= energy_lock_move_us) {
				energy_lock_position =
				    (energy_lock_position + 1) % PLAYER_POWER_SAVE_LOCK_POSITIONS;
				energy_lock_move_us = now + PLAYER_POWER_SAVE_LOCK_MOVE_US;
			}
			vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT,
			                      RGBA8(0, 0, 0, 255));
			int lock_x, lock_y;
			player_power_save_lock_position(energy_lock_position,
			                                &lock_x, &lock_y);
			draw_player_power_save_status_at(lock_x, lock_y);
			if (input_lock.locked)
				draw_player_input_lock_at(lock_x,
				    lock_y > 430 ? lock_y - 50 : lock_y + 34, 1.0f);
		} else {
			if (hud_opacity > 0.01f || volume_opacity > 0.01f)
				draw_hud(source, &status, paused, vt_audio_volume_percent(),
				         hud_opacity, volume_opacity, dragging, drag_fraction);
			else if (vt_preferences_player_status_always_visible())
				ui_brand_draw_status_indicators_alpha(1.0f);
			draw_buffering_overlay(buffering_opacity, now);
			/* Subtitles must remain above the HUD's opaque lower telemetry panel.
			 * Drawing them before draw_hud made a correctly decoded bottom-position
			 * cue look absent for the entire time the controls were visible. */
			char subtitle[512];
			if (vt_decoder_subtitle_text(player, status.position_ms,
			                             subtitle, sizeof(subtitle)) > 0)
				draw_subtitle_text(subtitle);
			if (input_lock.locked && hud_opacity > 0.01f)
				draw_player_input_lock_at(24, 86, hud_opacity);
			if (sidebar.animation > 0.01f)
				ui_sections_sidebar_draw(&sidebar);
			draw_player_right_sidebar(right_animation, right_focus_position,
			                          right_cursor, player, &status,
			                          resume_available, pending_audio_track,
			                          pending_subtitle_track);
		}
		vita2d_end_drawing();
		vita2d_wait_rendering_done();
		vt_decoder_render_complete(player);
		vita2d_swap_buffers();
		sceKernelDelayThread(1000);
	}
	VtDecoderPlayerStatus final_status;
	vt_decoder_get_status(player, &final_status);
	if (last_position_ms) *last_position_ms = final_status.position_ms;
	if (last_duration_ms) *last_duration_ms = final_status.duration_ms;
	if (last_audio_track) *last_audio_track = vt_decoder_active_audio_track(player);
	if (last_subtitle_track)
		*last_subtitle_track = vt_decoder_active_subtitle_track(player);
	vt_decoder_destroy(player);
	vt_performance_end_video(&clock_guard);
	if (ret < 0 && cancel) ret = 0;
	return ret;
}
