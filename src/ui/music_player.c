#include "ui/music_player.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "media/background_playback.h"
#include "media/player_input_lock.h"
#include "media/player_power_save.h"
#include "media/audio_volume.h"
#include "i18n/i18n.h"
#include "settings/preferences.h"
#include "system/display_awake.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/loading_screen.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/theme.h"
#include "ui/touch.h"

#define MUSIC_TIMELINE_X 112
#define MUSIC_TIMELINE_Y 432
#define MUSIC_TIMELINE_W 736
#define MUSIC_CONTROLS_Y 478
#define MUSIC_CONTROLS_H 62
#define MUSIC_TEXT_LEFT 72
#define MUSIC_TEXT_RIGHT 888
#define MUSIC_VOLUME_INTERVAL_US (80 * 1000ULL)
#define MUSIC_VOLUME_VISIBLE_US (1200 * 1000ULL)
#define MUSIC_STICK_CENTER 128
#define MUSIC_STICK_DEADZONE 34
#define MUSIC_VOLUME_STEP 5
#define MUSIC_VOLUME_MAX 300

static int g_shuffle;
static int g_repeat_one;

static void draw_music_sidebar_bitrate(float animation,
	                                   uint32_t bitrate_kbps) {
	if (animation <= 0.01f || bitrate_kbps == 0) return;
	float ox = -286.0f * (1.0f - animation);
	vita2d_draw_rectangle(ox + 18.0f, 482.0f, 250.0f, 42.0f,
	                      RGBA8(12, 31, 54, 246));
	vita2d_draw_rectangle(ox + 18.0f, 482.0f, 3.0f, 42.0f,
	                      VT_THEME_BLUE_LIGHT);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (small)
		ui_font_draw_textf(small, (int)ox + 34, 509, VT_THEME_TEXT,
		                   UI_FONT_SMALL, "MP3  |  ~%u kbps", bitrate_kbps);
}

static void draw_cover(vita2d_texture *cover, float x, float y, float size) {
	if (!cover) return;
	float w = (float)vita2d_texture_get_width(cover);
	float h = (float)vita2d_texture_get_height(cover);
	float scale = size / (w > h ? w : h);
	vita2d_draw_texture_scale(cover, x + (size - w * scale) * .5f,
	                          y + (size - h * scale) * .5f, scale, scale);
}

static void time_text(uint64_t ms, char out[24]) {
	uint64_t seconds = ms / 1000ULL;
	snprintf(out, 24, "%llu:%02llu", (unsigned long long)(seconds / 60ULL),
	         (unsigned long long)(seconds % 60ULL));
}

static unsigned with_alpha(unsigned color, unsigned alpha) {
	return (color & 0x00ffffffU) | ((alpha & 0xffU) << 24);
}

typedef struct {
	uint32_t count;
	uint32_t red;
	uint32_t green;
	uint32_t blue;
} ArtworkColorBin;

static unsigned palette_color(unsigned red, unsigned green, unsigned blue) {
	/* Album covers are commonly mastered either very dark or almost grey. Give
	 * their chroma enough room to drive the ambient field, then keep luminance
	 * in a range where the final contrast veil does not erase it. */
	int average = ((int)red + (int)green + (int)blue) / 3;
	int r = average + ((int)red - average) * 3 / 2;
	int g = average + ((int)green - average) * 3 / 2;
	int b = average + ((int)blue - average) * 3 / 2;
	if (r < 0) r = 0;
	if (r > 255) r = 255;
	if (g < 0) g = 0;
	if (g > 255) g = 255;
	if (b < 0) b = 0;
	if (b > 255) b = 255;
	int peak = r > g ? (r > b ? r : b) : (g > b ? g : b);
	if (peak < 150 && peak > 0) {
		r = r * 150 / peak;
		g = g * 150 / peak;
		b = b * 150 / peak;
	}
	return RGBA8(r, g, b, 255);
}

