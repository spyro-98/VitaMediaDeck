#ifdef VMD_ENABLE_PAF_NATIVE
#include "ui/native_vita.h"
#endif
#include "ui/music_player.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "media/background_playback.h"
#include "media/image_loader.h"
#include "media/player_input_lock.h"
#include "media/player_power_save.h"
#include "media/audio_volume.h"
#include "i18n/i18n.h"
#include "settings/preferences.h"
#include "system/display_awake.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/audio_layout.h"
#include "ui/theme_scene.h"
#include "ui/loading_screen.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/theme.h"
#include "ui/touch.h"

#define MUSIC_TIMELINE_X UI_AUDIO_SEEK_X
#define MUSIC_TIMELINE_Y UI_AUDIO_SEEK_Y
#define MUSIC_TIMELINE_W UI_AUDIO_SEEK_W
#define MUSIC_CONTROLS_Y UI_AUDIO_CONTROL_Y
#define MUSIC_CONTROLS_H UI_AUDIO_CONTROL_H
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
#define MUSIC_RIGHT_PANEL_W 430.0f
#define MUSIC_RIGHT_ROW_Y 82.0f
#define MUSIC_RIGHT_ROW_STEP 52.0f
#define MUSIC_RIGHT_GROUPS ((1u<<0)|(1u<<2)|(1u<<3))
#define MUSIC_RIGHT_Y(row) ui_menu_group_focus_y((row),MUSIC_RIGHT_ROW_Y,MUSIC_RIGHT_ROW_STEP,MUSIC_RIGHT_GROUPS)
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

