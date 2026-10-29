#ifndef VMD_IPTV_PLAYER_PANEL_H
#define VMD_IPTV_PLAYER_PANEL_H
#include "network/iptv_options.h"
#include "ui/scroll_view.h"
#include "ui/focus_glow.h"
#include <string.h>
#include <stdio.h>
typedef struct {
 int page,cursor,top,scroll,program_error,program_loading;
 UiScrollView viewport;
 UiFocusMotion focus;
 VtIptvProgram program;
} UiIptvPlayerPanel;
/* Choice zero is automatic/source default. Other rows keep stable IDs. */
static inline int ui_iptv_choices(const VtIptvOptions *o,const VtIptvSelection *s,int audio,int map[VT_IPTV_CHOICE_MAX+2]){
 int n=1,supported=0;map[0]=-1;if(!o)return n;
 unsigned count=audio?o->audio_count:o->video_count;
 for(unsigned i=0;i<count&&i<VT_IPTV_CHOICE_MAX;i++)if(!audio||vt_iptv_audio_matches(o,s,i)){map[n++]=i;if((audio?o->audio[i]:o->video[i]).supported)supported++;}
 if(!audio&&supported>1)map[n++]=-2;
 return n;
}
static inline int ui_iptv_choice_apply(const VtIptvOptions *o,VtIptvSelection *s,int audio,int index){
 if(!s||index< -1)return 0;
 const VtIptvChoice *c=NULL;
 if(index>=0){if(!o||(unsigned)index>=(audio?o->audio_count:o->video_count))return 0;
  c=audio?&o->audio[index]:&o->video[index];if(!c->supported||(audio&&!vt_iptv_audio_matches(o,s,index)))return 0;}
 char *id=audio?s->audio:s->video;const char *wanted=c?c->id:"";
 if(!strcmp(id,wanted)&&(audio||s->video_manual==(index>=0)))return 0;
 if(!audio)s->video_manual=index>=0;
 snprintf(id,VT_IPTV_CHOICE_ID_MAX,"%s",wanted);
 if(!audio&&s->audio[0]){
  int valid=0;if(o)for(unsigned i=0;i<o->audio_count;i++)if(!strcmp(s->audio,o->audio[i].id)&&vt_iptv_audio_matches(o,s,i))valid=1;
  if(!valid)s->audio[0]=0;
 }
 return 1;
}
void ui_iptv_player_panel_draw(UiIptvPlayerPanel *panel,float x,const VtIptvOptions *options,
                                const VtIptvSelection *selection);
#endif
