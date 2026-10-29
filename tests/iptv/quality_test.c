/* Production smoothness scoring and persistent preferences, no nominal FPS. */
#include "network/iptv_quality.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <jansson.h>
static unsigned checks;
#define CHECK(x) do{checks++;assert(x);}while(0)
static VtIptvQualityReport sample(int kind){
 VtIptvQualityMeter m={0};unsigned shown=0,dropped=0;uint64_t pts=0;
 for(unsigned i=0;i<=500;i++){
  uint64_t wall=i*20000ULL;
  int frozen=kind==1&&(i%100>=40&&i%100<70);
  if(!(i%2)&&!frozen){shown++;pts=kind==2?wall/3:wall;}
  if(kind==3&&i%10==0)dropped++;
  uint64_t audio=kind==2?wall/3:wall;
  vt_iptv_quality_observe(&m,1000000+wall,1,pts,audio,shown,dropped,kind!=2);
 }
 CHECK(m.report.valid);return m.report;
}
int main(void){
 VtIptvQualityMeter warmup={0};vt_iptv_quality_observe(&warmup,1,0,0,0,0,0,1);CHECK(!warmup.ready);
 VtIptvQualityReport smooth=sample(0),freeze=sample(1),slow=sample(2),drop=sample(3);
 CHECK(vt_iptv_quality_cost(&smooth)<1);CHECK(freeze.stall_us>1000000);
 CHECK(vt_iptv_quality_cost(&freeze)>vt_iptv_quality_cost(&smooth)+1000);
 CHECK(vt_iptv_quality_cost(&slow)>4000);CHECK(vt_iptv_quality_cost(&drop)>20);
 CHECK(vt_iptv_quality_better(&smooth,1920*1080,&freeze,480*280));
 CHECK(vt_iptv_quality_better(&smooth,1920*1080,&smooth,640*360));
 CHECK(!vt_iptv_quality_better(&slow,1920*1080,&smooth,640*360));
 VtIptvOptions *o=calloc(1,sizeof(*o));CHECK(o);o->video_count=2;o->audio_count=1;
 for(unsigned i=0;i<2;i++){
  VtIptvChoice *v=&o->video[i];snprintf(v->id,sizeof(v->id),"https://fixture.test/%u.m3u8",i);
  v->supported=1;v->width=i?1920:480;v->height=i?1080:280;v->bandwidth=i?9000000:400000;
 }
 strcpy(o->audio[0].id,"it");o->audio[0].supported=1;
 o->active=(VtIptvSelection){0};strcpy(o->active.video,o->video[1].id);strcpy(o->active.audio,"it");
 o->active_width=1920;o->active_height=1080;o->active_bandwidth=9000000;
 VtIptvPreference p;vt_iptv_preference_capture(o,&p);CHECK(strlen(p.video_key)==16&&p.height==1080);
 o->active_width=o->active_height=o->active_bandwidth=0;
 vt_iptv_preference_capture(o,&p);CHECK(p.height==1080&&p.width==1920&&p.bandwidth==9000000);
 o->active_width=1920;o->active_height=1080;o->active_bandwidth=9000000;
 VtIptvSelection selected={0};CHECK(vt_iptv_preference_apply(o,&p,&selected,1));CHECK(selected.video_manual&&!strcmp(selected.video,o->video[1].id));
 CHECK(!strcmp(selected.audio,"it"));CHECK(vt_iptv_preference_apply(o,&p,&selected,0)&&!selected.video_manual);
 char signature[17],reordered[17];vt_iptv_quality_signature(o,signature);
 VtIptvChoice swap=o->video[0];o->video[0]=o->video[1];o->video[1]=swap;
 vt_iptv_quality_signature(o,reordered);CHECK(!strcmp(signature,reordered));
 o->video[0].supported=0;CHECK(!vt_iptv_preference_apply(o,&p,&selected,1));vt_iptv_quality_signature(o,reordered);CHECK(strcmp(signature,reordered));o->video[0].supported=1;
 char directory[]="/private/tmp/vmd-quality.XXXXXX";CHECK(mkdtemp(directory));CHECK(!chdir(directory));
 CHECK(!mkdir("ux0:data",0700));CHECK(!mkdir("ux0:data/VitaMediaDeck",0700));
 const char *url="https://fixture.test/master.m3u8?token=private";
 VtIptvPreference restored;
 CHECK(!vt_iptv_quality_cached(url,signature,123,&restored));CHECK(!vt_iptv_quality_save(url,signature,123,&p,&smooth));
 CHECK(vt_iptv_quality_cached(url,signature,123,&restored)&&!memcmp(&p,&restored,sizeof(p)));
 CHECK(!vt_iptv_quality_cached(url,reordered,123,&restored));CHECK(!vt_iptv_quality_cached(url,signature,124,&restored));
 FILE *f=fopen("ux0:data/VitaMediaDeck/iptv-quality.json","rb");CHECK(f);char body[4096]={0};fread(body,1,sizeof(body)-1,f);fclose(f);CHECK(!strstr(body,"private")&&!strstr(body,"fixture.test"));
 json_t *root=json_loads(body,0,NULL);CHECK(root);CHECK(!json_object_set_new(json_array_get(root,0),"at",json_integer(1)));CHECK(!json_dump_file(root,"ux0:data/VitaMediaDeck/iptv-quality.json",0));json_decref(root);
 CHECK(!vt_iptv_quality_cached(url,signature,123,&restored));
 smooth.valid=0;CHECK(vt_iptv_quality_save(url,signature,123,&p,&smooth)<0);
 /* Per-channel preferences outlive the measurement cache and favourites. */
 strcpy(o->audio[0].language,"ita");strcpy(o->audio[0].name,"Italiano");
 vt_iptv_preference_capture(o,&p);
 CHECK(!vt_iptv_playback_save(url,&p));CHECK(vt_iptv_playback_load(url,&restored));
 CHECK(!memcmp(&p,&restored,sizeof(p)));
 CHECK(!vt_iptv_playback_load("https://fixture.test/other",&restored));
 strcpy(o->video[0].id,"https://fixture.test/rotated.m3u8?token=changed");
 strcpy(o->audio[0].id,"rotated-audio-id");
 selected=(VtIptvSelection){0};CHECK(vt_iptv_playback_apply(o,&p,&selected)==3);
 CHECK(selected.video_manual&&!strcmp(selected.video,o->video[0].id)&&!strcmp(selected.audio,o->audio[0].id));
 o->video[0].supported=0;CHECK(vt_iptv_playback_apply(o,&p,&selected)==2);
 CHECK(!selected.video[0]&&!selected.video_manual&&!strcmp(selected.audio,o->audio[0].id));
 o->video[0].supported=1;o->audio[0].supported=0;
 CHECK(vt_iptv_playback_apply(o,&p,&selected)==1&&!selected.audio[0]);
 o->video[0].supported=0;CHECK(!vt_iptv_playback_apply(o,&p,&selected));
 CHECK(!selected.video[0]&&!selected.audio[0]&&!selected.video_manual);
 for(unsigned i=0;i<160;i++){
  char channel[96];snprintf(channel,sizeof(channel),"https://fixture.test/channel-%u",i);
  CHECK(!vt_iptv_playback_save(channel,&p));
 }
 CHECK(vt_iptv_playback_load(url,&restored)&&!memcmp(&p,&restored,sizeof(p)));
 /* Stable identity migrates old data without expiring or coupling to favourites. */
 char identity[17],other_identity[17];vt_iptv_playback_identity("https://fixture.test/list.m3u","TV.one",identity);
 vt_iptv_playback_identity("https://fixture.test/list.m3u","TV.two",other_identity);CHECK(strcmp(identity,other_identity));
 CHECK(vt_iptv_playback_load_channel(url,identity,&restored)==2);
 CHECK(!vt_iptv_playback_save_channel(url,identity,&restored));
 CHECK(vt_iptv_playback_load_channel(url,identity,&restored)==1);
 const char *rotated="https://fixture.test/master.m3u8?token=new";
 CHECK(vt_iptv_playback_load_channel(rotated,identity,&restored)==3&&!memcmp(&p,&restored,sizeof(p)));
 CHECK(!vt_iptv_playback_load_channel(rotated,other_identity,&restored));
 CHECK(!vt_iptv_playback_save_channel(rotated,identity,&p));
 CHECK(vt_iptv_playback_load_channel(url,identity,&restored)==3&&!memcmp(&p,&restored,sizeof(p)));
 CHECK(!vt_iptv_playback_save_channel(url,identity,&p));
 CHECK(!mkdir("ux0:data/VitaMediaDeck/iptv-playback.json.tmp",0700));
 VtIptvPreference altered=p;altered.height=360;
 CHECK(vt_iptv_playback_save(url,&altered)<0);
 CHECK(vt_iptv_playback_load(url,&restored)&&!memcmp(&p,&restored,sizeof(p)));
 CHECK(access("ux0:data/VitaMediaDeck/iptv-playback.json.tmp",F_OK)!=0);
 CHECK(!rename("ux0:data/VitaMediaDeck/iptv-playback.json","ux0:data/VitaMediaDeck/iptv-playback.json.bak"));
 f=fopen("ux0:data/VitaMediaDeck/iptv-playback.json","wb");CHECK(f);fputs("broken",f);fclose(f);
 CHECK(vt_iptv_playback_load(url,&restored)&&!memcmp(&p,&restored,sizeof(p)));
 CHECK(!vt_iptv_playback_save(url,&p));
 CHECK(vt_iptv_playback_load(url,&restored)&&!memcmp(&p,&restored,sizeof(p)));
 f=fopen("ux0:data/VitaMediaDeck/iptv-playback.json","rb");CHECK(f);
 root=json_loadf(f,0,NULL);fclose(f);CHECK(root&&json_array_size(root)==161);json_decref(root);
 CHECK(!vt_iptv_playback_save_channel(url,identity,&p));
 CHECK(!mkdir("ux0:data/VitaMediaDeck/iptv-playback.json.tmp",0700));
 CHECK(vt_iptv_playback_remove_channel(rotated,identity)<0);
 CHECK(vt_iptv_playback_load_channel(url,identity,&restored)>0);
 CHECK(!vt_iptv_playback_remove_channel(rotated,identity));
 CHECK(!vt_iptv_playback_load_channel(url,identity,&restored));
 CHECK(vt_iptv_playback_load("https://fixture.test/channel-0",&restored));
 CHECK(!vt_iptv_playback_remove_channel(url,identity));
 f=fopen("ux0:data/VitaMediaDeck/iptv-playback.json","wb");CHECK(f);fputs("broken",f);fclose(f);
 f=fopen("ux0:data/VitaMediaDeck/iptv-playback.json.bak","wb");CHECK(f);fputs("broken too",f);fclose(f);
 CHECK(vt_iptv_playback_save(url,&p)<0); /* Never overwrite unrecoverable user choices. */
 free(o);printf("Smoothness and preference checks: %u passed; freezes, common audio/video slowdown, dropped frames, real cadence, resolution tie-break, active variant, cache expiry/context and private URL omission.\n",checks);
}
