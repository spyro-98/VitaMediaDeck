#include "ui/music_player.h"

#include <ctype.h>
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
#define MUSIC_HUD_VISIBLE_US (3500 * 1000ULL)
#define MUSIC_HUD_FADE_US (280 * 1000ULL)
#define MUSIC_STICK_CENTER 128
#define MUSIC_STICK_DEADZONE 34
#define MUSIC_STICK_NAV_LOW 48
#define MUSIC_STICK_NAV_HIGH 207
#define MUSIC_STICK_REPEAT_FIRST_US (320 * 1000ULL)
#define MUSIC_STICK_REPEAT_US (220 * 1000ULL)
#define MUSIC_VOLUME_STEP 5
#define MUSIC_VOLUME_MAX 300
#define MUSIC_RIGHT_PANEL_W 306.0f
#define MUSIC_RIGHT_ROW_Y 82.0f
#define MUSIC_RIGHT_ROW_STEP 68.0f
#define MUSIC_CONTROL_FEEDBACK_US 170000ULL

enum {
	MUSIC_CONTROL_NONE = 0,
	MUSIC_CONTROL_SHUFFLE,
	MUSIC_CONTROL_BACK,
	MUSIC_CONTROL_PLAY,
	MUSIC_CONTROL_FORWARD,
	MUSIC_CONTROL_REPEAT
};

static int g_shuffle;
static int g_repeat_one;

static unsigned fade_color(unsigned color, float opacity);

static int ends_with_ci(const char *text, const char *suffix) {
	if (!text || !suffix) return 0;
	size_t text_len = strlen(text), suffix_len = strlen(suffix);
	if (suffix_len > text_len) return 0;
	const char *tail = text + text_len - suffix_len;
	for (size_t i = 0; i < suffix_len; i++)
		if (tolower((unsigned char)tail[i]) != tolower((unsigned char)suffix[i]))
			return 0;
	return 1;
}

static vita2d_texture *load_artwork_texture(const char *path) {
	if (!path || !path[0]) return NULL;
	return ends_with_ci(path, ".png") ? vita2d_load_PNG_file(path)
	                                  : vita2d_load_JPEG_file(path);
}

static void format_audio_rate(uint32_t sample_rate, char out[24]) {
	if (!sample_rate) snprintf(out, 24, "-");
	else if (sample_rate % 1000U == 0)
		snprintf(out, 24, "%u kHz", sample_rate / 1000U);
	else snprintf(out, 24, "%u.%u kHz", sample_rate / 1000U,
	              (sample_rate % 1000U) / 100U);
}

static void format_audio_details(uint32_t sample_rate, uint16_t channels,
	                             uint16_t bits_per_sample, char out[72]) {
	char rate[24];
	format_audio_rate(sample_rate, rate);
	if (channels && bits_per_sample)
		snprintf(out, 72, "%s  |  %u ch  |  %u-bit", rate, channels,
		         bits_per_sample);
	else if (channels)
		snprintf(out, 72, "%s  |  %u ch", rate, channels);
	else snprintf(out, 72, "%s", rate);
}

static void draw_music_audio_hud(const char *codec_name,
	                             uint32_t bitrate_kbps,
	                             uint32_t sample_rate, float baseline,
	                             unsigned foreground, float opacity) {
	if (opacity <= 0.01f) return;
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (!small) return;
	char metadata[112], rate[24];
	format_audio_rate(sample_rate, rate);
	const char *codec = codec_name && codec_name[0] ? codec_name : "AUDIO";
	if (bitrate_kbps && sample_rate)
		snprintf(metadata, sizeof(metadata), "%s  |  %s  |  %u kbps",
		         codec, rate, bitrate_kbps);
	else if (bitrate_kbps)
		snprintf(metadata, sizeof(metadata),
		         vt_i18n_str(VT_STR_MUSIC_PLAYER_BITRATE_FORMAT),
		         codec, bitrate_kbps);
	else if (sample_rate)
		snprintf(metadata, sizeof(metadata), "%s  |  %s", codec, rate);
	else snprintf(metadata, sizeof(metadata), "%s", codec);
	int text_width = ui_font_text_width(small, UI_FONT_SMALL, metadata);
	float panel_x = 480.0f - (float)text_width * 0.5f - 12.0f;
	vita2d_draw_rectangle(panel_x, baseline - 21.0f,
	                      (float)text_width + 24.0f, 28.0f,
	                      fade_color(VT_THEME_GLASS_A(210), opacity));
	vita2d_draw_rectangle(panel_x, baseline - 21.0f, 3.0f, 28.0f,
	                      fade_color(VT_THEME_SPECTRAL, opacity));
	ui_font_draw_text(small, (int)(480.0f - text_width * 0.5f),
	                  (int)baseline, fade_color(foreground, opacity),
	                  UI_FONT_SMALL, metadata);
}

static void draw_cover(vita2d_texture *cover, float x, float y, float size,
	                   unsigned foreground) {
	if (!cover) return;
	unsigned frame = foreground == RGBA8(255, 255, 255, 255)
	               ? RGBA8(3, 10, 12, 228) : RGBA8(236, 244, 242, 218);
	vita2d_draw_rectangle(x - 10, y + 8, size + 20, size + 12,
	                      RGBA8(0, 0, 0, 62));
	vita2d_draw_rectangle(x - 5, y - 5, size + 10, size + 10, frame);
	vita2d_draw_rectangle(x - 5, y - 5, size + 10, 3,
	                      (foreground & 0x00ffffffU) | (220U << 24));
	/* Acquisition corners make the album object feel scanned, not framed. */
	unsigned mark = (foreground & 0x00ffffffU) | (196U << 24);
	vita2d_draw_rectangle(x - 12, y - 12, 28, 2, mark);
	vita2d_draw_rectangle(x - 12, y - 12, 2, 28, mark);
	vita2d_draw_rectangle(x + size - 16, y + size + 10, 28, 2, mark);
	vita2d_draw_rectangle(x + size + 10, y + size - 16, 2, 28, mark);
	float w = (float)vita2d_texture_get_width(cover);
	float h = (float)vita2d_texture_get_height(cover);
	float scale = size / (w > h ? w : h);
	vita2d_draw_texture_scale(cover, x + (size - w * scale) * .5f,
	                          y + (size - h * scale) * .5f, scale, scale);
}