static void artwork_palette(vita2d_texture *cover, unsigned colors[5]) {
	static const unsigned fallback[5] = {
		RGBA8(22, 55, 102, 255), RGBA8(35, 111, 196, 255),
		RGBA8(91, 184, 244, 255), RGBA8(203, 119, 222, 255),
		RGBA8(235, 177, 201, 255)
	};
	memcpy(colors, fallback, sizeof(fallback));
	if (!cover) return;
	unsigned width = vita2d_texture_get_width(cover);
	unsigned height = vita2d_texture_get_height(cover);
	unsigned stride_bytes = vita2d_texture_get_stride(cover);
	SceGxmTextureFormat format = vita2d_texture_get_format(cover);
	unsigned base_format = (unsigned)format & 0x9f000000U;
	unsigned bytes_per_pixel = base_format == SCE_GXM_TEXTURE_BASE_FORMAT_U8U8U8
	                         ? 3U
	                         : base_format == SCE_GXM_TEXTURE_BASE_FORMAT_U8U8U8U8
	                         ? 4U
	                         : base_format == SCE_GXM_TEXTURE_BASE_FORMAT_U8
	                         ? 1U : 0U;
	const unsigned char *pixels = vita2d_texture_get_datap(cover);
	if (!pixels || !bytes_per_pixel || width < 2 || height < 2 ||
	    stride_bytes < width * bytes_per_pixel) return;
	/* A handful of fixed pixels made the palette depend on whatever happened to
	 * sit at those coordinates (often a neutral face/background). A compact
	 * 8x8x8 histogram samples the whole cover and favours both population and
	 * saturation; subsequent picks are rewarded for distance from colours
	 * already chosen. This runs once when the player opens, never per frame. */
	ArtworkColorBin bins[512];
	memset(bins, 0, sizeof(bins));
	unsigned step_x = width / 32U; if (step_x < 1U) step_x = 1U;
	unsigned step_y = height / 32U; if (step_y < 1U) step_y = 1U;
	for (unsigned y = step_y / 2U; y < height; y += step_y) {
		for (unsigned x = step_x / 2U; x < width; x += step_x) {
			const unsigned char *sample = pixels + y * stride_bytes +
			                              x * bytes_per_pixel;
			unsigned r = sample[0];
			unsigned g = bytes_per_pixel > 1 ? sample[1] : sample[0];
			unsigned b = bytes_per_pixel > 2 ? sample[2] : sample[0];
			unsigned index = (r >> 5) * 64U + (g >> 5) * 8U + (b >> 5);
			bins[index].count++;
			bins[index].red += r;
			bins[index].green += g;
			bins[index].blue += b;
		}
	}
	unsigned chosen_rgb[5][3];
	memset(chosen_rgb, 0, sizeof(chosen_rgb));
	for (int pick = 0; pick < 5; pick++) {
		int best = -1;
		uint64_t best_score = 0;
		for (int index = 0; index < 512; index++) {
			if (!bins[index].count) continue;
			unsigned r = bins[index].red / bins[index].count;
			unsigned g = bins[index].green / bins[index].count;
			unsigned b = bins[index].blue / bins[index].count;
			unsigned maximum = r > g ? (r > b ? r : b) : (g > b ? g : b);
			unsigned minimum = r < g ? (r < b ? r : b) : (g < b ? g : b);
			unsigned saturation = maximum - minimum;
			uint64_t diversity = ~0ULL;
			for (int previous = 0; previous < pick; previous++) {
				int dr = (int)r - (int)chosen_rgb[previous][0];
				int dg = (int)g - (int)chosen_rgb[previous][1];
				int db = (int)b - (int)chosen_rgb[previous][2];
				uint64_t distance = (uint64_t)(dr * dr + dg * dg + db * db);
				if (distance < diversity) diversity = distance;
			}
			uint64_t score = (uint64_t)bins[index].count *
			                 (64ULL + saturation * 3ULL) *
			                 (pick ? 1024ULL + diversity : 1ULL);
			if (score > best_score) { best_score = score; best = index; }
		}
		if (best < 0) break;
		unsigned r = bins[best].red / bins[best].count;
		unsigned g = bins[best].green / bins[best].count;
		unsigned b = bins[best].blue / bins[best].count;
		chosen_rgb[pick][0] = r;
		chosen_rgb[pick][1] = g;
		chosen_rgb[pick][2] = b;
		colors[pick] = palette_color(r, g, b);
		bins[best].count = 0;
	}
}

