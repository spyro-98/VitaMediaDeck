#include "ui/font.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <psp2/pgf.h>
#include <psp2/registrymgr.h>
#include <psp2/sysmodule.h>
#include <psp2/system_param.h>

#define UI_FONT_RUN_CAPACITY 256
#define UI_FONT_FORMAT_CAPACITY 1024
#define UI_FONT_FIT_CAPACITY 512

typedef enum SystemFontKind {
	SYSTEM_FONT_JAPANESE = 0,
	SYSTEM_FONT_CHINESE,
	SYSTEM_FONT_KOREAN,
	SYSTEM_FONT_LATIN,
	SYSTEM_FONT_COUNT
} SystemFontKind;

#define CJK_FONT_COUNT SYSTEM_FONT_LATIN

static vita2d_pgf *g_system_fonts[SYSTEM_FONT_COUNT];
static int g_system_base_heights[SYSTEM_FONT_COUNT];
static int g_system_init_attempted;
static int g_system_language = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
static int g_system_preference = UI_FONT_SYSTEM_AUTO;

static int cjk_codepoint(unsigned int cp) {
	return (cp >= 0x1100U && cp <= 0x11FFU) ||
	       (cp >= 0x2E80U && cp <= 0x33FFU) ||
	       (cp >= 0x3400U && cp <= 0x4DBFU) ||
	       (cp >= 0x4E00U && cp <= 0x9FFFU) ||
	       (cp >= 0xA960U && cp <= 0xA97FU) ||
	       (cp >= 0xAC00U && cp <= 0xD7AFU) ||
	       (cp >= 0xF900U && cp <= 0xFAFFU) ||
	       (cp >= 0xFE00U && cp <= 0xFE1FU) ||
	       (cp >= 0xFE30U && cp <= 0xFE4FU) ||
	       (cp >= 0xFF00U && cp <= 0xFFEFU);
}

typedef struct InterRange {
	uint32_t first;
	uint32_t last;
} InterRange;

/* Generated from the identical charmaps in the bundled Inter 4.1 Medium and
 * SemiBold files.  Broad Unicode-block checks are unsafe here: for example,
 * Inter has common Cyrillic but intentionally leaves holes in Cyrillic
 * Extended-B. Routing one of those holes to FreeType would render .notdef and
 * prevent the Vita system font from acting as the fallback. */
static const InterRange g_inter_text_ranges[] = {
	{ 0x0020U, 0x007EU }, { 0x00A0U, 0x00ACU },
	{ 0x00AEU, 0x0148U }, { 0x014AU, 0x01C3U },
	{ 0x01C5U, 0x0254U }, { 0x0256U, 0x027BU },
	{ 0x027EU, 0x0284U }, { 0x0286U, 0x0290U },
	{ 0x0292U, 0x02A4U }, { 0x02A6U, 0x0304U },
	{ 0x0306U, 0x030AU }, { 0x030CU, 0x030CU },
	{ 0x030FU, 0x030FU }, { 0x0313U, 0x0313U },
	{ 0x0315U, 0x0315U }, { 0x031BU, 0x031BU },
	{ 0x0323U, 0x0323U }, { 0x0326U, 0x0328U },
	{ 0x032CU, 0x032CU }, { 0x0337U, 0x0338U },
	{ 0x0342U, 0x0343U }, { 0x0346U, 0x036FU },
	{ 0x0374U, 0x0376U }, { 0x037AU, 0x037FU },
	{ 0x0384U, 0x038AU }, { 0x038CU, 0x038CU },
	{ 0x038EU, 0x03A1U }, { 0x03A3U, 0x03D7U },
	{ 0x03DCU, 0x03DDU }, { 0x03F0U, 0x03F6U },
	{ 0x03F9U, 0x03FAU }, { 0x03FCU, 0x0479U },
	{ 0x0480U, 0x049DU }, { 0x04A0U, 0x04FFU },
	{ 0x052FU, 0x052FU },
	{ 0x1DBFU, 0x1DF5U }, { 0x1DFCU, 0x1E9BU },
	{ 0x1E9DU, 0x1F15U }, { 0x1F18U, 0x1F1DU },
	{ 0x1F20U, 0x1F45U }, { 0x1F48U, 0x1F4DU },
	{ 0x1F50U, 0x1F57U }, { 0x1F59U, 0x1F59U },
	{ 0x1F5BU, 0x1F5BU }, { 0x1F5DU, 0x1F5DU },
	{ 0x1F5FU, 0x1F7DU }, { 0x1F80U, 0x1FB4U },
	{ 0x1FB6U, 0x1FC4U }, { 0x1FC6U, 0x1FD3U },
	{ 0x1FD6U, 0x1FDBU }, { 0x1FDDU, 0x1FEFU },
	{ 0x1FF2U, 0x1FF4U }, { 0x1FF6U, 0x1FFEU },
	{ 0x2000U, 0x200BU }, { 0x2010U, 0x2027U },
	{ 0x202FU, 0x2055U }, { 0x2057U, 0x2057U },
	{ 0x205FU, 0x205FU }, { 0x2070U, 0x2071U },
	{ 0x2074U, 0x208EU }, { 0x2090U, 0x209CU },
	{ 0x20A0U, 0x20AFU }, { 0x20B1U, 0x20B5U },
	{ 0x20B8U, 0x20BAU }, { 0x20BCU, 0x20C0U },
	{ 0x2150U, 0x217FU }, { 0x2183U, 0x2186U },
	{ 0x2189U, 0x2189U }, { 0x2190U, 0x2199U },
	{ 0x21A9U, 0x21AAU }, { 0x2212U, 0x2212U },
	{ 0x25B6U, 0x25B7U },
	{ 0x25C0U, 0x25C1U }, { 0x26A0U, 0x26A0U },
	{ 0x2713U, 0x2713U }, { 0x2717U, 0x2717U },
	{ 0xFEFFU, 0xFEFFU }
};

