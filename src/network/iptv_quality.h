#ifndef VMD_IPTV_QUALITY_H
#define VMD_IPTV_QUALITY_H
#include "network/iptv_options.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#define VT_IPTV_QUALITY_WARMUP_US 2000000ULL
#define VT_IPTV_QUALITY_WINDOW_US 8000000ULL
typedef struct {
 uint64_t elapsed_us,media_us,audio_us,stall_us,lag_integral_us,max_gap_us;
 unsigned cadence_breaks,dropped,shown;
 int valid,hardware;
} VtIptvQualityReport;
typedef struct {VtIptvQualityReport report;int candidate,total,skipped;} VtIptvQualityTrial;
typedef struct {
 uint64_t start,last,last_frame,first_pts,last_pts,first_audio,interval_us;
 unsigned first_shown,first_dropped,last_shown,frame_events;
 VtIptvQualityReport report;
 int ready;
} VtIptvQualityMeter;
/* Inputs are actual presentation and audio clocks, never declared FPS. */
static inline void vt_iptv_quality_observe(VtIptvQualityMeter *m,uint64_t now,int have_picture,
 uint64_t pts,uint64_t audio,unsigned shown,unsigned dropped,int hardware){
 if(!m->ready){
  if(!have_picture)return;
  *m=(VtIptvQualityMeter){.start=now,.last=now,.last_frame=now,.first_pts=pts,.last_pts=pts,
   .first_audio=audio,.interval_us=40000,.first_shown=shown,.first_dropped=dropped,.last_shown=shown,.ready=1};return;
 }
 if(now<=m->last)return;
 uint64_t dt=now-m->last,gap=now-m->last_frame;
 uint64_t allowed=m->interval_us*2+20000;if(allowed<120000)allowed=120000;
 if(m->frame_events>=2){
  uint64_t before=m->last-m->last_frame;
  uint64_t excess=gap>allowed?gap-allowed:0,old=before>allowed?before-allowed:0;
  m->report.stall_us+=excess-old;
 }
 if(have_picture&&shown!=m->last_shown){
  if(pts>m->last_pts){
   uint64_t media=pts-m->last_pts,frames=shown-m->last_shown;
   uint64_t interval=media/(frames?frames:1);
   if(interval>=8000&&interval<=1000000)m->interval_us=m->frame_events?(m->interval_us*3+interval)/4:interval;
   if(m->frame_events>=2&&gap>media+40000)m->report.cadence_breaks++;
  }else m->report.cadence_breaks++;
  if(gap>m->report.max_gap_us)m->report.max_gap_us=gap;
  m->last_frame=now;m->last_pts=pts;m->last_shown=shown;m->frame_events++;
 }
 uint64_t lag=audio>m->last_pts+100000?audio-m->last_pts-100000:0;
 m->report.lag_integral_us+=(lag/1000)*dt;
 m->last=now;m->report.elapsed_us=now-m->start;
 m->report.media_us=pts>=m->first_pts?pts-m->first_pts:0;
 m->report.audio_us=audio>=m->first_audio?audio-m->first_audio:0;
 m->report.shown=shown>=m->first_shown?shown-m->first_shown:0;
 m->report.dropped=dropped>=m->first_dropped?dropped-m->first_dropped:0;
 m->report.hardware=hardware;
 m->report.valid=m->report.elapsed_us>=VT_IPTV_QUALITY_WINDOW_US&&m->frame_events>=3;
}
static inline double vt_iptv_quality_cost(const VtIptvQualityReport *r){
 if(!r||!r->valid||!r->elapsed_us)return 1e12;
 double elapsed=r->elapsed_us;
 double video=fmax(0,1-r->media_us/elapsed),audio=fmax(0,1-r->audio_us/elapsed);
 double dropped=(double)r->dropped/(r->shown+r->dropped+1);
 return r->stall_us/elapsed*8000+video*4000+audio*3000+dropped*500+
        r->cadence_breaks/(elapsed/1000000.)*60+r->lag_integral_us/elapsed*.4;
}
static inline int vt_iptv_quality_better(const VtIptvQualityReport *candidate,uint64_t pixels,
 const VtIptvQualityReport *best,uint64_t best_pixels){
 double a=vt_iptv_quality_cost(candidate),b=vt_iptv_quality_cost(best);
 if(fabs(a-b)>5)return a<b;
 return a<20&&b<20?pixels>best_pixels:a<b;
}
static inline void vt_iptv_preference_capture(const VtIptvOptions *o,VtIptvPreference *out){
 memset(out,0,sizeof(*out));if(!o)return;
 vt_iptv_choice_key(o->active.video,out->video_key);vt_iptv_choice_key(o->active.audio,out->audio_key);
 for(unsigned i=0;i<o->audio_count;i++)if(!strcmp(o->audio[i].id,o->active.audio)){
  snprintf(out->audio_language,sizeof(out->audio_language),"%s",o->audio[i].language);
  snprintf(out->audio_name,sizeof(out->audio_name),"%s",o->audio[i].name);break;
 }
 out->width=o->active_width;out->height=o->active_height;out->bandwidth=o->active_bandwidth;
 /* Some demuxers publish the variant before codec dimensions are available. */
 for(unsigned i=0;i<o->video_count;i++)if(!strcmp(o->video[i].id,o->active.video)){
  if(!out->width)out->width=o->video[i].width;
  if(!out->height)out->height=o->video[i].height;
  if(!out->bandwidth)out->bandwidth=o->video[i].bandwidth;
  break;
 }
}
/* Startup persistence reads immutable discovered choices and the UI-owned
 * selection, not the producer's live active fields. Decoder dimensions are
 * the confirmed output; source-default audio remains source-default. */