static unsigned palette_foreground(const unsigned colors[5]) {
	unsigned red = 0, green = 0, blue = 0;
	for (int i = 0; i < 5; i++) {
		red += colors[i] & 0xffU;
		green += (colors[i] >> 8) & 0xffU;
		blue += (colors[i] >> 16) & 0xffU;
	}
	return ui_contrast_bw(RGBA8(red / 5U, green / 5U, blue / 5U, 255));
}

static void draw_flow_background(const unsigned colors[5], uint64_t now,
	                             unsigned foreground) {
	float t = vt_preferences_reduce_motion()
	        ? 0.0f : (float)(now % 90000000ULL) / 1000000.0f;
	vita2d_draw_rectangle(0, UI_BRAND_HEADER_HEIGHT, 960,
	                      544 - UI_BRAND_HEADER_HEIGHT, colors[0]);
	/* Five broad fields and six translucent rings are enough to read as a soft
	 * mesh gradient, while keeping overdraw modest on the Vita's 960x544 target. */
	for (int blob = 0; blob < 5; blob++) {
		float phase = t * (0.035f + blob * 0.004f) + blob * 1.37f;
		float cx = 480.0f + cosf(phase) * (360.0f - blob * 22.0f);
		float cy = 282.0f + sinf(phase * 1.17f) * (210.0f - blob * 12.0f);
		for (int ring = 6; ring >= 1; ring--) {
			float radius = 112.0f + ring * 58.0f;
			unsigned alpha = (unsigned)(24 + (7 - ring) * 8);
			vita2d_draw_fill_circle(cx, cy, radius,
			                        with_alpha(colors[blob], alpha));
		}
	}
	/* The veil follows the average palette: every foreground element in the
	 * player becomes either black or white, never a low-contrast accent. */
	vita2d_draw_rectangle(0, UI_BRAND_HEADER_HEIGHT, 960,
	                      544 - UI_BRAND_HEADER_HEIGHT,
	                      foreground == RGBA8(0, 0, 0, 255)
	                          ? RGBA8(255, 255, 255, 94)
	                          : RGBA8(0, 5, 12, 104));
}

static void draw_input_lock_at(int x, int y) {
	unsigned color = VT_THEME_TEXT;
	unsigned panel = RGBA8(8, 18, 32, 224);
	vita2d_draw_rectangle(x, y, 54, 44, panel);
	vita2d_draw_rectangle(x, y, 3, 44, VT_THEME_BLUE_LIGHT);
	vita2d_draw_line(x + 20, y + 19, x + 20, y + 12, color);
	vita2d_draw_line(x + 20, y + 12, x + 27, y + 6, color);
	vita2d_draw_line(x + 27, y + 6, x + 34, y + 12, color);
	vita2d_draw_line(x + 34, y + 12, x + 34, y + 19, color);
	vita2d_draw_rectangle(x + 16, y + 18, 22, 18, color);
	vita2d_draw_fill_circle((float)x + 27.0f, (float)y + 26.0f, 2.0f, panel);
	vita2d_draw_rectangle(x + 26, y + 26, 3, 6, panel);
}