static vita2d_texture *load_artwork_texture(const char *path) {
	if (!path || !path[0]) return NULL;
	return vt_image_load_texture(path, 512, NULL, NULL, 0);
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
	                             unsigned foreground, float opacity,
	                             float left, float available, int centered) {
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
	char fitted[112];ui_font_fit_text(small,UI_FONT_SMALL,metadata,fitted,sizeof(fitted),(int)available);
	int text_width = ui_font_text_width(small, UI_FONT_SMALL, fitted);
	float text_x=left+(centered?(available-text_width)*.5f:0);
	float panel_x = text_x - 8;
	vita2d_draw_rectangle(panel_x, baseline - 21.0f,
	                      (float)text_width + 16.0f, 28.0f,
	                      fade_color(VT_THEME_GLASS_A(210), opacity));
	vita2d_draw_rectangle(panel_x, baseline - 21.0f, 3.0f, 28.0f,
	                      fade_color(VT_THEME_SPECTRAL, opacity));
	ui_font_draw_text(small, (int)text_x,
	                  (int)baseline, fade_color(foreground, opacity),
	                  UI_FONT_SMALL, fitted);
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
	/* A quiet frame leaves the entire cover visible and separates it from
	 * the ambient animation without acquisition brackets crowding the HUD. */
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

static const char *music_theme_label(int theme) {
	switch (theme) {
		case VT_MUSIC_THEME_NONE:
			return vt_i18n_str(VT_STR_MUSIC_PLAYER_THEME_NONE);
		case VT_MUSIC_THEME_APP:
			return vt_i18n_str(VT_STR_MUSIC_PLAYER_THEME_APP);
		case VT_MUSIC_THEME_RANDOM:
			return vt_i18n_str(VT_STR_MUSIC_PLAYER_THEME_RANDOM);
		case VT_MUSIC_THEME_COLD:
			return vt_i18n_str(VT_STR_MUSIC_PLAYER_THEME_COLD);
		case VT_MUSIC_THEME_WARM:
			return vt_i18n_str(VT_STR_MUSIC_PLAYER_THEME_WARM);
		default:
			return vt_i18n_str(VT_STR_MUSIC_PLAYER_THEME_ARTWORK);
	}
}

static int music_theme_step(int theme, int direction, int has_cover) {
	if (!direction) direction = 1;
	for (int attempts = 0; attempts < 6; attempts++) {
		theme = (theme + (direction < 0 ? 5 : 1)) % 6;
		if (has_cover || theme != VT_MUSIC_THEME_ARTWORK) return theme;
	}
	return VT_MUSIC_THEME_APP;
}

static void random_music_palette(unsigned colors[5], uint32_t seed) {
	if (!seed) seed = 0x6D2B79F5U;
	for (int i = 0; i < 5; i++) {
		seed = seed * 1664525U + 1013904223U;
		unsigned red = 48U + ((seed >> 16) & 159U);
		seed = seed * 1664525U + 1013904223U;
		unsigned green = 48U + ((seed >> 16) & 159U);
		seed = seed * 1664525U + 1013904223U;
		unsigned blue = 48U + ((seed >> 16) & 159U);
		colors[i] = palette_color(red, green, blue);
	}
}

static void music_theme_palette(int theme, const unsigned artwork[5],
	                            const unsigned random[5], unsigned out[5]) {
	const unsigned app[5] = {
		VT_THEME_SURFACE, RGBA8(24, 70, 64, 255),
		VT_THEME_SIGNAL_BRIGHT, VT_THEME_TEXT, VT_THEME_FOCUS
	};
	static const unsigned cold[5] = {
		RGBA8(10, 29, 70, 255), RGBA8(22, 70, 138, 255),
		RGBA8(29, 142, 183, 255), RGBA8(91, 209, 215, 255),
		RGBA8(177, 230, 244, 255)
	};
	static const unsigned warm[5] = {
		RGBA8(78, 16, 28, 255), RGBA8(137, 35, 29, 255),
		RGBA8(199, 72, 37, 255), RGBA8(232, 141, 55, 255),
		RGBA8(247, 205, 119, 255)
	};
	const unsigned *source = artwork;
	if (theme == VT_MUSIC_THEME_APP) source = app;
	else if (theme == VT_MUSIC_THEME_RANDOM) source = random;
	else if (theme == VT_MUSIC_THEME_COLD) source = cold;
	else if (theme == VT_MUSIC_THEME_WARM) source = warm;
	memcpy(out, source, sizeof(unsigned) * 5U);
}

static void draw_flow_background(const unsigned colors[5], uint64_t now,
	                             unsigned foreground, int theme) {
	float t = vt_preferences_reduce_motion()
	        ? 0.0f : (float)(now % 90000000ULL) / 1000000.0f;
	vita2d_draw_rectangle(0, UI_BRAND_HEADER_HEIGHT, 960,
	                      544 - UI_BRAND_HEADER_HEIGHT, VT_THEME_BG);
	if (!vt_ui_theme_legacy_layout(ui_theme_id()))
		ui_theme_fx_background(VT_THEME_BG);
	if (theme == VT_MUSIC_THEME_NONE) return;
	/* Apple Music-inspired ambient artwork: a few very large, feathered colour
	 * fields drift behind the cover. Concentric low-alpha discs approximate a
	 * soft blur without shaders or per-frame texture allocation on the Vita. */
	for (int blob = 0; blob < 5; blob++) {
		float phase = t * (0.035f + blob * 0.006f) + blob * 1.37f;
		float cx = 480.0f + cosf(phase) * (250.0f - blob * 21.0f);
		float cy = 290.0f + sinf(phase * 0.83f) * (142.0f - blob * 11.0f);
		float radius = 168.0f + (float)(blob % 3) * 42.0f;
		for (int feather = 0; feather < 5; feather++) {
			float fraction = (float)(5 - feather) / 5.0f;
			unsigned alpha = 5U + (unsigned)(fraction * fraction * 15.0f);
			vita2d_draw_fill_circle(cx, cy, radius * fraction,
			                        with_alpha(colors[blob], alpha));
		}
	}
	/* A black readability veil lets the title and controls remain stable even
	 * when a cover is mostly white or highly saturated. */
	vita2d_draw_rectangle(0, UI_BRAND_HEADER_HEIGHT, 960,
	                      544 - UI_BRAND_HEADER_HEIGHT, RGBA8(0, 0, 0, 76));
	/* No idle particles or optical connection lines over readable metadata. */
	(void)foreground;
}

static void draw_metadata_scrim(const UiAudioLayout *layout, float bottom, unsigned foreground,
	                            float opacity) {
	if (opacity <= 0.01f) return;
	unsigned fill = foreground == RGBA8(255, 255, 255, 255)
	              ? RGBA8(0, 5, 12, 128) : RGBA8(255, 255, 255, 132);
	unsigned edge = with_alpha(foreground, 58);
	float x=layout->text_x-12,y=layout->title_y-30,w=layout->text_width+24;
	vita2d_draw_rectangle(x, y, w, bottom-y+8,
	                      fade_color(fill, opacity));
	vita2d_draw_rectangle(x, y, w, 1,
	                      fade_color(edge, opacity));
	vita2d_draw_rectangle(x, bottom+7, w, 1,
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
	                         float opacity, float x) {
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
	vita2d_draw_rectangle(x, 82, width, 34, fade_color(panel, opacity));
	vita2d_draw_rectangle(x, 82, 3, 34,
	                      fade_color(snapshot->state == VT_BACKGROUND_ERROR
	                          ? VT_THEME_DANGER : VT_THEME_BLUE_LIGHT, opacity));
	if (loading) draw_music_spinner_compact(x+22, 99, now, opacity);
	ui_font_draw_text(small, x+14 + (loading ? 24 : 0), 106,
	                  fade_color(state_color, opacity),
	                  UI_FONT_SMALL, label);
}

static unsigned music_control_fill(unsigned base, int control,
	                               int feedback_control,
	                               uint64_t feedback_until, uint64_t now) {
	return control == feedback_control && now < feedback_until
	     ? VT_THEME_BLUE_LIGHT : base;
}

/* Compact symbols retain 48x44 touch regions. No per-frame textures/fonts. */
static void draw_transport(int x, int control, int paused, int enabled,
                           int feedback, uint64_t until, uint64_t now,
                           unsigned foreground, float opacity) {
	if(opacity<=.01f)return;
	float y=MUSIC_CONTROLS_Y;
#ifdef VMD_ENABLE_PAF_NATIVE
    const char *native_label = control == MUSIC_CONTROL_PLAY ? (paused ? "\xE2\x96\xB6" : "II")
                              : control == MUSIC_CONTROL_BACK ? "\xE2\x97\x81" : "\xE2\x96\xB7";
    if (ui_native_vita_button_fade(x,y,48,44,native_label,
        enabled && control==feedback && now<until,opacity*(enabled?1.0f:.4f))) return;
#endif
	unsigned fill=foreground==RGBA8(255,255,255,255)?VT_THEME_GLASS_A(224):RGBA8(240,244,248,230);
	fill=music_control_fill(fill,control,feedback,until,now);
	unsigned ink=fade_color(enabled?ui_contrast_bw(fill):VT_THEME_TEXT_FAINT,opacity);
	vita2d_draw_rectangle(x,y,48,44,fade_color(fill,opacity));
 ui_theme_scene_transport(x,y,48,44,control==MUSIC_CONTROL_PLAY,opacity);
	if(control==MUSIC_CONTROL_PLAY && !paused) {
		vita2d_draw_rectangle(x+17,y+12,4,20,ink);
		vita2d_draw_rectangle(x+27,y+12,4,20,ink);
	} else if(control==MUSIC_CONTROL_PLAY) {
		for(int i=0;i<14;i++)vita2d_draw_rectangle(x+18+i,y+11+i*.75f,1,22-i*1.5f,ink);
	} else {
		int back=control==MUSIC_CONTROL_BACK;
		vita2d_draw_rectangle(x+(back?12:34),y+12,2,20,ink);
		for(int i=0;i<13;i++)vita2d_draw_rectangle(x+(back?29-i:18+i),y+12+i*.75f,1,20-i*1.5f,ink);
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
#ifdef VMD_ENABLE_PAF_NATIVE
    ui_native_vita_media_text(1);
#endif
	if (small)
		ui_font_draw_text(small, text_x, baseline, VT_THEME_COLD_DIM,
		                  UI_FONT_SMALL,
		                  vt_i18n_str(VT_STR_PLAYER_POWER_SAVE_ACTIVE));
#ifdef VMD_ENABLE_PAF_NATIVE
    ui_native_vita_media_text(0);
#endif
}

static void draw_marquee(vita2d_font *font, float size, const char *text,
	                     float baseline, unsigned color, uint64_t now,
	                     uint64_t phase_offset, float left, float available, int centered) {
	if (!font || !text || !text[0]) return;
	float width = (float)ui_font_text_width(font, size, text);
	if (width <= available) {
		ui_font_draw_text(font, left+(centered?(available-width)*.5f:0), baseline,
		                  color, size, text);
		return;
	}
	if (vt_preferences_reduce_motion()) {
		char fitted[512];ui_font_fit_text(font,size,text,fitted,sizeof(fitted),(int)available);
		ui_font_draw_text(font,left,baseline,color,size,fitted);
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
	                          (int)(left+available), (int)(baseline + 8));
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
	                                 int music_theme,
	                                 const char *codec_name,
	                                 uint32_t bitrate_kbps,
	                                 uint32_t sample_rate,
	                                 uint16_t channels,
	                                 uint16_t bits_per_sample) {
	if (animation <= 0.01f) return;
#ifdef VMD_ENABLE_PAF_NATIVE
    int native_scope = ui_native_vita_scope(UI_NATIVE_SCOPE_ACTIONS);
#endif
	const float width = MUSIC_RIGHT_PANEL_W;
	float x = 960.0f - width * animation;
	ui_menu_surface(x,0,width,544,1);
	vita2d_draw_rectangle(x, 0, 1, 544, VT_THEME_BORDER);
	vita2d_draw_rectangle(x + 4, 0, width - 4, 1, VT_THEME_BORDER);
	vita2d_font *body = ui_runtime_font(UI_FONT_BODY);
	vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
	if (body)
		ui_font_draw_text_exact(body, x + 28, 47, UI_MENU_TEXT, UI_FONT_BODY,
		                  vt_i18n_str(VT_STR_PLAYER_PANEL_TITLE));
	if (small) {

		int key_width = ui_font_text_width(small, UI_FONT_SMALL, UI_PAD_R1);
		ui_font_draw_text_exact(small,
		                  (int)(x + width - 44.0f - key_width * 0.5f), 38,
		                  UI_MENU_TEXT,
		                  UI_FONT_SMALL, UI_PAD_R1);
	}
	const char *labels[3] = {
		vt_i18n_str(VT_STR_MUSIC_PLAYER_ORDER_LABEL),
		vt_i18n_str(VT_STR_MUSIC_PLAYER_REPEAT_LABEL),
		vt_i18n_str(VT_STR_MUSIC_PLAYER_THEME_LABEL)
	};
	const char *values[3] = {
		vt_i18n_str(g_shuffle ? VT_STR_MUSIC_PLAYER_RANDOM
		                       : VT_STR_MUSIC_PLAYER_LINEAR),
		vt_i18n_str(g_repeat_one ? VT_STR_MUSIC_PLAYER_REPEAT_ONE
		                          : VT_STR_MUSIC_PLAYER_REPEAT_OFF),
		music_theme_label(music_theme)
	};
	for (int i = 0; i < 3; i++) {
		float y = MUSIC_RIGHT_Y(i);
		ui_menu_row(x+18,y,width-36,MUSIC_RIGHT_ROW_STEP,focused&&i==cursor,1);
        if(i==0||i==2)ui_menu_category(x+36,y-UI_MENU_HEADING_HEIGHT,width-72,i==0?UI_MENU_GROUP_PLAYBACK:UI_MENU_GROUP_VIEW);
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
		float y = MUSIC_RIGHT_Y(i+3);
		ui_menu_row(x+18,y,width-36,MUSIC_RIGHT_ROW_STEP,0,1);
        if(i==0){ui_menu_category(x+36,y-UI_MENU_HEADING_HEIGHT,width-72,UI_MENU_GROUP_INFO);}
		if (!small) continue;
		ui_font_draw_text_exact(small, x + 36, y + 18, UI_MENU_MUTED,
		                  UI_FONT_SMALL, info_labels[i]);
		char fitted[96];
		ui_font_fit_text(small, UI_FONT_SMALL, info_values[i], fitted,
		                 sizeof(fitted), (int)width - 72);
		int value_width = ui_font_text_width(small, UI_FONT_SMALL, fitted);
		ui_font_draw_text_exact(small, x + width - value_width - 30, y + 38,
		                  VT_THEME_COLD_LIGHT, UI_FONT_SMALL, fitted);
	}
    UiFocusMotion menu_focus={.x=x+18,.y=MUSIC_RIGHT_Y(focus_position),.width=width-36,.height=48,.initialized=1};
    ui_menu_focus_draw(&menu_focus);
	for (int i = 0; i < 3; i++) {
		float y = MUSIC_RIGHT_Y(i);
		if (!small) continue;
		ui_font_draw_text_exact(small, x + 36, y + 20,
		                  focused && i == cursor ? UI_MENU_TEXT
		                                           : UI_MENU_MUTED,
		                  UI_FONT_SMALL, labels[i]);
		int value_width = ui_font_text_width(small, UI_FONT_SMALL, values[i]);
		ui_font_draw_text_exact(small, x + width - value_width - 30, y + 38,
		                  focused && i == cursor ? UI_MENU_ACCENT
		                                           : UI_MENU_MUTED,
		                  UI_FONT_SMALL, values[i]);
	}
#ifdef VMD_ENABLE_PAF_NATIVE
#endif
	if (small)
		ui_font_draw_text_exact(small, x + 28, 518, UI_MENU_MUTED,
		                  UI_FONT_SMALL, vt_i18n_str(VT_STR_MUSIC_PLAYER_PANEL_HINT));
#ifdef VMD_ENABLE_PAF_NATIVE
    ui_native_vita_scope(native_scope);
#endif
}

int ui_music_player_shuffle_enabled(void) { return g_shuffle; }
int ui_music_player_repeat_one_enabled(void) { return g_repeat_one; }

int ui_music_player_run(const char *artwork_path, const char *album,
	                    const char *codec_name,
	                    uint32_t average_bitrate_kbps) {
	vita2d_texture *cover = load_artwork_texture(artwork_path);
	unsigned artwork_colors[5], random_colors[5], palette[5];
	artwork_palette(cover, artwork_colors);
	random_music_palette(random_colors,
	                     (uint32_t)sceKernelGetProcessTimeWide() ^
	                     (uint32_t)(uintptr_t)artwork_path);
	int music_theme = vt_preferences_music_theme();
	if (!cover && music_theme == VT_MUSIC_THEME_ARTWORK)
		music_theme = VT_MUSIC_THEME_APP;
	music_theme_palette(music_theme, artwork_colors, random_colors, palette);
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
	int audio_layout=vt_preferences_audio_layout();
	sidebar.audio_layout_available=1;
	sidebar.audio_layout_cursor=sidebar.audio_layout_selected=audio_layout;
	UiAudioLayout layout=ui_theme_audio_layout(audio_layout,cover!=NULL);
	int right_open = 0;
	int right_cursor = 0;
	float right_animation = 0.0f;
	float right_focus_position = 0.0f;
	UiFocusMotion right_motion={0};
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
		int input_available = sceCtrlPeekBufferPositive(0, &ctrl, 1) > 0;
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
			energy_redraw = 3;
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
		if (!input_lock.locked && hud_visible && !energy_saving &&
		    ui_brand_sections_hit(touch_flags, touch.x, touch.y)) {
			pressed |= vt_preferences_player_swap_shoulders() ? SCE_CTRL_LEFT : SCE_CTRL_LTRIGGER;
			touch_flags = UI_TOUCH_EVENT_NONE;
		}
		unsigned touch_sequence_flags = touch_flags;
		if ((touch_flags & UI_TOUCH_EVENT_DOWN) && !hud_touch_interactive)
			touch_reveal_only = 1;
		if (input_lock.locked) {
			pressed = 0;
			touch_flags = UI_TOUCH_EVENT_NONE;
			dragging_timeline = 0;
		}
        if(pressed&SCE_CTRL_TRIANGLE){
            vt_audio_volume_toggle_mute();volume_visible_until_us=input_now+MUSIC_VOLUME_VISIBLE_US;
            hud_visible=1;hud_deadline_us=input_now+MUSIC_HUD_VISIBLE_US;
            pressed&=~SCE_CTRL_TRIANGLE;
        }
		int power_event = player_power_save_update(
		    &power_save_input, ctrl.buttons, input_now,
		    input_available && !input_lock.locked);
		pressed &= ~SCE_CTRL_START;
		if (power_event == PLAYER_POWER_SAVE_EVENT_TOGGLE) {
			energy_saving = !energy_saving;
			energy_redraw = 3;
			energy_lock_position = 0;
			energy_lock_move_us = input_now + PLAYER_POWER_SAVE_LOCK_MOVE_US;
			dragging_timeline = 0;
			touch_reveal_only = 0;
			sidebar.open = 0;
			sidebar.animation = 0.0f;
			right_open = 0;
			right_animation = 0.0f;
			ui_touch_reset();
			touch_flags = UI_TOUCH_EVENT_NONE;
		}
		if (power_event == PLAYER_POWER_SAVE_EVENT_SHORT_PRESS) {
			action = UI_MUSIC_PLAYER_MINIMIZE;
			break;
		}
		unsigned panel_buttons = vt_preferences_player_swap_shoulders()
		    ? SCE_CTRL_LEFT | SCE_CTRL_RIGHT : SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER;
		if (energy_saving && !input_lock.locked &&
		    ((pressed & panel_buttons) || (touch_flags & UI_TOUCH_EVENT_DOWN))) {
			energy_saving = 0;
			hud_visible = 1;
			hud_opacity = 1.0f;
			hud_deadline_us = input_now + MUSIC_HUD_VISIBLE_US;
			touch_reveal_only = 0;
			ui_touch_reset();
			touch_flags = UI_TOUCH_EVENT_NONE;
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
		if (!input_lock.locked) {
			/* Default: shoulders own the panels and D-pad owns transport. The
			 * compatibility option swaps exactly those roles, matching video. */
			unsigned left_toggle = swapped ? SCE_CTRL_LEFT : SCE_CTRL_LTRIGGER;
			unsigned right_toggle = swapped ? SCE_CTRL_RIGHT : SCE_CTRL_RTRIGGER;
			if ((pressed & left_toggle) &&
			    (right_open || right_animation > 0.01f)) {
				right_open = 0;
				right_animation = 0.0f;
			}
			if ((pressed & right_toggle) &&
			    (sidebar.open || sidebar.animation > 0.01f)) {
				sidebar.open = 0;
				sidebar.animation = 0.0f;
			}
			if (swapped) pressed &= ~(SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER);
			if (swapped && !sidebar.open && !right_open &&
			    (pressed & SCE_CTRL_LEFT)) {
				pressed &= ~SCE_CTRL_LEFT;
				pressed |= SCE_CTRL_LTRIGGER;
			}
			if (pressed & right_toggle) {
				if (sidebar.open || sidebar.animation > 0.01f) {
					sidebar.open = 0;
					sidebar.animation = 0.0f;
				}
				right_open = !right_open;
				right_cursor = 0;
				right_focus_position = 0.0f;
				pressed &= ~right_toggle;
			}
			if (!right_open && right_animation <= 0.01f) {
				int section = ui_sections_sidebar_handle_buttons(
				    &sidebar, &pressed, ctrl.buttons, ctrl.ly);
				if(section>=UI_SECTION_AUDIO_LAYOUT_BASE && section<UI_SECTION_AUDIO_LAYOUT_BASE+3) {
					int next=section-UI_SECTION_AUDIO_LAYOUT_BASE;
					if(!vt_preferences_set_audio_layout(next))audio_layout=sidebar.audio_layout_selected=next;
					else ui_message_show(vt_i18n_str(VT_STR_MUSIC_LAYOUT_TITLE),vt_i18n_str(VT_STR_MUSIC_LAYOUT_SAVE_FAILED),2000);
					section=UI_SECTION_NONE;
				}
				if (section != UI_SECTION_NONE) {
					action = UI_MUSIC_PLAYER_SECTION_BASE + section;
					break;
				}
			} else if (right_open) {
				int stick_nav = stick_repeat_step(ctrl.ly, &stick_y_direction,
				                                  &stick_y_repeat_at_us, input_now);
				if (((pressed & SCE_CTRL_UP) || stick_nav < 0) && right_cursor > 0)
					right_cursor--;
				if (((pressed & SCE_CTRL_DOWN) || stick_nav > 0) && right_cursor < 2)
					right_cursor++;
				if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) {
					if (right_cursor == 0) g_shuffle = !g_shuffle;
					else if (right_cursor == 1) g_repeat_one = !g_repeat_one;
					else {
						int direction = pressed & SCE_CTRL_LEFT ? -1 : 1;
						music_theme = music_theme_step(music_theme, direction,
						                               cover != NULL);
						vt_preferences_set_music_theme(music_theme);
						music_theme_palette(music_theme, artwork_colors,
						                    random_colors, palette);
					}
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
		if (!panels_open && !input_lock.locked) {
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
		if (!snapshot.is_live && !panels_open && (pressed & SCE_CTRL_SQUARE)) {
			g_shuffle = !g_shuffle;
			control_feedback = MUSIC_CONTROL_SHUFFLE;
			control_feedback_until_us = input_now + MUSIC_CONTROL_FEEDBACK_US;
		}

		if (!panels_open && (pressed & SCE_CTRL_CIRCLE)) {
			action = UI_MUSIC_PLAYER_STOP;
			break;
		}

		int ry_dev = (int)ctrl.ry - MUSIC_STICK_CENTER;
		if (!panels_open && !input_lock.locked &&
		    (ry_dev > MUSIC_STICK_DEADZONE || ry_dev < -MUSIC_STICK_DEADZONE) &&
		    input_now - last_volume_us >= MUSIC_VOLUME_INTERVAL_US) {
			int volume = vt_audio_volume_level_percent() +
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
			if(section>=UI_SECTION_AUDIO_LAYOUT_BASE && section<UI_SECTION_AUDIO_LAYOUT_BASE+3) {
				int next=section-UI_SECTION_AUDIO_LAYOUT_BASE;
				if(!vt_preferences_set_audio_layout(next))audio_layout=sidebar.audio_layout_selected=next;
				else ui_message_show(vt_i18n_str(VT_STR_MUSIC_LAYOUT_TITLE),vt_i18n_str(VT_STR_MUSIC_LAYOUT_SAVE_FAILED),2000);
				section=UI_SECTION_NONE;
			}
			if (section != UI_SECTION_NONE) {
				action = UI_MUSIC_PLAYER_SECTION_BASE + section;
				break;
			}
			touch_flags = UI_TOUCH_EVENT_NONE;
		} else if (right_open && (touch_flags & UI_TOUCH_EVENT_TAP)) {
			int panel_x = (int)(960.0f - MUSIC_RIGHT_PANEL_W * right_animation);
			int handled = 0;
			for (int i = 0; i < 3; i++) {
				if (!ui_touch_hit_rect(touch.x, touch.y, panel_x + 18,
				                       (int)(MUSIC_RIGHT_Y(i)),
				                       (int)MUSIC_RIGHT_PANEL_W - 36, 48)) continue;
				right_cursor = i;
				handled = 1;
				if (i == 0) g_shuffle = !g_shuffle;
				else if (i == 1) g_repeat_one = !g_repeat_one;
				else {
					music_theme = music_theme_step(music_theme, 1, cover != NULL);
					vt_preferences_set_music_theme(music_theme);
					music_theme_palette(music_theme, artwork_colors,
					                    random_colors, palette);
				}
			}
			if (!handled && touch.x < panel_x) right_open = 0;
			touch_flags = UI_TOUCH_EVENT_NONE;
		}

		if (!panels_open && !touch_reveal_only && hud_touch_interactive &&
		    snapshot.duration_ms &&
		    snapshot.state != VT_BACKGROUND_ERROR &&
		    (touch_flags & UI_TOUCH_EVENT_DOWN) &&
		    ui_touch_hit_rect(touch.x, touch.y, MUSIC_TIMELINE_X,
		                      MUSIC_TIMELINE_Y - 16, MUSIC_TIMELINE_W, 32)) {
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
			    ui_touch_hit_rect(touch.x, touch.y, UI_AUDIO_PLAY_X, MUSIC_CONTROLS_Y, UI_AUDIO_CONTROL_W, MUSIC_CONTROLS_H)) {
				vt_background_playback_toggle_pause();
				control_feedback = MUSIC_CONTROL_PLAY;
			} else if (snapshot.duration_ms && snapshot.state != VT_BACKGROUND_ERROR &&
			           ui_touch_hit_rect(touch.x, touch.y, UI_AUDIO_BACK_X, MUSIC_CONTROLS_Y, UI_AUDIO_CONTROL_W, MUSIC_CONTROLS_H)) {
				vt_background_playback_seek_relative(-10000);
				control_feedback = MUSIC_CONTROL_BACK;
			} else if (snapshot.duration_ms && snapshot.state != VT_BACKGROUND_ERROR &&
			           ui_touch_hit_rect(touch.x, touch.y, UI_AUDIO_NEXT_X, MUSIC_CONTROLS_Y, UI_AUDIO_CONTROL_W, MUSIC_CONTROLS_H)) {
				vt_background_playback_seek_relative(10000);
				control_feedback = MUSIC_CONTROL_FORWARD;
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

			hud_opacity = approach_time_based(
			    hud_opacity, hud_visible ? 1.0f : 0.0f, 16.0f,
			    animation_delta_s);
		}
		ui_focus_motion_tick(&right_motion,0,(float)right_cursor,1,1);
		right_focus_position=right_motion.y;
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
				energy_redraw = 3;
			}
			if (energy_redraw) {
				const unsigned *music_theme_before=ui_theme_media_begin();
		 vita2d_start_drawing();
				vita2d_clear_screen();
				vita2d_draw_rectangle(0, 0, 960, 544, RGBA8(0, 0, 0, 255));
				int lock_x, lock_y;
				player_power_save_lock_position(energy_lock_position,
				                                &lock_x, &lock_y);
				draw_power_save_status_at(lock_x, lock_y);
				if (input_lock.locked) {
					draw_input_lock_at(lock_x,
					    lock_y > 430 ? lock_y - 50 : lock_y + 34, 0.18f);
				}
				ui_theme_media_end(music_theme_before);
		 vita2d_end_drawing();
				vita2d_wait_rendering_done();
				vita2d_swap_buffers();
				energy_redraw--;
			}
			sceKernelDelayThread(10000);
			continue;
		}
		const unsigned *music_theme_before=ui_theme_media_begin();
		 vita2d_start_drawing();
		vita2d_clear_screen();
		ui_theme_scene_begin(UI_SCENE_AUDIO,snapshot.title);
#ifdef VMD_ENABLE_PAF_NATIVE
        ui_native_vita_media_text(1);
#endif
		draw_flow_background(palette,now,foreground,music_theme);
		ui_brand_draw_breadcrumbs(snapshot.is_live ? vt_i18n_str(VT_STR_ONLINE_RADIO) :
		    snapshot.is_podcast ? "Podcast" : vt_i18n_str(VT_STR_LOCAL_MEDIA_LOCAL_AUDIO),
		    NULL, snapshot.title);
		UiAudioLayout target=ui_theme_audio_layout(audio_layout,cover!=NULL);
		/* Presentation-only interpolation; artwork remains the same texture. */
		float blend=vt_preferences_reduce_motion()?1.0f:1.0f-expf(-20.0f*animation_delta_s);
#define MOVE_LAYOUT(field) layout.field+=(target.field-layout.field)*blend
		MOVE_LAYOUT(cover_x);MOVE_LAYOUT(cover_y);MOVE_LAYOUT(cover_size);
		MOVE_LAYOUT(text_x);MOVE_LAYOUT(text_width);MOVE_LAYOUT(title_y);
#undef MOVE_LAYOUT
		int centered = vt_ui_theme_legacy_layout(ui_theme_id())
		                   ? (!cover || audio_layout == VT_AUDIO_LAYOUT_CENTER)
		                   : (audio_layout == VT_AUDIO_LAYOUT_CENTER &&
		                      (ui_theme_id() == VT_UI_THEME_APERTURE ||
		                       ui_theme_id() == VT_UI_THEME_GHOST_MESH));
		if (!vt_ui_theme_legacy_layout(ui_theme_id())) {
			float cx = layout.cover_x + layout.cover_size * .5f,
			      cy = layout.cover_y + layout.cover_size * .5f;
			ui_theme_scene_model(UI_SCENE_AUDIO, cx, cy, layout.cover_size * 1.4f, 1,
			                     snapshot.state == VT_BACKGROUND_PLAYING);
		}
		if(cover){
			if (vt_ui_theme_legacy_layout(ui_theme_id()))
				draw_cover(cover, layout.cover_x, layout.cover_y, layout.cover_size, foreground);
			else
				ui_theme_scene_art(cover, layout.cover_x, layout.cover_y, layout.cover_size,
				                   snapshot.state == VT_BACKGROUND_PLAYING);
		}
		draw_music_state(&snapshot,foreground,now,hud_opacity,centered?18:layout.text_x);
		vita2d_font *display=ui_runtime_font(UI_FONT_DISPLAY);
		vita2d_font *body=ui_runtime_font(UI_FONT_BODY);
		vita2d_font *small=ui_runtime_font(UI_FONT_SMALL);
		float line=layout.title_y;
		float author_y=line+36;
		if(snapshot.channel[0])line+=36;
		float album_y=line+28;
		if(album && album[0])line+=28;
		float info_y=line+26;
		draw_metadata_scrim(&layout,info_y,foreground,hud_opacity);
		draw_marquee(display,UI_FONT_DISPLAY,snapshot.title,layout.title_y,foreground,now,0,
		    layout.text_x,layout.text_width,centered);
		if(snapshot.channel[0])draw_marquee(body,UI_FONT_BODY,snapshot.channel,author_y,foreground,now,600000ULL,
		    layout.text_x,layout.text_width,centered);
		if(album && album[0])draw_marquee(small,UI_FONT_SMALL,album,album_y,foreground,now,1200000ULL,
		    layout.text_x,layout.text_width,centered);
		draw_music_audio_hud(display_codec,display_bitrate,snapshot.audio_sample_rate,
		    info_y,foreground,hud_opacity,layout.text_x,layout.text_width,centered);
		float progress = dragging_timeline ? drag_fraction : snapshot.duration_ms
		               ? (float)snapshot.position_ms / (float)snapshot.duration_ms : 0.0f;
		if (progress < 0) progress = 0;
		if (progress > 1) progress = 1;
		if (!snapshot.is_live) {
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
		}
		char current[24], duration[24];
		uint64_t shown_position = dragging_timeline
		                        ? (uint64_t)(drag_fraction * snapshot.duration_ms)
		                        : snapshot.position_ms;
		time_text(shown_position, current);
		time_text(snapshot.duration_ms, duration);
		if (snapshot.is_live) snprintf(duration, sizeof(duration), "Live");
		if(small) {
			char elapsed[64];
			snprintf(elapsed,sizeof(elapsed),snapshot.is_live?"%s":"%s / %s",
			    snapshot.is_live?"Live":current,duration);
			ui_font_draw_text(small,18,478,fade_color(foreground,hud_opacity),UI_FONT_SMALL,elapsed);
			char hint[128];
			snprintf(hint,sizeof(hint),UI_PAD_TRIANGLE "  %s   " UI_PAD_L1 "  %s",vt_i18n_str(vt_audio_volume_muted()?VT_STR_PLAYER_UNMUTE:VT_STR_PLAYER_MUTE),vt_i18n_str(VT_STR_SECTIONS_TITLE));
			char fitted[128];ui_font_fit_text(small,UI_FONT_SMALL,hint,fitted,sizeof(fitted),580);
			ui_font_draw_text(small,18,526,fade_color(foreground,hud_opacity),UI_FONT_SMALL,fitted);
			snprintf(hint,sizeof(hint),UI_PAD_R1 "  %s",vt_i18n_str(VT_STR_PLAYER_PANEL_TITLE));
			ui_font_fit_text(small,UI_FONT_SMALL,hint,fitted,sizeof(fitted),310);
			int width=ui_font_text_width(small,UI_FONT_SMALL,fitted);
			ui_font_draw_text(small,942-width,526,fade_color(foreground,hud_opacity),UI_FONT_SMALL,fitted);
		}
		int ready=snapshot.state!=VT_BACKGROUND_ERROR;
		if(!snapshot.is_live)draw_transport(UI_AUDIO_BACK_X,MUSIC_CONTROL_BACK,0,ready&&snapshot.duration_ms,
		    control_feedback,control_feedback_until_us,now,foreground,hud_opacity);
		draw_transport(UI_AUDIO_PLAY_X,MUSIC_CONTROL_PLAY,snapshot.state==VT_BACKGROUND_PAUSED,ready,
		    control_feedback,control_feedback_until_us,now,foreground,hud_opacity);
		if(!snapshot.is_live)draw_transport(UI_AUDIO_NEXT_X,MUSIC_CONTROL_FORWARD,0,ready&&snapshot.duration_ms,
		    control_feedback,control_feedback_until_us,now,foreground,hud_opacity);
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
#ifdef VMD_ENABLE_PAF_NATIVE
        ui_native_vita_media_text(0);
#endif
		if (sidebar.animation > 0.01f)
			ui_sections_sidebar_draw(&sidebar);
		draw_music_right_sidebar(right_animation, right_focus_position,
		                         right_cursor, right_open, music_theme, display_codec,
		                         display_bitrate, snapshot.audio_sample_rate,
		                         snapshot.audio_channels,
		                         snapshot.audio_bits_per_sample);
		ui_theme_media_end(music_theme_before);
		 vita2d_end_drawing();
		vita2d_wait_rendering_done();
		vita2d_swap_buffers();
		sceKernelDelayThread(1000);
	}
	if (cover) vita2d_free_texture(cover);
	if (action == UI_MUSIC_PLAYER_STOP) vt_background_playback_stop();
	return action;
}
