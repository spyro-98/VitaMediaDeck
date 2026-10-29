#ifndef VMD_IPTV_OPTIONS_H
#define VMD_IPTV_OPTIONS_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define VT_IPTV_CHOICE_MAX 64
#define VT_IPTV_CHOICE_ID_MAX 2048
#define VT_IPTV_OPTIONS_DIRECTORY 12
/* Empty IDs mean automatic/source default. IDs are internal, never labels. */
typedef struct {char video[VT_IPTV_CHOICE_ID_MAX],audio[VT_IPTV_CHOICE_ID_MAX];int video_manual;} VtIptvSelection;
typedef struct {
 char video_key[17],audio_key[17];
 char audio_language[64],audio_name[160];
 uint32_t width,height,bandwidth;
} VtIptvPreference;
static inline void vt_iptv_choice_key(const char *id,char out[17]){
 static const char digits[]="0123456789abcdef";
 if(!id||!*id){out[0]=0;return;}
 uint64_t h=1469598103934665603ULL;
 for(const unsigned char *p=(const unsigned char*)id;*p;p++){h^=*p;h*=1099511628211ULL;}
 for(int i=15;i>=0;i--){out[i]=digits[h&15];h>>=4;}out[16]=0;
}
typedef struct {
 char id[VT_IPTV_CHOICE_ID_MAX],name[160],language[64],group[256],audio_group[256],codec[96];
 uint32_t width,height,bandwidth;
 int supported;
} VtIptvChoice;
typedef struct {
 VtIptvChoice video[VT_IPTV_CHOICE_MAX],audio[VT_IPTV_CHOICE_MAX];
 unsigned video_count,audio_count;
 char default_video[VT_IPTV_CHOICE_ID_MAX];
 char title[256],description[1024],provider[160];
 char channel_name[160],channel_logo[512],channel_id[128];
 VtIptvSelection active;
 uint32_t active_width,active_height,active_bandwidth;
 int discovered;
 int content_kind; /* Actual document kind + 1; zero means unknown. */
} VtIptvOptions;
#define VT_IPTV_UPCOMING_MAX 6
#define VT_IPTV_GUIDE_TRANSFER_LIMIT (16u*1024u*1024u)
#define VT_IPTV_GUIDE_EXPANDED_LIMIT (64u*1024u*1024u)
#define VT_IPTV_PUBLIC_ITALY_GUIDE "https://www.open-epg.com/files/italy3.xml.gz"
enum {VT_IPTV_GUIDE_OK,VT_IPTV_GUIDE_MISSING,VT_IPTV_GUIDE_NO_CHANNEL,VT_IPTV_GUIDE_NO_PROGRAM};
typedef struct {char title[256],description[512];int64_t start,end;} VtIptvUpcoming;
typedef struct {
 const char *url,*channel,*name,*title;
 int shift_seconds,refresh;
} VtIptvGuideQuery;
typedef struct {
 char title[256],subtitle[256],description[4096],category[160],episode[128],next_title[256];
 int64_t start,end,next_start,next_end;
 int available;
 VtIptvUpcoming upcoming[VT_IPTV_UPCOMING_MAX];
 unsigned upcoming_count;
 int guide_status,cached,stale,public_source;
 char channel_name[160],channel_logo[512],channel_id[128];
} VtIptvProgram;
struct VtIptvSourceConfig;
int vt_iptv_options_load(const char *url,int dash,const struct VtIptvSourceConfig *config,
                         VtIptvOptions *out,volatile int *cancel);
int vt_iptv_program_parse(const void *data,size_t size,const char *channel,int64_t now,
                          const char *language,VtIptvProgram *out);
int vt_iptv_program_load(const char *url,const char *channel,const struct VtIptvSourceConfig *config,
                         const char *language,VtIptvProgram *out,volatile int *cancel);
int vt_iptv_program_parse_channel(const void *data,size_t size,const VtIptvGuideQuery *query,int64_t now,
                                  const char *language,VtIptvProgram *out,volatile int *cancel);
int vt_iptv_program_load_channel(const VtIptvGuideQuery *query,const struct VtIptvSourceConfig *config,
                                 const char *language,VtIptvProgram *out,volatile int *cancel);
/* Audio groups are constrained by the chosen HLS video. DASH groups are empty. */
static inline int vt_iptv_audio_matches(const VtIptvOptions *o,const VtIptvSelection *s,unsigned index){
 if(!o||index>=o->audio_count)return 0;
 const char *video=s&&s->video[0]?s->video:o->default_video;
 if(!*video)return 1;
 for(unsigned i=0;i<o->video_count;i++)if(!strcmp(video,o->video[i].id))
  return !o->audio[index].group[0]||!strcmp(o->audio[index].group,o->video[i].audio_group);
 return 0;
}
#endif
