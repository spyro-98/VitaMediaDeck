#ifndef VITAWAVE_UI_TEXT_READER_H
#define VITAWAVE_UI_TEXT_READER_H

/* Full, scrollable UTF-8 text view. The source remains owned by the caller
 * and is never copied into a smaller/truncated display buffer. */
void ui_text_reader_run(const char *title, const char *text);

/* Settings-style two-tab variant. The right tab is active; R1, LEFT, CIRCLE,
 * or touching the left tab returns to the parent tab. */
void ui_text_reader_run_tabbed(const char *parent_title, const char *title,
	                           const char *text);

#endif
