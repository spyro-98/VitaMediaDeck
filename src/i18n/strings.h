#ifndef VITAWAVE_I18N_STRINGS_H
#define VITAWAVE_I18N_STRINGS_H

/* Localizable string-key list generated from the X-macro tables in
 * i18n/strings_*.def. Each UI module owns one file, allowing independent
 * patches to add strings without editing this shared header. Add new keys to
 * the module's .def file listed by i18n/strings.def with
 * VT_STR(NAME, "Italian text", "English text"), never directly here. */
typedef enum {
#define VT_STR(id, it, en) VT_STR_##id,
#include "i18n/strings.def"
#undef VT_STR
	VT_STRING_COUNT
} VtStringId;

#endif /* VITAWAVE_I18N_STRINGS_H */