static void time_text(uint64_t ms, char out[24]) {
	uint64_t seconds = ms / 1000ULL;
	uint64_t hours = seconds / 3600ULL;
	seconds %= 3600ULL;
	if (hours)
		snprintf(out, 24, "%llu:%02llu:%02llu", (unsigned long long)hours,
		         (unsigned long long)(seconds / 60ULL),
		         (unsigned long long)(seconds % 60ULL));
	else
		snprintf(out, 24, "%llu:%02llu", (unsigned long long)(seconds / 60ULL),
		         (unsigned long long)(seconds % 60ULL));
}

static unsigned with_alpha(unsigned color, unsigned alpha) {
	return (color & 0x00ffffffU) | ((alpha & 0xffU) << 24);
}

static unsigned fade_color(unsigned color, float opacity) {
	if (opacity <= 0.0f) return color & 0x00ffffffU;
	if (opacity >= 1.0f) return color;
	unsigned alpha = (color >> 24) & 0xffU;
	return (color & 0x00ffffffU) |
	       ((unsigned)((float)alpha * opacity + 0.5f) << 24);
}

static float approach_time_based(float value, float target, float rate,
	                             float delta_seconds) {
	if (delta_seconds <= 0.0f) return value;
	float blend = 1.0f - expf(-rate * delta_seconds);
	return value + (target - value) * blend;
}

static void draw_music_spinner_compact(float center_x, float center_y,
	                                   uint64_t now_us, float opacity) {
	if (opacity <= 0.01f) return;
	float phase = vt_preferences_reduce_motion()
	            ? 0.0f : (float)(now_us % 900000ULL) / 900000.0f * 6.2831853f;
	for (int dot = 0; dot < 8; dot++) {
		float angle = (float)dot * 0.7853982f;
		float wave = (cosf(angle - phase) + 1.0f) * 0.5f;
		unsigned alpha = (unsigned)(55.0f + wave * 200.0f);
		vita2d_draw_fill_circle(center_x + cosf(angle) * 7.0f,
		                        center_y + sinf(angle) * 7.0f, 1.8f,
		                        fade_color(with_alpha(VT_THEME_BLUE_LIGHT, alpha),
		                                   opacity));
	}
}

/* Produces one navigation step when the stick crosses a threshold, then a
 * restrained repeat while it remains held. This deliberately mirrors a
 * physical D-pad instead of issuing a seek every rendered frame. */
static int stick_repeat_step(unsigned char value, int *held_direction,
	                         uint64_t *repeat_at_us, uint64_t now_us) {
	int direction = value < MUSIC_STICK_NAV_LOW ? -1
	              : value > MUSIC_STICK_NAV_HIGH ? 1 : 0;
	if (!direction) {
		*held_direction = 0;
		*repeat_at_us = 0;
		return 0;
	}
	if (direction != *held_direction) {
		*held_direction = direction;
		*repeat_at_us = now_us + MUSIC_STICK_REPEAT_FIRST_US;
		return direction;
	}
	if (now_us < *repeat_at_us) return 0;
	*repeat_at_us = now_us + MUSIC_STICK_REPEAT_US;
	return direction;
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
		RGBA8(0, 0, 0, 255), RGBA8(4, 18, 20, 255),
		RGBA8(29, 77, 84, 255), RGBA8(132, 82, 43, 255),
		RGBA8(222, 237, 236, 255)
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
	(void)colors;
	/* The new player field is always pitch black; cover colours illuminate only
	 * particles and optical rings, so a spectral-white foreground is stable. */
	return RGBA8(255, 255, 255, 255);
}

static void draw_flow_background(const unsigned colors[5], uint64_t now,
	                             unsigned foreground) {
	float t = vt_preferences_reduce_motion()
	        ? 0.0f : (float)(now % 90000000ULL) / 1000000.0f;
	vita2d_draw_rectangle(0, UI_BRAND_HEADER_HEIGHT, 960,
	                      544 - UI_BRAND_HEADER_HEIGHT, VT_THEME_BG);
	/* Cover colours survive as thin optical rings rather than illuminating the
	 * whole OLED. Each ring is cut back to black in its centre. */
	for (int ring = 0; ring < 5; ring++) {
		float phase = t * (0.022f + ring * 0.003f) + ring * 1.31f;
		float cx = 480.0f + cosf(phase) * (238.0f - ring * 18.0f);
		float cy = 286.0f + sinf(phase * 1.13f) * (132.0f - ring * 9.0f);
		float radius = 86.0f + ring * 43.0f;
		vita2d_draw_fill_circle(cx, cy, radius,
		                        with_alpha(colors[ring], 17U + ring * 3U));
		vita2d_draw_fill_circle(cx, cy, radius - 2.0f,
		                        RGBA8(0, 0, 0, 238));
	}
	/* The artwork palette remains unique to the track as a sparse point-cloud
	 * reassembly field. */
	unsigned int phase = vt_preferences_reduce_motion()
	                   ? 0U : (unsigned int)((now / 52000ULL) % 420ULL);
	for (unsigned int i = 0; i < 46; i++) {
		unsigned int seed = 0x45D9F3BU * (i + 17U);
		seed ^= seed >> 15;
		float x = 24.0f + (float)(seed % 912U);
		float y = 76.0f + (float)((seed / 277U + phase * (1U + i % 2U)) % 438U);
		unsigned dot_alpha = 26U + seed % 78U;
		unsigned particle = i % 9U == 0U ? VT_THEME_SIGNAL_BRIGHT
		                  : i % 7U == 0U ? VT_THEME_SPECTRAL
		                                  : colors[i % 5U];
		float size = i % 11U == 0U ? 3.0f : i % 4U == 0U ? 2.0f : 1.0f;
		vita2d_draw_rectangle(x, y, size, size,
		                      with_alpha(particle, dot_alpha));
		if (i % 10U == 0U)
			vita2d_draw_line(x, y, 480.0f, 204.0f,
			                 VT_THEME_SPECTRAL_A(16U));
	}
	(void)foreground;
}

