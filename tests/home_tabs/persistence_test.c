#define main permutations_main
#include "test.c"
#undef main
#include <sys/stat.h>
#include <unistd.h>
static int rename_failure;
int sceIoGetstat(const char *path,SceIoStat *out){struct stat st;if(stat(path,&st))return -1;out->st_size=st.st_size;return 0;}
int sceIoRemove(const char *path){return unlink(path);}
int sceIoRename(const char *from,const char *to){if(rename_failure&&strstr(from,".tmp"))return -1;return rename(from,to);}
static void write_bytes(const char *path,const unsigned char *bytes,size_t count){FILE*f=fopen(path,"wb");CHECK(f);CHECK(fwrite(bytes,1,count,f)==count);CHECK(!fclose(f));}
int main(void){
 CHECK(!mkdir("ux0:data",0700));CHECK(!mkdir("ux0:data/VitaMediaDeck",0700));
 unsigned char old[6]={5,0,4,3,1,2};write_bytes(ORDER_PATH,old,6);
 unsigned char saved[VT_UI_THEME_COUNT][HOME_TAB_COUNT];
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++){
  ui_theme_apply(theme);loaded=0;order_theme=-1;ui_home_tab_set(HOME_LIBRARY);
  CHECK(!memcmp(order,old,6));
  for(int i=0;i<6;i++)saved[theme][i]=(unsigned char)((i+theme)%6);
  if(theme==6){unsigned char t=saved[theme][0];saved[theme][0]=saved[theme][1];saved[theme][1]=t;}
  memcpy(order,saved[theme],6);CHECK(!save_order());
 }
 /* A theme switch loads that theme's order and keeps the active semantic tab. */
 ui_home_tab_set(HOME_IMAGES);
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++){
  ui_theme_apply(theme);CHECK(ui_home_tab()==HOME_IMAGES);CHECK(!memcmp(order,saved[theme],6));
 }
 ui_theme_apply(VT_UI_THEME_GHOST_MESH);load();unsigned char before[6];memcpy(before,order,6);
 unsigned char t=order[0];order[0]=order[1];order[1]=t;rename_failure=1;
 CHECK(save_order()<0);rename_failure=0;loaded=0;load();CHECK(!memcmp(order,before,6));
 char path[128],backup[128];order_path(VT_UI_THEME_GHOST_MESH,path,"");order_path(VT_UI_THEME_GHOST_MESH,backup,".bak");
 write_bytes(backup,before,6);write_bytes(path,(const unsigned char*)"bad",3);loaded=0;load();CHECK(!memcmp(order,before,6));
 t=order[0];order[0]=order[1];order[1]=t;rename_failure=1;CHECK(save_order()<0);rename_failure=0;
 loaded=0;load();CHECK(!memcmp(order,before,6));
 unsigned char global[6];CHECK(!order_read(ORDER_PATH,global)&&!memcmp(global,old,6));
 puts("Per-theme tab positions: migration, eight independent orders, theme switching, failed-save "
      "rollback and backup recovery passed");
 return 0;
}
