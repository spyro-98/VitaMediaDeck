/* Shared components in every theme, measured with the real packaged faces. */
#define VMD_CHROME_HELPERS_ONLY
#include "../ui_chrome/test.c"
int main(int argc,char**argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));
 FT_Face inter,mono;CHECK(!FT_New_Face(ft,argv[1],0,&inter));
 CHECK(!FT_New_Face(ft,"assets/fonts/IBMPlexMono-Regular.ttf",0,&mono));
 const char*keys[]={UI_PAD_CROSS,UI_PAD_CIRCLE,UI_PAD_SQUARE,UI_PAD_TRIANGLE,UI_PAD_L1,UI_PAD_R1};
 const int widths[]={64,96,146,186,226,302,384};
 host_record_text=1;
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++){
  ui_theme_apply(theme);face=theme==VT_UI_THEME_DECK_90?mono:inter;
  for(unsigned k=0;k<6;k++)for(unsigned w=0;w<sizeof(widths)/sizeof(widths[0]);w++){
   vita2d_clear_screen();host_text_count=0;
   ui_action_button(100,100,widths[w],44,VT_THEME_SURFACE,keys[k],language?"Apri collegamento salvato":"Open saved connection",0);
   CHECK(host_text_count>=1);CHECK(host_text_rects[0].prompt);
   CHECK(host_text_rects[0].x>=110&&host_text_rects[0].x+host_text_rects[0].width<=100+widths[w]-10);
   if(host_text_count>1){
    CHECK(host_text_rects[1].x>=host_text_rects[0].x+host_text_rects[0].width+12);
    CHECK(host_text_rects[1].x+host_text_rects[1].width<=100+widths[w]-12);
   }
   host_text_count=0;ui_action_button(100,100,widths[w],44,VT_THEME_SURFACE,keys[k],"",0);
   CHECK(host_text_count==1&&host_text_rects[0].prompt);
   CHECK(abs(2*host_text_rects[0].x+host_text_rects[0].width-(200+widths[w]))<=2);
  }
  host_text_count=0;ui_footer(536,vt_i18n_str(VT_STR_UI_OPEN_HINT),vt_i18n_str(VT_STR_GUIDE_EXIT_HINT));
  CHECK(host_text_count==2);
  CHECK(host_text_rects[0].x>=28&&host_text_rects[1].x+host_text_rects[1].width<=932);
  CHECK(host_text_rects[1].x>=host_text_rects[0].x+host_text_rects[0].width+20);
  vita2d_clear_screen();ui_chrome_background(VT_THEME_BG,VT_THEME_COLD);
  ui_brand_draw_home_header();
  for(int k=0;k<6;k++)ui_action_button(80,80+k*66,800,50,VT_THEME_SURFACE,keys[k],language?"Etichetta con margini riservati":"Label with reserved spacing",0);
  ui_footer(536,vt_i18n_str(VT_STR_UI_OPEN_HINT),vt_i18n_str(VT_STR_GUIDE_EXIT_HINT));
  char name[80];snprintf(name,sizeof(name),"controller-spacing-%d-%s",theme,language?"it":"en");png(name);
  host_text_count=0;
 }
 FT_Done_Face(inter);FT_Done_Face(mono);FT_Done_FreeType(ft);
 printf("Controller spacing: %u checks; eight themes, IT/EN, seven button widths, all six keys, 12px icon/label and right insets, centered icon-only controls and separated footer columns passed.\n",checks);
}