static void draw_marquee(vita2d_font *font, float size, const char *text,
	                     float baseline, unsigned color, uint64_t now,
	                     uint64_t phase_offset) {
	if (!font || !text || !text[0]) return;
	const float left = MUSIC_TEXT_LEFT;
	const float available = MUSIC_TEXT_RIGHT - MUSIC_TEXT_LEFT;
	float width = (float)ui_font_text_width(font, size, text);
	if (width <= available) {
		ui_font_draw_text(font, (960.0f - width) * .5f, baseline,
		                  color, size, text);
		return;
	}
	const float speed = 34.0f;
	const float pause = 1.25f;
	float travel = width - available;
	float moving = travel / speed;
	float cycle = pause + moving + pause + moving;
	float seconds = (float)((now + phase_offset) %
	                          (uint64_t)(cycle * 1000000.0f)) / 1000000.0f;
	float offset;
	if (seconds < pause) offset = 0.0f;
	else if ((seconds -= pause) < moving) offset = seconds * speed;
	else if ((seconds -= moving) < pause) offset = travel;
	else {
		seconds -= pause;
		offset = travel - seconds * speed;
	}
	vita2d_set_clip_rectangle((int)left, (int)(baseline - size - 8),
	                          (int)MUSIC_TEXT_RIGHT, (int)(baseline + 8));
	vita2d_enable_clipping();
	ui_font_draw_text(font, left - offset, baseline, color, size, text);
	vita2d_disable_clipping();
}

static void draw_music_volume(int percent, float opacity,
	                          unsigned foreground) {
	if (opacity <= 0.01f) return;
	const float x = 899.0f, y = 172.0f, h = 190.0f;
	unsigned alpha = (unsigned)(220.0f * opacity);
	vita2d_draw_rectangle(x - 18, y - 32, 55, h + 62,
	                      foreground == RGBA8(255, 255, 255, 255)
	                          ? RGBA8(2, 8, 18, alpha)
	                          : RGBA8(255, 255, 255, alpha));
	vita2d_draw_rectangle(x, y, 9, h, with_alpha(foreground, alpha / 2));
	float fraction = (float)percent / (float)MUSIC_VOLUME_MAX;
	if (fraction < 0.0f) fraction = 0.0f;
	if (fraction > 1.0f) fraction = 1.0f;
	vita2d_draw_rectangle(x, y + h * (1.0f - fraction), 9, h * fraction,
	                      with_alpha(foreground, alpha));
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (small)
		ui_font_draw_textf(small, x - 13, y - 9, with_alpha(foreground, alpha),
		                   UI_FONT_SMALL, "%d%%", percent);
}

static void draw_music_right_sidebar(float animation, int cursor) {
	if (animation <= 0.01f) return;
	const float width = 306.0f;
	float x = 960.0f - width * animation;
	vita2d_draw_rectangle(x, UI_BRAND_HEADER_HEIGHT, width,
	                      544 - UI_BRAND_HEADER_HEIGHT, RGBA8(2, 8, 17, 245));
	vita2d_draw_rectangle(x, UI_BRAND_HEADER_HEIGHT, 4,
	                      544 - UI_BRAND_HEADER_HEIGHT, VT_THEME_BLUE_LIGHT);
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (body)
		ui_font_draw_text(body, x + 28, 112, VT_THEME_TEXT, UI_FONT_BODY,
		                  vt_i18n_str(VT_STR_SETTINGS_TITLE));
	const char *rows[2] = {
		vt_i18n_str(g_shuffle ? VT_STR_MUSIC_PLAYER_RANDOM
		                       : VT_STR_MUSIC_PLAYER_LINEAR),
		vt_i18n_str(g_repeat_one ? VT_STR_MUSIC_PLAYER_REPEAT_ONE
		                          : VT_STR_MUSIC_PLAYER_REPEAT_OFF)
	};
	for (int i = 0; i < 2; i++) {
		float y = 146.0f + i * 64.0f;
		vita2d_draw_rectangle(x + 18, y, width - 36, 52,
		                      i == cursor ? VT_THEME_SURFACE_FOCUS
		                                  : VT_THEME_SURFACE);
		if (small)
			ui_font_draw_text(small, x + 38, y + 33,
			                  i == cursor ? VT_THEME_TEXT : VT_THEME_TEXT_MUTED,
			                  UI_FONT_SMALL, rows[i]);
	}
}

int ui_music_player_shuffle_enabled(void) { return g_shuffle; }
int ui_music_player_repeat_one_enabled(void) { return g_repeat_one; }

