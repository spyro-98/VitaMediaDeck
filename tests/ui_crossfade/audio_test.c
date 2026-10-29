/* Production audio screen and drawer, host raster/input/session substitutes. */
#define SCE_CTRL_HEADPHONE 8192
#define main iptv_ui_main
#include "../iptv/ui_test.c"
#undef main
#include "../../src/ui/music_player.c"
static VtBackgroundPlaybackSnapshot audio;
static int layout_preference, pause_calls,seek_calls,cover_loads,cover_frees,has_art=1;
int vt_preferences_audio_layout(void){return layout_preference;}
int vt_preferences_set_audio_layout(int n){layout_preference=n;return 0;}
static int music_preference;
int vt_preferences_music_theme(void){return music_preference;}
int vt_preferences_set_music_theme(int n){music_preference=n;return 0;}
int vt_preferences_player_swap_shoulders(void){return 0;}
int vt_preferences_music_keep_display_awake(void){return 0;}
int vt_background_playback_snapshot(VtBackgroundPlaybackSnapshot *s){*s=audio;return 1;}
void vt_background_playback_toggle_pause(void){pause_calls++;}
void vt_background_playback_checkpoint(void){}
void vt_background_playback_seek_relative(int64_t n){(void)n;seek_calls++;}
void vt_background_playback_seek_to(uint64_t n){(void)n;seek_calls++;}
#include "../../src/media/audio_volume.c"
void vt_display_keep_awake_tick(void){}
vita2d_texture *vt_image_load_texture(const char *p,unsigned max,VtImageInfo *info,char *error,size_t capacity){(void)p;(void)max;(void)info;(void)error;(void)capacity;cover_loads++;return has_art?vt_artwork_thumbnail_get("fixture-square.png"):NULL;}
void vita2d_free_texture(vita2d_texture *t){(void)t;cover_frees++;}
unsigned vita2d_texture_get_stride(const vita2d_texture *t){(void)t;return icon.w*4;}
SceGxmTextureFormat vita2d_texture_get_format(const vita2d_texture *t){(void)t;return SCE_GXM_TEXTURE_BASE_FORMAT_U8U8U8U8;}
void *vita2d_texture_get_datap(const vita2d_texture *t){(void)t;return icon.rgba;}
void vita2d_draw_fill_circle(float x,float y,float r,unsigned c){for(int dy=-(int)r;dy<=r;dy++)for(int dx=-(int)r;dx<=r;dx++)if(dx*dx+dy*dy<=r*r)pixel(x+dx,y+dy,c,255);}
#ifndef VMD_AUDIO_TEST_LIBRARY
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 for(language=0;language<2;language++)for(int kind=0;kind<3;kind++)for(int mode=0;mode<3;mode++)for(has_art=0;has_art<2;has_art++){
  audio=(VtBackgroundPlaybackSnapshot){.state=VT_BACKGROUND_PAUSED,.duration_ms=342000,.position_ms=138000,.audio_sample_rate=48000,.audio_bitrate_kbps=256,.audio_channels=2};
  audio.is_live=kind==2;audio.is_podcast=kind==1;
  if(audio.is_live)audio.duration_ms=0;
  strcpy(audio.title,"When the city sleeps — a long title to check the artwork and metadata boundary");
  strcpy(audio.channel,has_art?"Open Frequency / Studio North":"");
  layout_preference=mode;script_count=2;input_script[0]=0;input_script[1]=SCE_CTRL_CIRCLE;frame=0;capture_frame=0;
  CHECK(ui_music_player_run("fixture","Night Transit","AAC",256)==UI_MUSIC_PLAYER_STOP);
  CHECK(!pause_calls&&!seek_calls);memcpy(pixels,first_pixels,sizeof(pixels));char name[96];snprintf(name,sizeof(name),"audio-%d-layout-%d-%s-%s",kind,mode,language?"it":"en",has_art?"cover":"no-cover");png(name);
 }
 /* L1 -> Player -> next layout -> apply -> close: no playback command. */
 has_art=1;language=0;layout_preference=0;frame=0;capture_frame=10;script_count=12;
 unsigned sequence[]={0,SCE_CTRL_LTRIGGER,0,SCE_CTRL_RIGHT,0,SCE_CTRL_DOWN,0,SCE_CTRL_CROSS,0,SCE_CTRL_LTRIGGER,0,SCE_CTRL_CIRCLE};
 memcpy(input_script,sequence,sizeof(sequence));int loads_before=cover_loads;
 CHECK(ui_music_player_run("fixture","Night Transit","AAC",256)==UI_MUSIC_PLAYER_STOP);
 CHECK(layout_preference==1&&!pause_calls&&!seek_calls&&cover_loads==loads_before+1);
 memcpy(pixels,first_pixels,sizeof(pixels));png("audio-layout-change-preserves-pause");
 UiSectionsSidebar menu;ui_sections_sidebar_init(&menu,UI_SECTION_LOCAL_MEDIA);menu.audio_layout_available=menu.audio_layout_page=menu.open=1;menu.animation=1;menu.audio_layout_cursor=menu.audio_layout_selected=2;menu.focus_cursor=2;
 ui_sections_sidebar_draw(&menu);png("audio-layout-left-panel");
 CHECK(ui_sections_sidebar_handle_touch(&menu,UI_TOUCH_EVENT_TAP,80,214)==UI_SECTION_AUDIO_LAYOUT_BASE);
 CHECK(!pause_calls&&!seek_calls);CHECK(cover_loads==37&&cover_frees==19);
 vita2d_clear_screen();draw_music_right_sidebar(1,2,2,1,VT_MUSIC_THEME_ARTWORK,"AAC-LC",320,48000,2,16);png("audio-right-panel");
 /* Exercise actual audio rendering with every interface palette. */
 for(int theme=1;theme<VT_UI_THEME_COUNT;theme++){
  ui_theme_apply(theme);const unsigned *before=ui_theme_colors;
  frame=0;capture_frame=0;script_count=2;input_script[0]=0;input_script[1]=SCE_CTRL_CIRCLE;
  CHECK(ui_music_player_run("fixture","Night Transit","AAC",256)==UI_MUSIC_PLAYER_STOP);
  CHECK(ui_theme_colors==before&&ui_theme_id()==theme);
  memcpy(pixels,first_pixels,sizeof(pixels));char name[80];snprintf(name,sizeof(name),"theme-audio-%d",theme);png(name);
 }
 /* Triangle is mute in music, podcasts and radio; held input toggles once. */
 for(int kind=0;kind<3;kind++){
  audio.is_live=kind==2;audio.is_podcast=kind==1;
  vt_audio_volume_set_percent(147);CHECK(!vt_audio_volume_muted());
  unsigned mute_script[]={0,SCE_CTRL_TRIANGLE,SCE_CTRL_TRIANGLE,0,SCE_CTRL_CIRCLE};
  memcpy(input_script,mute_script,sizeof(mute_script));script_count=5;frame=0;capture_frame=2;
  int old_repeat=g_repeat_one;
  CHECK(ui_music_player_run("fixture","Mute check","AAC",256)==UI_MUSIC_PLAYER_STOP);
  CHECK(vt_audio_volume_muted()&&vt_audio_volume_percent()==0&&vt_audio_volume_level_percent()==147&&g_repeat_one==old_repeat);
  frame=0;CHECK(ui_music_player_run("fixture","Mute check","AAC",256)==UI_MUSIC_PLAYER_STOP);
  CHECK(!vt_audio_volume_muted()&&vt_audio_volume_percent()==147&&g_repeat_one==old_repeat);
 }
 ui_theme_apply(VT_UI_THEME_LEGACY);
 FT_Done_Face(face);FT_Done_FreeType(ft);printf("Crossfade native audio UI: %u checks passed\n",checks);
}

#endif
