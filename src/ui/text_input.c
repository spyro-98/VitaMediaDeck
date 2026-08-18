#include "ui/text_input.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/libime.h>
#include <psp2/sysmodule.h>
#include <psp2/types.h>
#include <vita2d.h>

#include "i18n/i18n.h"
#include "app_paths.h"
#include "settings/preferences.h"
#include "ui/brand.h"
#include "ui/runtime.h"

/* sceIme (unlike SceImeDialog) only shows the keyboard and leaves the text
 * field rendering to the app. All state pointed to by SceImeParam must
 * therefore stay valid until sceImeClose(): static buffers, never local to
 * the frame's stack. */
#define TEXT_INPUT_MAX_CHARS 255
#define IME_TIMEOUT_FRAMES (60 * 120)
#define IME_LIVE_UTF8_CAPACITY \
	((TEXT_INPUT_MAX_CHARS + SCE_IME_MAX_PREEDIT_LENGTH) * 3 + 1)

static int g_ime_module_loaded = 0;
static SceUInt32 g_ime_work[SCE_IME_WORK_BUFFER_SIZE / sizeof(SceUInt32)]
    __attribute__((aligned(64)));
static SceWChar16 g_ime_initial[TEXT_INPUT_MAX_CHARS + 1];
/* libime can add up to SCE_IME_MAX_PREEDIT_LENGTH composition characters on
 * top of the confirmed text (kana, accents, suggestions). */
static SceWChar16 g_ime_input[TEXT_INPUT_MAX_CHARS + SCE_IME_MAX_PREEDIT_LENGTH + 1];
static volatile int g_ime_entered = 0;
static volatile int g_ime_cancelled = 0;
static volatile SceUInt32 g_ime_caret_index = 0;

static void secure_zero(void *memory, size_t size) {
	volatile unsigned char *bytes = (volatile unsigned char *)memory;
	while (size--) *bytes++ = 0;
}

static void ime_diag_log(const char *fmt, ...) {
	static int first_call = 1;
	if (!vt_preferences_disk_logs_enabled()) return;
	sceIoMkdir(VITATUBE_DATA_DIR, 0777);
	int flags = SCE_O_WRONLY | SCE_O_CREAT | (first_call ? SCE_O_TRUNC : SCE_O_APPEND);
	first_call = 0;
	SceUID fd = sceIoOpen(VITATUBE_DATA_DIR "/ime_diag_log.txt", flags, 0777);
	if (fd < 0) return;
	char buf[192];
	va_list ap;
	va_start(ap, fmt);
	int len = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (len > 0) {
		if ((size_t)len >= sizeof(buf)) len = sizeof(buf) - 1;
		buf[len++] = '\n';
		sceIoWrite(fd, buf, len);
	}
	sceIoClose(fd);
}

static size_t utf8_decode_one(const char *in, uint32_t *codepoint) {
	const unsigned char *s = (const unsigned char *)in;
	uint32_t cp = 0xFFFDU;
	size_t length = 1;
	if (!s || !s[0]) return 0;
	if (s[0] < 0x80U) {
		cp = s[0];
	} else if (s[0] >= 0xC2U && s[0] <= 0xDFU &&
	           (s[1] & 0xC0U) == 0x80U) {
		cp = ((uint32_t)(s[0] & 0x1FU) << 6) | (uint32_t)(s[1] & 0x3FU);
		length = 2;
	} else if (s[0] >= 0xE0U && s[0] <= 0xEFU && s[1] && s[2] &&
	           (s[1] & 0xC0U) == 0x80U && (s[2] & 0xC0U) == 0x80U) {
		uint32_t decoded = ((uint32_t)(s[0] & 0x0FU) << 12) |
		                   ((uint32_t)(s[1] & 0x3FU) << 6) |
		                   (uint32_t)(s[2] & 0x3FU);
		if (decoded >= 0x800U && !(decoded >= 0xD800U && decoded <= 0xDFFFU)) {
			cp = decoded;
			length = 3;
		}
	} else if (s[0] >= 0xF0U && s[0] <= 0xF4U && s[1] && s[2] && s[3] &&
	           (s[1] & 0xC0U) == 0x80U && (s[2] & 0xC0U) == 0x80U &&
	           (s[3] & 0xC0U) == 0x80U) {
		uint32_t decoded = ((uint32_t)(s[0] & 0x07U) << 18) |
		                   ((uint32_t)(s[1] & 0x3FU) << 12) |
		                   ((uint32_t)(s[2] & 0x3FU) << 6) |
		                   (uint32_t)(s[3] & 0x3FU);
		if (decoded >= 0x10000U && decoded <= 0x10FFFFU) {
			cp = decoded;
			length = 4;
		}
	}
	if (codepoint) *codepoint = cp;
	return length;
}

