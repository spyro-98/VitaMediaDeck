#include "ui/brand.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>
#include <psp2/registrymgr.h>
#include <psp2/rtc.h>
#include <psp2/system_param.h>

#include <vita2d.h>

#include "i18n/i18n.h"
#include "ui/runtime.h"
#include "ui/theme.h"

#include <vita_https.h>

#define SCREEN_WIDTH  960
#define SCREEN_HEIGHT 544

#define HEADER_ICON_SIZE 48
#define HEADER_ICON_X 18
#define HEADER_ICON_Y ((UI_BRAND_HEADER_HEIGHT - HEADER_ICON_SIZE) / 2)
#define HEADER_NAME_X (HEADER_ICON_X + HEADER_ICON_SIZE + 14)
#define HEADER_NAME_BASELINE ((UI_BRAND_HEADER_HEIGHT + UI_FONT_DISPLAY) / 2 - 3)

/* --- Header layout -------------------------------------------------------
 *
 * The optional title/editor field is horizontally centred on the 960px screen
 * and the status indicators are pinned to the right edge:
 *
 *   left block   : logo 18..66, then "VitaTube" from x=80 in Poppins 28px.
 *                  8 glyphs, worst-case ~0.7em advance -> ~157px, ends ~237.
 *   search field : 430px wide (UNCHANGED: it still fits), centred ->
 *                  x = (960-430)/2 = 265, right edge 695.
 *                  Gap to the title: 265 - 241 = 24px minimum. OK.
 *   status block : leftmost possible x is
 *                    960 - 16 (right margin)
 *                        - 33 (battery incl. positive terminal)
 *                        - 14 (gap) - 96 (worst-case clock text width)
 *                        - 14 (gap) - 25 (Wi-Fi bars)          = 762.
 *                  Gap to the search field: 762 - 695 = 67px minimum. OK.
 *
 * Because 430px still clears both neighbours once centred, the field keeps its
 * original width and therefore its original text capacity (SEARCH_BOX_W - 96),
 * so no truncation behaviour changes. */
#define SEARCH_BOX_W 430
#define SEARCH_BOX_H 54
#define SEARCH_BOX_X ((SCREEN_WIDTH - SEARCH_BOX_W) / 2)
#define SEARCH_BOX_Y ((UI_BRAND_HEADER_HEIGHT - SEARCH_BOX_H) / 2)
#define SEARCH_CLEAR_SIZE 34
#define SEARCH_CLEAR_X (SEARCH_BOX_X + SEARCH_BOX_W - SEARCH_CLEAR_SIZE - 8)
#define SEARCH_CLEAR_Y (SEARCH_BOX_Y + (SEARCH_BOX_H - SEARCH_CLEAR_SIZE) / 2)

#define COLOR_BG      VT_THEME_BG
#define COLOR_TEXT    VT_THEME_TEXT
#define COLOR_MUTED   VT_THEME_TEXT_MUTED
#define COLOR_BLUE    VT_THEME_BLUE_BRIGHT
#define COLOR_FIELD   RGBA8(7, 15, 26, 238)
/* Status indicators: same palette family as the rest of the topbar. */
#define COLOR_DIM     VT_THEME_BORDER
#define COLOR_BATT_OK   RGBA8(76, 217, 100, 255)
#define COLOR_BATT_WARN RGBA8(255, 204, 0, 255)
#define COLOR_BATT_LOW  RGBA8(255, 69, 58, 255)

/* Right-anchored status indicators, laid out from the right edge inwards. */
#define STATUS_MARGIN_R 16
#define STATUS_GAP      14

static int g_brand_loading;

#define BATT_BODY_W 30
#define BATT_BODY_H 16
#define BATT_TIP_W   3
#define BATT_TIP_H   8
#define BATT_TOTAL_W (BATT_BODY_W + BATT_TIP_W)                        /* 33 */
#define BATT_X (SCREEN_WIDTH - STATUS_MARGIN_R - BATT_TOTAL_W)         /* 911 */
#define BATT_Y ((UI_BRAND_HEADER_HEIGHT - BATT_BODY_H) / 2)            /* 28 */

/* The clock is right-aligned on CLOCK_RIGHT and the Wi-Fi icon is placed
 * relative to the *measured* text width, so 24h mode ("23:59") doesn't leave a
 * hole where 12h mode ("12:59 PM") would need room. CLOCK_MAX_W only bounds
 * the layout for the overlap proof above and clamps a pathological font. */