/* Keep these scripts on a FreeType face rasterized at the exact requested
 * pixel size. libvita2d's atlas is keyed by glyph alone, so the runtime owns
 * one Inter instance per UI size. */
static int inter_codepoint(unsigned int cp) {
	size_t low = 0;
	size_t high = sizeof(g_inter_text_ranges) / sizeof(g_inter_text_ranges[0]);
	while (low < high) {
		size_t mid = low + (high - low) / 2;
		if (cp < g_inter_text_ranges[mid].first) high = mid;
		else if (cp > g_inter_text_ranges[mid].last) low = mid + 1;
		else return 1;
	}
	return 0;
}

static size_t utf8_codepoint(const char *text, uint32_t *codepoint) {
	const unsigned char *s = (const unsigned char *)text;
	if (!s || !s[0]) {
		if (codepoint) *codepoint = 0;
		return 0;
	}
	uint32_t cp;
	size_t length;
	if (s[0] < 0x80U) {
		cp = s[0];
		length = 1;
	} else if (s[0] >= 0xC2U && s[0] <= 0xDFU &&
	           (s[1] & 0xC0U) == 0x80U) {
		cp = ((uint32_t)(s[0] & 0x1FU) << 6) |
		     (uint32_t)(s[1] & 0x3FU);
		length = 2;
	} else if (s[0] >= 0xE0U && s[0] <= 0xEFU && s[1] && s[2] &&
	           (s[1] & 0xC0U) == 0x80U && (s[2] & 0xC0U) == 0x80U) {
		cp = ((uint32_t)(s[0] & 0x0FU) << 12) |
		     ((uint32_t)(s[1] & 0x3FU) << 6) |
		     (uint32_t)(s[2] & 0x3FU);
		if (cp < 0x800U || (cp >= 0xD800U && cp <= 0xDFFFU)) {
			cp = 0xFFFDU;
			length = 1;
		} else {
			length = 3;
		}
	} else if (s[0] >= 0xF0U && s[0] <= 0xF4U && s[1] && s[2] && s[3] &&
	           (s[1] & 0xC0U) == 0x80U && (s[2] & 0xC0U) == 0x80U &&
	           (s[3] & 0xC0U) == 0x80U) {
		cp = ((uint32_t)(s[0] & 0x07U) << 18) |
		     ((uint32_t)(s[1] & 0x3FU) << 12) |
		     ((uint32_t)(s[2] & 0x3FU) << 6) |
		     (uint32_t)(s[3] & 0x3FU);
		if (cp < 0x10000U || cp > 0x10FFFFU) {
			cp = 0xFFFDU;
			length = 1;
		} else {
			length = 4;
		}
	} else {
		cp = 0xFFFDU;
		length = 1;
	}
	if (codepoint) *codepoint = cp;
	return length;
}

