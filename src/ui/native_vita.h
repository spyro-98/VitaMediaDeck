#ifndef VMD_NATIVE_VITA_H
#define VMD_NATIVE_VITA_H
#include <stdint.h>
/* Complete Legacy Vita presentation. Input and media textures stay app-owned.
 * The firmware supplies system text, buttons, panels and busy indicators. */
#define UI_NATIVE_TEXT_LIMIT 160
#define UI_NATIVE_BUTTON_LIMIT 64
#define UI_NATIVE_PANEL_LIMIT 48
#define UI_NATIVE_BUSY_LIMIT 4
int ui_native_vita_prepare(void);
int ui_native_vita_active(void);
int ui_native_vita_error(void);
int ui_native_vita_text(float x, float baseline, float width, unsigned size, unsigned color,
                        const char *text);
int ui_native_vita_button(float x, float y, float w, float h, const char *label, int selected);
int ui_native_vita_button_fade(float x, float y, float w, float h, const char *label, int selected,
                               float alpha);
void ui_native_vita_media_text(int active);
int ui_native_vita_panel(float x, float y, float w, float h, unsigned color, int selected);
int ui_native_vita_focus(float x, float y, float w, float h);
int ui_native_vita_busy(float x, float y, float size);
unsigned ui_native_vita_utf16(const char *text, uint16_t *out, unsigned capacity);
#endif