#define CLOCK_RIGHT (BATT_X - STATUS_GAP)                              /* 897 */
#define CLOCK_MAX_W 96
#define CLOCK_BASELINE ((UI_BRAND_HEADER_HEIGHT + UI_FONT_BODY) / 2 - 3)

#define WIFI_BARS      4
#define WIFI_BAR_W     4
#define WIFI_BAR_GAP   3
#define WIFI_W (WIFI_BARS * WIFI_BAR_W + (WIFI_BARS - 1) * WIFI_BAR_GAP) /* 25 */
#define WIFI_BOTTOM_Y  (UI_BRAND_HEADER_HEIGHT / 2 + 9)
#define WIFI_BAR_BASE_H 5
#define WIFI_BAR_STEP   4   /* heights 5, 9, 13, 17 */

/* --- Cached system status ------------------------------------------------
 *
 * draw_header() runs once per frame on EVERY screen, so scePower/sceRtc/
 * sceNetCtl must not be hit at 60Hz. The cache refreshes on a 2s cadence.
 *
 * The deadline is stored as an ABSOLUTE timestamp and compared with `<`.
 * Deliberately no `now - last >= period` form: these are uint64_t and this
 * project already shipped a confirmed hardware bug where such a subtraction
 * underflowed into a huge value. The clock is also read exactly ONCE per call
 * and reused, so the comparison and the new deadline can never disagree. */
#define STATUS_REFRESH_US 2000000ULL

typedef struct {
	uint64_t next_refresh_us;   /* 0 => never sampled yet, refresh immediately */

	int battery_pct;            /* 0..100, or -1 when unavailable */
	int battery_charging;

	int clock_valid;
	int clock_hour;             /* 0..23 */
	int clock_minute;           /* 0..59 */

	int wifi_valid;             /* 0 => sceNetCtlInetGetState() failed */
	int wifi_bars;              /* 0..WIFI_BARS lit bars */
	int wifi_strength_known;

	int time_format_probed;
	int time_format_12h;
} header_status;

static header_status s_status;

static void status_sample(header_status *st) {
	/* Battery. A negative return means "unknown": the icon is then skipped
	 * entirely rather than drawn empty (which would read as "flat"). */
	int pct = scePowerGetBatteryLifePercent();
	if (pct < 0) {
		st->battery_pct = -1;
	} else {
		if (pct > 100) pct = 100;
		st->battery_pct = pct;
	}
	st->battery_charging = scePowerIsBatteryCharging() ? 1 : 0;

	/* Clock. Read the system 12h/24h preference once per session; if the
	 * registry read fails we simply stay on 24h. */
	if (!st->time_format_probed) {
		int fmt = 0;
		st->time_format_probed = 1;
		st->time_format_12h =
			(sceRegMgrSystemParamGetInt(SCE_SYSTEM_PARAM_ID_TIME_FORMAT, &fmt) >= 0 &&
			 fmt == SCE_SYSTEM_PARAM_TIME_FORMAT_12HR) ? 1 : 0;
	}
	SceDateTime now_local;
	memset(&now_local, 0, sizeof(now_local));
	if (sceRtcGetCurrentClockLocalTime(&now_local) >= 0 &&
	    now_local.hour < 24 && now_local.minute < 60) {
		st->clock_valid = 1;
		st->clock_hour = (int)now_local.hour;
		st->clock_minute = (int)now_local.minute;
	} else {
		st->clock_valid = 0;
	}

	/* vita-https owns and guards sceNetCtl, including the startup window in
	 * which calling an unresolved system stub would crash on hardware. */
	if (!vita_https_is_connected()) {
		st->wifi_valid = 0;
		st->wifi_bars = 0;
		st->wifi_strength_known = 0;
		return;
	}
	st->wifi_valid = 1;
	st->wifi_strength_known = 0;
	{
		int signal = 0;
		if (vita_https_wifi_signal_percent(&signal) >= 0) {
			unsigned int rssi = signal < 0 ? 0u : (unsigned int)signal;
			if (rssi > 100u) rssi = 100u;
			st->wifi_strength_known = 1;
			st->wifi_bars = rssi >= 75u ? 4 : (rssi >= 50u ? 3 : (rssi >= 25u ? 2 : 1));
		} else {
			/* Associated but signal strength unreadable: show every bar in
			 * the muted colour instead of guessing a level. */
			st->wifi_bars = WIFI_BARS;
		}
	}
}