static int japanese_codepoint(unsigned int cp) {
	return (cp >= 0x3040U && cp <= 0x30FFU) ||
	       (cp >= 0x31F0U && cp <= 0x31FFU) ||
	       (cp >= 0xFF66U && cp <= 0xFF9FU);
}

static int korean_codepoint(unsigned int cp) {
	return (cp >= 0x1100U && cp <= 0x11FFU) ||
	       (cp >= 0x3130U && cp <= 0x318FU) ||
	       (cp >= 0xA960U && cp <= 0xA97FU) ||
	       (cp >= 0xAC00U && cp <= 0xD7AFU);
}

static int single_line_ascii(const char *text) {
	if (!text || !text[0]) return 0;
	for (const unsigned char *cursor = (const unsigned char *)text;
	     *cursor; cursor++) {
		if (*cursor >= 0x80U || *cursor == '\n') return 0;
	}
	return 1;
}

static vita2d_pgf *first_cjk_font(void) {
	for (int i = 0; i < CJK_FONT_COUNT; i++) {
		if (g_system_fonts[i]) return g_system_fonts[i];
	}
	return NULL;
}

static vita2d_pgf *cjk_font_for_text(const char *text) {
	if (g_system_preference >= SYSTEM_FONT_JAPANESE &&
	    g_system_preference <= SYSTEM_FONT_KOREAN &&
	    g_system_fonts[g_system_preference])
		return g_system_fonts[g_system_preference];
	int has_japanese = 0;
	int has_korean = 0;
	for (const char *cursor = text; cursor && *cursor;) {
		uint32_t cp;
		size_t bytes = utf8_codepoint(cursor, &cp);
		if (!bytes) break;
		if (japanese_codepoint(cp)) has_japanese = 1;
		if (korean_codepoint(cp)) has_korean = 1;
		cursor += bytes;
	}
	if (has_japanese && g_system_fonts[SYSTEM_FONT_JAPANESE])
		return g_system_fonts[SYSTEM_FONT_JAPANESE];
	if (has_korean && g_system_fonts[SYSTEM_FONT_KOREAN])
		return g_system_fonts[SYSTEM_FONT_KOREAN];
	/* Han-only text has no script marker. Use the console language as the best
	 * available glyph-variant hint instead of always imposing Chinese forms. */
	if (g_system_language == SCE_SYSTEM_PARAM_LANG_JAPANESE &&
	    g_system_fonts[SYSTEM_FONT_JAPANESE])
		return g_system_fonts[SYSTEM_FONT_JAPANESE];
	if (g_system_language == SCE_SYSTEM_PARAM_LANG_KOREAN &&
	    g_system_fonts[SYSTEM_FONT_KOREAN])
		return g_system_fonts[SYSTEM_FONT_KOREAN];
	if (g_system_fonts[SYSTEM_FONT_CHINESE])
		return g_system_fonts[SYSTEM_FONT_CHINESE];
	return first_cjk_font();
}

static vita2d_pgf *system_font_for_codepoint(unsigned int cp,
	                                          vita2d_font *packaged_font,
	                                          vita2d_pgf *cjk_context) {
	if (japanese_codepoint(cp) && g_system_fonts[SYSTEM_FONT_JAPANESE])
		return g_system_fonts[SYSTEM_FONT_JAPANESE];
	if (korean_codepoint(cp) && g_system_fonts[SYSTEM_FONT_KOREAN])
		return g_system_fonts[SYSTEM_FONT_KOREAN];
	if (cjk_codepoint(cp)) return cjk_context ? cjk_context : first_cjk_font();

	/* Exact-size Inter is the primary UI face.  The Vita system Latin PGF is a
	 * last-resort fallback only; libvita2d post-scales its cached bitmap and a
	 * fractional scale is visibly soft at 960x544. */
	if (packaged_font && inter_codepoint(cp)) return NULL;
	if (g_system_fonts[SYSTEM_FONT_LATIN])
		return g_system_fonts[SYSTEM_FONT_LATIN];
	return packaged_font ? NULL : first_cjk_font();
}

static float system_scale(vita2d_pgf *font, unsigned int size) {
	for (int i = 0; i < SYSTEM_FONT_COUNT; i++) {
		if (g_system_fonts[i] == font && g_system_base_heights[i] > 0)
			return (float)size / (float)g_system_base_heights[i];
	}
	return 1.0f;
}

