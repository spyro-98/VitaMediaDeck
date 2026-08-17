#include "i18n/strings.h"

#include <stddef.h>

#include "i18n/i18n.h"

static const char *const g_it[VT_STRING_COUNT] = {
#define VT_STR(id, it, en) it,
#include "i18n/strings.def"
#undef VT_STR
};

static const char *const g_en[VT_STRING_COUNT] = {
#define VT_STR(id, it, en) en,
#include "i18n/strings.def"
#undef VT_STR
};

/* The newly reduced catalog always has a complete English and Italian base.
 * Other language selections deliberately fall back to English until their
 * local/network terminology has been reviewed as a coherent set. */
const char *vt_i18n_str(VtStringId id) {
	if ((unsigned)id >= (unsigned)VT_STRING_COUNT) return "";
	return vt_i18n_current_language() == VT_LANG_IT ? g_it[id] : g_en[id];
}
