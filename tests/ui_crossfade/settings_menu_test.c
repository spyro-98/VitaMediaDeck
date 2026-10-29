/* Actual menu drawing, pixel cursor positions and native-safe text routing. */
#define VMD_ONLINE_UI
#define main old_ui_main
#include "../iptv/ui_test.c"
#undef main
#include "../../src/ui/iptv_player_panel.c"
void ui_home_tab_set(int tab){(void)tab;}
void ui_online_audio_set_home(int home){(void)home;}
static unsigned caption_pixels(int x,int y,int w,int h){unsigned n=0;unsigned c=UI_MENU_TEXT;
 for(int j=y;j<y+h;j++)for(int i=x;i<x+w;i++)if(abs(pixels[j][i][0]-(int)(c&255))<30&&abs(pixels[j][i][1]-(int)((c>>8)&255))<30&&abs(pixels[j][i][2]-(int)((c>>16)&255))<30)n++;return n;}
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 language=1;live_slots=0;download_slots=-1;
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++){
  ui_theme_apply(theme);host_reduced_motion=0;frame=1;
  UiSectionsSidebar side;ui_sections_sidebar_init(&side,UI_SECTION_LOCAL_MEDIA);side.open=1;side.animation=1;
  for(int i=0;i<90;i++){frame++;ui_sections_sidebar_tick(&side);}
  float from=side.fixed_focus.y;UiThemeRect before=sidebar_content_rect(&side,0),next=sidebar_content_rect(&side,1);
  unsigned press=SCE_CTRL_DOWN;ui_sections_sidebar_handle_buttons(&side,&press,0,128);CHECK(side.cursor==1);
  int gap=0,fractional=0;
  for(int i=0;i<24;i++){
   frame++;ui_sections_sidebar_tick(&side);float center=side.fixed_focus.y+side.fixed_focus.height*.5f;
   if(center>before.y+before.h&&center<next.y)gap=1;
   if(fabsf(side.fixed_focus.y-roundf(side.fixed_focus.y))>.01f)fractional=1;
   CHECK(side.fixed_focus.y>=from&&side.fixed_focus.y<=next.y+.01f);
   if(theme==VT_UI_THEME_LEGACY_VITA){vita2d_clear_screen();ui_sections_sidebar_draw(&side);char file[64];snprintf(file,sizeof(file),"focus-l1-%03d",i);png(file);}
  }
  CHECK(gap&&fractional);CHECK(fabsf(side.fixed_focus.y-next.y)<.2f);
  vita2d_clear_screen();ui_sections_sidebar_draw(&side);CHECK(caption_pixels(34,144,300,160)>100);
  CHECK(UI_MENU_BG==VT_THEME_BG_SOFT&&UI_MENU_TEXT==VT_THEME_TEXT&&UI_MENU_ACCENT==VT_THEME_SIGNAL_BRIGHT);
  if(theme==VT_UI_THEME_LEGACY_VITA){CHECK(pixels[250][390][2]>pixels[250][390][0]+20);CHECK(memcmp(pixels[250][390],pixels[390][390],3));}
  else {CHECK(pixels[250][390][0]!=5||pixels[250][390][1]!=20||pixels[250][390][2]!=41);}
  char name[80];snprintf(name,sizeof(name),"settings-style-l1-%d",theme);png(name);
  UiContextMenu menu={.open=1,.animation=1};UiContextAction items[]={
   {"Riproduci canale",NULL,1,1},{"Aggiungi ai preferiti",NULL,2,1},{"Qualità video",NULL,3,1},{"Lingua audio",NULL,4,1}};
  frame++;ui_context_menu_tick(&menu);vita2d_clear_screen();ui_context_menu_draw(&menu,"Opzioni IPTV",items,4);
  CHECK(caption_pixels(562,104,330,26)>30&&caption_pixels(562,156,330,26)>30);
  CHECK(pixels[172][930][0]!=5||pixels[172][930][1]!=20||pixels[172][930][2]!=41);
  snprintf(name,sizeof(name),"settings-style-r1-%d",theme);png(name);
  /* A body canvas must preserve each alternate theme's existing scene. */
  if(theme!=VT_UI_THEME_LEGACY_VITA){vita2d_clear_screen();ui_chrome_background(VT_THEME_BG,VT_THEME_SIGNAL_BRIGHT);memcpy(first_pixels,pixels,sizeof(pixels));ui_menu_surface(0,54,960,490,1);CHECK(!memcmp(first_pixels,pixels,sizeof(pixels)));}
  float start=menu.focus.y;menu.cursor=1;frame++;ui_context_menu_tick(&menu);CHECK(menu.focus.y<146&&menu.focus.y>=start);
  int moved=0;for(int i=0;i<18;i++){frame++;ui_context_menu_tick(&menu);if(menu.focus.y>94&&menu.focus.y<146)moved=1;}CHECK(moved);
  /* The geometry of an inactive action is transparent, including with a key. */
  vita2d_clear_screen();ui_chrome_background(VT_THEME_BG,VT_THEME_SIGNAL_BRIGHT);memcpy(first_pixels,pixels,sizeof(pixels));
  ui_action_button(18,110,302,46,VT_THEME_SURFACE,UI_PAD_CROSS,"Riproduci",0);
  unsigned changed=0;for(int yy=122;yy<147;yy++)for(int xx=74;xx<294;xx++)if(memcmp(pixels[yy][xx],first_pixels[yy][xx],3))changed++;
  CHECK(changed>20);CHECK(!memcmp(pixels[130][300],first_pixels[130][300],3));
 }
 FT_Done_Face(face);FT_Done_FreeType(ft);
 printf("Settings-style menus: %u checks; eight themes, visible IPTV captions, transparent inactive actions, isolated palettes, native-theme gradients, moving pixel focus crossing row gaps and L1/R1 captures passed. Host, not Vita pixels.\n",checks);
}