static void draw_metadata_scrim(float title_y, unsigned foreground,
	                            float opacity) {
	if (opacity <= 0.01f) return;
	unsigned fill = foreground == RGBA8(255, 255, 255, 255)
	              ? RGBA8(0, 5, 12, 128) : RGBA8(255, 255, 255, 132);
	unsigned edge = with_alpha(foreground, 58);
	vita2d_draw_rectangle(48, title_y - 34.0f, 864, 108,
	                      fade_color(fill, opacity));
	vita2d_draw_rectangle(48, title_y - 34.0f, 864, 1,
	                      fade_color(edge, opacity));
	vita2d_draw_rectangle(48, title_y + 73.0f, 864, 1,
	                      fade_color(edge, opacity));
}

static const char *music_state_text(VtBackgroundPlaybackState state) {
	if (state == VT_BACKGROUND_PREPARING) return vt_i18n_str(VT_STR_MINI_STATE_PREPARING);
	if (state == VT_BACKGROUND_BUFFERING) return vt_i18n_str(VT_STR_MINI_STATE_BUFFERING);
	if (state == VT_BACKGROUND_READY) return vt_i18n_str(VT_STR_MINI_STATE_READY);
	if (state == VT_BACKGROUND_PAUSED) return vt_i18n_str(VT_STR_MINI_STATE_PAUSED);
	if (state == VT_BACKGROUND_ERROR) return vt_i18n_str(VT_STR_MINI_STATE_ERROR);
	return vt_i18n_str(VT_STR_MINI_STATE_PLAYING);
}

static void draw_music_state(const VtBackgroundPlaybackSnapshot *snapshot,
	                         unsigned foreground, uint64_t now,
	                         float opacity) {
	if (!snapshot || opacity <= 0.01f) return;
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (!small) return;
	const char *label = music_state_text(snapshot->state);
	int loading = snapshot->state == VT_BACKGROUND_PREPARING ||
	              snapshot->state == VT_BACKGROUND_BUFFERING;
	int label_width = ui_font_text_width(small, UI_FONT_SMALL, label);
	int width = label_width + (loading ? 54 : 30);
	unsigned panel = foreground == RGBA8(255, 255, 255, 255)
	               ? RGBA8(3, 10, 12, 218) : RGBA8(236, 244, 242, 224);
	unsigned state_color = ui_contrast_bw(panel);
	vita2d_draw_rectangle(72, 82, width, 34, fade_color(panel, opacity));
	vita2d_draw_rectangle(72, 82, 3, 34,
	                      fade_color(snapshot->state == VT_BACKGROUND_ERROR
	                          ? VT_THEME_DANGER : VT_THEME_BLUE_LIGHT, opacity));
	if (loading) draw_music_spinner_compact(94, 99, now, opacity);
	ui_font_draw_text(small, 86 + (loading ? 24 : 0), 106,
	                  fade_color(state_color, opacity),
	                  UI_FONT_SMALL, label);
}

static unsigned music_control_fill(unsigned base, int control,
	                               int feedback_control,
	                               uint64_t feedback_until, uint64_t now) {
	return control == feedback_control && now < feedback_until
	     ? VT_THEME_BLUE_LIGHT : base;
}

static void draw_music_action_button(float x, float y, float width,
	                                 float height, unsigned fill,
	                                 const char *key, const char *label,
	                                 int active, float opacity) {
	if (opacity <= 0.01f) return;
	unsigned foreground = ui_contrast_bw(fill);
	unsigned accent = active ? VT_THEME_SIGNAL_LIGHT : VT_THEME_BORDER;
	vita2d_draw_rectangle(x, y, width, height, fade_color(fill, opacity));
	vita2d_draw_rectangle(x, y, width, 2, fade_color(accent, opacity));
	vita2d_draw_rectangle(x + 1, y + height - 1, width - 2, 1,
	                      fade_color(active ? accent : VT_THEME_BORDER_DIM,
	                                 opacity));
	vita2d_draw_rectangle(x, y, 12, 2, fade_color(accent, opacity));
	vita2d_draw_rectangle(x, y, 2, 10, fade_color(accent, opacity));
	vita2d_draw_rectangle(x + width - 12, y + height - 2, 12, 2,
	                      fade_color(accent, opacity));
	vita2d_draw_rectangle(x + width - 2, y + height - 10, 2, 10,
	                      fade_color(accent, opacity));

	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	float key_width = 0.0f;
	if (small && key && key[0]) {
		key_width = (float)ui_font_text_width(small, UI_FONT_SMALL, key) + 14.0f;
		if (key_width < 34.0f) key_width = 34.0f;
	}
	if (key_width > 0.0f) {
		unsigned cap = foreground == RGBA8(255, 255, 255, 255)
		             ? RGBA8(255, 255, 255, 28) : RGBA8(0, 0, 0, 28);
		vita2d_draw_rectangle(x + 10, y + 9, key_width, height - 18,
		                      fade_color(cap, opacity));
		int key_text_width = ui_font_text_width(small, UI_FONT_SMALL, key);
		ui_font_draw_text(small,
		                  (int)(x + 10 + (key_width - key_text_width) * .5f),
		                  (int)(y + height * .5f + 7),
		                  fade_color(foreground, opacity), UI_FONT_SMALL, key);
	}
	if (small && label && label[0]) {
		char fitted[192];
		float left = key_width > 0.0f ? x + key_width + 17.0f : x + 12.0f;
		float available = width - key_width - 24.0f;
		ui_font_fit_text(small, UI_FONT_SMALL, label, fitted, sizeof(fitted),
		                 (int)available);
		int label_width = ui_font_text_width(small, UI_FONT_SMALL, fitted);
		ui_font_draw_text(small,
		                  (int)(left + (available - label_width) * .5f),
		                  (int)(y + height * .5f + 7),
		                  fade_color(foreground, opacity), UI_FONT_SMALL, fitted);
	}
}

