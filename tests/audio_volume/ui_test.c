/* Production music-player loop: shared ceiling and zero apply in every theme. */
#define VMD_AUDIO_TEST_LIBRARY
#include "../ui_crossfade/audio_test.c"
int main(int argc,char **argv){
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 audio=(VtBackgroundPlaybackSnapshot){.state=VT_BACKGROUND_PLAYING,.duration_ms=240000,.position_ms=10000};
 strcpy(audio.title,"Volume test");layout_preference=0;has_art=1;
 for(int theme=0;theme<VT_UI_THEME_COUNT;theme++)for(language=0;language<2;language++)for(int up=0;up<2;up++){
  ui_theme_apply(theme);frame=0;capture_frame=9;script_count=12;axis_count=12;
  memset(input_script,0,sizeof(input_script));input_script[11]=SCE_CTRL_CIRCLE;
  for(unsigned i=0;i<12;i++)input_axes[i]=(SceCtrlData){.lx=128,.ly=128,.rx=128,.ry=up?0:255};
  vt_audio_volume_set_percent(up?149:1);
  CHECK(ui_music_player_run("fixture","Audio volume","AAC",128)==UI_MUSIC_PLAYER_STOP);
  CHECK(vt_audio_volume_level_percent()==(up?150:0));
  CHECK(!vt_audio_volume_muted());
  if(up&&!language){memcpy(pixels,first_pixels,sizeof(pixels));char name[80];snprintf(name,sizeof(name),"volume-150-theme-%d",theme);png(name);}
 }
 FT_Done_Face(face);FT_Done_FreeType(ft);
 printf("Audio volume UI: %u checks; real stick input clamps at 0/150, boost marker, all eight themes and IT/EN passed. Host rendering only.\n",checks);
}
