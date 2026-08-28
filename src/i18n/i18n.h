#ifndef VITAMEDIADECK_I18N_I18N_H
#define VITAMEDIADECK_I18N_I18N_H

#include "i18n/strings.h"

/* Effective language, never VT_LANGUAGE_AUTO. That value exists only as a
 * stored preference (src/settings/preferences.h); it is resolved to a
 * concrete language here. */
#define VT_LANG_EN 1
#define VT_LANG_IT 2

/* Call once at startup after vt_preferences_init(). If the stored preference
 * is VT_LANGUAGE_AUTO, read the system language with
 * sceRegMgrSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, ...), not
 * sceAppUtilInit(). The latter combined with sceCommonDialogSetConfigParam
 * caused two separate hardware regressions in this project (a black startup
 * screen, then immediate process termination; see the foundation/networking
 * milestone in mds/DEVELOPMENT_LOG.md). SceRegistryMgr is an independent
 * module that reads the registry through a syscall and needs no prior setup.
 * If the read fails or reports an unsupported language, English is used. */
void vt_i18n_init(void);

/* Effective language currently in use (one of the VT_LANG_* constants). */
int vt_i18n_current_language(void);

/* Changes the active language and persists it immediately through
 * preferences.c. `language` also accepts VT_LANGUAGE_AUTO; in that case the
 * effective language is recalculated from the system setting without an app
 * restart. */
int vt_i18n_set_language(int language);

/* Localized string for the currently active language. */
const char *vt_i18n_str(VtStringId id);

#endif /* VITAMEDIADECK_I18N_I18N_H */
