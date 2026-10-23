/* Production picker: a stable list must not rescan storage on every frame. */
#define VMD_ONLINE_UI
#define main original_iptv_main
#include "../iptv/ui_test.c"
#undef main
#include "../../src/ui/folder_picker.c"
void ui_home_tab_set(int tab){(void)tab;}
void ui_online_audio_set_home(int home){(void)home;}
void ui_brand_draw_path(const char *title,const char *path){ui_brand_draw_breadcrumbs(NULL,title,path);}
static int directory_position,enumerations,made;
SceUID sceIoDopen(const char *path){CHECK(path&&strstr(path,"ux0:"));directory_position=0;return 1;}
int sceIoDclose(SceUID id){CHECK(id==1);return 0;}
int sceIoDread(SceUID id,SceIoDirent *entry){
 CHECK(id==1);if(!directory_position)enumerations++;
 const char *names[]={"Album","song.mp3",".hidden","Photos","Created"};
 if(directory_position>=4+made)return 0;
 entry->d_stat.st_mode=directory_position==1?0:1;
 snprintf(entry->d_name,sizeof(entry->d_name),"%s",names[directory_position++]);return 1;
}
int sceIoMkdir(const char *path,int mode){CHECK(!strcmp(path,"ux0:Music/Created")&&mode==0777);made=1;return 0;}
static void reset_picker(void){frame=enumerations=made=0;script_count=8;memset(input_script,0,sizeof(input_script));rename_input=NULL;}
int main(int argc,char **argv){
 CHECK(argc==2);CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 ui_theme_apply(VT_UI_THEME_LEGACY);host_reduced_motion=1;
 char result[512];
 reset_picker();input_script[6]=SCE_CTRL_START;
 CHECK(ui_folder_picker("ux0:Music",result,sizeof(result))==1&&!strcmp(result,"ux0:Music"));
 CHECK(enumerations==1); /* Six UI frames, one directory enumeration. */
 reset_picker();input_script[1]=SCE_CTRL_CROSS;input_script[3]=SCE_CTRL_CIRCLE;input_script[5]=SCE_CTRL_START;
 CHECK(ui_folder_picker("ux0:Music",result,sizeof(result))==1&&!strcmp(result,"ux0:Music"));
 CHECK(enumerations==3); /* Enter child and return to parent each invalidates. */
 reset_picker();rename_input="Created";input_script[1]=SCE_CTRL_TRIANGLE;input_script[3]=SCE_CTRL_START;
 CHECK(ui_folder_picker("ux0:Music",result,sizeof(result))==1&&made&&enumerations==2);
 reset_picker();input_script[1]=SCE_CTRL_CIRCLE;strcpy(result,"unchanged");
 CHECK(!ui_folder_picker("ux0:",result,sizeof(result))&&!strcmp(result,"unchanged")&&enumerations==1);
 FT_Done_Face(face);FT_Done_FreeType(ft);
 printf("Destination picker: %u checks; idle enumeration cached, child/parent and mkdir invalidate, cancel preserves output.\n",checks);
}
