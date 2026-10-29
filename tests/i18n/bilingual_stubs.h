#ifndef VMD_TEST_BILINGUAL_STUBS_H
#define VMD_TEST_BILINGUAL_STUBS_H
/* Existing IT/EN UI fixtures. Full catalog/registry tests use real i18n.c. */
const char *vt_i18n_text(const char *en,const char *it){return vt_i18n_current_language()==VT_LANG_IT?it:en;}
const char *vt_i18n_language_code(void){return vt_i18n_current_language()==VT_LANG_IT?"it":"en";}
int vt_i18n_system_language(void){return vt_i18n_current_language()==VT_LANG_IT?5:1;}
const char *vt_i18n_language_name(int language){
 static const char *names[]={"System","English","Italiano","Español","Français","Deutsch","Português","Русский","日本語","한국어","简体中文","繁體中文"};
 return names[language>=0&&language<12?language:0];
}
#endif
