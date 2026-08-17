#ifndef VITATUBE_UI_TEXT_INPUT_H
#define VITATUBE_UI_TEXT_INPUT_H

#include <stddef.h>

/* Low-level on-screen keyboard (sceImeOpen/Update/Close). Unlike
 * SceImeDialog it does not create the white page and does not own a text
 * view: VitaTube keeps drawing its own header and shows the live text in
 * its centered editor. sceImeUpdate() is called outside the
 * GXM scene, before the framebuffer swap.
 *
 * `initial` is UTF-8 and is converted internally to UTF-16. The text typed
 * by the user is converted back from UTF-16 to UTF-8 into `out` (truncated
 * to out_max-1 bytes) — Basic Multilingual Plane ONLY, no surrogate pairs:
 * enough for normal source names and paths, not a full UTF-16 decoder.
 *
 * Returns 1 if the user confirmed with ENTER and out holds non-empty text,
 * 0 if they cancelled or confirmed empty, <0 if sceImeOpen() fails. */
int ui_text_input(const char *title, const char *initial, char *out, size_t out_max);

#endif /* VITATUBE_UI_TEXT_INPUT_H */