static void utf8_to_utf16(const char *in, SceWChar16 *out, size_t out_count) {
	/* Strict conversion avoids handing malformed UTF-16 to sceImeOpen(), which
	 * otherwise rejects the whole editor. Invalid UTF-8 becomes U+FFFD. */
	size_t i = 0, o = 0;
	while (in && in[i] && o + 1 < out_count) {
		uint32_t cp;
		size_t bytes = utf8_decode_one(in + i, &cp);
		if (!bytes) break;
		i += bytes;
		if (cp <= 0xFFFFU) {
			out[o++] = (SceWChar16)cp;
		} else {
			if (o + 2 >= out_count) break;
			cp -= 0x10000U;
			out[o++] = (SceWChar16)(0xD800U + (cp >> 10));
			out[o++] = (SceWChar16)(0xDC00U + (cp & 0x3FFU));
		}
	}
	out[o] = 0;
}

static int utf16_to_utf8(const SceWChar16 *in, char *out, size_t out_max) {
	size_t o = 0;
	if (!out || out_max == 0) return -1;
	for (size_t i = 0; in[i] != 0; i++) {
		uint32_t c = in[i];
		if (c >= 0xD800U && c <= 0xDBFFU &&
		    in[i + 1] >= 0xDC00U && in[i + 1] <= 0xDFFFU) {
			c = 0x10000U + ((c - 0xD800U) << 10) + (in[++i] - 0xDC00U);
		} else if (c >= 0xD800U && c <= 0xDFFFU) {
			c = 0xFFFDU;
		}
		size_t need = c < 0x80U ? 1 : (c < 0x800U ? 2 : (c < 0x10000U ? 3 : 4));
		if (o + need >= out_max) {
			out[o] = '\0';
			return -1;
		}
		if (c < 0x80) {
			out[o++] = (char)c;
		} else if (c < 0x800) {
			out[o++] = (char)(0xC0 | (c >> 6));
			out[o++] = (char)(0x80 | (c & 0x3F));
		} else if (c < 0x10000U) {
			out[o++] = (char)(0xE0 | (c >> 12));
			out[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
			out[o++] = (char)(0x80 | (c & 0x3F));
		} else {
			out[o++] = (char)(0xF0 | (c >> 18));
			out[o++] = (char)(0x80 | ((c >> 12) & 0x3F));
			out[o++] = (char)(0x80 | ((c >> 6) & 0x3F));
			out[o++] = (char)(0x80 | (c & 0x3F));
		}
	}
	out[o] = '\0';
	return 0;
}

static void ime_event_handler(void *arg, const SceImeEventData *event) {
	(void)arg;
	if (!event) return;
	if (event->id == SCE_IME_EVENT_UPDATE_TEXT) {
		g_ime_caret_index = event->param.text.caretIndex;
	} else if (event->id == SCE_IME_EVENT_UPDATE_CARET) {
		g_ime_caret_index = event->param.caretIndex;
	} else if (event->id == SCE_IME_EVENT_PRESS_ENTER) {
		g_ime_entered = 1;
	} else if (event->id == SCE_IME_EVENT_PRESS_CLOSE) {
		g_ime_cancelled = 1;
	}
}

static size_t utf16_caret_to_utf8_byte(const SceWChar16 *text,
	                                   SceUInt32 caret_index) {
	size_t bytes = 0;
	for (SceUInt32 i = 0; text && text[i] && i < caret_index; i++) {
		uint32_t c = text[i];
		if (c >= 0xD800U && c <= 0xDBFFU && i + 1 < caret_index &&
		    text[i + 1] >= 0xDC00U && text[i + 1] <= 0xDFFFU) {
			bytes += 4;
			i++;
		} else {
			bytes += c < 0x80U ? 1 : (c < 0x800U ? 2 : 3);
		}
	}
	return bytes;
}

static void utf16_masked_to_utf8(const SceWChar16 *text,
	                             SceUInt32 caret_index,
	                             char *out, size_t out_max,
	                             size_t *caret_byte) {
	static const char bullet[] = "\xe2\x80\xa2";
	size_t o = 0;
	size_t caret = 0;
	if (!out || out_max == 0) return;
	for (size_t i = 0; text && text[i];) {
		size_t units = text[i] >= 0xD800U && text[i] <= 0xDBFFU &&
		               text[i + 1] >= 0xDC00U && text[i + 1] <= 0xDFFFU ? 2 : 1;
		if (o + sizeof(bullet) > out_max) break;
		memcpy(out + o, bullet, sizeof(bullet) - 1);
		o += sizeof(bullet) - 1;
		i += units;
		if ((SceUInt32)i <= caret_index) caret = o;
	}
	out[o] = '\0';
	if (caret_byte) *caret_byte = caret;
}

static int text_input_run(const char *title, const char *initial,
	                      char *out, size_t out_max, int secure) {
	if (!out || out_max < 2 || !ui_runtime_is_ready()) return -1;
	if (!title) title = vt_i18n_str(VT_STR_BRAND_TEXT_INPUT_TITLE);

	/* `initial` and `out` can be the same buffer: copy before overwriting
	 * the output. */
	utf8_to_utf16(initial ? initial : "", g_ime_initial,
	                    sizeof(g_ime_initial) / sizeof(g_ime_initial[0]));
	memset(g_ime_input, 0, sizeof(g_ime_input));
	memcpy(g_ime_input, g_ime_initial, sizeof(g_ime_initial));
	out[0] = '\0';
	g_ime_entered = 0;
	g_ime_cancelled = 0;
	g_ime_caret_index = 0;
	while (g_ime_initial[g_ime_caret_index] != 0) g_ime_caret_index++;

	if (!g_ime_module_loaded) {
		int module_ret = sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
		ime_diag_log("sceSysmoduleLoadModule(IME) -> 0x%08X", (unsigned)module_ret);
		if (module_ret < 0) return -1;
		g_ime_module_loaded = 1;
	}

	SceImeParam param;
	sceImeParamInit(&param);
	param.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH |
	                           SCE_IME_LANGUAGE_ITALIAN |
	                           SCE_IME_LANGUAGE_RUSSIAN |
	                           SCE_IME_LANGUAGE_JAPANESE |
	                           SCE_IME_LANGUAGE_KOREAN |
	                           SCE_IME_LANGUAGE_SIMPLIFIED_CHINESE |
	                           SCE_IME_LANGUAGE_TRADITIONAL_CHINESE;
	param.languagesForced = SCE_TRUE;
	param.type = SCE_IME_TYPE_DEFAULT;
	param.option = secure ? (SCE_IME_OPTION_NO_AUTO_CAPITALIZATION |
	                         SCE_IME_OPTION_NO_ASSISTANCE) : 0;
	param.work = g_ime_work;
	param.arg = (void *)&g_ime_entered;
	param.handler = ime_event_handler;
	param.initialText = g_ime_initial;
	param.inputTextBuffer = g_ime_input;
	param.maxTextLength = out_max - 1 < TEXT_INPUT_MAX_CHARS
	                    ? (SceUInt32)(out_max - 1) : TEXT_INPUT_MAX_CHARS;
	param.enterLabel = SCE_IME_ENTER_LABEL_DEFAULT;

	int open_ret = sceImeOpen(&param);
	ime_diag_log("sceImeOpen(%s) -> 0x%08X", title, (unsigned)open_ret);
	if (open_ret < 0) return -1;
	int ime_open = 1;

	char live_text[IME_LIVE_UTF8_CAPACITY];
	int result = -1;
	int input_too_long = 0;
	for (int frame = 0; frame < IME_TIMEOUT_FRAMES; frame++) {
		size_t caret_byte;
		if (secure) {
			utf16_masked_to_utf8(g_ime_input, g_ime_caret_index, live_text,
			                       sizeof(live_text), &caret_byte);
		} else {
			utf16_to_utf8(g_ime_input, live_text, sizeof(live_text));
			caret_byte = utf16_caret_to_utf8_byte(g_ime_input,
			                                           g_ime_caret_index);
		}
		int caret_visible = vt_preferences_reduce_motion() ||
		                    ((sceKernelGetProcessTimeWide() / 500000ULL) & 1) == 0;

		vita2d_start_drawing();
		vita2d_clear_screen();
		const char *hint = vt_i18n_str(input_too_long
		                                ? VT_STR_BRAND_TEXT_INPUT_TOO_LONG
		                                : VT_STR_BRAND_TEXT_INPUT_HINT);
		ui_brand_draw_search_backdrop_editing_label(live_text, caret_byte,
		                                            caret_visible, title, hint);
		vita2d_end_drawing();
		vita2d_wait_rendering_done();

		/* The IME is updated outside the GXM scene and immediately before
		 * the swap, while the VitaTube field stays in the app's framebuffer. */
		int update_ret = sceImeUpdate();
		vita2d_swap_buffers();
		sceDisplayWaitVblankStart();

		if (g_ime_entered) {
			int conversion_ret = utf16_to_utf8(g_ime_input, out, out_max);
			if (conversion_ret < 0) {
				/* maxTextLength is expressed by the IME in UTF-16 units, while
				 * callers provide a UTF-8 byte capacity. Keep the editor alive so
				 * CJK or supplementary characters can be shortened without losing
				 * the value the user just entered. */
				out[0] = '\0';
				g_ime_entered = 0;
				input_too_long = 1;
				continue;
			}
			sceImeClose();
			ime_open = 0;
			result = out[0] ? 1 : 0;
			break;
		}
		if (g_ime_cancelled) {
			sceImeClose();
			ime_open = 0;
			result = 0;
			break;
		}
		if (update_ret < 0) {
			ime_diag_log("sceImeUpdate frame %d -> 0x%08X", frame, (unsigned)update_ret);
			sceImeClose();
			ime_open = 0;
			result = -1;
			break;
		}
	}

	if (ime_open) {
		sceImeClose();
		ime_diag_log("IME terminato per timeout/errore");
	}
	if (secure) {
		/* The caller owns the confirmed value. Do not leave a second copy of a
		 * password in the process-wide IME buffers after the keyboard closes. */
		secure_zero(g_ime_initial, sizeof(g_ime_initial));
		secure_zero(g_ime_input, sizeof(g_ime_input));
	}
	return result;
}

int ui_text_input(const char *title, const char *initial,
	              char *out, size_t out_max) {
	return text_input_run(title, initial, out, out_max, 0);
}

int ui_text_input_secure(const char *title, const char *initial,
	                     char *out, size_t out_max) {
	return text_input_run(title, initial, out, out_max, 1);
}