static inline void vt_iptv_preference_confirmed(const VtIptvOptions *o,const VtIptvSelection *s,
 uint32_t width,uint32_t height,VtIptvPreference *out){
 memset(out,0,sizeof(*out));out->width=width;out->height=height;
 const VtIptvChoice *video=NULL;
 for(unsigned i=0;o&&i<o->video_count;i++)if(o->video[i].supported){
  const VtIptvChoice *c=&o->video[i];
  if(s&&s->video[0]&&!strcmp(c->id,s->video)){video=c;break;}
  if((!s||!s->video[0])&&height&&c->height==height&&(!width||c->width==width)){
   if(!video||!strcmp(c->id,o->default_video))video=c;
  }
 }
 if(video){vt_iptv_choice_key(video->id,out->video_key);out->bandwidth=video->bandwidth;
  if(!out->width)out->width=video->width;
  if(!out->height)out->height=video->height;
 }
 for(unsigned i=0;o&&s&&s->audio[0]&&i<o->audio_count;i++)if(!strcmp(o->audio[i].id,s->audio)){
  vt_iptv_choice_key(o->audio[i].id,out->audio_key);
  snprintf(out->audio_language,sizeof(out->audio_language),"%s",o->audio[i].language);
  snprintf(out->audio_name,sizeof(out->audio_name),"%s",o->audio[i].name);break;
 }
}
static inline int vt_iptv_preference_apply(const VtIptvOptions *o,const VtIptvPreference *p,VtIptvSelection *out,int manual){
 if(!o||!p||!out||!p->video_key[0])return 0;
 for(unsigned i=0;i<o->video_count;i++){
  char key[17];vt_iptv_choice_key(o->video[i].id,key);
  if(!o->video[i].supported||strcmp(key,p->video_key))continue;
  snprintf(out->video,sizeof(out->video),"%s",o->video[i].id);out->video_manual=manual;
  out->audio[0]=0;
  for(unsigned a=0;a<o->audio_count;a++){
   vt_iptv_choice_key(o->audio[a].id,key);
   if(o->audio[a].supported&&!strcmp(key,p->audio_key)&&vt_iptv_audio_matches(o,out,a)){
    snprintf(out->audio,sizeof(out->audio),"%s",o->audio[a].id);break;
   }
  }
  return 1;
 }
 return 0;
}
/* Resolution and language restore independently. Signed rendition URLs may
 * rotate: match their stable metadata after the exact ID, then use the source
 * default for a genuinely unavailable field. Never probe playback here. */
