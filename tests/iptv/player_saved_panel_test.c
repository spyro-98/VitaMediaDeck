/* Actual video drawer rendering and independent saved-channel callback. */
#define main original_ui_main
#include "ui_test.c"
#undef main
#define SCE_GXM_TEXTURE_FORMAT_R5G6B5 0
#define SCE_GXM_TEXTURE_FILTER_LINEAR 0
vita2d_texture *vita2d_create_empty_texture_format(unsigned,unsigned,int);
unsigned vita2d_texture_get_stride(const vita2d_texture *);
void *vita2d_texture_get_datap(const vita2d_texture *);
void vita2d_texture_set_filters(vita2d_texture *,int,int);
void vita2d_free_texture(vita2d_texture *);
void vita2d_draw_fill_circle(float x,float y,float radius,unsigned color){vita2d_draw_rectangle(x-radius,y-radius,radius*2,radius*2,color);}
#define vt_hw_player_screen_run actual_hw_run
#define vt_hw_player_screen_run_live actual_hw_run_live
#include "../../src/media/hw_player_screen.c"
#undef vt_hw_player_screen_run
#undef vt_hw_player_screen_run_live
#include "../../src/ui/iptv_player_panel.c"
int vt_preferences_fill_screen(void){return 0;}
int vt_preferences_loop_enabled(void){return 0;}
int vt_decoder_audio_track_count(const VtDecoderPlayer *p){(void)p;return 0;}
int vt_decoder_subtitle_track_count(const VtDecoderPlayer *p){(void)p;return 0;}
int vt_decoder_active_audio_track(const VtDecoderPlayer *p){(void)p;return 0;}
int vt_decoder_active_subtitle_track(const VtDecoderPlayer *p){(void)p;return 0;}
int vt_decoder_audio_track_info(const VtDecoderPlayer *p,int i,VtDecoderTrackInfo *o){(void)p;(void)i;(void)o;return -1;}
int vt_decoder_subtitle_track_info(const VtDecoderPlayer *p,int i,VtDecoderTrackInfo *o){(void)p;(void)i;(void)o;return -1;}
VtDecoderSubtitleState vt_decoder_subtitle_state(const VtDecoderPlayer *p){(void)p;return VT_DECODER_SUBTITLE_DISABLED;}
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 VtIptvChannel channel={.url="https://fixture.test/channel.m3u8",.title="Example TV",.mode=VT_IPTV_HLS};
 VtIptvPreference confirmed={.width=1920,.height=1080};int have_confirmed=1;
 PlayerFavoriteContext ctx={.channel=&channel,.confirmed=&confirmed,.have_confirmed=&have_confirmed};
 VtHwPlayerScreenSource source={.saved=player_saved_channel,.saved_ctx=&ctx};
 VtDecoderPlayerStatus status={.opened=1,.hardware_accelerated=1,.width=1920,.height=1080,.fps=30,.video_bitrate_bps=9000000};
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++)for(int live=0;live<2;live++)for(int decoder=0;decoder<2;decoder++){
  ui_theme_apply(theme);saved_view=0;saved_link_count=0;channel.starred=0;memset(&stored_channel,0,sizeof(stored_channel));
  for(int saved=0;saved<2;saved++){
   CHECK(source.saved(source.saved_ctx,0)==saved);UiIptvPlayerPanel panel={0};int row=player_saved_row(&source,live,!live,decoder);
   unsigned right_groups=player_right_groups(row);CHECK(row>=4&&row<=6&&RIGHT_PANEL_Y(row)+RIGHT_PANEL_ROW_HEIGHT<=482);
   host_record_text=1;host_text_count=0;host_text_log[0]=0;
   vita2d_start_drawing();vita2d_clear_screen();ui_chrome_background(VT_THEME_BG,VT_THEME_COLD);
   draw_player_right_sidebar(1,(float)row,row,NULL,&status,!live,decoder,0,0,live,&source,&panel);vita2d_end_drawing();host_record_text=0;
   CHECK(strstr(host_text_log,(saved?TR(IPTV_DELETE):TR(IPTV_SAVE_CHANNEL))));
   for(int i=0;i<host_text_count;i++)CHECK(host_text_rects[i].y<=544);
   if(live&&decoder){char name[80];snprintf(name,sizeof(name),"player-save-%d-%u-%d",theme,language,saved);png(name);}
   player_toggle_saved(&source);
   CHECK(source.saved(source.saved_ctx,0)==!saved);
   if(!saved)CHECK(!stored_channel.starred&&stored_channel.preferred.height==1080);
  }
 }
 write_failure=1;CHECK(source.saved(source.saved_ctx,1)<0&&!source.saved(source.saved_ctx,0));write_failure=0;
 FT_Done_Face(face);FT_Done_FreeType(ft);
 printf("Player saved-channel R1: %u checks; live/file drawers, save/remove labels, independent stars, confirmed quality, write failure, 7-row/footer bounds and eight themes IT/EN passed.\n",checks);
}
