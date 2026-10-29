/* Real Home event loop and in-memory registry. Changes land immediately after
 * its first frame, well inside the old 250 ms refresh gate, without a restart. */
#define VMD_LIBRARY_UI_NO_MAIN
#include "ui_test.c"
static VtLibraryItem old_item,new_item;
static int mutation;
static void mutate_after_first_frame(unsigned tick){
 if(tick)return;
 if(mutation==1){vt_library_started(&new_item);vt_library_progress(new_item.history_id,125000,3600000);}
 if(mutation==2){home_favourites_fixture[0]=new_item;home_publication_revision++;}
 if(mutation==3){home_source_extra=1;fixture_sources_revision++;}
 if(mutation==4){vt_library_progress(old_item.history_id,3600000,3600000);}
}
static void make_video(VtLibraryItem *v,const char *label,int index){
 *v=(VtLibraryItem){.provider=VT_LIBRARY_LOCAL,.kind=VT_LIBRARY_VIDEO,.duration_ms=3600000};
 snprintf(v->locator,sizeof(v->locator),"ux0:video/%s-%d.mp4",label,index);
 snprintf(v->title,sizeof(v->title),"%s %d",label,index);snprintf(v->history_id,sizeof(v->history_id),"%s-%d",label,index);
 vt_library_identity(v);
}
static void prepare_live(int theme,int role,int change){
 ui_theme_apply(theme);ui_home_tab_set(HOME_LIBRARY);view=LIBRARY_HOME;
 dashboard_empty=dashboard_cold=dashboard_drop_resume=0;progressive=publication_fixture=0;
 home_favourites_override=1;home_favourites_count=1;home_source_extra=0;
 dashboard_sources=1;mini_top=theme%2?472:536;dash_selected=role;mutation=change;
 script_exit();input_script[1]=SCE_CTRL_CROSS;
 if(role!=UI_DASH_CONTINUE)input_script[3]=SCE_CTRL_CROSS;
 frame_observer=mutate_after_first_frame;tap_frame=-1;
}
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];language=1;CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++){
  make_video(&old_item,"old",theme);make_video(&new_item,"new",theme);
  vt_library_started(&old_item);vt_library_progress(old_item.history_id,45000,3600000);
  home_favourites_fixture[0]=old_item;
  /* Re-enter with an old carousel anchor, after a different video played. */
  vt_library_started(&new_item);vt_library_progress(new_item.history_id,125000,3600000);
  prepare_live(theme,UI_DASH_CONTINUE,0);strcpy(dash_anchor[UI_DASH_CONTINUE],old_item.id);dash_offset[0]=5;
  VtLibraryItem chosen;CHECK(ui_library_screen(&chosen)==UI_LIBRARY_PLAY&&!strcmp(chosen.id,new_item.id)&&frame==1);
  /* Same live Home: a new play must appear in Recently played even if resumable. */
  vt_library_started(&old_item);prepare_live(theme,UI_DASH_RECENT,1);
  CHECK(ui_library_screen(&chosen)==UI_LIBRARY_PLAY&&!strcmp(chosen.id,new_item.id)&&frame==3);
  /* Same Home: a new favourite replaces the old publication on the next frame. */
  home_favourites_fixture[0]=old_item;prepare_live(theme,UI_DASH_FAVOURITES,2);
  CHECK(ui_library_screen(&chosen)==UI_LIBRARY_PLAY&&!strcmp(chosen.id,new_item.id)&&frame==3);
  /* A source saved while Home is visible is loaded and becomes directly selectable. */
  prepare_live(theme,UI_DASH_CONNECTIONS,3);
  CHECK(ui_library_screen(&chosen)==UI_LIBRARY_CONNECT&&!strcmp(chosen.title,"Newest connection")&&frame==3);
  /* Completed playback is no longer retained as the active Continue choice. */
  vt_library_started(&old_item);prepare_live(theme,UI_DASH_CONTINUE,4);
  CHECK(ui_library_screen(&chosen)==UI_LIBRARY_PLAY&&strcmp(chosen.id,old_item.id)&&frame==1);
  DashboardData *data=calloc(1,sizeof(*data));CHECK(data);VtLibrarySearchStatus status={0};
  home_favourites_count=1;home_favourites_fixture[0]=old_item;dashboard_refresh(data,&status,0);
  strcpy(dash_anchor[UI_DASH_FAVOURITES],old_item.id);home_favourites_count=0;
  dashboard_refresh(data,&status,1);CHECK(!data->count[UI_DASH_FAVOURITES]);free(data);
  frame_observer=NULL;
 }
 FT_Done_Face(face);FT_Done_FreeType(ft);
 printf("Immediate Home refresh: %u checks; old re-entry anchors, same-session play/favourite/source changes within 1–3 frames, completed-video removal, no resurrected favourites, eight themes and both mini-player heights passed. Host fixtures, not Vita runtime.\n",checks);
}
