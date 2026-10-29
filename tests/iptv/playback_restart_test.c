/* Real play_link + real preference files, process death before player exit. */
#define VMD_REAL_PLAYBACK_STORAGE
#define main original_ui_main
#include "ui_test.c"
#undef main
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/stat.h>
extern char **environ;
static int phase,runs;
static int player(const VtHwPlayerScreenSource *source){
 runs++;VtIptvOptions *o=(VtIptvOptions*)source->iptv_options;VtIptvSelection *s=source->iptv_selection;
 int video=!strcmp(s->video,o->video[1].id),audio=!strcmp(s->audio,o->audio[1].id);
 if(phase==2||phase==4){CHECK(runs==1&&video&&audio&&s->video_manual);}
 if(phase==3){CHECK(runs==1&&!video&&audio&&!s->video_manual);}
 o->active=*s;if(!o->active.video[0])strcpy(o->active.video,o->video[0].id);
 if(!o->active.audio[0])strcpy(o->active.audio,o->audio[0].id);
 o->active_width=o->video[video].width;o->active_height=o->video[video].height;
 CHECK(source->iptv_started&&!source->iptv_started(source->iptv_started_ctx,o->active_width,o->active_height));
 if(phase==1&&runs==1){strcpy(s->video,o->video[1].id);strcpy(s->audio,o->audio[1].id);s->video_manual=1;return VT_HW_PLAYER_ACTION_STREAM_CHANGE;}
 VtIptvPreference stored={0};char identity[17];vt_iptv_playback_identity(current_list.url,catalog.items[0].channel_id,identity);
 CHECK(vt_iptv_playback_load_channel(link,identity,&stored)>0&&stored.height==1080&&!strcmp(stored.audio_language,"ita"));
 CHECK(!save_attempts); /* Last quality persists without creating a favourite. */
 if(phase==1){raise(SIGKILL);abort();}return 0;
}
int main(int argc,char **argv){
 if(argc==2){
  phase=atoi(argv[1]);host_variant_epoch=phase;host_unavailable_video=phase==3;host_quality_variants=host_playback_audio=1;
  new_link();mode=VT_IPTV_HLS;snprintf(link,sizeof(link),"https://fixture.test/channel.m3u8?token=version-%d",phase);
  strcpy(current_list.url,"https://fixture.test/channels.m3u");catalog.count=1;catalog.items[0]=(VtIptvChannel){.mode=VT_IPTV_HLS};
  strcpy(catalog.items[0].url,link);strcpy(catalog.items[0].channel_id,"FixtureTV.it");strcpy(catalog.items[0].title,"Fixture TV");
  live_player_observer=player;const char *message="";CHECK(play_link(&message)==UI_SECTION_NONE&&runs==1);return 0;
 }
 char directory[]="/private/tmp/vmd-playback-restart.XXXXXX";CHECK(mkdtemp(directory)&&!chdir(directory));
 CHECK(!mkdir("ux0:data",0700)&&!mkdir("ux0:data/VitaMediaDeck",0700));
 char binary[4096];CHECK(realpath(argv[0],binary));
 for(int p=1;p<=4;p++){
  char value[2]={(char)('0'+p),0};char *args[]={binary,value,NULL};pid_t child;int status;
  CHECK(!posix_spawn(&child,binary,NULL,NULL,args,environ)&&waitpid(child,&status,0)==child);
  if(p==1)CHECK(WIFSIGNALED(status)&&WTERMSIG(status)==SIGKILL);
  else CHECK(WIFEXITED(status)&&WEXITSTATUS(status)==0);
 }
 puts("Playback restart: confirmed 1080p/Italian choice flushed before SIGKILL; fresh process restores it with rotated channel/variant/audio IDs, temporary missing-quality fallback preserves the choice; no favourite and no graceful exit.");
}
