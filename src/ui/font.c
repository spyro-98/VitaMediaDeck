#include "ui/font.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <psp2/pgf.h>
#include <psp2/sysmodule.h>

#define UI_FONT_RUN_CAPACITY 256
#define UI_FONT_FORMAT_CAPACITY 1024

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
static int g_cjk_init_attempted;

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

static vita2d_pgf *first_cjk_font(void) {
	for (int i = 0; i < CJK_FONT_COUNT; i++) {
		if (g_system_fonts[i]) return g_system_fonts[i];
	}
	return NULL;
}

static vita2d_pgf *cjk_font_for_text(const char *text) {
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
	if (g_system_fonts[SYSTEM_FONT_CHINESE])
		return g_system_fonts[SYSTEM_FONT_CHINESE];
	return first_cjk_font();
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
	vita2d_pgf *system_latin = g_system_fonts[SYSTEM_FONT_LATIN];
	int height = system_latin
	           ? vita2d_pgf_text_height(system_latin,
	                                    system_scale(system_latin, size), "Ag")
	           : (latin_font ? vita2d_font_text_height(latin_font, size, "Ag") : 0);
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
		/* The console PGF is the primary UI face again.  Besides matching the
		 * system rendering at native Vita resolution, its Latin set includes
		 * Cyrillic; the language-specific PGFs cover Japanese, Chinese and
		 * Korean.  The packaged TTF is now only a bootstrap fallback. */
		vita2d_pgf *system_font = cjk_codepoint(cp)
		                         ? cjk_font
		                         : g_system_fonts[SYSTEM_FONT_LATIN];
		if (!system_font && !latin_font) system_font = first_cjk_font();
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
	if (g_cjk_init_attempted) return -1;
	g_cjk_init_attempted = 1;
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
		const char *probe = i == SYSTEM_FONT_LATIN ? "\xd0\x96" : "\xe6\x97\xa5";
		g_system_base_heights[i] = vita2d_pgf_text_height(
			g_system_fonts[i], 1.0f, probe);
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
