#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../../src/media/media_tracks.c"

static unsigned checks, opens, threads_created;
static const char *cue_prefix="";
#define CHECK(x) do { checks++; if(!(x)){fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);abort();} } while(0)
typedef struct { pthread_t thread; int(*entry)(SceSize,void*); SceSize size; void *arg; } HostThread;
static HostThread host_thread;
static void *launch(void *p){HostThread *t=p;t->entry(t->size,t->arg);return NULL;}
int sceKernelCreateThread(const char*n,int(*entry)(SceSize,void*),int p,unsigned s,int a,int b,void*c){(void)n;(void)p;(void)s;(void)a;(void)b;(void)c;host_thread.entry=entry;threads_created++;return 1;}
int sceKernelStartThread(SceUID id,SceSize size,void*arg){(void)id;host_thread.size=size;host_thread.arg=malloc(size);memcpy(host_thread.arg,arg,size);return pthread_create(&host_thread.thread,NULL,launch,&host_thread);}
int sceKernelWaitThreadEnd(SceUID id,void*a,void*b){(void)id;(void)a;(void)b;int r=pthread_join(host_thread.thread,NULL);free(host_thread.arg);host_thread.arg=NULL;return r;}
int sceKernelDeleteThread(SceUID id){(void)id;return 0;}
int sceKernelExitThread(int r){(void)r;pthread_exit(NULL);return 0;}
int sceKernelDelayThread(unsigned us){usleep(us);return 0;}
uint64_t sceKernelGetProcessTimeWide(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
int sceKernelGetThreadCurrentPriority(void){return 128;}
SceUID sceKernelGetThreadId(void){return 1;}
int sceKernelChangeThreadPriority(SceUID id,int p){(void)id;(void)p;return 0;}
void log_printf(const char *fmt,...){va_list a;va_start(a,fmt);vfprintf(stderr,fmt,a);va_end(a);}

static volatile int block_reads, read_blocked;
static VtSubtitleReader *current_reader;
typedef struct { FILE *file; int interrupted; } FileCursor;
static int read_file(void*p,void*b,size_t n){
 if(block_reads){read_blocked=1;while(block_reads&&!current_reader->operation_cancel)usleep(100);
  if(current_reader->operation_cancel){block_reads=0;((FileCursor*)p)->interrupted=1;return -1;}}
 FileCursor*c=p;return c->interrupted?-1:(int)fread(b,1,n,c->file);
}
static int64_t seek_file(void*p,int64_t o,int w){FileCursor*c=p;return c->interrupted?-1:fseeko(c->file,o,w)?-1:ftello(c->file);}
static void close_file(void*p){FileCursor*c=p;fclose(c->file);free(c);}
static int open_file(void*p,VtDecoderStreamHandle*out){FILE*f=fopen(p,"rb");if(!f)return -1;fseeko(f,0,SEEK_END);int64_t size=ftello(f);rewind(f);FileCursor*c=calloc(1,sizeof(*c));c->file=f;*out=(VtDecoderStreamHandle){.opaque=c,.read=read_file,.seek=seek_file,.close=close_file,.size=size};opens++;return 0;}
static int find_subtitle(const char *path){AVFormatContext*f=NULL;CHECK(avformat_open_input(&f,path,NULL,NULL)>=0);int index=-1;for(unsigned i=0;i<f->nb_streams;i++)if(f->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_SUBTITLE){index=i;break;}avformat_close_input(&f);CHECK(index>=0);return index;}
static void expect_cue(VtSubtitleReader*r,uint64_t position,int cue){
 char text[512],expected[32];snprintf(expected,sizeof(expected),"%scue-%d",cue_prefix,cue);
 uint64_t deadline=sceKernelGetProcessTimeWide()+2000000;
 do{int ret=vt_subtitle_reader_text(r,position,text,sizeof(text));
  if(ret>0&&strstr(text,expected)){CHECK(1);return;}
  if(ret<0){fprintf(stderr,"Reader failed %d at %llu ms\n",ret,(unsigned long long)position);break;}
  usleep(1000);
 }while(sceKernelGetProcessTimeWide()<deadline);
 fprintf(stderr,"Missing %s at %llu; state=%d eof=%d queue=%u demux=%llu text=%s\n",expected,(unsigned long long)position,vt_subtitle_reader_state(r),r->eof,r->cue_count,(unsigned long long)r->demux_position_ms,text);
 subtitle_cue_lock(r);for(unsigned i=0;i<r->cue_count;i++)fprintf(stderr,"cue %llu..%llu %s\n",(unsigned long long)r->cues[i].start_ms,(unsigned long long)r->cues[i].end_ms,r->cues[i].text);subtitle_cue_unlock(r);
 CHECK(0);
}
int main(int argc,char**argv){
 alarm(30);CHECK(argc>=2);
 for(int f=1;f<argc;f++){
  int index=find_subtitle(argv[f]);volatile int cancel=0;
  VtDecoderStreamFactory factory={.opaque=argv[f],.open=open_file};
  VtDecoderTrackInfo audio[VT_MEDIA_MAX_AUDIO_TRACKS],subs[VT_MEDIA_MAX_SUBTITLE_TRACKS];
  int audio_count=0,sub_count=0;
  CHECK(!vt_media_tracks_probe(&factory,audio,&audio_count,subs,&sub_count,&cancel));
  CHECK(audio_count==(strstr(argv[f],"multi-track")?2:1));
  CHECK(sub_count==(strstr(argv[f],"multi-track")?2:1));
  unsigned before_threads=threads_created;VtSubtitleReader*r=vt_subtitle_reader_create();CHECK(r);current_reader=r;
  CHECK(!vt_subtitle_reader_open(r,&factory,index,0,&cancel));expect_cue(r,250,0);
  const int targets[]={18,4,22,1,12,20,0,19};
  for(unsigned i=0;i<sizeof(targets)/sizeof(targets[0]);i++){
   CHECK(!vt_subtitle_reader_open(r,&factory,index,targets[i]*1000+250,&cancel));
   expect_cue(r,targets[i]*1000+250,targets[i]);
  }
  /* EOF followed by backwards seek must recover without toggling the track. */
  CHECK(!vt_subtitle_reader_open(r,&factory,index,23990,&cancel));
  for(int i=0;i<1000&&!r->eof;i++){char t[8];vt_subtitle_reader_text(r,23990,t,sizeof(t));usleep(1000);}
  CHECK(r->eof);
  CHECK(!vt_subtitle_reader_open(r,&factory,index,5250,&cancel));expect_cue(r,5250,5);
  /* Interrupt an in-flight demux read, then seek without toggling subtitles. */
  CHECK(!vt_subtitle_reader_open(r,&factory,index,250,&cancel));expect_cue(r,250,0);
  block_reads=1;read_blocked=0;
  for(int i=0;i<1000&&!read_blocked;i++){char t[8];vt_subtitle_reader_text(r,18250,t,sizeof(t));usleep(1000);}
  if(read_blocked){CHECK(!vt_subtitle_reader_open(r,&factory,index,4250,&cancel));expect_cue(r,4250,4);}
  else {block_reads=0;fprintf(stderr,"No blocking read reached for this small fixture.\n");}
  if(sub_count==2){
   CHECK(!strcmp(audio[0].language,"ita")&&!strcmp(audio[1].language,"eng"));
   CHECK(!strcmp(subs[0].language,"ita")&&!strcmp(subs[1].language,"eng"));
   cue_prefix="ENG ";
   CHECK(!vt_subtitle_reader_open(r,&factory,subs[1].stream_index,5250,&cancel));expect_cue(r,5250,5);
   CHECK(!vt_subtitle_reader_open(r,&factory,subs[1].stream_index,18250,&cancel));expect_cue(r,18250,18);
   cue_prefix="";
  }
  vt_subtitle_reader_destroy(r);current_reader=NULL;CHECK(threads_created==before_threads+1);
  fprintf(stderr,"PASS subtitle seek/reopen/EOF: %s opens=%u\n",argv[f],opens);
 }
 printf("Subtitle production worker: %u checks passed.\n",checks);
}
