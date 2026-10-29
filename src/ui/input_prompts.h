#ifndef VMD_INPUT_PROMPTS_H
#define VMD_INPUT_PROMPTS_H

#include <stddef.h>

/* Explicit private-use tokens in app-owned copy only. Never recognize ordinary
 * X/O/L1 words in filenames, server names, user input or subtitles as buttons. */
#define UI_PAD_CROSS    "\xee\x80\x80"
#define UI_PAD_CIRCLE   "\xee\x80\x81"
#define UI_PAD_SQUARE   "\xee\x80\x82"
#define UI_PAD_TRIANGLE "\xee\x80\x83"
#define UI_PAD_L1       "\xee\x80\x84"
#define UI_PAD_R1       "\xee\x80\x85"
enum { UI_PAD_FIRST = 0xe000, UI_PAD_COUNT = 6, UI_PAD_GUTTER = 6 };

static inline int ui_input_prompt_id(unsigned cp) {
    return cp >= UI_PAD_FIRST && cp < UI_PAD_FIRST + UI_PAD_COUNT
               ? (int)(cp - UI_PAD_FIRST) : -1;
}
static inline int ui_input_prompt_at(const char *text) {
    const unsigned char *p = (const unsigned char *)text;
    return p && p[0] == 0xee && p[1] == 0x80 && p[2] >= 0x80 && p[2] < 0x86
               ? p[2] - 0x80 : -1;
}
static inline int ui_input_prompt_contains(const char *text) {
    if (text) for (; *text; text++) if (ui_input_prompt_at(text) >= 0) return 1;
    return 0;
}
static inline int ui_input_prompt_width(int id, unsigned size) {
    return (id >= 4 ? (int)(size * 3 / 2) : (int)size) + UI_PAD_GUTTER;
}

/* Session resources: load outside the draw pass, free after GPU completion. */
int ui_input_prompts_load(void);
void ui_input_prompts_free(void);
int ui_input_prompt_draw(int id, int x, int baseline, unsigned size, unsigned alpha);

#endif
