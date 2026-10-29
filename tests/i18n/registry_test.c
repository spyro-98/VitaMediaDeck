#include <assert.h>
#include <stdio.h>
#include "i18n/i18n.h"
#include "settings/preferences.h"
#include "common/text_log.h"
static int preference,system_language,registry_error,save_error;
int sceRegMgrSystemParamGetInt(int key,int *out){(void)key;*out=system_language;return registry_error;}
void log_printf(const char *format,...){(void)format;}
int vt_preferences_language(void){return preference;}
int vt_preferences_set_language(int value){if(value<0||value>=VT_LANGUAGE_COUNT||save_error)return -1;preference=value;return 0;}
const char *vt_i18n_str(VtStringId id){(void)id;return "System";}
#include "../../src/i18n/i18n.c"
int main(void){
 const int mapping[]={1,5,3,2,4,7,8,0,9,11,10};
 for(int language=1;language<VT_LANG_COUNT;language++){
  preference=0;system_language=mapping[language-1];vt_i18n_init();assert(vt_i18n_current_language()==language);
  assert(vt_i18n_system_language()==mapping[language-1]);assert(vt_i18n_language_code()[0]);assert(vt_i18n_language_name(language)[0]);
  assert(!vt_i18n_set_language(language)&&preference==language);system_language=1;vt_i18n_init();assert(vt_i18n_current_language()==language);
 }
 preference=0;system_language=18;vt_i18n_init();assert(vt_i18n_current_language()==VT_LANG_PT);
 registry_error=-1;vt_i18n_init();assert(vt_i18n_current_language()==VT_LANG_EN);registry_error=0;
 assert(!vt_i18n_set_language(VT_LANG_JA));save_error=1;assert(vt_i18n_set_language(VT_LANG_FR)<0&&vt_i18n_current_language()==VT_LANG_JA);save_error=0;
 assert(vt_i18n_set_language(VT_LANG_COUNT)<0);puts("Restored languages: all explicit/system IDs, PT-BR mapping, failed-read English default and failed-write rollback passed.");
}
