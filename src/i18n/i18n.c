#include "i18n/i18n.h"

#include <psp2/registrymgr.h>
#include <psp2/system_param.h>

#include "common/text_log.h"
#include "settings/preferences.h"

static int g_current_language = VT_LANG_EN;

static int detect_system_language(void) {
	int lang = SCE_SYSTEM_PARAM_LANG_ENGLISH_US;
	int ret = sceRegMgrSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &lang);
	/* Keep the registry result in the opt-in diagnostic log so language
	 * fallback problems can be diagnosed without blocking application startup. */
	log_printf("i18n: sceRegMgrSystemParamGetInt(LANG) -> 0x%08X, lang=%d (IT=%d)",
	          (unsigned)ret, lang, SCE_SYSTEM_PARAM_LANG_ITALIAN);
	if (ret < 0) return VT_LANG_EN;
	if (lang == SCE_SYSTEM_PARAM_LANG_ITALIAN) return VT_LANG_IT;
	return VT_LANG_EN;
}

static int resolve_language(int preference) {
	if (preference == VT_LANGUAGE_EN) return VT_LANG_EN;
	if (preference == VT_LANGUAGE_IT) return VT_LANG_IT;
	return detect_system_language();
}

void vt_i18n_init(void) {
	int preference = vt_preferences_language();
	g_current_language = resolve_language(preference);
	static const char *const codes[] = { "?", "EN", "IT" };
	log_printf("i18n: preference=%d -> active language=%s", preference,
	          g_current_language >= VT_LANG_EN && g_current_language <= VT_LANG_IT
	              ? codes[g_current_language] : "EN");
}

int vt_i18n_current_language(void) {
	return g_current_language;
}

int vt_i18n_set_language(int language) {
	int ret = vt_preferences_set_language(language);
	if (ret == 0) g_current_language = resolve_language(language);
	return ret;
}