static const header_status *status_get(void) {
	uint64_t now_us = sceKernelGetProcessTimeWide();  /* read exactly once */
	if (s_status.next_refresh_us == 0 || now_us >= s_status.next_refresh_us) {
		status_sample(&s_status);
		s_status.next_refresh_us = now_us + STATUS_REFRESH_US;
		/* next_refresh_us == 0 means "never sampled"; on the astronomically
		 * unlikely wrap keep it non-zero so the flag stays unambiguous. */
		if (s_status.next_refresh_us == 0) s_status.next_refresh_us = 1;
	}
	return &s_status;
}

static unsigned int status_color_alpha(unsigned int color, float opacity) {
	if (opacity < 0.0f) opacity = 0.0f;
	if (opacity > 1.0f) opacity = 1.0f;
	unsigned int alpha = (unsigned int)(opacity * 255.0f + 0.5f);
	return (color & 0x00FFFFFFU) | (alpha << 24);
}

static void draw_battery(const header_status *st, float opacity) {
	if (st->battery_pct < 0) return;  /* silent degradation: nothing drawn */

	unsigned int level_color = st->battery_pct >= 50 ? COLOR_BATT_OK
	                        : (st->battery_pct >= 20 ? COLOR_BATT_WARN
	                                                 : COLOR_BATT_LOW);
	if (st->battery_charging) level_color = COLOR_BATT_OK;
	level_color = status_color_alpha(level_color, opacity);
	unsigned int muted_color = status_color_alpha(COLOR_MUTED, opacity);
	unsigned int text_color = status_color_alpha(COLOR_TEXT, opacity);

	/* Shell: four 2px edges + the positive terminal on the right. */
	vita2d_draw_rectangle(BATT_X, BATT_Y, BATT_BODY_W, 2.0f, muted_color);
	vita2d_draw_rectangle(BATT_X, BATT_Y + BATT_BODY_H - 2.0f,
	                      BATT_BODY_W, 2.0f, muted_color);
	vita2d_draw_rectangle(BATT_X, BATT_Y, 2.0f, BATT_BODY_H, muted_color);
	vita2d_draw_rectangle(BATT_X + BATT_BODY_W - 2.0f, BATT_Y,
	                      2.0f, BATT_BODY_H, muted_color);
	vita2d_draw_rectangle(BATT_X + BATT_BODY_W,
	                      BATT_Y + (BATT_BODY_H - BATT_TIP_H) / 2.0f,
	                      BATT_TIP_W, BATT_TIP_H, muted_color);

	/* Charge level. */
	const float inner_x = BATT_X + 3.0f;
	const float inner_y = BATT_Y + 3.0f;
	const float inner_w = BATT_BODY_W - 6.0f;   /* 24 */
	const float inner_h = BATT_BODY_H - 6.0f;   /* 10 */
	float fill_w = inner_w * (float)st->battery_pct / 100.0f;
	if (st->battery_pct > 0 && fill_w < 2.0f) fill_w = 2.0f;
	if (fill_w > 0.0f) {
		vita2d_draw_rectangle(inner_x, inner_y, fill_w, inner_h, level_color);
	}

	if (st->battery_charging) {
		/* Lightning bolt built from 2px scanlines: narrow at the top right,
		 * widest in the middle, narrow at the bottom left. vita2d has no
		 * rotated primitive, so a small pixel glyph is the legible option. */
		const float cx = BATT_X + BATT_BODY_W / 2.0f;
		const float cy = BATT_Y + BATT_BODY_H / 2.0f;
		static const float bolt_dx[6] = { 4.0f, 3.0f, 0.0f, 1.0f, 1.0f, 0.0f };
		static const float bolt_w[6]  = { 4.0f, 4.0f, 8.0f, 7.0f, 4.0f, 4.0f };
		for (int i = 0; i < 6; i++) {
			vita2d_draw_rectangle(cx - 4.0f + bolt_dx[i],
			                      cy - 6.0f + (float)i * 2.0f,
			                      bolt_w[i], 2.0f, text_color);
		}
	}
}

/* Returns the x of the leftmost pixel used by the clock, so the Wi-Fi icon
 * can be placed right next to it. Returns CLOCK_RIGHT when nothing is drawn. */
