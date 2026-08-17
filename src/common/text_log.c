#include "text_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>

#include "debugScreen.h"
#include "settings/preferences.h"

/* 256 KiB: negligible on the Vita's 512 MB, ample for a test session (this
 * project's longest individual lines — JSON dumps — are a few hundred bytes
 * each). */
#define LOG_BUF_SIZE (256 * 1024)
#define LOG_LINE_MAX 512 /* per single log_printf call/displayed line */
#define LINES_PER_PAGE 12 /* see sizing note in log_view() */

static char g_log_buf[LOG_BUF_SIZE];
static size_t g_log_len;
static int g_log_total_lines; /* numero di '\n' presenti nel buffer */
static volatile int g_log_lock;

static PsvDebugScreenFont *g_big_font; /* created on the first log_view(), then reused */

static void log_lock(void) {
	while (__sync_lock_test_and_set(&g_log_lock, 1))
		sceKernelDelayThread(100);
}

static void log_unlock(void) {
	__sync_lock_release(&g_log_lock);
}

void log_init(void) {
	g_log_lock = 0;
	g_log_len = 0;
	g_log_total_lines = 0;
	g_log_buf[0] = '\0';
}

static void log_append(const char *text) {
	size_t len = strlen(text);
	if (g_log_len + len >= LOG_BUF_SIZE) {
		/* Buffer full: not fatal, it just stops recording new history. The
		 * live-print (which has already happened) is not affected. */
		return;
	}
	memcpy(g_log_buf + g_log_len, text, len);
	g_log_len += len;
	g_log_buf[g_log_len] = '\0';
	for (const char *p = text; *p; p++) {
		if (*p == '\n') g_log_total_lines++;
	}
}

void log_printf(const char *format, ...) {
	char buf[LOG_LINE_MAX];
	va_list args;
	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	psvDebugScreenPuts(buf);
	log_lock();
	log_append(buf);
	log_unlock();
}

/* Byte offset in the buffer of the start of line `line_index` (0-based).
 * If line_index >= total lines, returns the end of the buffer (empty page). */
static size_t offset_of_line(int line_index) {
	if (line_index <= 0) return 0;

	int count = 0;
	for (size_t i = 0; i < g_log_len; i++) {
		if (g_log_buf[i] == '\n') {
			count++;
			if (count == line_index) return i + 1;
		}
	}
	return g_log_len;
}

/* Prints (on an already-cleared screen) up to `max_lines` logical lines
 * starting from `start_line`, with an extra blank line after each. */
static void print_page(int start_line, int max_lines) {
	size_t i = offset_of_line(start_line);
	int lines_shown = 0;

	while (i < g_log_len && lines_shown < max_lines) {
		size_t line_start = i;
		while (i < g_log_len && g_log_buf[i] != '\n') i++;

		size_t line_len = i - line_start;
		if (line_len >= LOG_LINE_MAX) line_len = LOG_LINE_MAX - 1;

		char line[LOG_LINE_MAX];
		memcpy(line, g_log_buf + line_start, line_len);
		line[line_len] = '\0';

		psvDebugScreenPuts(line);
		psvDebugScreenPuts("\n\n"); /* extra blank line: the requested spacing */

		if (i < g_log_len) i++; /* skip the original '\n' */
		lines_shown++;
	}
}

void log_view(void) {
	PsvDebugScreenFont *normal_font = psvDebugScreenGetFont();

	/* Page sizing: default 8x8 font (960/8=120 columns, 544/8=68 rows). At
	 * 2x it becomes 16x16 (60 columns, 34 rows). With an extra blank line
	 * after each one, 34 physical rows = ~17 logical lines at most: 12
	 * leaves a safety margin against psvDebugScreenPuts's automatic wrap
	 * (which otherwise, once the page is full, would restart from the top
	 * and overwrite — exactly the problem this module solves, so it must be
	 * avoided in here too). */
	if (!g_big_font) {
		g_big_font = psvDebugScreenScaleFont2x(normal_font);
	}
	if (g_big_font) {
		psvDebugScreenSetFont(g_big_font);
	}

	int start_line = g_log_total_lines - LINES_PER_PAGE;
	if (start_line < 0) start_line = 0;

	SceCtrlData ctrl, prev_ctrl;
	/* Read immediately (not zeroed out): if a button is still physically
	 * held down when entering this function (e.g. the user didn't have time
	 * to release CROSS after closing a previous log_view()), it must not
	 * count as a "new" press — otherwise the view would close instantly
	 * without the user managing to see it. */
	sceCtrlPeekBufferPositive(0, &prev_ctrl, 1);

	for (;;) {
		psvDebugScreenPuts("\e[2J\e[1;1H");
		print_page(start_line, LINES_PER_PAGE);
		/* Waits for a rising edge (button just pressed), not the held
		 * state — otherwise holding the button down would scroll multiple
		 * pages from what the user perceives as a single press. */
		for (;;) {
			sceCtrlPeekBufferPositive(0, &ctrl, 1);
			unsigned int pressed = ctrl.buttons & ~prev_ctrl.buttons;
			prev_ctrl = ctrl;

			if (pressed & (SCE_CTRL_CROSS | SCE_CTRL_START)) {
				psvDebugScreenSetFont(normal_font);
				psvDebugScreenPuts("\e[2J\e[1;1H");
				return;
			}
			if (pressed & (SCE_CTRL_UP | SCE_CTRL_LTRIGGER)) {
				start_line -= LINES_PER_PAGE;
				if (start_line < 0) start_line = 0;
				break;
			}
			if (pressed & (SCE_CTRL_DOWN | SCE_CTRL_RTRIGGER)) {
				int max_start = g_log_total_lines - 1;
				if (max_start < 0) max_start = 0;
				start_line += LINES_PER_PAGE;
				if (start_line > max_start) start_line = max_start;
				break;
			}
			sceKernelDelayThread(16 * 1000);
		}
	}
}

int log_save(const char *path) {
	if (!path) return -1;
	if (!vt_preferences_disk_logs_enabled()) return 0;
	/* Write to a temporary file + rename, not a direct O_TRUNC on the final
	 * file: a crash of ANOTHER thread while this save is in progress used
	 * to leave the log at 0 bytes (observed twice in the 2026-08-07 dumps,
	 * precisely in the sessions where the log was needed most). With the
	 * temporary file, the previous file stays intact until the rename. */
	char tmp_path[256];
	int n = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
	if (n <= 0 || (size_t)n >= sizeof(tmp_path)) return -1;
	log_lock();
	SceUID fd = sceIoOpen(tmp_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) { log_unlock(); return (int)fd; }

	size_t written_total = 0;
	while (written_total < g_log_len) {
		int written = sceIoWrite(fd, g_log_buf + written_total,
		                         g_log_len - written_total);
		if (written <= 0) {
			sceIoClose(fd);
			log_unlock();
			return written < 0 ? written : -1;
		}
		written_total += (size_t)written;
	}
	sceIoClose(fd);
	sceIoRemove(path);
	int rename_ret = sceIoRename(tmp_path, path);
	log_unlock();
	return rename_ret < 0 ? rename_ret : 0;
}