static int run_width(vita2d_font *latin_font, unsigned int size,
	                 vita2d_pgf *system_font, const char *run) {
	if (system_font)
		return vita2d_pgf_text_width(system_font,
		                             system_scale(system_font, size), run);
	if (latin_font) return vita2d_font_text_width(latin_font, size, run);
	if (first_cjk_font())
		return vita2d_pgf_text_width(first_cjk_font(),
		                             system_scale(first_cjk_font(), size), run);
	return 0;
}

static void draw_run(vita2d_font *latin_font, int x, int y,
	                 unsigned int color, unsigned int size,
	                 vita2d_pgf *system_font, const char *run) {
	if (system_font) {
		vita2d_pgf_draw_text(system_font, x, y, color,
		                     system_scale(system_font, size), run);
	} else if (latin_font) {
		vita2d_font_draw_text(latin_font, x, y, color, size, run);
	} else if (first_cjk_font()) {
		vita2d_pgf_draw_text(first_cjk_font(), x, y, color,
		                     system_scale(first_cjk_font(), size), run);
	}
}

static int line_height(vita2d_font *latin_font, unsigned int size) {
	int height = latin_font
	           ? vita2d_font_text_height(latin_font, size, "Ag")
	           : 0;
	if (height <= 0 && g_system_fonts[SYSTEM_FONT_LATIN])
		height = vita2d_pgf_text_height(
			g_system_fonts[SYSTEM_FONT_LATIN],
			system_scale(g_system_fonts[SYSTEM_FONT_LATIN], size), "Ag");
	if (first_cjk_font() && (int)size > height) height = (int)size;
	return height > 0 ? height : (int)size;
}

static int flush_run(vita2d_font *latin_font, int draw, int x, int y,
	                 unsigned int color, unsigned int size,
	                 vita2d_pgf *system_font,
	                 char run[UI_FONT_RUN_CAPACITY], size_t *run_length) {
	if (!run_length || *run_length == 0) return 0;
	run[*run_length] = '\0';
	int width = run_width(latin_font, size, system_font, run);
	if (draw)
		draw_run(latin_font, x, y, color, size, system_font, run);
	*run_length = 0;
	return width;
}

static int mixed_layout(vita2d_font *latin_font, int draw, int x, int y,
	                    unsigned int color, unsigned int size,
	                    const char *text, int *height) {
	if (height) *height = 0;
	if (!text || !text[0]) return 0;
	/* Most on-screen copy is short ASCII. Bypass the UTF-8 run builder and the
	 * contextual CJK scan in that hot path; this is significant on list screens
	 * that redraw dozens of labels at 60 Hz. */
	if (single_line_ascii(text)) {
		vita2d_pgf *system_font = system_font_for_codepoint(
			(unsigned int)(unsigned char)text[0], latin_font, NULL);
		int width = run_width(latin_font, size, system_font, text);
		if (draw) draw_run(latin_font, x, y, color, size, system_font, text);
		if (height) *height = line_height(latin_font, size);
		return width;
	}
	char run[UI_FONT_RUN_CAPACITY];
	size_t run_length = 0;
	vita2d_pgf *run_system_font = NULL;
	int have_run = 0;
	int pen_x = x;
	int max_width = 0;
	int lines = 1;
	int advance_y = line_height(latin_font, size);
	vita2d_pgf *cjk_font = cjk_font_for_text(text);
	const char *cursor = text;
	while (*cursor) {
		uint32_t cp;
		size_t bytes = utf8_codepoint(cursor, &cp);
		if (bytes == 0) break;
		if (cp == '\n') {
			pen_x += flush_run(latin_font, draw, pen_x,
			                   y + (lines - 1) * advance_y, color, size,
			                   run_system_font, run, &run_length);
			if (pen_x - x > max_width) max_width = pen_x - x;
			pen_x = x;
			lines++;
			have_run = 0;
			cursor += bytes;
			continue;
		}
		vita2d_pgf *system_font = system_font_for_codepoint(
			cp, latin_font, cjk_font);
		if (have_run && (system_font != run_system_font ||
		                 run_length + bytes >= sizeof(run))) {
			pen_x += flush_run(latin_font, draw, pen_x,
			                   y + (lines - 1) * advance_y, color, size,
			                   run_system_font, run, &run_length);
			have_run = 0;
		}
		if (!have_run) {
			run_system_font = system_font;
			have_run = 1;
		}
		memcpy(run + run_length, cursor, bytes);
		run_length += bytes;
		cursor += bytes;
	}
	pen_x += flush_run(latin_font, draw, pen_x,
	                   y + (lines - 1) * advance_y, color, size,
	                   run_system_font, run, &run_length);
	if (pen_x - x > max_width) max_width = pen_x - x;
	if (height) *height = lines * advance_y;
	return max_width;
}