static int draw_clock(const header_status *st, float opacity) {
	if (!st->clock_valid) return CLOCK_RIGHT;  /* silent degradation */

	vita2d_font *font = ui_runtime_font(UI_FONT_BODY);
	if (!font) return CLOCK_RIGHT;

	char text[16];
	if (st->time_format_12h) {
		int hour12 = st->clock_hour % 12;
		if (hour12 == 0) hour12 = 12;
		/* Purely numeric format plus the AM/PM marker: no translatable words,
		 * so it stays out of strings_brand.def. The 12h/24h choice already
		 * follows the console's own system setting. */
		snprintf(text, sizeof(text), "%d:%02d %s", hour12, st->clock_minute,
		         st->clock_hour < 12 ? "AM" : "PM");
	} else {
		snprintf(text, sizeof(text), "%02d:%02d", st->clock_hour, st->clock_minute);
	}

	int width = ui_font_text_width(font, UI_FONT_BODY, text);
	if (width < 0) width = 0;
	if (width > CLOCK_MAX_W) width = CLOCK_MAX_W;  /* keeps the overlap proof */
	int x = CLOCK_RIGHT - width;
	ui_font_draw_text(font, x, CLOCK_BASELINE,
	                  status_color_alpha(COLOR_TEXT, opacity), UI_FONT_BODY, text);
	return x;
}

static void draw_wifi(const header_status *st, int clock_left, float opacity) {
	if (!st->wifi_valid) return;  /* silent degradation: nothing drawn */

	int x = clock_left - STATUS_GAP - WIFI_W;
	/* Never let a wide clock push the bars into the search field. */
	const int min_x = CLOCK_RIGHT - CLOCK_MAX_W - STATUS_GAP - WIFI_W;
	if (x < min_x) x = min_x;

	unsigned int on_color = COLOR_BLUE;
	if (!st->wifi_strength_known) {
		/* Connecting, or connected with unknown signal strength. */
		on_color = COLOR_MUTED;
	}
	on_color = status_color_alpha(on_color, opacity);
	unsigned int dim_color = status_color_alpha(COLOR_DIM, opacity);

	for (int i = 0; i < WIFI_BARS; i++) {
		float h = (float)(WIFI_BAR_BASE_H + i * WIFI_BAR_STEP);
		float bx = (float)(x + i * (WIFI_BAR_W + WIFI_BAR_GAP));
		vita2d_draw_rectangle(bx, (float)WIFI_BOTTOM_Y - h, WIFI_BAR_W, h,
		                      i < st->wifi_bars ? on_color : dim_color);
	}
}

void ui_brand_draw_status_indicators_alpha(float opacity) {
	if (opacity <= 0.0f) return;
	if (opacity > 1.0f) opacity = 1.0f;
	const header_status *st = status_get();
	draw_battery(st, opacity);
	draw_wifi(st, draw_clock(st, opacity), opacity);
}

void ui_brand_draw_status_indicators(void) {
	ui_brand_draw_status_indicators_alpha(1.0f);
}

static unsigned int mix_rgb(unsigned int from, unsigned int to, int step, int steps) {
	int fr = from & 0xFF;
	int fg = (from >> 8) & 0xFF;
	int fb = (from >> 16) & 0xFF;
	int tr = to & 0xFF;
	int tg = (to >> 8) & 0xFF;
	int tb = (to >> 16) & 0xFF;
	int r = fr + (tr - fr) * step / steps;
	int g = fg + (tg - fg) * step / steps;
	int b = fb + (tb - fb) * step / steps;
	return RGBA8(r, g, b, 255);
}

static void draw_gradient_bar(void) {
	const int strips = 48;
	const float strip_w = (float)SCREEN_WIDTH / (float)strips;
	const unsigned int black = RGBA8(4, 6, 10, 255);
	const unsigned int deep_blue = RGBA8(8, 45, 92, 255);
	for (int i = 0; i < strips; i++) {
		vita2d_draw_rectangle((float)i * strip_w, 0.0f, strip_w + 1.0f,
		                       (float)UI_BRAND_HEADER_HEIGHT,
		                       mix_rgb(black, deep_blue, i, strips - 1));
	}
	vita2d_draw_rectangle(0.0f, (float)UI_BRAND_HEADER_HEIGHT - 2.0f,
	                       (float)SCREEN_WIDTH, 2.0f, COLOR_BLUE);
}

