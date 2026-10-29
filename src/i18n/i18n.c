#include "i18n/i18n.h"

#include <psp2/registrymgr.h>
#include <psp2/system_param.h>

#include "common/text_log.h"
#include "settings/preferences.h"

static int g_current_language = VT_LANG_EN;

static const char *const language_codes[VT_LANG_COUNT]={"en","en","it","es","fr","de","pt","ru","ja","ko","zh-CN","zh-TW"};
static const int system_languages[VT_LANG_COUNT]={1,1,5,3,2,4,7,8,0,9,11,10};
static int detect_system_language(void) {
 int system=1;int ret=sceRegMgrSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG,&system);
 log_printf("i18n: system language read=%d value=%d",ret,system);
 if(ret<0)return VT_LANG_EN;
 for(int i=1;i<VT_LANG_COUNT;i++)if(system_languages[i]==system)return i;
 if(system==18)return VT_LANG_PT; /* Brazil Portuguese */
 return VT_LANG_EN;
}
static int resolve_language(int preference){
 return preference>VT_LANGUAGE_AUTO&&preference<VT_LANGUAGE_COUNT?preference:detect_system_language();
}
const char *vt_i18n_language_code(void){return language_codes[g_current_language];}
int vt_i18n_system_language(void){return system_languages[g_current_language];}
const char *vt_i18n_language_name(int language){
 static const char *const names[VT_LANG_COUNT]={"System","English","Italiano","Español","Français","Deutsch","Português","Русский","日本語","한국어","简体中文","繁體中文"};
 if(language==0)return vt_i18n_str(VT_STR_SETTINGS_LANGUAGE_SYSTEM);
 return names[language>0&&language<VT_LANG_COUNT?language:VT_LANG_EN];
}
void vt_i18n_init(void) {
	int preference = vt_preferences_language();
	g_current_language = resolve_language(preference);
	log_printf("i18n: preference=%d active=%s",preference,vt_i18n_language_code());
}

int vt_i18n_current_language(void) {
	return g_current_language;
}

int vt_i18n_set_language(int language) {
	int ret = vt_preferences_set_language(language);
	if (ret == 0) g_current_language = resolve_language(language);
	return ret;
}
