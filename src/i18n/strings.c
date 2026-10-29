#include "i18n/strings.h"
#include <stddef.h>
#include <string.h>
#include "i18n/i18n.h"
static const char *const g_it[VT_STRING_COUNT]={
#define VT_STR(id,it,en) it,
#include "i18n/strings.def"
#undef VT_STR
};
static const char *const g_en[VT_STRING_COUNT]={
#define VT_STR(id,it,en) en,
#include "i18n/strings.def"
#undef VT_STR
};
typedef struct {const char *english,*translated;} Translation;
#define VT_TRANSLATION(en,value) {en,value},
static const Translation es[]={
#include "i18n/strings_translation_es.def"
};
static const Translation fr[]={
#include "i18n/strings_translation_fr.def"
};
static const Translation de[]={
#include "i18n/strings_translation_de.def"
};
static const Translation pt[]={
#include "i18n/strings_translation_pt.def"
};
static const Translation ru[]={
#include "i18n/strings_translation_ru.def"
};
static const Translation ja[]={
#include "i18n/strings_translation_ja.def"
};
static const Translation ko[]={
#include "i18n/strings_translation_ko.def"
};
static const Translation zh_s[]={
#include "i18n/strings_translation_zh_s.def"
};
static const Translation zh_t[]={
#include "i18n/strings_translation_zh_t.def"
};
#undef VT_TRANSLATION
const char *vt_i18n_text(const char *english,const char *italian){
 if(!english)return "";
 int language=vt_i18n_current_language();
 if(language==VT_LANG_IT)return italian?italian:english;
 const Translation *table=NULL;size_t count=0;
#define LOCALE(id,name) case id:table=name;count=sizeof(name)/sizeof(*name);break
 switch(language){
 LOCALE(VT_LANG_ES,es);LOCALE(VT_LANG_FR,fr);LOCALE(VT_LANG_DE,de);LOCALE(VT_LANG_PT,pt);LOCALE(VT_LANG_RU,ru);
 LOCALE(VT_LANG_JA,ja);LOCALE(VT_LANG_KO,ko);LOCALE(VT_LANG_ZH_S,zh_s);LOCALE(VT_LANG_ZH_T,zh_t);
 default:return english;
 }
#undef LOCALE
 size_t lo=0,hi=count;
 while(lo<hi){size_t mid=lo+(hi-lo)/2;int order=strcmp(english,table[mid].english);
  if(!order)return table[mid].translated;
  if(order<0)hi=mid;else lo=mid+1;
 }
 return english;
}
const char *vt_i18n_str(VtStringId id){
 if((unsigned)id>=(unsigned)VT_STRING_COUNT)return "";
 return vt_i18n_text(g_en[id],g_it[id]);
}
