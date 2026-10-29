#include "network/dash_manifest.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libxml/parser.h>
#include <libavutil/mathematics.h>
#include <libavutil/parseutils.h>

#define MAX_TICKS ((uint64_t)INT64_MAX / 4)
static int named(const xmlNode *n, const char *name) {
    return n && n->type == XML_ELEMENT_NODE && !xmlStrcmp(n->name, BAD_CAST name);
}
static xmlNode *child(xmlNode *n, const char *name) {
    for (xmlNode *p=n?n->children:NULL;p;p=p->next) if(named(p,name))return p;
    return NULL;
}
static int text_prop(xmlNode *n,const char *name,char *out,size_t capacity) {
    xmlChar *p=n?xmlGetProp(n,BAD_CAST name):NULL;
    if(!p){out[0]=0;return 0;}
    size_t size=xmlStrlen(p);int result=size<capacity?1:-1;
    if(result>0)memcpy(out,p,size+1);else out[0]=0;
    xmlFree(p);return result;
}
static int inherited(xmlNode **nodes,unsigned count,const char *key,char *out,size_t capacity) {
    for(unsigned i=count;i>0;i--){int r=text_prop(nodes[i-1],key,out,capacity);if(r)return r;}
    out[0]=0;return 0;
}
static int integer(const char *text,uint64_t *out) {
    if(!text[0])return -1;
    uint64_t value=0;
    for(const unsigned char *p=(const void*)text;*p;p++) {
        if(!isdigit(*p)||value>(MAX_TICKS-(*p-'0'))/10)return -1;
        value=value*10+(*p-'0');
    }
    *out=value;return 0;
}
static int uint_prop(xmlNode *n,const char *key,uint64_t fallback,uint64_t *out) {
    char value[64];int r=text_prop(n,key,value,sizeof(value));
    if(r<0)return -1;
    if(!r){*out=fallback;return 0;}
    return integer(value,out);
}
static int inherited_uint(xmlNode **nodes,unsigned count,const char *key,uint64_t fallback,uint64_t *out) {
    char value[64];int r=inherited(nodes,count,key,value,sizeof(value));
    if(r<0)return -1;
    if(!r){*out=fallback;return 0;}
    return integer(value,out);
}
static int duration_text(const char *text,int64_t *out) {
    if(*text++!='P')return -1;
    double seconds=0;int clock=0,any=0,rank=0;
    while(*text) {
        if(*text=='T'&&!clock){clock=1;rank=0;text++;continue;}
        if(!isdigit((unsigned char)*text))return -1;
        char *end;errno=0;double v=strtod(text,&end);if(errno||end==text||!isfinite(v)||v<0)return -1;
        int next;double scale;
        switch(*end) {
        case 'D':if(clock)return -1;next=1;scale=86400;break;
        case 'H':if(!clock)return -1;next=1;scale=3600;break;
        case 'M':if(!clock)return -1;next=2;scale=60;break;
        case 'S':if(!clock)return -1;next=3;scale=1;break;
        default:return -1;
        }
        if(next<=rank)return -1;
        rank=next;seconds+=v*scale;
        if(seconds>315576000.0)return -1;
        any=1;text=end+1;
    }
    if(!any)return -1;
    *out=(int64_t)(seconds*1000000.0+0.5);return 0;
}
static int duration_prop(xmlNode *n,const char *key,int64_t fallback,int64_t *out) {
    char text[96];int r=text_prop(n,key,text,sizeof(text));
    if(r<0)return -1;
    if(!r){*out=fallback;return 0;}
    return duration_text(text,out);
}
static int bounded_tree(xmlNode *n,unsigned depth,unsigned *count) {
    if(depth>32)return -1;
    for(;n;n=n->next) {
        if(++*count>16000||n->type==XML_ENTITY_REF_NODE)return -1;
        if(n->children&&bounded_tree(n->children,depth+1,count))return -1;
    }
    return 0;
}
static int base_url(xmlNode **nodes,unsigned count,const char *base,char *out) {
    snprintf(out,VT_IPTV_RESOURCE_URL_MAX,"%s",base);
    for(unsigned i=0;i<count;i++) {
        xmlNode *n=child(nodes[i],"BaseURL");if(!n)continue;
        xmlChar *p=xmlNodeGetContent(n);if(!p)return -1;
        char resolved[VT_IPTV_RESOURCE_URL_MAX];
        int result=vt_iptv_resource_resolve(out,(const char*)p,resolved,sizeof(resolved));xmlFree(p);
        if(result)return -1;
        snprintf(out,VT_IPTV_RESOURCE_URL_MAX,"%s",resolved);
    }
    return 0;
}
static int expand(const VtDashTrack *t,const char *input,uint64_t number,uint64_t time,char *out) {
    char path[VT_IPTV_RESOURCE_URL_MAX];size_t used=0;
    while(*input) {
        char value[160];size_t size;
        if(*input!='$'){if(used+1>=sizeof(path))return -1;path[used++]=*input++;continue;}
        input++;
        if(*input=='$'){value[0]='$';value[1]=0;input++;}
        else {
            const char *end=strchr(input,'$');if(!end||end-input>80)return -1;
            char key[81];memcpy(key,input,end-input);key[end-input]=0;input=end+1;
            char *format=strchr(key,'%');int width=0;
            if(format) {
                *format++=0;if(*format++!='0')return -1;
                if(!isdigit((unsigned char)*format))return -1;
                while(isdigit((unsigned char)*format)){width=width*10+(*format++-'0');if(width>20)return -1;}
                if(strcmp(format,"d"))return -1;
            }
            uint64_t n;
            if(!strcmp(key,"RepresentationID")) {
                if(format||!t->id[0])return -1;
                snprintf(value,sizeof(value),"%s",t->id);
            } else {
                if(!strcmp(key,"Number"))n=number;
                else if(!strcmp(key,"Time"))n=time;
                else if(!strcmp(key,"Bandwidth"))n=t->bandwidth;
                else return -1;
                snprintf(value,sizeof(value),"%0*llu",width,(unsigned long long)n);
            }
        }
        size=strlen(value);if(size>=sizeof(path)-used)return -1;memcpy(path+used,value,size);used+=size;
    }
    path[used]=0;return vt_iptv_resource_resolve(t->base,path,out,VT_IPTV_RESOURCE_URL_MAX);
}
static int64_t presentation(const VtDashManifest *m,const VtDashTrack *t,uint64_t tick) {
    int64_t delta=av_rescale((int64_t)tick-(int64_t)t->offset,1000000,(int64_t)t->timescale);
    if(delta==INT64_MIN||delta>INT64_MAX-m->period_us)return INT64_MIN;
    return m->period_us+delta;
}
static uint64_t tick_at(const VtDashManifest *m,const VtDashTrack *t,int64_t us) {
    int64_t delta=av_rescale(us-m->period_us,(int64_t)t->timescale,1000000);
    if(delta<0&&(uint64_t)(-delta)>t->offset)return 0;
    if(delta>0&&(uint64_t)delta>MAX_TICKS-t->offset)return MAX_TICKS;
    return t->offset+delta;
}
/* Walk compressed timeline runs, never expand repeats into allocated arrays. */
static int locate(const VtDashManifest *m,const VtDashTrack *t,uint64_t tick,
                  uint64_t *start,uint64_t *length,uint64_t *ordinal,int bounds) {
    uint64_t current=0,index=0,first=0,last=0;int found=0;
    uint64_t limit=tick_at(m,t,m->dynamic?m->available_us:m->period_us+m->duration_us);
    xmlNode *node=t->timeline?t->timeline->children:NULL;
    if(!t->timeline) {
        uint64_t count=0;
        if(t->list){for(xmlNode *n=t->list->children;n;n=n->next)if(named(n,"SegmentURL"))count++;}
        else if(t->duration&&limit>0)count=limit/t->duration+(m->dynamic?0:limit%t->duration!=0);
        if(!t->duration||!count)return m->dynamic?VT_IPTV_WAIT:VT_IPTV_INVALID;
        if(count>MAX_TICKS/t->duration)return VT_IPTV_LIMIT;
        uint64_t i=tick/t->duration;
        if(bounds){*start=0;*length=count*t->duration;*ordinal=count;return 0;}
        if(i>=count)return m->dynamic?VT_IPTV_WAIT:VT_IPTV_END;
        *start=i*t->duration;*length=t->duration;*ordinal=i;return 0;
    }
    for(;node;node=node->next) {
        if(!named(node,"S"))continue;
        uint64_t duration,repeat,at;
        if(uint_prop(node,"t",current,&at)||uint_prop(node,"d",0,&duration)||!duration||at<current)return VT_IPTV_INVALID;
        char r[64];int has=text_prop(node,"r",r,sizeof(r));if(has<0)return VT_IPTV_INVALID;
        if(has&&!strcmp(r,"-1")) {
            xmlNode *next=node->next;while(next&&!named(next,"S"))next=next->next;
            uint64_t end=limit;
            if(next&&uint_prop(next,"t",MAX_TICKS,&end))return VT_IPTV_INVALID;
            if(end==MAX_TICKS||end<=at)return m->dynamic?VT_IPTV_WAIT:VT_IPTV_INVALID;
            repeat=(end-at)/duration+((!m->dynamic||next)&&(end-at)%duration!=0);
        } else {
            if(has&&integer(r,&repeat))return VT_IPTV_INVALID;
            if(!has)repeat=0;
            repeat++;
        }
        if(!repeat||repeat>(MAX_TICKS-at)/duration||index>MAX_TICKS-repeat)return VT_IPTV_LIMIT;
        uint64_t end=at+duration*repeat;
        if(!index)first=at;
        if(m->dynamic&&end>limit){repeat=limit>at?(limit-at)/duration:0;end=at+duration*repeat;}
        if(!bounds&&!found&&repeat&&tick<end) {
            uint64_t i=tick>at?(tick-at)/duration:0;
            *start=at+i*duration;*length=duration;*ordinal=index+i;found=1;
        }
        current=end;index+=repeat;last=end;
    }
    if(bounds){*start=first;*length=last;*ordinal=index;return index?0:VT_IPTV_WAIT;}
    return found?0:m->dynamic?VT_IPTV_WAIT:VT_IPTV_END;
}
static int prepare_track(VtDashManifest *m,VtDashTrack *t,xmlNode **levels,const char *base) {
    xmlNode *segments[4]={0};int kind=0;
    for(int i=0;i<4;i++) {
        if(child(levels[i],"ContentProtection")||child(levels[i],"SegmentBase"))return VT_IPTV_UNSUPPORTED;
        xmlNode *templ=child(levels[i],"SegmentTemplate"),*list=child(levels[i],"SegmentList");
        if(templ&&list)return VT_IPTV_INVALID;
        if(templ||list){int k=list?2:1;if(kind&&kind!=k)return VT_IPTV_UNSUPPORTED;kind=k;segments[i]=list?list:templ;}
    }
    if(!kind||base_url(levels,4,base,t->base))return VT_IPTV_UNSUPPORTED;
    if(inherited_uint(segments,4,"timescale",1,&t->timescale)||!t->timescale||t->timescale>1000000000||
       inherited_uint(segments,4,"presentationTimeOffset",0,&t->offset)||
       inherited_uint(segments,4,"startNumber",1,&t->start_number)||
       inherited_uint(segments,4,"duration",0,&t->duration))return VT_IPTV_INVALID;
    for(int i=0;i<4;i++) {
        xmlNode *timeline=child(segments[i],"SegmentTimeline");if(timeline)t->timeline=timeline;
        if(kind==2&&child(segments[i],"SegmentURL"))t->list=segments[i];
        char complete[16];if(text_prop(segments[i],"availabilityTimeComplete",complete,sizeof(complete))>0&&strcmp(complete,"true"))return VT_IPTV_UNSUPPORTED;
    }
    if(kind==1) {
        char initialization[VT_IPTV_RESOURCE_URL_MAX];
        if(inherited(segments,4,"initialization",initialization,sizeof(initialization))<=0||
           inherited(segments,4,"media",t->media,sizeof(t->media))<=0||expand(t,initialization,0,0,t->initialization))return VT_IPTV_INVALID;
    } else {
        xmlNode *initialization=NULL;
        for(int i=0;i<4;i++){xmlNode *n=child(segments[i],"Initialization");if(n)initialization=n;}
        char url[VT_IPTV_RESOURCE_URL_MAX];
        if(!t->list||!initialization||xmlHasProp(initialization,BAD_CAST"range")||
           text_prop(initialization,"sourceURL",url,sizeof(url))<=0||vt_iptv_resource_resolve(t->base,url,t->initialization,sizeof(t->initialization)))return VT_IPTV_UNSUPPORTED;
        for(xmlNode *n=t->list->children;n;n=n->next)if(named(n,"SegmentURL")&&
            (xmlHasProp(n,BAD_CAST"mediaRange")||xmlHasProp(n,BAD_CAST"indexRange")))return VT_IPTV_UNSUPPORTED;
    }
    uint64_t end,count,first;int r=locate(m,t,0,&first,&end,&count,1);if(r)return r;
    t->first_tick=first;t->end_tick=end;t->count=count;
    if(count>MAX_TICKS-t->start_number)return VT_IPTV_LIMIT;
    t->first_us=presentation(m,t,first);t->end_us=presentation(m,t,end);
    int64_t end_limit=m->dynamic?m->available_us:m->period_us+m->duration_us;
    if(t->first_us< -315576000000000LL||t->end_us==INT64_MIN||t->end_us>end_limit+60000000)return VT_IPTV_INVALID;
    uint64_t start,len,index;r=locate(m,t,first,&start,&len,&index,0);if(r)return r;
    t->segment_us=av_rescale(len,1000000,t->timescale);
    if(t->first_us==INT64_MIN||t->end_us==INT64_MIN||t->segment_us<=0||t->segment_us>60000000)return VT_IPTV_LIMIT;
    return 0;
}
static int frame_budget(xmlNode **levels,uint64_t width,uint64_t height) {
    char value[64];int r=inherited(levels,4,"frameRate",value,sizeof(value));
    if(r<0)return 0;
    if(!r)return 1;
    char *end;double rate=strtod(value,&end);
    if(end==value||!isfinite(rate)||rate<=0)return 0;
    if(*end=='/') {
        char *tail;double denominator=strtod(end+1,&tail);
        if(tail==end+1||!isfinite(denominator)||denominator<=0)return 0;
        rate/=denominator;end=tail;
    }
    /* Avoid selecting 720p50/60 just because its bitrate is low enough.
     * Keep the conservative 720p30 pixel-rate budget for automatic selection. */
    return !*end&&rate<=60&&width*height*rate<=1280.0*720.0*30.0;
}
int vt_dash_manifest_parse(const void *data,size_t size,const char *base,int64_t utc_us,
                          const VtIptvSourceConfig *limits,VtDashManifest **out) {
    if(!out)return VT_IPTV_INVALID;
    *out=NULL;
    if(!data||!size||size>VT_IPTV_PLAYLIST_LIMIT||memchr(data,0,size))return VT_IPTV_LIMIT;
    VtDashManifest *m=calloc(1,sizeof(*m));if(!m)return VT_IPTV_MEMORY;
    m->document=xmlReadMemory(data,(int)size,NULL,NULL,XML_PARSE_NONET|XML_PARSE_NOBLANKS|XML_PARSE_NOERROR|XML_PARSE_NOWARNING);
    int result=VT_IPTV_INVALID;xmlNode *root=m->document?xmlDocGetRootElement(m->document):NULL;
    unsigned nodes=0;
    if(!named(root,"MPD")||m->document->intSubset||m->document->extSubset||bounded_tree(root,0,&nodes))goto fail;
    char type[32];if(text_prop(root,"type",type,sizeof(type))<0)goto fail;
    if(type[0]&&strcmp(type,"static")&&strcmp(type,"dynamic"))goto fail;
    m->dynamic=!strcmp(type,"dynamic");
    xmlNode *period=child(root,"Period");if(!period)goto fail;
    for(xmlNode *p=period->next;p;p=p->next)if(named(p,"Period")){result=VT_IPTV_UNSUPPORTED;goto fail;}
    if(text_prop(period,"id",m->period_id,sizeof(m->period_id))<0)goto fail;
    int64_t total,update,delay;
    if(duration_prop(root,"mediaPresentationDuration",0,&total)||duration_prop(period,"start",0,&m->period_us)||
       duration_prop(period,"duration",total>m->period_us?total-m->period_us:0,&m->duration_us)||
       duration_prop(root,"minimumUpdatePeriod",2000000,&update)||duration_prop(root,"suggestedPresentationDelay",0,&delay))goto fail;
    m->update_ms=(uint64_t)update/1000;if(m->update_ms<500)m->update_ms=500;if(m->update_ms>30000)m->update_ms=30000;
    m->delay_ms=(uint64_t)delay/1000;
    if(m->dynamic) {
        char ast[128];int64_t start;
        if(text_prop(root,"availabilityStartTime",ast,sizeof(ast))<=0||av_parse_time(&start,ast,0)<0||start<0||utc_us<=start)goto fail;
        m->available_us=utc_us-start;
        if(m->duration_us&&m->available_us>m->period_us+m->duration_us)m->available_us=m->period_us+m->duration_us;
        xmlNode *timing=child(root,"UTCTiming");char scheme[160],value[VT_IPTV_RESOURCE_URL_MAX];
        if(timing&&text_prop(timing,"schemeIdUri",scheme,sizeof(scheme))>0&&text_prop(timing,"value",value,sizeof(value))>0) {
            if(!strcmp(scheme,"urn:mpeg:dash:utc:direct:2014"))snprintf(m->utc_value,sizeof(m->utc_value),"%s",value);
            else if(!strcmp(scheme,"urn:mpeg:dash:utc:http-xsdate:2014")||!strcmp(scheme,"urn:mpeg:dash:utc:http-iso:2014")) {
                if(vt_iptv_resource_resolve(base,value,m->utc_value,sizeof(m->utc_value)))goto fail;
                m->utc_http=1;
            }
        }
    } else if(!m->duration_us){result=VT_IPTV_UNSUPPORTED;goto fail;}
    if(limits&&limits->inspect){
        limits->inspect->video_count=limits->inspect->audio_count=0;
        xmlNode *program=child(root,"ProgramInformation");
        xmlNode *title=child(program,"Title"),*source=child(program,"Source");
        if(title){xmlChar *v=xmlNodeGetContent(title);if(v){snprintf(limits->inspect->title,sizeof(limits->inspect->title),"%s",v);xmlFree(v);}}
        if(source){xmlChar *v=xmlNodeGetContent(source);if(v){snprintf(limits->inspect->provider,sizeof(limits->inspect->provider),"%s",v);xmlFree(v);}}
    }
    int selected[2]={0},preferred[2]={0};
    for(xmlNode *adapt=period->children;adapt;adapt=adapt->next)if(named(adapt,"AdaptationSet")) {
        for(xmlNode *rep=adapt->children;rep;rep=rep->next)if(named(rep,"Representation")) {
            xmlNode *levels[]={root,period,adapt,rep};char mime[96],codec[96];uint64_t bandwidth,width,height;
            if(inherited(levels,4,"mimeType",mime,sizeof(mime))<0||inherited(levels,4,"codecs",codec,sizeof(codec))<=0)continue;
            int track=!strncmp(codec,"avc1",4)||!strncmp(codec,"avc3",4)?0:
                (!strcmp(codec,"mp4a.40.2")||!strcmp(codec,"mp4a.40.5")||!strcmp(codec,"mp4a.40.29"))?1:-1;
            if(track<0|| (mime[0]&&strcmp(mime,track?"audio/mp4":"video/mp4")))continue;
            if(inherited_uint(levels,4,"bandwidth",0,&bandwidth)||!bandwidth||bandwidth>UINT32_MAX||
               inherited_uint(levels,4,"width",0,&width)||inherited_uint(levels,4,"height",0,&height))continue;
            char id[VT_IPTV_CHOICE_ID_MAX],rep_id[128]="",adapt_id[128]="",language[64]="";
            text_prop(rep,"id",rep_id,sizeof(rep_id));text_prop(adapt,"id",adapt_id,sizeof(adapt_id));
            inherited(levels,4,"lang",language,sizeof(language));
            /* Stable identity, never representation array position. */
            if(!rep_id[0])snprintf(rep_id,sizeof(rep_id),"%.60s-%ux%u-%u",codec,(unsigned)width,(unsigned)height,(unsigned)bandwidth);
            if(!adapt_id[0]&&track)snprintf(adapt_id,sizeof(adapt_id),"%.60s",language);
            snprintf(id,sizeof(id),"%s|%s",adapt_id,rep_id);
            VtIptvChoice *choice=NULL;
            if(limits&&limits->inspect){
                VtIptvOptions *o=limits->inspect;unsigned *n=track?&o->audio_count:&o->video_count;
                if(*n<VT_IPTV_CHOICE_MAX){VtIptvChoice *c=track?&o->audio[(*n)++]:&o->video[(*n)++];memset(c,0,sizeof(*c));
                    snprintf(c->id,sizeof(c->id),"%s",id);snprintf(c->name,sizeof(c->name),"%s",rep_id);
                    snprintf(c->language,sizeof(c->language),"%s",language);snprintf(c->codec,sizeof(c->codec),"%s",codec);
                    c->width=width;c->height=height;c->bandwidth=bandwidth;c->supported=0;choice=c;
                }
            }
            const char *wanted=limits?(track?limits->selection.audio:limits->selection.video):"";
            if(*wanted&&strcmp(wanted,id))continue;
            if(!track&&(!width||!height))continue;
            if(!*wanted&&!track&&limits&&((limits->width_limit&&width>limits->width_limit)||
               (limits->height_limit&&height>limits->height_limit)||(limits->bandwidth_limit&&bandwidth>limits->bandwidth_limit)))continue;
            int within=track?1:width<=1280&&height<=720&&bandwidth<=4000000&&frame_budget(levels,width,height);
            /* Preference only: a source with just 1080p/high-bitrate video is
             * still attempted. Explicit user selection bypasses this ranking. */
            int choose=1;
            if(selected[track]&&!*wanted){
                if(within<preferred[track])choose=0;
                if(within==preferred[track]&&
                   (within?bandwidth<=m->tracks[track].bandwidth:bandwidth>=m->tracks[track].bandwidth))choose=0;
            }
            if(!choose&&!choice)continue;
            VtDashTrack *candidate=calloc(1,sizeof(*candidate));if(!candidate){result=VT_IPTV_MEMORY;goto fail;}
            if(text_prop(rep,"id",candidate->id,sizeof(candidate->id))<0){free(candidate);continue;}
            snprintf(candidate->codec,sizeof(candidate->codec),"%s",codec);
            candidate->bandwidth=(uint32_t)bandwidth;candidate->width=(uint32_t)width;candidate->height=(uint32_t)height;
            int r=prepare_track(m,candidate,levels,base);
            if(choice)choice->supported=!r;
            if(!r&&choose){m->tracks[track]=*candidate;selected[track]=1;preferred[track]=within;snprintf(track?m->active.audio:m->active.video,VT_IPTV_CHOICE_ID_MAX,"%s",id);}free(candidate);
        }
    }
    if(!selected[0]||!selected[1]){result=VT_IPTV_UNSUPPORTED;goto fail;}
    *out=m;return 0;
fail:
    vt_dash_manifest_free(m);return result;
}
void vt_dash_manifest_free(VtDashManifest *m){if(m){xmlFreeDoc(m->document);free(m);}}
int vt_dash_segment(const VtDashManifest *m,int index,uint64_t tick,VtDashSegment *out) {
    if(!m||index<0||index>1||!out)return VT_IPTV_INVALID;
    const VtDashTrack *t=&m->tracks[index];uint64_t time,duration,ordinal;
    int result=locate(m,t,tick,&time,&duration,&ordinal,0);if(result)return result;
    memset(out,0,sizeof(*out));out->time=time;out->duration=duration;out->number=t->start_number+ordinal;
    out->start_us=presentation(m,t,time);out->end_us=presentation(m,t,time+duration);
    if(out->start_us==INT64_MIN||out->end_us==INT64_MIN)return VT_IPTV_LIMIT;
    if(t->list) {
        xmlNode *n=t->list->children;for(;n;n=n->next)if(named(n,"SegmentURL")){if(!ordinal)break;ordinal--;}
        char url[VT_IPTV_RESOURCE_URL_MAX];
        if(!n||text_prop(n,"media",url,sizeof(url))<=0)return VT_IPTV_INVALID;
        return vt_iptv_resource_resolve(t->base,url,out->url,sizeof(out->url));
    }
    return expand(t,t->media,out->number,time,out->url)?VT_IPTV_INVALID:0;
}
int vt_dash_start(const VtDashManifest *m,int track,int64_t us,VtDashSegment *out) {
    if(!m||track<0||track>1)return VT_IPTV_INVALID;
    return vt_dash_segment(m,track,tick_at(m,&m->tracks[track],us),out);
}
int vt_dash_same_tracks(const VtDashManifest *a,const VtDashManifest *b) {
    if(strcmp(a->period_id,b->period_id)||a->period_us!=b->period_us)return 0;
    for(int i=0;i<2;i++)if(strcmp(a->tracks[i].id,b->tracks[i].id)||strcmp(a->tracks[i].codec,b->tracks[i].codec)||
        strcmp(a->tracks[i].initialization,b->tracks[i].initialization)||a->tracks[i].timescale!=b->tracks[i].timescale||
        a->tracks[i].offset!=b->tracks[i].offset)return 0;
    return 1;
}
