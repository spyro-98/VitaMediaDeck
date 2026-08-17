#include "media/hw_player_screen.h"

#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "media/audio_volume.h"
#include "settings/preferences.h"
#include "system/display_awake.h"
#include "system/performance.h"
#include "ui/brand.h"
#include "ui/loading_screen.h"
#include "ui/runtime.h"
#include "ui/theme.h"
#include "ui/touch.h"

#define SCREEN_WIDTH 960
#define SCREEN_HEIGHT 544
#define TIMELINE_X 48
#define TIMELINE_Y 509
#define TIMELINE_W 864
#define STICK_CENTER 128
#define STICK_DEADZONE 42

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

static void draw_text_clipped(vita2d_font *font, int x, int y, int width,
	                          unsigned size, unsigned color, const char *text) {
	if (!font || !text) return;
	char buffer[192];
	snprintf(buffer, sizeof(buffer), "%s", text);
	while (buffer[0] && ui_font_text_width(font, size, buffer) > width) {
		size_t length = strlen(buffer);
		if (!length) break;
		do { buffer[--length] = '\0'; }
		while (length && ((unsigned char)buffer[length] & 0xc0) == 0x80);
	}
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

static void draw_hud(const VtHwPlayerScreenSource *source,
	                 const VtDecoderPlayerStatus *status, int paused,
	                 int volume, int volume_only, uint64_t now) {
	(void)now;
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (!volume_only) {
		vita2d_draw_rectangle(0, 0, SCREEN_WIDTH, 82, RGBA8(0, 0, 0, 190));
		draw_text_clipped(body, 30, 43, 720, UI_FONT_BODY, VT_THEME_TEXT,
		                  source->title ? source->title : "Video");
		draw_text_clipped(small, 30, 69, 720, UI_FONT_SMALL,
		                  VT_THEME_TEXT_MUTED,
		                  source->location ? source->location : "");
		ui_brand_draw_status_indicators_alpha(1.0f);
		vita2d_draw_rectangle(0, 420, SCREEN_WIDTH, 124, RGBA8(0, 0, 0, 175));
		char decoder[16];
		snprintf(decoder, sizeof(decoder), "%s DEC",
		         status->hardware_accelerated ? "HW" : "SW");
		unsigned border = status->hardware_accelerated
		                ? RGBA8(84, 229, 132, 255) : RGBA8(70, 219, 255, 255);
		vita2d_draw_rectangle(48, 434, 92, 31, RGBA8(0, 0, 0, 180));
		vita2d_draw_rectangle(48, 434, 92, 2, border);
		vita2d_draw_rectangle(48, 463, 92, 2, border);
		if (small) ui_font_draw_text(small, 60, 457, VT_THEME_TEXT,
		                             UI_FONT_SMALL, decoder);
		char quality[48];
		snprintf(quality, sizeof(quality), "%up  %d fps",
		         status->height, status->fps);
		if (small) ui_font_draw_text(small, 154, 457, VT_THEME_TEXT,
		                             UI_FONT_SMALL, quality);
		vita2d_draw_fill_circle(480, 460, 29, VT_THEME_SURFACE_FOCUS);
		if (paused) draw_play_icon(470, 444, 32, VT_THEME_TEXT);
		else {
			vita2d_draw_rectangle(470, 444, 8, 32, VT_THEME_TEXT);
			vita2d_draw_rectangle(485, 444, 8, 32, VT_THEME_TEXT);
		}
		float progress = status->duration_ms
		               ? (float)status->position_ms / (float)status->duration_ms : 0.0f;
		if (progress < 0.0f) progress = 0.0f;
		if (progress > 1.0f) progress = 1.0f;
		vita2d_draw_rectangle(TIMELINE_X, TIMELINE_Y, TIMELINE_W, 5,
		                      RGBA8(96, 125, 160, 120));
		vita2d_draw_rectangle(TIMELINE_X, TIMELINE_Y, TIMELINE_W * progress, 5,
		                      VT_THEME_BLUE_BRIGHT);
		vita2d_draw_fill_circle(TIMELINE_X + TIMELINE_W * progress,
		                        TIMELINE_Y + 2.5f, 6, VT_THEME_TEXT);
		char current[24], duration[24];
		format_time(status->position_ms, current);
		format_time(status->duration_ms, duration);
		if (small) {
			ui_font_draw_text(small, TIMELINE_X, 501, VT_THEME_TEXT,
			                  UI_FONT_SMALL, current);
			int width = ui_font_text_width(small, UI_FONT_SMALL, duration);
			ui_font_draw_text(small, TIMELINE_X + TIMELINE_W - width, 501,
			                  VT_THEME_TEXT, UI_FONT_SMALL, duration);
		}
	}
	/* Volume is intentionally above the timeline, never over the duration. */
	int x = 824, y = volume_only ? 392 : 436;
	vita2d_draw_rectangle(x, y, 96, 34, RGBA8(0, 0, 0, 200));
	if (small) ui_font_draw_textf(small, x + 14, y + 24, VT_THEME_TEXT,
	                             UI_FONT_SMALL, "%d%%", volume);
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
		        ? "Opening authenticated stream" : "Opening local media",
		.quality_height = source->expected_height
	};
	int ret = ui_player_loading_run(&loading, open_task, &open, &cancel, NULL, NULL);
	if (ret < 0) {
		vt_decoder_destroy(player);
		vt_performance_end_video(&clock_guard);
		return ret;
	}
	SceCtrlData controls, previous;
	memset(&controls, 0, sizeof(controls));
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	sceCtrlPeekBufferPositive(0, &previous, 1);
	ui_touch_reset();
	int paused = 0;
	int hud_visible = 1;
	int dragging = 0;
	uint64_t hud_deadline = sceKernelGetProcessTimeWide() + 3500000ULL;
	uint64_t volume_deadline = 0;
	uint64_t last_volume_input = 0;
	for (;;) {
		uint64_t now = sceKernelGetProcessTimeWide();
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
				loading.status = "Hardware unavailable - opening software decoder";
				ret = ui_player_loading_run(&loading, fallback_task, &task,
				                            &cancel, NULL, NULL);
				loading.status = source->authenticated_remote
				               ? "Opening authenticated stream" : "Opening local media";
				if (ret >= 0) {
					paused = 0;
					hud_visible = 1;
					hud_deadline = now + 3500000ULL;
					continue;
				}
			}
			ret = -1;
			break;
		}
		if (status.eof) {
			if (!vt_preferences_loop_enabled()) { ret = 0; break; }
			SeekTask task = { player, 0 };
			cancel = 0;
			ret = ui_player_loading_run(&loading, seek_task, &task, &cancel,
			                            NULL, NULL);
			if (ret < 0) break;
			paused = 0;
			vt_decoder_set_paused(player, 0);
			hud_visible = 1;
			hud_deadline = now + 3500000ULL;
			continue;
		}
		if (pressed & SCE_CTRL_CIRCLE) { ret = 0; break; }
		if (pressed & SCE_CTRL_CROSS) {
			paused = !paused;
			vt_decoder_set_paused(player, paused);
			hud_visible = 1;
			hud_deadline = now + 3500000ULL;
		}
		int seek_direction = (pressed & (SCE_CTRL_RIGHT | SCE_CTRL_RTRIGGER)) ? 1
		                   : (pressed & (SCE_CTRL_LEFT | SCE_CTRL_LTRIGGER)) ? -1 : 0;
		if (seek_direction && status.duration_ms) {
			int64_t target = (int64_t)status.position_ms + seek_direction * 10000LL;
			if (target < 0) target = 0;
			if ((uint64_t)target > status.duration_ms) target = status.duration_ms;
			SeekTask task = { player, (uint64_t)target };
			cancel = 0;
			ret = ui_player_loading_run(&loading, seek_task, &task, &cancel, NULL, NULL);
			if (ret < 0) break;
			hud_visible = 1;
			hud_deadline = now + 3500000ULL;
		}
		int right_y = (int)controls.ry - STICK_CENTER;
		if ((right_y > STICK_DEADZONE || right_y < -STICK_DEADZONE) &&
		    now - last_volume_input >= 80000ULL) {
			int volume = vt_audio_volume_percent() + (right_y < 0 ? 5 : -5);
			if (volume < 0) volume = 0;
			if (volume > 300) volume = 300;
			vt_audio_volume_set_percent(volume);
			vt_decoder_set_volume(player, volume);
			last_volume_input = now;
			volume_deadline = now + 1200000ULL;
		}
		if ((touch_flags & UI_TOUCH_EVENT_DOWN) &&
		    ui_touch_hit_rect(touch.x, touch.y, TIMELINE_X, TIMELINE_Y - 22,
		                      TIMELINE_W, 46)) dragging = 1;
		if (dragging && (touch_flags & (UI_TOUCH_EVENT_MOVE | UI_TOUCH_EVENT_UP))) {
			if (touch_flags & UI_TOUCH_EVENT_UP) {
				float fraction = (float)(touch.x - TIMELINE_X) / (float)TIMELINE_W;
				if (fraction < 0) fraction = 0;
				if (fraction > 1) fraction = 1;
				SeekTask task = { player, (uint64_t)(fraction * status.duration_ms) };
				cancel = 0;
				ret = ui_player_loading_run(&loading, seek_task, &task, &cancel, NULL, NULL);
				dragging = 0;
				if (ret < 0) break;
			}
		} else if ((touch_flags & UI_TOUCH_EVENT_TAP) && touch.y < 410) {
			hud_visible = !hud_visible;
			hud_deadline = now + 3500000ULL;
		}
		if (!paused && now > hud_deadline) hud_visible = 0;
		vt_display_keep_awake_tick();
		vita2d_start_drawing();
		vita2d_clear_screen();
		vt_decoder_present(player, vt_preferences_fill_screen());
		vt_decoder_get_status(player, &status);
		if (hud_visible || now < volume_deadline)
			draw_hud(source, &status, paused, vt_audio_volume_percent(),
			         !hud_visible, now);
		else if (vt_preferences_player_status_always_visible())
			ui_brand_draw_status_indicators_alpha(1.0f);
		if (status.ready_frames == 0 && !paused)
			ui_draw_spinner(480, 272, now);
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
	return ret;
}