static void draw_logo(void) {
	vita2d_texture *logo = ui_runtime_logo();
	if (logo) {
		unsigned int w = vita2d_texture_get_width(logo);
		unsigned int h = vita2d_texture_get_height(logo);
		if (w > 0 && h > 0) {
			float sx = (float)HEADER_ICON_SIZE / (float)w;
			float sy = (float)HEADER_ICON_SIZE / (float)h;
			float scale = sx < sy ? sx : sy;
			/* Center the actually-drawn dimensions on the topbar's axis so
			 * both square and rectangular brand assets remain balanced. */
			float drawn_w = (float)w * scale;
			float drawn_h = (float)h * scale;
			float draw_x = (float)HEADER_ICON_X +
			               ((float)HEADER_ICON_SIZE - drawn_w) * 0.5f;
			float draw_y = ((float)UI_BRAND_HEADER_HEIGHT - drawn_h) * 0.5f;
			vita2d_draw_texture_scale(logo, draw_x, draw_y, scale, scale);
			return;
		}
	}

	/* Always-visible fallback in case the PNG fails to load. */
	vita2d_draw_fill_circle(HEADER_ICON_X + HEADER_ICON_SIZE * 0.5f,
	                        HEADER_ICON_Y + HEADER_ICON_SIZE * 0.5f,
	                        HEADER_ICON_SIZE * 0.46f, COLOR_BLUE);
	/* No vita2d_draw_array() with auto-generated vertices: that function
	 * forwards the pointer straight to GXM, and the stack isn't GPU-mapped memory. */
}

static void fit_search_text(vita2d_font *font, const char *text,
	                         char *out, size_t out_size) {
	if (!out || out_size == 0) return;
	size_t len = strlen(text);
	if (len >= out_size) len = out_size - 1;
	while (len > 0 && (((unsigned char)text[len] & 0xC0) == 0x80)) len--;
	memcpy(out, text, len);
	out[len] = '\0';
	while (len > 0 && ui_font_text_width(font, UI_FONT_BODY, out) > SEARCH_BOX_W - 96) {
		len--;
		while (len > 0 && (((unsigned char)out[len] & 0xC0) == 0x80)) len--;
		out[len] = '\0';
	}
}

static size_t utf8_next(const char *text, size_t index, size_t length) {
	if (index >= length) return length;
	unsigned char lead = (unsigned char)text[index];
	size_t step = lead < 0x80 ? 1 : ((lead & 0xE0) == 0xC0 ? 2 : 3);
	return index + step <= length ? index + step : length;
}

static void fit_search_edit_text(vita2d_font *font, const char *text,
	                              size_t caret_byte, char *out,
	                              size_t out_size, int *caret_px) {
	if (!out || out_size == 0) return;
	const int max_width = SEARCH_BOX_W - 96;
	size_t length = text ? strlen(text) : 0;
	if (caret_byte > length) caret_byte = length;
	while (caret_byte > 0 &&
	       (((unsigned char)text[caret_byte] & 0xC0) == 0x80)) caret_byte--;

	/* The caret takes priority: if the prefix doesn't fit, advance the start
	 * of the segment one UTF-8 character at a time. */
	size_t start = 0;
	char before[256];
	for (;;) {
		size_t count = caret_byte - start;
		if (count >= sizeof(before)) count = sizeof(before) - 1;
		memcpy(before, text + start, count);
		before[count] = '\0';
		if (ui_font_text_width(font, UI_FONT_BODY, before) <= max_width ||
		    start >= caret_byte) break;
		start = utf8_next(text, start, caret_byte);
	}

	size_t end = caret_byte;
	while (end < length) {
		size_t next = utf8_next(text, end, length);
		size_t count = next - start;
		if (count >= out_size) break;
		memcpy(out, text + start, count);
		out[count] = '\0';
		if (ui_font_text_width(font, UI_FONT_BODY, out) > max_width) break;
		end = next;
	}
	size_t count = end - start;
	if (count >= out_size) count = out_size - 1;
	memcpy(out, text + start, count);
	out[count] = '\0';

	size_t before_count = caret_byte - start;
	if (before_count >= sizeof(before)) before_count = sizeof(before) - 1;
	memcpy(before, text + start, before_count);
	before[before_count] = '\0';
	if (caret_px) *caret_px = ui_font_text_width(font, UI_FONT_BODY, before);
}

