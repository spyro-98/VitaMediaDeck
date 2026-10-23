#define VMD_ONLINE_UI
#define main previous_iptv_main
#include "../iptv/ui_test.c"
#undef main
#define SCE_O_WRONLY 1
#define SCE_O_CREAT 2
#define SCE_O_TRUNC 4
int sceIoOpen(const char*,int,int);int sceIoWrite(int,const void*,size_t);int sceIoClose(int);
#include "../../src/ui/personal_media_screen.c"
static VtNetworkSource db[32];static VtNetworkCredential secret_db[32];static int db_count,remember,source_saves,key_saves,browses,viewer_calls,prepare_calls,queue_fetches,shuffle;static unsigned player_actions[16],player_index;
static VtBackgroundPlaybackSnapshot audio_snapshot;static unsigned last_prepare_stops;
void ui_home_tab_set(int t){(void)t;}
void ui_online_audio_set_home(int h){(void)h;}
void ui_brand_draw_header_placeholder(const char*q,const char*title){(void)q;ui_brand_draw_header(title);}
int vt_i18n_current_language(void){return language?VT_LANG_IT:VT_LANG_EN;}
int vt_preferences_remember_network_passwords(void){return remember;}
int vt_network_sources_load(VtNetworkSource*s,int cap){CHECK(cap>=db_count);memcpy(s,db,(size_t)db_count*sizeof(*s));return db_count;}
int vt_network_sources_save(const VtNetworkSource*s,int n){source_saves++;if(write_failure)return -1;memcpy(db,s,(size_t)n*sizeof(*s));db_count=n;return 0;}
int vt_network_credentials_load(VtNetworkCredential*c,int n){memcpy(c,secret_db,(size_t)n*sizeof(*c));return db_count;}
int vt_network_credentials_save(const VtNetworkCredential*c,int n){CHECK(remember);key_saves++;memcpy(secret_db,c,(size_t)n*sizeof(*c));return 0;}
int vt_network_credentials_clear(void){memset(secret_db,0,sizeof(secret_db));return 0;}
const char *vt_network_protocol_name(VtNetworkProtocol p){return p==VT_NETWORK_NAVIDROME?"Navidrome":"Immich";}
int vt_personal_base_url(const VtNetworkSource*s,char*out,size_t n){snprintf(out,n,"https://%s",s->host);return s->host[0]?0:-1;}
int ui_network_browse_personal(VtNetworkSource*s,VtNetworkCredential*c){CHECK(s->protocol==VT_NETWORK_NAVIDROME||s->protocol==VT_NETWORK_IMMICH);CHECK(c->password[0]);browses++;return 0;}
int ui_text_input_secure(const char*t,const char*i,char*out,size_t n){(void)t;(void)i;if(!rename_input)return 0;snprintf(out,n,"mock-secret");return 1;}
int sceIoOpen(const char*p,int f,int mode){CHECK(strstr(p,"preview")||strstr(p,"cover"));(void)f;(void)mode;return 9;}
int sceIoWrite(int fd,const void*p,size_t n){CHECK(fd==9&&p&&n>0);return (int)n;}
int sceIoClose(int fd){CHECK(fd==9);return 0;}int sceIoRemove(const char*p){CHECK(strstr(p,"preview")||strstr(p,"cover"));return 0;}
int vt_personal_preview(const VtNetworkSource*s,const VtNetworkCredential*c,const char*p,unsigned char**b,size_t*n,volatile int*cancel){CHECK(s&&c&&p&&cancel);*n=3;*b=malloc(*n);memcpy(*b,"abc",*n);return 0;}
int vt_personal_artwork(const VtNetworkSource*s,const VtNetworkCredential*c,const char*p,unsigned char**b,size_t*n,volatile int*cancel){return vt_personal_preview(s,c,p,b,n,cancel);}
void vt_video_thumbnail_suspend(void){}
int ui_image_viewer_show(const char*p,const char*t){CHECK(strstr(p,"preview")&&t);viewer_calls++;return 0;}
void vt_network_media_history_id(const VtNetworkSource*s,const char*p,char out[16]){(void)s;snprintf(out,16,"%s",p);}
int vt_background_playback_snapshot(VtBackgroundPlaybackSnapshot*out){*out=audio_snapshot;if(stops!=last_prepare_stops)out->visible=0;return out->visible;}
int vt_background_playback_prepare_remote_audio(const VtNetworkSource*s,const VtNetworkCredential*c,const char*p,const char*id,const char*title,uint64_t duration){CHECK(s->protocol==VT_NETWORK_NAVIDROME&&c->password[0]&&p&&title);prepare_calls++;last_prepare_stops=stops;audio_snapshot.visible=1;snprintf(audio_snapshot.video_id,sizeof(audio_snapshot.video_id),"%s",id);audio_snapshot.duration_ms=duration;return 0;}
int vt_background_playback_activate(uint64_t n){CHECK(n==0);return 0;}
void vt_background_playback_set_audio_details(const char*a,const char*b){CHECK(a&&b);}
int ui_music_player_run(const char*a,const char*b,const char*c,uint32_t d){CHECK(a&&b&&c);(void)d;CHECK(player_index<16);return player_actions[player_index++];}
int ui_music_player_shuffle_enabled(void){return shuffle;}
int ui_network_personal_resume_credential(const VtNetworkSource*s,VtNetworkCredential*c){(void)s;(void)c;return 0;}
int vt_personal_list(const VtNetworkSource*s,const VtNetworkCredential*c,const char*p,VtNetworkEntry*e,int n,char*d,size_t z){(void)d;(void)z;CHECK(s->protocol==VT_NETWORK_NAVIDROME&&c->password[0]&&n==VT_NETWORK_MAX_ENTRIES&&!strcmp(p,"playlist:many@100"));queue_fetches++;memset(e,0,2*sizeof(*e));for(int i=0;i<2;i++){e[i].is_audio=1;snprintf(e[i].path,sizeof(e[i].path),"song:next%d",i);snprintf(e[i].name,sizeof(e[i].name),"Next track %d",i);}return 2;}
int sceKernelGetRandomNumber(void*p,size_t n){memset(p,0,n);return 0;}
static void reset_inputs(unsigned action){frame=0;script_count=4;memset(input_script,0,sizeof(input_script));input_script[1]=action;input_script[3]=SCE_CTRL_CIRCLE;capture_frame=0;mini_top=544;axis_count=0;tap_frame=-1;}
int main(int argc,char**argv){CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));FT_Face inter=face,mono;CHECK(!FT_New_Face(ft,"assets/fonts/IBMPlexMono-Regular.ttf",0,&mono));allow_audio_fullscreen=1;
 db_count=3;db[0].protocol=VT_NETWORK_JELLYFIN;db[1].protocol=VT_NETWORK_NAVIDROME;db[2].protocol=VT_NETWORK_IMMICH;
 for(int k=0;k<db_count;k++){snprintf(db[k].name,sizeof(db[k].name),"Studio %d",k);snprintf(db[k].host,sizeof(db[k].host),"https://media.example/instance/%d",k);snprintf(secret_db[k].password,sizeof(secret_db[k].password),"mock-secret");}
 remember=1;
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++)for(int provider=VT_NETWORK_NAVIDROME;provider<=VT_NETWORK_IMMICH;provider++)for(int mini=0;mini<2;mini++){
  ui_theme_apply(theme);face=theme==VT_UI_THEME_DECK_90?mono:inter;reset_inputs(0);mini_top=mini?472:544;CHECK(ui_personal_media_screen(provider)==UI_SECTION_LOCAL_MEDIA);memcpy(pixels,first_pixels,sizeof(pixels));char name[128];snprintf(name,sizeof(name),"personal-%d-%s-%s%s",theme,provider==VT_NETWORK_NAVIDROME?"navidrome":"immich",language?"it":"en",mini?"-mini":"");png(name);
  reset_inputs(SCE_CTRL_CROSS);CHECK(ui_personal_media_screen(provider)==UI_SECTION_LOCAL_MEDIA);
 }
 CHECK(browses==56);int saves=source_saves;
 reset_inputs(SCE_CTRL_TRIANGLE);rename_input=NULL;CHECK(ui_personal_media_screen(VT_NETWORK_IMMICH)==UI_SECTION_LOCAL_MEDIA&&source_saves==saves);
 reset_inputs(SCE_CTRL_SQUARE);rename_input="test.example";CHECK(ui_personal_media_screen(VT_NETWORK_IMMICH)==UI_SECTION_LOCAL_MEDIA&&db_count==4&&db[3].protocol==VT_NETWORK_IMMICH);CHECK(key_saves>0);
 int original_count=db_count;write_failure=1;reset_inputs(SCE_CTRL_SQUARE);CHECK(ui_personal_media_screen(VT_NETWORK_IMMICH)==UI_SECTION_LOCAL_MEDIA&&db_count==original_count);write_failure=0;
 VtNetworkEntry items[2]={{.name="Song 1",.path="song:one",.is_audio=1},{.name="Song 2",.path="song:two",.is_audio=1}};player_actions[0]=UI_MUSIC_PLAYER_NEXT;player_actions[1]=UI_MUSIC_PLAYER_MINIMIZE;player_index=0;memset(&audio_snapshot,0,sizeof(audio_snapshot));CHECK(ui_personal_media_open(&db[1],&secret_db[1],&items[0],items,2,0)==0&&player_index==2&&prepare_calls==2);
 /* Repeat restarts the same track; shuffle chooses a different queued track. */
 int prepared=prepare_calls;player_index=0;player_actions[0]=UI_MUSIC_PLAYER_REPEAT;player_actions[1]=UI_MUSIC_PLAYER_MINIMIZE;CHECK(ui_personal_media_open(&db[1],&secret_db[1],&items[0],items,2,0)==0&&prepare_calls==prepared+2);
 shuffle=1;player_index=0;player_actions[0]=UI_MUSIC_PLAYER_NEXT;player_actions[1]=UI_MUSIC_PLAYER_MINIMIZE;CHECK(ui_personal_media_open(&db[1],&secret_db[1],&items[0],items,2,0)==0&&g_music_queue.selected==1);shuffle=0;
 VtNetworkEntry paged[3]={items[0],items[1],{.name="Next page",.path="playlist:many@100",.is_directory=2}};player_index=0;player_actions[0]=UI_MUSIC_PLAYER_NEXT;player_actions[1]=UI_MUSIC_PLAYER_NEXT;player_actions[2]=UI_MUSIC_PLAYER_MINIMIZE;CHECK(ui_personal_media_open(&db[1],&secret_db[1],&paged[0],paged,3,0)==0&&queue_fetches==1&&g_music_queue.count==2&&!strcmp(g_music_queue.entries[0].path,"song:next0"));
 player_actions[player_index]=UI_MUSIC_PLAYER_SECTION_BASE+UI_SECTION_IMMICH;CHECK(music_fullscreen(0,NULL)==VT_HW_PLAYER_ACTION_SECTION_BASE+UI_SECTION_IMMICH);CHECK(ui_sections_take_requested()==UI_SECTION_IMMICH);
 VtNetworkEntry photo={.name="Photo.HEIC",.path="photo:one",.is_image=1};CHECK(ui_personal_media_open(&db[2],&secret_db[2],&photo,&photo,1,0)==0&&viewer_calls==1);
 /* Restored-player navigation is forwarded once to the underlying screen. */
 ui_sections_request(UI_SECTION_IMMICH);unsigned pressed=0;UiSectionsSidebar sidebar;ui_sections_sidebar_init(&sidebar,UI_SECTION_NAVIDROME);CHECK(ui_sections_sidebar_handle_buttons(&sidebar,&pressed,0,128)==UI_SECTION_IMMICH);CHECK(ui_sections_take_requested()==UI_SECTION_NONE);
 /* All eleven rows fit with live player + download actions. */
 live_slots=1;reset_inputs(SCE_CTRL_LTRIGGER);script_count=6;input_script[3]=SCE_CTRL_CIRCLE;input_script[4]=0;input_script[5]=SCE_CTRL_CIRCLE;capture_frame=1;CHECK(ui_personal_media_screen(VT_NETWORK_IMMICH)==UI_SECTION_LOCAL_MEDIA);memcpy(pixels,first_pixels,sizeof(pixels));png("personal-sidebar-all-sections");live_slots=0;
 ui_personal_media_shutdown();FT_Done_Face(mono);FT_Done_Face(inter);FT_Done_FreeType(ft);printf("Personal media native UI: %u checks, 7 themes, 2 languages, connection/edit/cancel/save failure and player handoff passed\n",checks);return 0;}