static inline unsigned vt_iptv_playback_apply(const VtIptvOptions *o,const VtIptvPreference *p,VtIptvSelection *out){
 if(!o||!p||!out)return 0;
 out->video[0]=out->audio[0]=0;out->video_manual=0;
 unsigned found=0;int video=-1;
 for(unsigned i=0;i<o->video_count;i++)if(o->video[i].supported){char key[17];vt_iptv_choice_key(o->video[i].id,key);
  if(p->video_key[0]&&!strcmp(key,p->video_key)){video=i;break;}
 }
 if(video<0&&p->height)for(unsigned i=0;i<o->video_count;i++)if(o->video[i].supported&&o->video[i].height==p->height&&(!p->width||o->video[i].width==p->width)){
  if(video<0||llabs((long long)o->video[i].bandwidth-p->bandwidth)<llabs((long long)o->video[video].bandwidth-p->bandwidth))video=i;
 }
 if(video>=0){snprintf(out->video,sizeof(out->video),"%s",o->video[video].id);out->video_manual=1;found|=1;}
 int audio=-1;
 for(unsigned i=0;i<o->audio_count;i++)if(o->audio[i].supported&&vt_iptv_audio_matches(o,out,i)){char key[17];vt_iptv_choice_key(o->audio[i].id,key);
  if(p->audio_key[0]&&!strcmp(key,p->audio_key)){audio=i;break;}
 }
 if(audio<0&&p->audio_language[0])for(unsigned i=0;i<o->audio_count;i++)if(o->audio[i].supported&&vt_iptv_audio_matches(o,out,i)&&!strcmp(o->audio[i].language,p->audio_language)){
  if(audio<0)audio=i;
  if(p->audio_name[0]&&!strcmp(o->audio[i].name,p->audio_name)){audio=i;break;}
 }
 if(audio>=0){snprintf(out->audio,sizeof(out->audio),"%s",o->audio[audio].id);found|=2;}
 return found;
}
static inline void vt_iptv_playback_identity(const char *playlist,const char *channel_id,char out[17]){
 out[0]=0;if(!playlist||!*playlist||!channel_id||!*channel_id)return;
 char scope[17],channel[17],joined[64];vt_iptv_choice_key(playlist,scope);vt_iptv_choice_key(channel_id,channel);
 snprintf(joined,sizeof(joined),"iptv:%s:%s",scope,channel);vt_iptv_choice_key(joined,out);
}
/* Stable playlist + tvg-id identities survive channel URL/token changes.
 * The original URL hash remains a compatibility alias for older data/bookmarks. */
/* load: 0 absent, 1 stable/current URL, 2 legacy URL, 3 stable/rotated URL. */
int vt_iptv_playback_load_channel(const char *url,const char *identity,VtIptvPreference *out);
int vt_iptv_playback_save_channel(const char *url,const char *identity,const VtIptvPreference *preference);
int vt_iptv_playback_remove_channel(const char *url,const char *identity);
int vt_iptv_playback_load(const char *url,VtIptvPreference *out);
int vt_iptv_playback_save(const char *url,const VtIptvPreference *preference);
static inline void vt_iptv_quality_signature(const VtIptvOptions *o,char out[17]){
 uint64_t hash=0;unsigned count=0;
 if(o)for(unsigned i=0;i<o->video_count;i++)if(o->video[i].supported){char key[17],facts[192];vt_iptv_choice_key(o->video[i].id,key);snprintf(facts,sizeof(facts),"%s:%u:%u:%u:%.96s",key,o->video[i].width,o->video[i].height,o->video[i].bandwidth,o->video[i].codec);vt_iptv_choice_key(facts,key);hash^=strtoull(key,NULL,16);count++;}
 char text[64];snprintf(text,sizeof(text),"%llx:%u",(unsigned long long)hash,count);vt_iptv_choice_key(text,out);
}
int vt_iptv_quality_cached(const char *url,const char *signature,unsigned context,VtIptvPreference *out);
int vt_iptv_quality_save(const char *url,const char *signature,unsigned context,const VtIptvPreference *p,const VtIptvQualityReport *report);
#endif