static void draw_header(const char *query, int editing,
	                    size_t caret_byte, int caret_visible,
	                    const char *placeholder) {
	draw_gradient_bar();
	draw_logo();

	vita2d_font *font = ui_runtime_font(UI_FONT_BODY);
	if (font) {
		vita2d_font *display = ui_runtime_font(UI_FONT_DISPLAY);
		vita2d_font *title_font = display ? display : font;
		unsigned title_size = display ? UI_FONT_DISPLAY : UI_FONT_BODY;
		ui_font_draw_text(title_font, HEADER_NAME_X,
		                       HEADER_NAME_BASELINE,
		                       COLOR_TEXT, title_size,
		                       "VitaTube");
		if (g_brand_loading) {
			int title_w = ui_font_text_width(title_font, title_size, "VitaTube");
			float start_x = (float)(HEADER_NAME_X + title_w + 12);
			uint64_t phase = sceKernelGetProcessTimeWide() / 140000ULL;
			for (int i = 0; i < 4; i++) {
				int step = (int)((phase + (uint64_t)i) % 4ULL);
				float radius = step == 0 ? 3.8f : step == 1 ? 3.1f : 2.4f;
				unsigned color = step == 0 ? VT_THEME_BLUE_LIGHT
				               : step == 1 ? VT_THEME_BLUE_BRIGHT
				                           : VT_THEME_BORDER;
				vita2d_draw_fill_circle(start_x + i * 10.0f, 27.0f, radius, color);
			}
		}
	}

	/* Right corner: Wi-Fi, clock, battery (battery outermost). Values come
	 * from a 2s cache, so this costs nothing per frame. */
	ui_brand_draw_status_indicators();

	const char *source = (query && query[0]) ? query
	                                      : (editing ? "" : placeholder);
	if (!editing) {
		if (font && source && source[0]) {
			char fitted[256];
			fit_search_text(font, source, fitted, sizeof(fitted));
			int width = ui_font_text_width(font, UI_FONT_BODY, fitted);
			ui_font_draw_text(font, (SCREEN_WIDTH - width) / 2,
			                  SEARCH_BOX_Y + 36, COLOR_TEXT,
			                  UI_FONT_BODY, fitted);
		}
		g_brand_loading = 0;
		return;
	}

	/* Text editor: a compact underline inside the shared top bar. */
	vita2d_draw_rectangle((float)SEARCH_BOX_X, (float)(SEARCH_BOX_Y + SEARCH_BOX_H - 2),
	                       (float)SEARCH_BOX_W, 2.0f, COLOR_BLUE);

	if (font) {
		char fitted[256];
		int caret_px = 0;
		if (editing) {
			fit_search_edit_text(font, source, caret_byte, fitted,
			                          sizeof(fitted), &caret_px);
		} else {
			fit_search_text(font, source, fitted, sizeof(fitted));
		}
		unsigned int color = (query && query[0]) ? COLOR_TEXT : COLOR_MUTED;
		ui_font_draw_text(font, SEARCH_BOX_X + 18, SEARCH_BOX_Y + 36,
		                       color, UI_FONT_BODY, fitted);
		if (editing && caret_visible) {
			float caret_x = (float)(SEARCH_BOX_X + 18 + caret_px + 1);
			vita2d_draw_rectangle(caret_x, SEARCH_BOX_Y + 12.0f, 2.0f, 30.0f,
			                       COLOR_TEXT);
		}
	}

	if (query && query[0]) {
		/* Deliberately large clear button: 34px visible, with an even more
		 * generous hit area via ui_brand_search_clear_hit(). */
		vita2d_draw_fill_circle(SEARCH_CLEAR_X + SEARCH_CLEAR_SIZE * 0.5f,
		                        SEARCH_CLEAR_Y + SEARCH_CLEAR_SIZE * 0.5f,
		                        14.0f, RGBA8(31, 47, 68, 255));
		vita2d_draw_line(SEARCH_CLEAR_X + 11.0f, SEARCH_CLEAR_Y + 11.0f,
		                 SEARCH_CLEAR_X + 23.0f, SEARCH_CLEAR_Y + 23.0f,
		                 COLOR_TEXT);
		vita2d_draw_line(SEARCH_CLEAR_X + 23.0f, SEARCH_CLEAR_Y + 11.0f,
		                 SEARCH_CLEAR_X + 11.0f, SEARCH_CLEAR_Y + 23.0f,
		                 COLOR_TEXT);
	}
	/* Activity is a property of this rendered frame. This prevents one page's
	 * asynchronous job from leaving the marker enabled on the next page. */
	g_brand_loading = 0;
}

