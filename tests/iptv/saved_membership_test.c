/* Production explicit-save/favourite semantics; no network or physical storage. */
#define main original_ui_main
#include "ui_test.c"
#undef main
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 VtIptvChannel channel={.title="Example TV",.url="https://example.org/channel.m3u8",.mode=VT_IPTV_HLS};
 saved_link_count=save_writes=save_attempts=write_failure=stored_result=0;memset(&stored_channel,0,sizeof(stored_channel));
 CHECK(!channel_saved(&channel));CHECK(!save_link(&channel));
 CHECK(save_writes==1&&!stored_channel.starred&&channel_saved(&channel));
 CHECK(!toggle_star(&channel)&&stored_channel.starred&&channel.starred);
 CHECK(!toggle_star(&channel)&&!stored_channel.starred&&!channel.starred&&channel_saved(&channel));
 strcpy(stored_channel.preferred.audio_language,"ita");stored_channel.preferred.height=720;
 VtIptvChannel duplicate=channel;memset(&duplicate.preferred,0,sizeof(duplicate.preferred));
 CHECK(!save_link(&duplicate)&&stored_channel.preferred.height==720&&!strcmp(stored_channel.preferred.audio_language,"ita"));
 VtIptvChannel before=stored_channel;write_failure=1;
 CHECK(save_link(&duplicate)<0&&!memcmp(&before,&stored_channel,sizeof(before)));
 strcpy(duplicate.url,"https://example.org/unsaved.m3u8");
 CHECK(!channel_saved(&duplicate)&&save_link(&duplicate)<0&&!channel_saved(&duplicate));write_failure=0;
 /* R1 offers Save only for an unsaved channel, never a duplicate Saved status. */
 catalog.count=result_count=1;catalog.items[0]=channel;results[0]=0;catalog_focus=0;saved_view=0;
 UiContextAction actions[16];int n=options_for_page(actions),save=0;
 for(int i=0;i<n;i++)if(actions[i].action==23)save++;CHECK(!save);
 /* Saved links remain removable from an opened list and both presentations. */
 for(int split=0;split<2;split++)for(int saved=0;saved<2;saved++){
  split_view=split;saved_view=saved;saved_channels=1;picker_focus=0;filter_dirty=1;
  n=options_for_page(actions);int remove=0;
  CHECK(n<=16);for(int i=0;i<n;i++)if(actions[i].action==15){remove++;CHECK(i<=2&&(actions[i].enabled>>8)==UI_MENU_GROUP_LIBRARY);}
  CHECK(remove==1);
 }
 saved_view=split_view=0;filter_dirty=1;
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++){
  ui_theme_apply(theme);UiContextMenu menu={.open=1,.animation=1};n=options_for_page(actions);int index=-1;
  for(int i=0;i<n;i++)if(actions[i].action==15)index=i;
  CHECK(index>0&&index<=2);menu.cursor=index-1;
  unsigned pressed=SCE_CTRL_DOWN,touches=0;UiTouchEvent touch={0};
  CHECK(ui_context_menu_input(&menu,actions,n,&pressed,SCE_CTRL_DOWN,128,&touches,&touch)==-1&&menu.cursor==index);
  pressed=SCE_CTRL_CROSS;
  CHECK(ui_context_menu_input(&menu,actions,n,&pressed,0,128,&touches,&touch)==15);
  menu.open=1;vita2d_start_drawing();vita2d_clear_screen();ui_chrome_background(VT_THEME_BG,VT_THEME_COLD);
  ui_context_menu_draw(&menu,TR(IPTV_TITLE),actions,n);vita2d_end_drawing();
  char name[80];snprintf(name,sizeof(name),"saved-channel-remove-%d-%u",theme,language);png(name);
 }
 catalog.items[0]=duplicate;n=options_for_page(actions);
 for(int i=0;i<n;i++)if(actions[i].action==23){CHECK(actions[i].enabled&1);save++;}CHECK(save==1);
 FT_Done_Face(face);FT_Done_FreeType(ft);
 printf("Saved-channel membership: %u checks; explicit save without a star, star/unstar retain the saved link, preference preservation, failure rollback and clear catalogue actions passed.\n",checks);
}