int ui_music_player_run(const char *artwork_path, const char *album,
	                    uint32_t average_bitrate_kbps) {
	vita2d_texture *cover = artwork_path && artwork_path[0]
	                       ? vita2d_load_JPEG_file(artwork_path) : NULL;
	unsigned palette[5];
	artwork_palette(cover, palette);
	unsigned foreground = palette_foreground(palette);
	SceCtrlData ctrl, previous;
	memset(&ctrl, 0, sizeof(ctrl));
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	ui_touch_reset();
	sceCtrlPeekBufferPositive(0, &previous, 1);
	PlayerInputLock input_lock;
	memset(&input_lock, 0, sizeof(input_lock));
	PlayerPowerSaveInput power_save_input;
	memset(&power_save_input, 0, sizeof(power_save_input));
	power_save_input.armed = (previous.buttons & SCE_CTRL_START) == 0;
	int energy_saving = 0;
	int energy_redraw = 0;
	int energy_lock_position = 0;
	uint64_t energy_lock_move_us = 0;
	int dragging_timeline = 0;
	UiSectionsSidebar sidebar;
	ui_sections_sidebar_init(&sidebar, UI_SECTION_LOCAL_MEDIA);
	int right_open = 0;
	int right_cursor = 0;
	float right_animation = 0.0f;
	uint64_t last_volume_us = 0;
	uint64_t volume_visible_until_us = 0;
	int action = UI_MUSIC_PLAYER_STOP;
	for (;;) {
		VtBackgroundPlaybackSnapshot snapshot;
		if (!vt_background_playback_snapshot(&snapshot)) {
			action = g_repeat_one ? UI_MUSIC_PLAYER_REPEAT
			                      : UI_MUSIC_PLAYER_NEXT;
			break;
		}
		sceCtrlPeekBufferPositive(0, &ctrl, 1);
		unsigned pressed = ctrl.buttons & ~previous.buttons;
		previous = ctrl;
		uint64_t input_now = sceKernelGetProcessTimeWide();
		int lock_changed = player_input_lock_update(&input_lock, ctrl.buttons,
		                                            input_now);
		if (lock_changed) {
			energy_redraw = 1;
			dragging_timeline = 0;
			sidebar.open = 0;
			right_open = 0;
		}
		UiTouchEvent touch;
		unsigned touch_flags = ui_touch_poll(&touch);
		if (input_lock.locked) {
			pressed = 0;
			touch_flags = UI_TOUCH_EVENT_NONE;
			dragging_timeline = 0;
		}
		int power_event = player_power_save_update(
		    &power_save_input, ctrl.buttons, input_now, !input_lock.locked);
		pressed &= ~SCE_CTRL_START;
		if (power_event == PLAYER_POWER_SAVE_EVENT_TOGGLE) {
			energy_saving = !energy_saving;
			energy_redraw = 1;
			energy_lock_position = 0;
			energy_lock_move_us = input_now + PLAYER_POWER_SAVE_LOCK_MOVE_US;
			dragging_timeline = 0;
			sidebar.open = 0;
			right_open = 0;
			ui_touch_reset();
		}
		if (power_event == PLAYER_POWER_SAVE_EVENT_SHORT_PRESS) {
			action = UI_MUSIC_PLAYER_MINIMIZE;
			break;
		}
		if (energy_saving) {
			/* Match the video players: only START, SELECT and the unlocked
			 * emergency CIRCLE exit remain active in audio-only mode. */
			pressed &= SCE_CTRL_CIRCLE;
			touch_flags = UI_TOUCH_EVENT_NONE;
			dragging_timeline = 0;
		}
		int swapped = vt_preferences_player_swap_shoulders();
		unsigned transport_pressed = pressed;
		if (!energy_saving && !input_lock.locked) {
			/* Default: shoulders own the panels and D-pad owns transport. The
			 * compatibility option swaps exactly those roles, matching video. */
			if (swapped) pressed &= ~(SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER);
			if (swapped && !sidebar.open && !right_open &&
			    (pressed & SCE_CTRL_LEFT)) {
				pressed &= ~SCE_CTRL_LEFT;
				pressed |= SCE_CTRL_LTRIGGER;
			}
			unsigned right_toggle = swapped ? SCE_CTRL_RIGHT : SCE_CTRL_RTRIGGER;
			if (!sidebar.open && (pressed & right_toggle)) {
				right_open = !right_open;
				right_cursor = 0;
				pressed &= ~right_toggle;
			}
			if (!right_open) {
				int section = ui_sections_sidebar_handle_buttons(
				    &sidebar, &pressed, ctrl.buttons, ctrl.ly);
				if (section != UI_SECTION_NONE) {
					action = UI_MUSIC_PLAYER_SECTION_BASE + section;
					break;
				}
			} else {
				if ((pressed & SCE_CTRL_UP) && right_cursor > 0) right_cursor--;
				if ((pressed & SCE_CTRL_DOWN) && right_cursor < 1) right_cursor++;
				if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) {
					if (right_cursor == 0) g_shuffle = !g_shuffle;
					else g_repeat_one = !g_repeat_one;
				}
				if (pressed & SCE_CTRL_CIRCLE) right_open = 0;
				pressed = 0;
			}
		}
		int panels_open = sidebar.open || right_open;
		if (!panels_open && (pressed & SCE_CTRL_CROSS))
			vt_background_playback_toggle_pause();
		if (!panels_open && ((swapped ? transport_pressed : pressed) &
		    (swapped ? SCE_CTRL_LTRIGGER : SCE_CTRL_LEFT)))
			vt_background_playback_seek_relative(-10000);
		if (!panels_open && ((swapped ? transport_pressed : pressed) &
		    (swapped ? SCE_CTRL_RTRIGGER : SCE_CTRL_RIGHT)))
			vt_background_playback_seek_relative(10000);
		if (!panels_open && (pressed & SCE_CTRL_SQUARE)) g_shuffle = !g_shuffle;
		if (!panels_open && (pressed & SCE_CTRL_TRIANGLE)) g_repeat_one = !g_repeat_one;
		if (!panels_open && (pressed & SCE_CTRL_CIRCLE)) {
			action = UI_MUSIC_PLAYER_STOP;
			break;
		}

		int ry_dev = (int)ctrl.ry - MUSIC_STICK_CENTER;
		if (!panels_open && !energy_saving && !input_lock.locked &&
		    (ry_dev > MUSIC_STICK_DEADZONE || ry_dev < -MUSIC_STICK_DEADZONE) &&
		    input_now - last_volume_us >= MUSIC_VOLUME_INTERVAL_US) {
			int volume = vt_audio_volume_percent() +
			             (ry_dev < 0 ? MUSIC_VOLUME_STEP : -MUSIC_VOLUME_STEP);
			if (volume < 0) volume = 0;
			if (volume > MUSIC_VOLUME_MAX) volume = MUSIC_VOLUME_MAX;
			vt_audio_volume_set_percent(volume);
			last_volume_us = input_now;
			volume_visible_until_us = input_now + MUSIC_VOLUME_VISIBLE_US;
		}

		if (sidebar.open) {
			int section = ui_sections_sidebar_handle_touch(
			    &sidebar, touch_flags, touch.x, touch.y);
			if (section != UI_SECTION_NONE) {
				action = UI_MUSIC_PLAYER_SECTION_BASE + section;
				break;
			}
			touch_flags = UI_TOUCH_EVENT_NONE;
		} else if (right_open && (touch_flags & UI_TOUCH_EVENT_TAP)) {
			for (int i = 0; i < 2; i++) {
				if (!ui_touch_hit_rect(touch.x, touch.y, 672, 146 + i * 64,
				                       270, 52)) continue;
				right_cursor = i;
				if (i == 0) g_shuffle = !g_shuffle;
				else g_repeat_one = !g_repeat_one;
			}
			if (touch.x < 654) right_open = 0;
			touch_flags = UI_TOUCH_EVENT_NONE;
		}

		if (!panels_open && (touch_flags & UI_TOUCH_EVENT_DOWN) &&
		    ui_touch_hit_rect(touch.x, touch.y, MUSIC_TIMELINE_X,
		                      MUSIC_TIMELINE_Y - 18, MUSIC_TIMELINE_W, 38))
			dragging_timeline = 1;
		if (!panels_open && dragging_timeline &&
		    (touch_flags & (UI_TOUCH_EVENT_DOWN | UI_TOUCH_EVENT_MOVE |
		                    UI_TOUCH_EVENT_HOLD | UI_TOUCH_EVENT_UP))) {
			float fraction = (float)(touch.x - MUSIC_TIMELINE_X) /
			                 (float)MUSIC_TIMELINE_W;
			if (fraction < 0.0f) fraction = 0.0f;
			if (fraction > 1.0f) fraction = 1.0f;
			if (snapshot.duration_ms)
				vt_background_playback_seek_to(
				    (uint64_t)(fraction * (float)snapshot.duration_ms));
			if (touch_flags & UI_TOUCH_EVENT_UP) dragging_timeline = 0;
		} else if (!panels_open && (touch_flags & UI_TOUCH_EVENT_TAP)) {
			if (ui_touch_hit_rect(touch.x, touch.y, 372, MUSIC_CONTROLS_Y, 166, MUSIC_CONTROLS_H))
				vt_background_playback_toggle_pause();
			else if (ui_touch_hit_rect(touch.x, touch.y, 220, MUSIC_CONTROLS_Y, 144, MUSIC_CONTROLS_H))
				vt_background_playback_seek_relative(-10000);
			else if (ui_touch_hit_rect(touch.x, touch.y, 546, MUSIC_CONTROLS_Y, 144, MUSIC_CONTROLS_H))
				vt_background_playback_seek_relative(10000);
			else if (ui_touch_hit_rect(touch.x, touch.y, 46, MUSIC_CONTROLS_Y, 166, MUSIC_CONTROLS_H))
				g_shuffle = !g_shuffle;
			else if (ui_touch_hit_rect(touch.x, touch.y, 698, MUSIC_CONTROLS_Y, 216, MUSIC_CONTROLS_H))
				g_repeat_one = !g_repeat_one;
		}
		ui_sections_sidebar_tick(&sidebar);
		if (vt_preferences_reduce_motion())
			right_animation = right_open ? 1.0f : 0.0f;
		else
			right_animation += ((right_open ? 1.0f : 0.0f) - right_animation) * .28f;

		if (vt_preferences_music_keep_display_awake() || energy_saving)
			vt_display_keep_awake_tick();
		uint64_t now = sceKernelGetProcessTimeWide();
		if (energy_saving) {
			if (input_lock.locked && now >= energy_lock_move_us) {
				energy_lock_position =
				    (energy_lock_position + 1) % PLAYER_POWER_SAVE_LOCK_POSITIONS;
				energy_lock_move_us = now + PLAYER_POWER_SAVE_LOCK_MOVE_US;
				energy_redraw = 1;
			}
			if (energy_redraw) {
				vita2d_start_drawing();
				vita2d_clear_screen();
				vita2d_draw_rectangle(0, 0, 960, 544, RGBA8(0, 0, 0, 255));
				if (input_lock.locked) {
					int lock_x, lock_y;
					player_power_save_lock_position(energy_lock_position,
					                                &lock_x, &lock_y);
					draw_input_lock_at(lock_x, lock_y);
				}
				vita2d_end_drawing();
				vita2d_wait_rendering_done();
				vita2d_swap_buffers();
				energy_redraw = 0;
			}
			sceKernelDelayThread(10000);
			continue;
		}
		vita2d_start_drawing();
		vita2d_clear_screen();
		draw_flow_background(palette, now, foreground);
		ui_brand_draw_header(NULL);
		/* The composition sits lower than the video HUD and leaves a quiet top
		 * band for VitaTube's system header. */
		if (cover) draw_cover(cover, 368.0f, 92.0f, 224.0f);
		vita2d_font *display = ui_runtime_font(UI_FONT_DISPLAY);
		vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
		vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
		float title_y = cover ? 355.0f : 238.0f;
		draw_marquee(display, UI_FONT_DISPLAY, snapshot.title, title_y,
		              foreground, now, 0);
		draw_marquee(body, UI_FONT_BODY, snapshot.channel, title_y + 36.0f,
		              foreground, now, 600000ULL);
		draw_marquee(small, UI_FONT_SMALL, album, title_y + 64.0f,
		              foreground, now, 1200000ULL);
		float progress = snapshot.duration_ms
		               ? (float)snapshot.position_ms / (float)snapshot.duration_ms : 0.0f;
		if (progress < 0) progress = 0;
		if (progress > 1) progress = 1;
		vita2d_draw_rectangle(MUSIC_TIMELINE_X, MUSIC_TIMELINE_Y,
		                      MUSIC_TIMELINE_W, 5, with_alpha(foreground, 72));
		vita2d_draw_rectangle(MUSIC_TIMELINE_X, MUSIC_TIMELINE_Y,
		                      MUSIC_TIMELINE_W * progress, 5, foreground);
		vita2d_draw_fill_circle(MUSIC_TIMELINE_X + MUSIC_TIMELINE_W * progress,
		                        MUSIC_TIMELINE_Y + 2.5f, 6.0f, foreground);
		char current[24], duration[24];
		time_text(snapshot.position_ms, current);
		time_text(snapshot.duration_ms, duration);
		if (small) {
			ui_font_draw_text(small, MUSIC_TIMELINE_X, 465, foreground,
			                  UI_FONT_SMALL, current);
			int width = ui_font_text_width(small, UI_FONT_SMALL, duration);
			ui_font_draw_text(small, MUSIC_TIMELINE_X + MUSIC_TIMELINE_W - width,
			                  465, foreground, UI_FONT_SMALL, duration);
		}
		unsigned button_fill = foreground == RGBA8(255, 255, 255, 255)
		                     ? RGBA8(3, 11, 22, 224)
		                     : RGBA8(240, 244, 248, 230);
		ui_action_button(46, MUSIC_CONTROLS_Y, 166, 56, button_fill,
		                 "Square", g_shuffle ? "Shuffle on" : "Shuffle", g_shuffle);
		ui_action_button(220, MUSIC_CONTROLS_Y, 144, 56, button_fill,
		                 "Left", "-10 s", 0);
		ui_action_button(372, MUSIC_CONTROLS_Y, 166, 56, button_fill,
		                 "Cross",
		                 snapshot.state == VT_BACKGROUND_PAUSED ? "Play" : "Pause", 1);
		ui_action_button(546, MUSIC_CONTROLS_Y, 144, 56, button_fill,
		                 "Right", "+10 s", 0);
		ui_action_button(698, MUSIC_CONTROLS_Y, 216, 56, button_fill,
		                 "Triangle", g_repeat_one ? "Repeat one" : "Repeat", g_repeat_one);
		if (snapshot.state == VT_BACKGROUND_PREPARING ||
		    snapshot.state == VT_BACKGROUND_BUFFERING)
			ui_draw_spinner_compact(455, MUSIC_CONTROLS_Y + 28, now);
		float volume_opacity = now < volume_visible_until_us
		                     ? (float)(volume_visible_until_us - now) /
		                       (float)MUSIC_VOLUME_VISIBLE_US : 0.0f;
		if (!right_open)
			draw_music_volume(vt_audio_volume_percent(), volume_opacity, foreground);
		if (input_lock.locked) draw_input_lock_at(24, 86);
		if (sidebar.animation > 0.01f)
			ui_sections_sidebar_draw(sidebar.cursor, sidebar.animation,
			                         sidebar.focus_cursor);
		draw_music_sidebar_bitrate(sidebar.animation, average_bitrate_kbps);
		draw_music_right_sidebar(right_animation, right_cursor);
		vita2d_end_drawing();
		vita2d_wait_rendering_done();
		vita2d_swap_buffers();
		sceKernelDelayThread(1000);
	}
	if (cover) vita2d_free_texture(cover);
	if (action == UI_MUSIC_PLAYER_STOP) vt_background_playback_stop();
	return action;
}