void ui_brand_draw_header(const char *query) {
	draw_header(query, 0, 0, 0, "");
}

void ui_brand_set_loading(int loading) {
	g_brand_loading = loading != 0;
}

void ui_brand_draw_header_placeholder(const char *query, const char *placeholder) {
	draw_header(query, 0, 0, 0,
	            placeholder && placeholder[0] ? placeholder : "");
}

int ui_brand_search_field_hit(int x, int y) {
	return x >= SEARCH_BOX_X && x < SEARCH_BOX_X + SEARCH_BOX_W &&
	       y >= SEARCH_BOX_Y && y < SEARCH_BOX_Y + SEARCH_BOX_H;
}

int ui_brand_search_clear_hit(int x, int y) {
	/* Treat the whole trailing end-cap as the clear control. On real hardware
	 * the mapped contact can drift a few pixels from the visible fingertip;
	 * keeping the target inside the search field avoids both missed clears and
	 * overlap with the status indicators to its right. */
	return x >= SEARCH_CLEAR_X - 14 && x < SEARCH_BOX_X + SEARCH_BOX_W &&
	       y >= SEARCH_BOX_Y && y < SEARCH_BOX_Y + SEARCH_BOX_H;
}

void ui_brand_draw_search_backdrop(const char *query) {
	vita2d_draw_rectangle(0.0f, 0.0f, (float)SCREEN_WIDTH,
	                       (float)SCREEN_HEIGHT, COLOR_BG);
	ui_brand_draw_header(query);

	vita2d_font *font = ui_runtime_font(UI_FONT_DISPLAY);
	if (font) {
		const char *title = vt_i18n_str(VT_STR_BRAND_TEXT_INPUT_TITLE);
		const char *hint = vt_i18n_str(VT_STR_BRAND_TEXT_INPUT_HINT);
		vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
		int title_w = ui_font_text_width(font, UI_FONT_DISPLAY, title);
		int hint_w = small ? ui_font_text_width(small, UI_FONT_SMALL, hint) : 0;
		ui_font_draw_text(font, (SCREEN_WIDTH - title_w) / 2, 180,
		                       COLOR_TEXT, UI_FONT_DISPLAY, title);
		if (small) {
			ui_font_draw_text(small, (SCREEN_WIDTH - hint_w) / 2, 220,
			                       COLOR_MUTED, UI_FONT_SMALL, hint);
		}
	}
}

void ui_brand_draw_search_backdrop_editing(const char *query,
	                                       size_t caret_byte,
	                                       int caret_visible) {
	ui_brand_draw_search_backdrop_editing_label(
		query, caret_byte, caret_visible, vt_i18n_str(VT_STR_BRAND_TEXT_INPUT_TITLE),
		vt_i18n_str(VT_STR_BRAND_TEXT_INPUT_HINT));
}

void ui_brand_draw_search_backdrop_editing_label(const char *query,
	                                             size_t caret_byte,
	                                             int caret_visible,
	                                             const char *title,
	                                             const char *hint) {
	vita2d_draw_rectangle(0.0f, 0.0f, (float)SCREEN_WIDTH,
	                       (float)SCREEN_HEIGHT, COLOR_BG);
	draw_header(query, 1, caret_byte, caret_visible, "");

	vita2d_font *font = ui_runtime_font(UI_FONT_DISPLAY);
	if (font) {
		if (!title || !title[0]) title = vt_i18n_str(VT_STR_BRAND_TEXT_INPUT_TITLE);
		if (!hint) hint = "";
		vita2d_font *small = ui_runtime_font(UI_FONT_SMALL);
		int title_w = ui_font_text_width(font, UI_FONT_DISPLAY, title);
		int hint_w = small ? ui_font_text_width(small, UI_FONT_SMALL, hint) : 0;
		ui_font_draw_text(font, (SCREEN_WIDTH - title_w) / 2, 180,
		                       COLOR_TEXT, UI_FONT_DISPLAY, title);
		if (small) {
			ui_font_draw_text(small, (SCREEN_WIDTH - hint_w) / 2, 220,
			                       COLOR_MUTED, UI_FONT_SMALL, hint);
		}
	}
}