int ui_font_fallback_init(void) {
	if (first_cjk_font() && g_system_fonts[SYSTEM_FONT_LATIN]) return 0;
	if (g_system_init_attempted) return -1;
	g_system_init_attempted = 1;
	int detected_language = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
	if (sceRegMgrSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG,
	                               &detected_language) >= 0)
		g_system_language = detected_language;
	int ret = sceSysmoduleLoadModule(SCE_SYSMODULE_PGF);
	if (ret < 0) return ret;
	static const SceFontLanguageCode languages[SYSTEM_FONT_COUNT] = {
		SCE_FONT_LANGUAGE_JAPANESE,
		SCE_FONT_LANGUAGE_CHINESE,
		SCE_FONT_LANGUAGE_KOREAN,
		SCE_FONT_LANGUAGE_LATIN
	};
	for (int i = 0; i < SYSTEM_FONT_COUNT; i++) {
		vita2d_system_pgf_config config = { languages[i], NULL };
		g_system_fonts[i] = vita2d_load_system_pgf(1, &config);
		if (!g_system_fonts[i]) continue;
		static const char *const probes[SYSTEM_FONT_COUNT] = {
			"\xe6\x97\xa5",       /* Japanese: 日 */
			"\xe5\x9b\xbd",       /* Chinese: 国 */
			"\xed\x95\x9c",       /* Korean: 한 */
			"Ag"
		};
		g_system_base_heights[i] = vita2d_pgf_text_height(
			g_system_fonts[i], 1.0f, probes[i]);
		if (g_system_base_heights[i] <= 0) g_system_base_heights[i] = 20;
	}
	return first_cjk_font() || g_system_fonts[SYSTEM_FONT_LATIN] ? 0 : -1;
}

void ui_font_fallback_term(void) {
	for (int i = 0; i < SYSTEM_FONT_COUNT; i++) {
		if (g_system_fonts[i]) vita2d_free_pgf(g_system_fonts[i]);
		g_system_fonts[i] = NULL;
		g_system_base_heights[i] = 0;
	}
	g_system_init_attempted = 0;
	/* PGF stays resident until process exit, like the other once-loaded Vita
	 * UI modules; unloading it while weak-import users still exist is unsafe. */
}

int ui_font_fallback_ready(void) {
	return first_cjk_font() != NULL || g_system_fonts[SYSTEM_FONT_LATIN] != NULL;
}

int ui_font_fallback_language_mask(void) {
	int mask = 0;
	for (int i = 0; i < SYSTEM_FONT_COUNT; i++) {
		if (g_system_fonts[i]) mask |= 1 << i;
	}
	return mask;
}

void ui_font_set_system_preference(int preference) {
	if (preference < UI_FONT_SYSTEM_AUTO ||
	    preference > UI_FONT_SYSTEM_LATIN)
		preference = UI_FONT_SYSTEM_AUTO;
	g_system_preference = preference;
}

int ui_font_draw_text(vita2d_font *latin_font, int x, int y,
	                  unsigned int color, unsigned int size,
	                  const char *text) {
	return mixed_layout(latin_font, 1, x, y, color, size, text, NULL);
}

int ui_font_draw_textf(vita2d_font *latin_font, int x, int y,
	                   unsigned int color, unsigned int size,
	                   const char *format, ...) {
	char text[UI_FONT_FORMAT_CAPACITY];
	va_list args;
	va_start(args, format);
	vsnprintf(text, sizeof(text), format, args);
	va_end(args);
	return ui_font_draw_text(latin_font, x, y, color, size, text);
}

void ui_font_text_dimensions(vita2d_font *latin_font, unsigned int size,
	                         const char *text, int *width, int *height) {
	int measured_height = 0;
	int measured_width = mixed_layout(latin_font, 0, 0, 0, 0, size, text,
	                                  &measured_height);
	if (width) *width = measured_width;
	if (height) *height = measured_height;
}

