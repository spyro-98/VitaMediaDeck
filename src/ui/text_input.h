#ifndef VITAWAVE_UI_TEXT_INPUT_H
#define VITAWAVE_UI_TEXT_INPUT_H

#include <stddef.h>

/* Low-level on-screen keyboard (sceImeOpen/Update/Close). Unlike
 * SceImeDialog it does not create the white page and does not own a text
 * view: VitaWave keeps drawing its own header and shows the live text in
 * its centered editor. sceImeUpdate() is called outside the
 * GXM scene, before the framebuffer swap.
 *
 * `initial` is validated UTF-8 and converted internally to UTF-16. The text
 * typed by the user is converted back to UTF-8, including surrogate pairs.
 * A value that cannot fit in out is rejected instead of being silently cut.
 *
 * Returns 1 if the user confirmed with ENTER and out holds non-empty text,
 * 0 if they cancelled or confirmed empty, <0 on IME/conversion failure. */
int ui_text_input(const char *title, const char *initial, char *out, size_t out_max);

/* Password/secret variant. The confirmed UTF-8 value is unchanged, while the
 * app-owned live field shows one bullet per Unicode scalar and IME assistance
 * is disabled so typed secrets are not exposed through suggestions. */
int ui_text_input_secure(const char *title, const char *initial,
	                     char *out, size_t out_max);

#endif /* VITAWAVE_UI_TEXT_INPUT_H */
