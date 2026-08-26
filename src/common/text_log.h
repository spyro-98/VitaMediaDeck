#ifndef VITAWAVE_COMMON_TEXT_LOG_H
#define VITAWAVE_COMMON_TEXT_LOG_H

/* Text log with scrollable history — the vendored debug screen
 * (debugScreen.c) has no scrollback of its own: when the text exceeds the
 * screen, writing restarts from the top and OVERWRITES instead of
 * scrolling (verified by reading psvDebugScreenPuts: `if ((coordY + height)
 * > SCREEN_HEIGHT) { coordX = coordY = 0; }`). With multiple diagnostic
 * steps in sequence this made it impossible to reread the output of a
 * previous step.
 *
 * log_printf() prints live (same visible behavior as psvDebugScreenPrintf
 * during normal execution) AND saves the text into a buffer; log_view()
 * then shows it paginated, with a larger font (2x, scaled with
 * psvDebugScreenScaleFont2x already available in debugScreen.h) and an
 * extra blank line between each line for readability. */

void log_init(void);

__attribute__((__format__ (__printf__, 1, 2)))
void log_printf(const char *format, ...);

/* Blocking viewer mode: clean screen, large font, spaced lines, opens on
 * the most recent page of history.
 * Controls: UP/DOWN (or L1/R1) scroll to the previous/next page; CROSS or
 * START exit and return to the caller (the font goes back to normal). */
void log_view(void);

/* Saves the current history to a file without displaying it. Useful in the
 * graphical path: diagnostics remain retrievable via VitaShell without
 * taking the user back to the debug screen. Returns 0 or a negative sceIo
 * error. */
int log_save(const char *path);

#endif