int ui_font_text_width(vita2d_font *latin_font, unsigned int size,
	                   const char *text) {
	return mixed_layout(latin_font, 0, 0, 0, 0, size, text, NULL);
}

int ui_font_text_height(vita2d_font *latin_font, unsigned int size,
	                    const char *text) {
	int height = 0;
	mixed_layout(latin_font, 0, 0, 0, 0, size, text, &height);
	return height;
}

static size_t utf8_previous(const char *text, size_t index) {
	if (!text || index == 0) return 0;
	index--;
	while (index > 0 && (((unsigned char)text[index] & 0xC0U) == 0x80U))
		index--;
	return index;
}

static size_t single_line_bytes(const char *text) {
	if (!text) return 0;
	const char *newline = strchr(text, '\n');
	return newline ? (size_t)(newline - text) : strlen(text);
}

int ui_font_fit_text(vita2d_font *latin_font, unsigned int size,
	                 const char *text, char *out, size_t out_size,
	                 int max_width) {
	static const char ellipsis[] = "\xe2\x80\xa6";
	if (!out || out_size == 0) return 0;
	out[0] = '\0';
	if (!text || !text[0] || max_width <= 0) return 0;

	size_t full_bytes = strlen(text);
	size_t source_bytes = single_line_bytes(text);
	int line_was_cut = source_bytes < full_bytes;
	size_t copy_bytes = source_bytes;
	if (copy_bytes >= out_size) copy_bytes = out_size - 1;
	while (copy_bytes > 0 && copy_bytes < source_bytes &&
	       (((unsigned char)text[copy_bytes] & 0xC0U) == 0x80U))
		copy_bytes--;
	memcpy(out, text, copy_bytes);
	out[copy_bytes] = '\0';
	int width = ui_font_text_width(latin_font, size, out);
	if (!line_was_cut && copy_bytes == source_bytes && width <= max_width)
		return width;

	int ellipsis_width = ui_font_text_width(latin_font, size, ellipsis);
	if (ellipsis_width <= 0 || ellipsis_width > max_width || out_size < 4) {
		while (copy_bytes > 0 && width > max_width) {
			copy_bytes = utf8_previous(out, copy_bytes);
			out[copy_bytes] = '\0';
			width = ui_font_text_width(latin_font, size, out);
		}
		return width;
	}

	/* Reserve the ellipsis first. A proportional first cut avoids a long
	 * byte-by-byte loop for paths or URLs while the final loop preserves exact
	 * font metrics and UTF-8 boundaries. */
	const int prefix_limit = max_width - ellipsis_width;
	if (copy_bytes + sizeof(ellipsis) > out_size) {
		copy_bytes = out_size - sizeof(ellipsis);
		while (copy_bytes > 0 &&
		       (((unsigned char)out[copy_bytes] & 0xC0U) == 0x80U))
			copy_bytes--;
		out[copy_bytes] = '\0';
		width = ui_font_text_width(latin_font, size, out);
	}
	if (width > prefix_limit && width > 0) {
		size_t estimate = (size_t)((long long)copy_bytes * prefix_limit / width);
		if (estimate < copy_bytes) {
			while (estimate > 0 &&
			       (((unsigned char)out[estimate] & 0xC0U) == 0x80U))
				estimate--;
			copy_bytes = estimate;
			out[copy_bytes] = '\0';
			width = ui_font_text_width(latin_font, size, out);
		}
	}
	while (copy_bytes > 0 && width > prefix_limit) {
		copy_bytes = utf8_previous(out, copy_bytes);
		out[copy_bytes] = '\0';
		width = ui_font_text_width(latin_font, size, out);
	}
	memcpy(out + copy_bytes, ellipsis, sizeof(ellipsis));
	return ui_font_text_width(latin_font, size, out);
}

int ui_font_draw_text_centered(vita2d_font *latin_font, int center_x, int y,
	                           int max_width, unsigned int color,
	                           unsigned int size, const char *text) {
	char fitted[UI_FONT_FIT_CAPACITY];
	int width = ui_font_fit_text(latin_font, size, text, fitted,
	                             sizeof(fitted), max_width);
	if (fitted[0])
		ui_font_draw_text(latin_font, center_x - width / 2, y, color, size,
		                  fitted);
	return width;
}