static void draw_input_lock_at(int x, int y, float opacity) {
	unsigned color = fade_color(VT_THEME_TEXT, opacity);
	unsigned panel = fade_color(VT_THEME_GLASS_A(224), opacity);
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

static void draw_power_save_status_at(int x, int y) {
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
	if (vt_preferences_reduce_motion()) {
		ui_font_draw_text_centered(font, 480, (int)baseline,
		                           (int)available, color, (unsigned)size, text);
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
	                          ? RGBA8(3, 10, 12, alpha)
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

static void draw_music_right_sidebar(float animation, float focus_position,
	                                 int cursor, int focused,
	                                 const char *codec_name,
	                                 uint32_t bitrate_kbps,
	                                 uint32_t sample_rate,
	                                 uint16_t channels,
	                                 uint16_t bits_per_sample) {
	if (animation <= 0.01f) return;
	const float width = MUSIC_RIGHT_PANEL_W;
	float x = 960.0f - width * animation;
	vita2d_draw_rectangle(x, 0, width, 544, VT_THEME_BG_SOFT);
	vita2d_draw_rectangle(x, 0, 3, 544, VT_THEME_SPECTRAL);
	vita2d_draw_rectangle(x + 4, 0, 1, 544, VT_THEME_SIGNAL_BRIGHT);
	vita2d_draw_rectangle(x + 4, 0, width - 4, 1, VT_THEME_BORDER);
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (body)
		ui_font_draw_text(body, x + 28, 47, VT_THEME_TEXT, UI_FONT_BODY,
		                  vt_i18n_str(VT_STR_PLAYER_PANEL_TITLE));
	if (small) {
		vita2d_draw_rectangle(x + width - 65, 18, 42, 26, VT_THEME_SURFACE_RAISED);
		int key_width = ui_font_text_width(small, UI_FONT_SMALL, "R1");
		ui_font_draw_text(small,
		                  (int)(x + width - 44.0f - key_width * 0.5f), 38,
		                  VT_THEME_TEXT,
		                  UI_FONT_SMALL, "R1");
	}
	const char *labels[2] = {
		vt_i18n_str(VT_STR_MUSIC_PLAYER_ORDER_LABEL),
		vt_i18n_str(VT_STR_MUSIC_PLAYER_REPEAT_LABEL)
	};
	const char *values[2] = {
		vt_i18n_str(g_shuffle ? VT_STR_MUSIC_PLAYER_RANDOM
		                       : VT_STR_MUSIC_PLAYER_LINEAR),
		vt_i18n_str(g_repeat_one ? VT_STR_MUSIC_PLAYER_REPEAT_ONE
		                          : VT_STR_MUSIC_PLAYER_REPEAT_OFF)
	};
	for (int i = 0; i < 2; i++) {
		float y = MUSIC_RIGHT_ROW_Y + i * MUSIC_RIGHT_ROW_STEP;
		ui_panel(x + 18, y, width - 36, 56, VT_THEME_SURFACE,
		         VT_THEME_BORDER_DIM, 0);
	}
	const char *info_labels[3] = {
		vt_i18n_str(VT_STR_MUSIC_PLAYER_FORMAT_LABEL),
		vt_i18n_str(VT_STR_MUSIC_PLAYER_BITRATE_LABEL),
		vt_i18n_str(VT_STR_MUSIC_PLAYER_DETAILS_LABEL)
	};
	char bitrate_value[48], details_value[72];
	if (bitrate_kbps)
		snprintf(bitrate_value, sizeof(bitrate_value),
		         vt_i18n_str(VT_STR_MUSIC_PLAYER_BITRATE_VALUE), bitrate_kbps);
	else snprintf(bitrate_value, sizeof(bitrate_value), "-");
	format_audio_details(sample_rate, channels, bits_per_sample, details_value);
	const char *info_values[3] = {
		codec_name && codec_name[0] ? codec_name : "AUDIO", bitrate_value,
		details_value
	};
	for (int i = 0; i < 3; i++) {
		float y = MUSIC_RIGHT_ROW_Y + (i + 2) * MUSIC_RIGHT_ROW_STEP;
		ui_panel(x + 18, y, width - 36, 56, VT_THEME_SURFACE,
		         VT_THEME_BORDER_DIM, 0);
		if (!small) continue;
		ui_font_draw_text(small, x + 36, y + 23, VT_THEME_TEXT_MUTED,
		                  UI_FONT_SMALL, info_labels[i]);
		char fitted[96];
		ui_font_fit_text(small, UI_FONT_SMALL, info_values[i], fitted,
		                 sizeof(fitted), (int)width - 72);
		int value_width = ui_font_text_width(small, UI_FONT_SMALL, fitted);
		ui_font_draw_text(small, x + width - value_width - 30, y + 46,
		                  VT_THEME_COLD_LIGHT, UI_FONT_SMALL, fitted);
	}
	if (focused) {
		float focus_y = MUSIC_RIGHT_ROW_Y + focus_position * MUSIC_RIGHT_ROW_STEP;
		vita2d_draw_rectangle(x + 14, focus_y - 4, width - 28, 64,
		                      VT_THEME_HALO_A(76));
		ui_panel(x + 18, focus_y, width - 36, 56,
		         VT_THEME_SURFACE_FOCUS, VT_THEME_SIGNAL_LIGHT, 0);
	}
	for (int i = 0; i < 2; i++) {
		float y = MUSIC_RIGHT_ROW_Y + i * MUSIC_RIGHT_ROW_STEP;
		if (!small) continue;
		ui_font_draw_text(small, x + 36, y + 23,
		                  focused && i == cursor ? VT_THEME_TEXT
		                                           : VT_THEME_TEXT_MUTED,
		                  UI_FONT_SMALL, labels[i]);
		int value_width = ui_font_text_width(small, UI_FONT_SMALL, values[i]);
		ui_font_draw_text(small, x + width - value_width - 30, y + 46,
		                  focused && i == cursor ? VT_THEME_BLUE_LIGHT
		                                           : VT_THEME_TEXT_MUTED,
		                  UI_FONT_SMALL, values[i]);
	}
	if (small)
		ui_font_draw_text(small, x + 28, 518, VT_THEME_TEXT_MUTED,
		                  UI_FONT_SMALL, vt_i18n_str(VT_STR_MUSIC_PLAYER_PANEL_HINT));
}

int ui_music_player_shuffle_enabled(void) { return g_shuffle; }
int ui_music_player_repeat_one_enabled(void) { return g_repeat_one; }

int ui_music_player_run(const char *artwork_path, const char *album,
	                    const char *codec_name,
	                    uint32_t average_bitrate_kbps) {
	vita2d_texture *cover = load_artwork_texture(artwork_path);
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
	input_lock.armed = (previous.buttons & SCE_CTRL_SELECT) == 0;
	PlayerPowerSaveInput power_save_input;
	memset(&power_save_input, 0, sizeof(power_save_input));
	power_save_input.armed = (previous.buttons & SCE_CTRL_START) == 0;
	int energy_saving = 0;
	int energy_redraw = 0;
	int energy_lock_position = 0;
	uint64_t energy_lock_move_us = 0;
	int dragging_timeline = 0;
	float drag_fraction = 0.0f;
	int touch_reveal_only = 0;
	UiSectionsSidebar sidebar;
	ui_sections_sidebar_init(&sidebar, UI_SECTION_LOCAL_MEDIA);
	int right_open = 0;
	int right_cursor = 0;
	float right_animation = 0.0f;
	float right_focus_position = 0.0f;
	int control_feedback = MUSIC_CONTROL_NONE;
	uint64_t control_feedback_until_us = 0;
	int hud_visible = 1;
	float hud_opacity = 1.0f;
	VtBackgroundPlaybackState previous_playback_state = VT_BACKGROUND_IDLE;
	uint64_t hud_deadline_us = sceKernelGetProcessTimeWide() +
	                           MUSIC_HUD_VISIBLE_US;
	uint64_t animation_last_us = sceKernelGetProcessTimeWide();
	int stick_x_direction = 0;
	uint64_t stick_x_repeat_at_us = 0;
	int stick_y_direction = 0;
	uint64_t stick_y_repeat_at_us = 0;
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
		uint64_t animation_delta_us = input_now >= animation_last_us
		                            ? input_now - animation_last_us : 0;
		if (animation_delta_us > 50000ULL) animation_delta_us = 50000ULL;
		animation_last_us = input_now;
		float animation_delta_s = (float)animation_delta_us / 1000000.0f;
		if (snapshot.state != previous_playback_state) {
			previous_playback_state = snapshot.state;
			hud_visible = 1;
			hud_deadline_us = input_now + MUSIC_HUD_VISIBLE_US;
		}
		int hud_touch_interactive = hud_visible || hud_opacity > 0.65f;
		int lock_changed = player_input_lock_update(&input_lock, ctrl.buttons,
		                                            input_now);
		if (lock_changed) {
			energy_redraw = 1;
			hud_visible = 1;
			hud_deadline_us = input_now + MUSIC_HUD_VISIBLE_US;
			dragging_timeline = 0;
			touch_reveal_only = 0;
			sidebar.open = 0;
			right_open = 0;
		}
		pressed &= ~SCE_CTRL_SELECT;
		UiTouchEvent touch;
		unsigned touch_flags = ui_touch_poll(&touch);
		unsigned touch_sequence_flags = touch_flags;
		if ((touch_flags & UI_TOUCH_EVENT_DOWN) && !hud_touch_interactive)
			touch_reveal_only = 1;
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
			touch_reveal_only = 0;
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
		int left_stick_active = ctrl.lx < MUSIC_STICK_NAV_LOW ||
		                        ctrl.lx > MUSIC_STICK_NAV_HIGH ||
		                        ctrl.ly < MUSIC_STICK_NAV_LOW ||
		                        ctrl.ly > MUSIC_STICK_NAV_HIGH;
		int right_stick_active =
		    (int)ctrl.ry - MUSIC_STICK_CENTER > MUSIC_STICK_DEADZONE ||
		    (int)ctrl.ry - MUSIC_STICK_CENTER < -MUSIC_STICK_DEADZONE;
		if (!energy_saving && !input_lock.locked &&
		    (ctrl.buttons || left_stick_active || right_stick_active)) {
			hud_visible = 1;
			hud_deadline_us = input_now + MUSIC_HUD_VISIBLE_US;
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
			if (!sidebar.open && sidebar.animation <= 0.01f &&
			    (pressed & right_toggle)) {
				right_open = !right_open;
				right_cursor = 0;
				right_focus_position = 0.0f;
				pressed &= ~right_toggle;
			}
			if (!right_open && right_animation <= 0.01f) {
				int section = ui_sections_sidebar_handle_buttons(
				    &sidebar, &pressed, ctrl.buttons, ctrl.ly);
				if (section != UI_SECTION_NONE) {
					action = UI_MUSIC_PLAYER_SECTION_BASE + section;
					break;
				}
			} else if (right_open) {
				int stick_nav = stick_repeat_step(ctrl.ly, &stick_y_direction,
				                                  &stick_y_repeat_at_us, input_now);
				if (((pressed & SCE_CTRL_UP) || stick_nav < 0) && right_cursor > 0)
					right_cursor--;
				if (((pressed & SCE_CTRL_DOWN) || stick_nav > 0) && right_cursor < 1)
					right_cursor++;
				if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) {
					if (right_cursor == 0) g_shuffle = !g_shuffle;
					else g_repeat_one = !g_repeat_one;
				}
				if (pressed & SCE_CTRL_CIRCLE) right_open = 0;
				pressed = 0;
			} else {
				pressed = 0;
			}
		}
		if (!right_open) {
			stick_y_direction = 0;
			stick_y_repeat_at_us = 0;
		}
		/* Keep closing drawers modal until no pixels remain. */
		int panels_open = sidebar.open || right_open ||
		                  sidebar.animation > 0.01f || right_animation > 0.01f;
		int panels_closing = (!sidebar.open && sidebar.animation > 0.01f) ||
		                     (!right_open && right_animation > 0.01f);
		if (panels_closing) {
			ui_touch_reset();
			touch_flags = UI_TOUCH_EVENT_NONE;
			touch_reveal_only = 0;
		}
		if (!panels_open && (pressed & SCE_CTRL_CROSS) &&
		    snapshot.state != VT_BACKGROUND_ERROR) {
			vt_background_playback_toggle_pause();
			control_feedback = MUSIC_CONTROL_PLAY;
			control_feedback_until_us = input_now + MUSIC_CONTROL_FEEDBACK_US;
		}
		unsigned transport = swapped ? transport_pressed : pressed;
		int seek_direction = !panels_open &&
		    (transport & (swapped ? SCE_CTRL_RTRIGGER : SCE_CTRL_RIGHT)) ? 1
		                   : !panels_open &&
		    (transport & (swapped ? SCE_CTRL_LTRIGGER : SCE_CTRL_LEFT)) ? -1 : 0;
		if (!panels_open && !energy_saving && !input_lock.locked) {
			int stick_seek = stick_repeat_step(ctrl.lx, &stick_x_direction,
			                                   &stick_x_repeat_at_us, input_now);
			if (!seek_direction) seek_direction = stick_seek;
		} else {
			stick_x_direction = 0;
			stick_x_repeat_at_us = 0;
		}
		if (seek_direction && snapshot.duration_ms &&
		    snapshot.state != VT_BACKGROUND_ERROR) {
			vt_background_playback_seek_relative(seek_direction * 10000);
			control_feedback = seek_direction < 0 ? MUSIC_CONTROL_BACK
			                                      : MUSIC_CONTROL_FORWARD;
			control_feedback_until_us = input_now + MUSIC_CONTROL_FEEDBACK_US;
		}
		if (!panels_open && (pressed & SCE_CTRL_SQUARE)) {
			g_shuffle = !g_shuffle;
			control_feedback = MUSIC_CONTROL_SHUFFLE;
			control_feedback_until_us = input_now + MUSIC_CONTROL_FEEDBACK_US;
		}
		if (!panels_open && (pressed & SCE_CTRL_TRIANGLE)) {
			g_repeat_one = !g_repeat_one;
			control_feedback = MUSIC_CONTROL_REPEAT;
			control_feedback_until_us = input_now + MUSIC_CONTROL_FEEDBACK_US;
		}
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
			int panel_x = (int)(960.0f - MUSIC_RIGHT_PANEL_W * right_animation);
			int handled = 0;
			for (int i = 0; i < 2; i++) {
				if (!ui_touch_hit_rect(touch.x, touch.y, panel_x + 18,
				                       (int)(MUSIC_RIGHT_ROW_Y + i * MUSIC_RIGHT_ROW_STEP),
				                       (int)MUSIC_RIGHT_PANEL_W - 36, 56)) continue;
				right_cursor = i;
				handled = 1;
				if (i == 0) g_shuffle = !g_shuffle;
				else g_repeat_one = !g_repeat_one;
			}
			if (!handled && touch.x < panel_x) right_open = 0;
			touch_flags = UI_TOUCH_EVENT_NONE;
		}

		if (!panels_open && !touch_reveal_only && hud_touch_interactive &&
		    snapshot.duration_ms &&
		    snapshot.state != VT_BACKGROUND_ERROR &&
		    (touch_flags & UI_TOUCH_EVENT_DOWN) &&
		    ui_touch_hit_rect(touch.x, touch.y, MUSIC_TIMELINE_X,
		                      MUSIC_TIMELINE_Y - 22, MUSIC_TIMELINE_W, 48)) {
			dragging_timeline = 1;
			drag_fraction = (float)(touch.x - MUSIC_TIMELINE_X) /
			                (float)MUSIC_TIMELINE_W;
			if (drag_fraction < 0.0f) drag_fraction = 0.0f;
			if (drag_fraction > 1.0f) drag_fraction = 1.0f;
		}
		if (!panels_open && dragging_timeline &&
		    (touch_flags & (UI_TOUCH_EVENT_MOVE |
		                    UI_TOUCH_EVENT_HOLD | UI_TOUCH_EVENT_UP))) {
			drag_fraction = (float)(touch.x - MUSIC_TIMELINE_X) /
			                (float)MUSIC_TIMELINE_W;
			if (drag_fraction < 0.0f) drag_fraction = 0.0f;
			if (drag_fraction > 1.0f) drag_fraction = 1.0f;
			if (touch_flags & UI_TOUCH_EVENT_UP) {
				vt_background_playback_seek_to(
				    (uint64_t)(drag_fraction * (float)snapshot.duration_ms));
				dragging_timeline = 0;
			}
		} else if (!panels_open && touch_reveal_only &&
		           (touch_flags & UI_TOUCH_EVENT_TAP)) {
			hud_visible = 1;
			hud_deadline_us = input_now + MUSIC_HUD_VISIBLE_US;
		} else if (!panels_open && !touch_reveal_only && hud_touch_interactive &&
		           (touch_flags & UI_TOUCH_EVENT_TAP)) {
			control_feedback = MUSIC_CONTROL_NONE;
			if (snapshot.state != VT_BACKGROUND_ERROR &&
			    ui_touch_hit_rect(touch.x, touch.y, 372, MUSIC_CONTROLS_Y, 166, MUSIC_CONTROLS_H)) {
				vt_background_playback_toggle_pause();
				control_feedback = MUSIC_CONTROL_PLAY;
			} else if (snapshot.duration_ms && snapshot.state != VT_BACKGROUND_ERROR &&
			           ui_touch_hit_rect(touch.x, touch.y, 220, MUSIC_CONTROLS_Y, 128, MUSIC_CONTROLS_H)) {
				vt_background_playback_seek_relative(-10000);
				control_feedback = MUSIC_CONTROL_BACK;
			} else if (snapshot.duration_ms && snapshot.state != VT_BACKGROUND_ERROR &&
			           ui_touch_hit_rect(touch.x, touch.y, 562, MUSIC_CONTROLS_Y, 128, MUSIC_CONTROLS_H)) {
				vt_background_playback_seek_relative(10000);
				control_feedback = MUSIC_CONTROL_FORWARD;
			} else if (ui_touch_hit_rect(touch.x, touch.y, 46, MUSIC_CONTROLS_Y, 166, MUSIC_CONTROLS_H)) {
				g_shuffle = !g_shuffle;
				control_feedback = MUSIC_CONTROL_SHUFFLE;
			} else if (ui_touch_hit_rect(touch.x, touch.y, 698, MUSIC_CONTROLS_Y, 216, MUSIC_CONTROLS_H)) {
				g_repeat_one = !g_repeat_one;
				control_feedback = MUSIC_CONTROL_REPEAT;
			}
			if (control_feedback != MUSIC_CONTROL_NONE) {
				control_feedback_until_us = input_now + MUSIC_CONTROL_FEEDBACK_US;
				hud_visible = 1;
				hud_deadline_us = input_now + MUSIC_HUD_VISIBLE_US;
			} else {
				hud_visible = 0;
				hud_deadline_us = 0;
			}
		}
		if (touch_sequence_flags & UI_TOUCH_EVENT_UP) touch_reveal_only = 0;
		ui_sections_sidebar_tick(&sidebar);
		/* Keep preparing, buffering, paused and error feedback explicit. The
		 * immersive quiet state is only entered during normal playback. */
		if (snapshot.state == VT_BACKGROUND_PLAYING &&
		    !panels_open && !dragging_timeline &&
		         input_now > hud_deadline_us)
			hud_visible = 0;
		if (vt_preferences_reduce_motion()) {
			right_animation = right_open ? 1.0f : 0.0f;
			right_focus_position = (float)right_cursor;
			hud_opacity = hud_visible ? 1.0f : 0.0f;
		} else {
			right_animation = approach_time_based(
			    right_animation, right_open ? 1.0f : 0.0f, 18.0f,
			    animation_delta_s);
			right_focus_position = approach_time_based(
			    right_focus_position, (float)right_cursor, 22.0f,
			    animation_delta_s);
			hud_opacity = approach_time_based(
			    hud_opacity, hud_visible ? 1.0f : 0.0f, 16.0f,
			    animation_delta_s);
		}
		if (!hud_visible && hud_opacity < 0.001f) hud_opacity = 0.0f;

		if (vt_preferences_music_keep_display_awake() || energy_saving)
			vt_display_keep_awake_tick();
		uint64_t now = sceKernelGetProcessTimeWide();
		const char *display_codec = snapshot.audio_codec[0]
		                            ? snapshot.audio_codec : codec_name;
		uint32_t display_bitrate = snapshot.audio_bitrate_kbps
		                           ? snapshot.audio_bitrate_kbps
		                           : average_bitrate_kbps;
		if (energy_saving) {
			if (now >= energy_lock_move_us) {
				energy_lock_position =
				    (energy_lock_position + 1) % PLAYER_POWER_SAVE_LOCK_POSITIONS;
				energy_lock_move_us = now + PLAYER_POWER_SAVE_LOCK_MOVE_US;
				energy_redraw = 1;
			}
			if (energy_redraw) {
				vita2d_start_drawing();
				vita2d_clear_screen();
				vita2d_draw_rectangle(0, 0, 960, 544, RGBA8(0, 0, 0, 255));
				int lock_x, lock_y;
				player_power_save_lock_position(energy_lock_position,
				                                &lock_x, &lock_y);
				draw_power_save_status_at(lock_x, lock_y);
				if (input_lock.locked) {
					draw_input_lock_at(lock_x,
					    lock_y > 430 ? lock_y - 50 : lock_y + 34, 1.0f);
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
		 * band for VitaMediaDeck's system header. */
		if (cover) draw_cover(cover, 368.0f, 92.0f, 224.0f, foreground);
		draw_music_state(&snapshot, foreground, now, hud_opacity);
		vita2d_font *display = ui_runtime_font(UI_FONT_DISPLAY);
		vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
		vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
		if (small && hud_opacity > 0.01f) {
			char shortcuts[192];
			ui_font_fit_text(small, UI_FONT_SMALL,
			                 vt_i18n_str(VT_STR_PLAYER_SHORTCUTS_HINT), shortcuts,
			                 sizeof(shortcuts), 300);
			vita2d_draw_rectangle(626, 94, 2, 30,
			                      fade_color(VT_THEME_COLD, hud_opacity));
			ui_font_draw_text(small, 640, 114,
			                  fade_color(VT_THEME_COLD_LIGHT, hud_opacity),
			                  UI_FONT_SMALL, shortcuts);
		}
		float title_y = cover ? 355.0f : 238.0f;
		draw_music_audio_hud(display_codec, display_bitrate,
		                     snapshot.audio_sample_rate,
		                     title_y - 20.0f, foreground, hud_opacity);
		/* Metadata itself stays readable in the quiet state. Only the protective
		 * panel belongs to the HUD and therefore dissolves with the controls. */
		draw_metadata_scrim(title_y, foreground, hud_opacity);
		draw_marquee(display, UI_FONT_DISPLAY, snapshot.title, title_y,
		              foreground, now, 0);
		draw_marquee(body, UI_FONT_BODY, snapshot.channel, title_y + 36.0f,
		              foreground, now, 600000ULL);
		draw_marquee(small, UI_FONT_SMALL, album, title_y + 64.0f,
		              foreground, now, 1200000ULL);
		float progress = dragging_timeline ? drag_fraction : snapshot.duration_ms
		               ? (float)snapshot.position_ms / (float)snapshot.duration_ms : 0.0f;
		if (progress < 0) progress = 0;
		if (progress > 1) progress = 1;
		vita2d_draw_rectangle(MUSIC_TIMELINE_X, MUSIC_TIMELINE_Y,
		                      MUSIC_TIMELINE_W, 5,
		                      fade_color(with_alpha(foreground, 72), hud_opacity));
		vita2d_draw_rectangle(MUSIC_TIMELINE_X, MUSIC_TIMELINE_Y,
		                      MUSIC_TIMELINE_W * progress, 5,
		                      fade_color(foreground, hud_opacity));
		vita2d_draw_fill_circle(MUSIC_TIMELINE_X + MUSIC_TIMELINE_W * progress,
		                        MUSIC_TIMELINE_Y + 2.5f,
		                        dragging_timeline ? 9.0f : 6.0f,
		                        fade_color(foreground, hud_opacity));
		char current[24], duration[24];
		uint64_t shown_position = dragging_timeline
		                        ? (uint64_t)(drag_fraction * snapshot.duration_ms)
		                        : snapshot.position_ms;
		time_text(shown_position, current);
		time_text(snapshot.duration_ms, duration);
		if (small) {
			ui_font_draw_text(small, MUSIC_TIMELINE_X, 465,
			                  fade_color(foreground, hud_opacity),
			                  UI_FONT_SMALL, current);
			int width = ui_font_text_width(small, UI_FONT_SMALL, duration);
			ui_font_draw_text(small, MUSIC_TIMELINE_X + MUSIC_TIMELINE_W - width,
			                  465, fade_color(foreground, hud_opacity),
			                  UI_FONT_SMALL, duration);
		}
		unsigned button_fill = foreground == RGBA8(255, 255, 255, 255)
		                     ? VT_THEME_GLASS_A(224)
		                     : RGBA8(240, 244, 248, 230);
		draw_music_action_button(46, MUSIC_CONTROLS_Y, 166, 56,
		                 music_control_fill(button_fill, MUSIC_CONTROL_SHUFFLE,
		                                    control_feedback,
		                                    control_feedback_until_us, now),
		                 vt_i18n_str(VT_STR_MUSIC_PLAYER_KEY_SQUARE),
		                 vt_i18n_str(g_shuffle ? VT_STR_MUSIC_PLAYER_SHUFFLE_ON
		                                           : VT_STR_MUSIC_PLAYER_SHUFFLE),
		                 g_shuffle, hud_opacity);
		draw_music_action_button(220, MUSIC_CONTROLS_Y, 128, 56,
		                 music_control_fill(button_fill, MUSIC_CONTROL_BACK,
		                                    control_feedback,
		                                    control_feedback_until_us, now),
		                 NULL, "<", 0, hud_opacity);
		draw_music_action_button(372, MUSIC_CONTROLS_Y, 166, 56,
		                 music_control_fill(button_fill, MUSIC_CONTROL_PLAY,
		                                    control_feedback,
		                                    control_feedback_until_us, now),
		                 vt_i18n_str(VT_STR_MUSIC_PLAYER_KEY_CROSS),
		                 snapshot.state == VT_BACKGROUND_ERROR
		                     ? vt_i18n_str(VT_STR_MINI_STATE_ERROR)
		                     : vt_i18n_str(snapshot.state == VT_BACKGROUND_PAUSED
		                         ? VT_STR_MUSIC_PLAYER_PLAY : VT_STR_MUSIC_PLAYER_PAUSE),
		                 snapshot.state != VT_BACKGROUND_ERROR, hud_opacity);
		draw_music_action_button(562, MUSIC_CONTROLS_Y, 128, 56,
		                 music_control_fill(button_fill, MUSIC_CONTROL_FORWARD,
		                                    control_feedback,
		                                    control_feedback_until_us, now),
		                 NULL, ">", 0, hud_opacity);
		draw_music_action_button(698, MUSIC_CONTROLS_Y, 216, 56,
		                 music_control_fill(button_fill, MUSIC_CONTROL_REPEAT,
		                                    control_feedback,
		                                    control_feedback_until_us, now),
		                 vt_i18n_str(VT_STR_MUSIC_PLAYER_KEY_TRIANGLE),
		                 vt_i18n_str(g_repeat_one ? VT_STR_MUSIC_PLAYER_REPEAT_ONE_ACTION
		                                             : VT_STR_MUSIC_PLAYER_REPEAT_ACTION),
		                 g_repeat_one, hud_opacity);
		float volume_opacity = 0.0f;
		if (now < volume_visible_until_us) {
			uint64_t remaining = volume_visible_until_us - now;
			volume_opacity = remaining >= MUSIC_HUD_FADE_US
			               ? 1.0f : (float)remaining / (float)MUSIC_HUD_FADE_US;
		}
		if (!right_open)
			draw_music_volume(vt_audio_volume_percent(), volume_opacity, foreground);
		if (input_lock.locked && hud_opacity > 0.01f)
			draw_input_lock_at(24, 86, hud_opacity);
		if (sidebar.animation > 0.01f)
			ui_sections_sidebar_draw(&sidebar);
		draw_music_right_sidebar(right_animation, right_focus_position,
		                         right_cursor, right_open, display_codec,
		                         display_bitrate, snapshot.audio_sample_rate,
		                         snapshot.audio_channels,
		                         snapshot.audio_bits_per_sample);
		vita2d_end_drawing();
		vita2d_wait_rendering_done();
		vita2d_swap_buffers();
		sceKernelDelayThread(1000);
	}
	if (cover) vita2d_free_texture(cover);
	if (action == UI_MUSIC_PLAYER_STOP) vt_background_playback_stop();
	return action;
}
