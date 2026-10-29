/* Production results/filter/menu flow; catalogue identities and artwork indices
 * must remain unchanged when presentation order changes. */
#define main original_ui_main
#include "ui_test.c"
#undef main
static void prepare_sort(int split){
 new_link();host_name_sort=host_sort_write_error=0;host_iptv_split=split;split_view=split;
 saved_view=saved_channels=starred_view=picker_focus=0;hide_unavailable=0;catalog_focus=catalog_top=0;
 memset(&stored_channel,0,sizeof(stored_channel));memset(&current_list,0,sizeof(current_list));stored_result=0;
 strcpy(current_list.url,"https://sort.test/list.m3u");strcpy(current_list.title,"Sort fixture");current_list.mode=VT_IPTV_LIST;
 const char *names[]={"zeta","Rai 10","rai 2","È TV","Rai 02","Alfa"};
 memset(&catalog,0,sizeof(catalog));catalog.count=6;
 for(unsigned i=0;i<catalog.count;i++){
  VtIptvChannel *v=&catalog.items[i];snprintf(v->title,sizeof(v->title),"%s",names[i]);snprintf(v->url,sizeof(v->url),"https://sort.test/%u.m3u8",i);
  snprintf(v->artwork_path,sizeof(v->artwork_path),"ux0:data/cache/%u.png",i);v->starred=i==0||i==2||i==5;v->mode=VT_IPTV_HLS;
 }
 search[0]=0;filter_dirty=1;filter_catalog();
}
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 const unsigned ordered[]={5,3,2,4,1,0};const char *message="";
 VtIptvCatalog *original=malloc(sizeof(*original));CHECK(original);
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++)for(int split=0;split<2;split++){
  ui_theme_apply(theme);prepare_sort(split);*original=catalog;
  CHECK(!change_channel_sort(&message)&&host_name_sort&&result_count==6);
  for(unsigned i=0;i<6;i++)CHECK(results[i]==ordered[i]);
  CHECK(results[catalog_focus]==0&&!memcmp(&catalog,original,sizeof(catalog)));
  CHECK(!change_channel_sort(&message)&&!host_name_sort&&results[catalog_focus]==0);
  for(unsigned i=0;i<6;i++)CHECK(results[i]==i);
  strcpy(search,"rai");filter_dirty=1;filter_catalog();CHECK(!change_channel_sort(&message)&&result_count==3);
  CHECK(results[0]==2&&results[1]==4&&results[2]==1);
  unsigned selected=results[catalog_focus];host_sort_write_error=1;
  CHECK(change_channel_sort(&message)<0&&host_name_sort&&results[catalog_focus]==selected);host_sort_write_error=0;
  search[0]=0;saved_view=saved_channels=starred_view=1;filter_dirty=1;filter_catalog();
  CHECK(result_count==3&&results[0]==5&&results[1]==2&&results[2]==0);
  /* Playlist collections retain their source order and have no channel-order control. */
  saved_channels=starred_view=0;for(unsigned i=0;i<6;i++)catalog.items[i].mode=VT_IPTV_LIST;
  split_view=picker_focus=0;filter_dirty=1;filter_catalog();for(unsigned i=0;i<6;i++)CHECK(results[i]==i);
  UiContextAction items[16];int n=options_for_page(items);CHECK(n<=16);for(int i=0;i<n;i++)CHECK(items[i].action!=26);
 }
 /* Reach the real R1 action with D-pad in each presentation. */
 ui_theme_apply(VT_UI_THEME_LEGACY_VITA);
 for(int split=0;split<2;split++){
  prepare_sort(split);UiContextAction items[16];int n=options_for_page(items),index=-1;
  CHECK(n<=16);for(int i=0;i<n;i++)if(items[i].action==26){index=i;CHECK(!strcmp(items[i].detail,TR(IPTV_ORDER_SOURCE)));}
  CHECK(index>=0);frame=0;script_count=0;axis_count=0;frame_observer=NULL;tap_frame=-1;mini_top=544;online_case=0;
  memset(input_script,0,sizeof(input_script));unsigned at=1;input_script[at++]=SCE_CTRL_RTRIGGER;at++;
  for(int i=0;i<index;i++){input_script[at++]=SCE_CTRL_DOWN;at++;}
  input_script[at++]=SCE_CTRL_CROSS;at+=18;
  input_script[at++]=SCE_CTRL_CIRCLE;at++;input_script[at++]=SCE_CTRL_CIRCLE;at++;input_script[at++]=SCE_CTRL_CIRCLE;script_count=at;
  CHECK(ui_iptv_screen()==UI_SECTION_LOCAL_MEDIA&&host_name_sort);
 }
 free(original);FT_Done_Face(face);FT_Done_FreeType(ft);
 printf("Channel name ordering: %u checks; natural A-Z/case/accents, equal-name stability, filtered/favourite views, selected identity/artwork retention, source-order reset, write failure, eight themes and real R1 D-pad routing in both presentations passed.\n",checks);
}
