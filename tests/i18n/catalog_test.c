#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "i18n/i18n.h"
static int language=VT_LANG_EN;
int vt_i18n_current_language(void){return language;}
#include "../../src/i18n/strings.c"
static unsigned checked;
static void check_table(const Translation *table,size_t count,int locale){
 language=locale;
 for(size_t i=0;i<count;i++){
  if(i)assert(strcmp(table[i-1].english,table[i].english)<0);
  assert(!strcmp(vt_i18n_text(table[i].english,NULL),table[i].translated));
  if(*table[i].english)assert(*table[i].translated);
  checked++;
 }
 for(int id=0;id<VT_STRING_COUNT;id++){
  if(!*g_en[id])continue;
  size_t found=0;
  for(size_t i=0;i<count;i++)if(!strcmp(g_en[id],table[i].english)){found=1;break;}
  if(!found)fprintf(stderr,"Missing generated key: %s\n",g_en[id]);
  assert(found); /* Every catalog ID must have a generated entry. */
 }
}
int main(void){
#define TEST(locale,table) check_table(table,sizeof(table)/sizeof(*table),locale)
 TEST(VT_LANG_ES,es);TEST(VT_LANG_FR,fr);TEST(VT_LANG_DE,de);TEST(VT_LANG_PT,pt);
 TEST(VT_LANG_RU,ru);TEST(VT_LANG_JA,ja);TEST(VT_LANG_KO,ko);TEST(VT_LANG_ZH_S,zh_s);TEST(VT_LANG_ZH_T,zh_t);
#undef TEST
 const char *play[]={"","Play","Riproduci","Reproducir","Lire","Abspielen","Reproduzir","Воспроизвести","再生","재생","播放","播放"};
 for(language=1;language<VT_LANG_COUNT;language++){
  if(language!=VT_LANG_IT)assert(!strcmp(vt_i18n_text("Play","Riproduci"),play[language]));
  for(int id=0;id<VT_STRING_COUNT;id++)assert(vt_i18n_str(id));
  assert(!strcmp(vt_i18n_text("unknown runtime label",NULL),"unknown runtime label"));
  assert(!*vt_i18n_str(-1)&&!*vt_i18n_str(VT_STRING_COUNT));
 }
 language=VT_LANG_IT;assert(!strcmp(vt_i18n_text("English","Italiano"),"Italiano"));
 assert(!*vt_i18n_text(NULL,NULL));
 printf("Offline catalogs: %u exact binary-search lookups, 11 locales, reviewed playback captions, all IDs and unknown-key boundaries passed.\n",checked);
}
