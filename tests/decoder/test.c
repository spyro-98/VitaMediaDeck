/* Production decoder source + real FFmpeg/NEON, kernel/CDRAM/GXM substitutes.
 * Tests memory ownership and synchronization, not physical Vita decode/energy. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <libavutil/pixfmt.h>
#define AV_PIX_FMT_VITA_NV12 AV_PIX_FMT_NV12
#define AV_PIX_FMT_VITA_YUV420P AV_PIX_FMT_YUV420P
#ifdef TEST_HW
#include "../../../vita-hw-decoder/src/internal/software_video.c"
#define api_start vita_hw_software_video_start
#define api_join vita_hw_software_video_join
#define api_fence vita_hw_software_video_render_complete
#else
#include "../../../vita-sw-decoder/src/internal/software_video.c"
#define api_start vita_sw_software_video_start
#define api_join vita_sw_software_video_join
#define api_fence vita_sw_software_video_render_complete
#endif
static unsigned checks;
static const char *fixture;
#define CHECK(x) do{checks++;assert(x);}while(0)
static struct {void *p;unsigned bytes;} blocks[256];
static unsigned allocations,frees,live_bytes,failure_nth,attempts,gpu_waits;
static atomic_uint poll_delays,event_waits,event_sleeps;
static pthread_mutex_t event_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t event_cond=PTHREAD_COND_INITIALIZER;
static unsigned event_bits,event_live;
uint64_t sceKernelGetProcessTimeWide(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
int sceKernelDelayThread(unsigned us){atomic_fetch_add(&poll_delays,1);usleep(us);return 0;}
int sceKernelGetThreadCurrentPriority(void){return 65;}
SceUID sceKernelGetThreadId(void){return 1;}
int sceKernelChangeThreadPriority(SceUID id,int pri){(void)id;(void)pri;return 0;}
int vita_hw_decoder_runtime_prepare(void){return 0;}
int vita_sw_decoder_runtime_prepare(void){return 0;}
void log_printf(const char *format,...){(void)format;}
SceUID sceKernelCreateEventFlag(const char *n,int a,int bits,void *o){(void)n;(void)a;(void)o;pthread_mutex_lock(&event_mutex);assert(!event_live);event_live=1;event_bits=bits;pthread_mutex_unlock(&event_mutex);return 3;}
int sceKernelDeleteEventFlag(SceUID id){assert(id==3);pthread_mutex_lock(&event_mutex);assert(event_live);event_live=event_bits=0;pthread_mutex_unlock(&event_mutex);return 0;}
int sceKernelSetEventFlag(SceUID id,unsigned bits){assert(id==3);pthread_mutex_lock(&event_mutex);assert(event_live);event_bits|=bits;pthread_cond_broadcast(&event_cond);pthread_mutex_unlock(&event_mutex);return 0;}
int sceKernelClearEventFlag(SceUID id,unsigned bits){assert(id==3);pthread_mutex_lock(&event_mutex);assert(event_live);event_bits&=bits;pthread_mutex_unlock(&event_mutex);return 0;}
int sceKernelWaitEventFlag(SceUID id,unsigned bits,unsigned mode,unsigned *out,SceUInt *timeout){
 assert(id==3&&mode==(SCE_EVENT_WAITOR|SCE_EVENT_WAITCLEAR_PAT));atomic_fetch_add(&event_waits,1);
 pthread_mutex_lock(&event_mutex);struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);
 deadline.tv_nsec+=(long)*timeout*1000;deadline.tv_sec+=deadline.tv_nsec/1000000000;deadline.tv_nsec%=1000000000;
 int ret=0;while(!(event_bits&bits)&&!ret){atomic_fetch_add(&event_sleeps,1);ret=pthread_cond_timedwait(&event_cond,&event_mutex,&deadline);}
 if(out)*out=event_bits;event_bits&=~bits;pthread_mutex_unlock(&event_mutex);return ret?-1:0;
}
SceUID sceKernelAllocMemBlock(const char *name,int type,unsigned bytes,SceKernelAllocMemBlockOpt *opt){
 (void)name;assert(type==1&&opt->alignment==CDRAM_ALIGNMENT);attempts++;
 if(failure_nth&&attempts==failure_nth)return -1;
 for(int i=0;i<256;i++)if(!blocks[i].p){blocks[i].p=calloc(1,bytes);assert(blocks[i].p);blocks[i].bytes=bytes;allocations++;live_bytes+=bytes;return i+1;}
 abort();
}
int sceKernelGetMemBlockBase(SceUID id,void **p){assert(id>0&&id<=256);*p=blocks[id-1].p;return 0;}
int sceKernelFreeMemBlock(SceUID id){assert(id>0&&id<=256&&blocks[id-1].p);live_bytes-=blocks[id-1].bytes;free(blocks[id-1].p);blocks[id-1].p=NULL;frees++;return 0;}
int sceGxmMapMemory(void*p,unsigned bytes,unsigned flags){(void)bytes;(void)flags;assert(p);return 0;}
int sceGxmUnmapMemory(void*p){assert(p);return 0;}
int sceGxmTextureInitLinear(SceGxmTexture*t,void*p,int fmt,unsigned w,unsigned h,unsigned m){(void)fmt;(void)m;t->pixels=p;t->w=w;t->h=h;return 0;}
void vita2d_wait_rendering_done(void){gpu_waits++;}
void vita2d_draw_texture_scale(const vita2d_texture*t,float a,float b,float c,float d){(void)t;(void)a;(void)b;(void)c;(void)d;}
void vita2d_texture_set_filters(vita2d_texture*t,int a,int b){(void)t;(void)a;(void)b;}
void vita2d_draw_texture_part_scale(const vita2d_texture*t,float a,float b,float c,float d,float e,float f,float g,float h){(void)t;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;}
static void state_init(SoftwareVideoState *s){memset(s,0,sizeof(*s));s->frame_event=-1;s->display_slot=s->retire_slot=-1;s->queue_capacity=SOFTWARE_VIDEO_COPY_FRAME_SLOTS;for(int i=0;i<SOFTWARE_VIDEO_FRAME_SLOTS;i++)s->slots[i].memblock=-1;for(int i=0;i<SOFTWARE_VIDEO_ZERO_COPY_SURFACES;i++){s->zero_surfaces[i].memblock=-1;s->zero_surfaces[i].release_event=-1;}}
static void reference(uint8_t *out,unsigned pitch,unsigned height,const AVFrame *f){
 memset(out,16,pitch*height);uint8_t *uv=out+pitch*height;memset(uv,128,pitch*(height/2));
 for(int y=0;y<f->height;y++)memcpy(out+y*pitch,f->data[0]+(ptrdiff_t)y*f->linesize[0],f->width);
#if SOFTWARE_VIDEO_PLANAR_COPY_ENABLED
 for(int p=1;p<=2;p++)for(int y=0;y<(f->height+1)/2;y++)for(int x=0;x<(f->width+1)/2;x++)
  uv[(p-1)*(pitch/2)*(height/2)+y*(pitch/2)+x]=f->data[p][(ptrdiff_t)y*f->linesize[p]+x];
#else
 for(int y=0;y<(f->height+1)/2;y++)for(int x=0;x<(f->width+1)/2;x++){uv[y*pitch+2*x]=f->data[1][(ptrdiff_t)y*f->linesize[1]+x];uv[y*pitch+2*x+1]=f->data[2][(ptrdiff_t)y*f->linesize[2]+x];}
#endif
}
static void pixels_test(void){
 CHECK(hardware_raster_supported(1920,1080));CHECK(hardware_raster_supported(1920,1088));
 CHECK(!hardware_raster_supported(1921,1080)&&!hardware_raster_supported(1920,1089));
 static const int dimensions[][2]={{1,1},{2,2},{15,15},{16,16},{17,17},{31,33},{33,31},{65,37},{640,360},{854,480},{960,544},{1280,720},{1920,1080},{1920,1088}};
 for(unsigned n=0;n<sizeof(dimensions)/sizeof(*dimensions);n++)for(int flip=0;flip<2;flip++){
  int w=dimensions[n][0],h=dimensions[n][1];AVFrame *f=av_frame_alloc();f->format=AV_PIX_FMT_YUV420P;f->width=w;f->height=h;CHECK(av_frame_get_buffer(f,32)==0);
  for(int p=0;p<3;p++)for(int y=0;y<(p?(h+1)/2:h);y++)for(int x=0;x<(p?(w+1)/2:w);x++)f->data[p][y*f->linesize[p]+x]=(x*11+y*7+p*67)&255;
  if(flip)for(int p=0;p<3;p++){f->data[p]+=((p?(h+1)/2:h)-1)*f->linesize[p];f->linesize[p]=-f->linesize[p];}
  SoftwareVideoState s;state_init(&s);s.content_width=w;s.content_height=h;s.pitch=s.texture_width=(w+15)&~15;s.texture_height=(h+15)&~15;s.frame_bytes=s.pitch*s.texture_height*3/2;
  uint8_t *out=malloc(s.frame_bytes+32),*ref=malloc(s.frame_bytes);memset(out,0xa5,s.frame_bytes+32);s.slots[0].pixels=out+16;
  reference(ref,s.pitch,s.texture_height,f);CHECK(copy_yuv420_frame(&s,0,f)==0);CHECK(!memcmp(ref,out+16,s.frame_bytes));
  for(int i=0;i<16;i++)CHECK(out[i]==0xa5&&out[s.frame_bytes+16+i]==0xa5);
  int stride=f->linesize[1];f->linesize[1]=0;CHECK(copy_yuv420_frame(&s,0,f)<0);f->linesize[1]=stride;
  uint8_t *saved=f->data[2];f->data[2]=NULL;CHECK(copy_yuv420_frame(&s,0,f)<0);f->data[2]=saved;
  free(out);free(ref);av_frame_free(&f);
 }
 printf("Pixel conversion: aligned/padded/odd sizes, negative strides, UV order, guards and invalid planes passed.\n");
}
static void *reserve(void *p){SoftwareVideoState*s=p;return (void*)(intptr_t)reserve_slot(s);}
static void wait_until_waiting(unsigned before){uint64_t until=sceKernelGetProcessTimeWide()+200000;while(atomic_load(&event_waits)==before&&sceKernelGetProcessTimeWide()<until)usleep(100);CHECK(atomic_load(&event_waits)>before);}
static void synchronization_test(void){
 SoftwareVideoState s;state_init(&s);volatile int cancel=0;s.cancel=&cancel;s.frame_event=sceKernelCreateEventFlag("test",0,0,NULL);
 /* A signal in the predicate-to-wait window must not be cleared by wait(). */
 video_wait_prepare(&s);video_notify(&s);unsigned sleeps=atomic_load(&event_sleeps);video_wait(&s);CHECK(atomic_load(&event_sleeps)==sleeps);
 for(int pass=0;pass<50;pass++){
  for(int i=0;i<s.queue_capacity;i++)s.slots[i].state=SLOT_READY;s.stop=0;
  unsigned before=atomic_load(&event_waits);pthread_t t;CHECK(!pthread_create(&t,NULL,reserve,&s));wait_until_waiting(before);
  slots_lock(&s);s.slots[3].state=SLOT_DISPLAYING;s.retire_slot=3;slots_unlock(&s);api_fence(&s);
  void *result=NULL;CHECK(!pthread_join(t,&result));CHECK((intptr_t)result==3&&s.slots[3].state==SLOT_WRITING);
 }
 for(int i=0;i<s.queue_capacity;i++)s.slots[i].state=SLOT_READY;
 unsigned before=atomic_load(&event_waits);pthread_t t;CHECK(!pthread_create(&t,NULL,reserve,&s));wait_until_waiting(before);
 __atomic_store_n(&s.stop,1,__ATOMIC_RELEASE);video_notify(&s);void *result=NULL;CHECK(!pthread_join(t,&result));CHECK((intptr_t)result==-1);
 sceKernelDeleteEventFlag(s.frame_event);s.frame_event=-1;
 before=atomic_load(&poll_delays);video_wait(&s);CHECK(atomic_load(&poll_delays)>before);
 printf("Queue waits: predicate/signal race, 50 GPU-fence releases, stop wake and polling fallback passed.\n");
}

typedef struct {AVCodecContext *codec;AVFrame *frame;} SurfaceJob;
static void *reserve_surface(void *p){SurfaceJob *j=p;return (void*)(intptr_t)zero_copy_get_buffer2(j->codec,j->frame,0);}
static void surface_test(void){
 SoftwareVideoState s;state_init(&s);volatile int cancel=0;s.cancel=&cancel;s.zero_copy=1;
 s.pitch=s.texture_width=s.texture_height=64;s.frame_bytes=6144;
 s.frame_event=sceKernelCreateEventFlag("surface",0,0,NULL);
 uint8_t *pixels=av_malloc(s.frame_bytes);CHECK(pixels);
 for(int i=0;i<SOFTWARE_VIDEO_ZERO_COPY_SURFACES;i++){s.zero_surfaces[i].in_use=1;s.zero_surfaces[i].release_event=s.frame_event;}
 s.zero_surfaces[0].pixels=pixels;s.zero_surfaces[0].in_use=0;
 AVCodecContext *codec=avcodec_alloc_context3(NULL);codec->opaque=&s;
 AVFrame *held=av_frame_alloc();held->format=AV_PIX_FMT_YUV420P;held->width=held->height=64;
 CHECK(zero_copy_get_buffer2(codec,held,0)==0);CHECK(s.zero_surfaces[0].in_use);
 s.slots[0].frame=av_frame_alloc();CHECK(av_frame_ref(s.slots[0].frame,held)==0);
 av_frame_unref(held);CHECK(s.zero_surfaces[0].in_use); /* GPU still owns its lease. */
 AVFrame *next=av_frame_alloc();next->format=AV_PIX_FMT_YUV420P;next->width=next->height=64;
 SurfaceJob job={codec,next};pthread_t thread;unsigned before=atomic_load(&event_waits);
 CHECK(!pthread_create(&thread,NULL,reserve_surface,&job));wait_until_waiting(before);
 s.retire_slot=0;s.slots[0].state=SLOT_DISPLAYING;api_fence(&s);
 void *result=NULL;CHECK(!pthread_join(thread,&result));CHECK((intptr_t)result==0&&next->data[0]==pixels);
 av_frame_free(&next);CHECK(!s.zero_surfaces[0].in_use);
 av_frame_free(&held);av_frame_free(&s.slots[0].frame);avcodec_free_context(&codec);
 av_free(pixels);sceKernelDeleteEventFlag(s.frame_event);
 printf("Direct surfaces: shared AVFrame ownership, GPU-fence release and pool notification passed.\n");
}
static void allocation_test(void){
 /* Actual CDRAM pool sizes and complete release for HD and Full HD. */
 for(int high=0;high<2;high++){
  SoftwareVideoState pool;state_init(&pool);
  pool.texture_width=pool.pitch=high?1920:1280;pool.texture_height=high?1088:720;
  pool.frame_bytes=pool.pitch*pool.texture_height*3/2;
  CHECK(!allocate_zero_copy_pool(&pool));CHECK(pool.zero_surface_capacity==(high?12:20));
  CHECK(live_bytes==(uint64_t)pool.zero_surfaces[0].allocation_size*(high?12:20));
  if(high)CHECK(live_bytes==36ULL*1024*1024);
  release_frame_buffers(&pool);CHECK(!live_bytes);
 }

 AVFormatContext *demux=avformat_alloc_context();AVStream *stream=avformat_new_stream(demux,NULL);CHECK(stream);stream->time_base=(AVRational){1,1000};stream->codecpar->codec_type=AVMEDIA_TYPE_VIDEO;stream->codecpar->codec_id=AV_CODEC_ID_H264;stream->codecpar->width=64;stream->codecpar->height=64;stream->codecpar->format=AV_PIX_FMT_YUV420P;
 SoftwareVideoState s;state_init(&s);s.demux=demux;s.texture_width=s.pitch=64;s.texture_height=64;s.frame_bytes=6144;s.content_width=s.content_height=64;
 s.hardware_direct_available=1;unsigned before=allocations;
 CHECK(open_h264_decoder(&s,stream,avcodec_find_decoder(AV_CODEC_ID_H264),1)==0);
 CHECK(allocations==before);for(int i=0;i<SOFTWARE_VIDEO_COPY_FRAME_SLOTS;i++)CHECK(!s.slots[i].pixels);
 avcodec_free_context(&s.decoder);s.hardware_direct_available=0;
 CHECK(open_h264_decoder(&s,stream,avcodec_find_decoder(AV_CODEC_ID_H264),0)==0);CHECK(allocations==before+SOFTWARE_VIDEO_COPY_FRAME_SLOTS);
 avcodec_free_context(&s.decoder);release_frame_buffers(&s);CHECK(live_bytes==0);
 #ifdef TEST_HW
 /* A partially allocated direct pool must be freed before copied fallback.
  * Real worker opens libavcodec, and reads a real MP4, not a fake decoder. */
 avformat_free_context(demux);demux=NULL;
 CHECK(avformat_open_input(&demux,fixture,NULL,NULL)==0);CHECK(avformat_find_stream_info(demux,NULL)>=0);
 volatile int cancel=0,gate=0;failure_nth=attempts+7;
 CHECK(api_start(&s,demux,0,64,64,30,0,&cancel,&gate,0)==0);
 CHECK(!s.hardware_direct_available&&!s.zero_copy);CHECK(live_bytes==SOFTWARE_VIDEO_COPY_FRAME_SLOTS*CDRAM_ALIGNMENT);
 for(int i=0;i<SOFTWARE_VIDEO_ZERO_COPY_SURFACES;i++)CHECK(!s.zero_surfaces[i].pixels);
 cancel=1;api_join(&s);failure_nth=0;CHECK(!live_bytes&&!event_live);
 #endif
 avformat_free_context(demux);CHECK(allocations==frees);printf("Allocation: no copied ring for direct output; lazy CPU ring and complete teardown passed.\n");
}
int main(int argc,char **argv){alarm(25);CHECK(argc==2);fixture=argv[1];pixels_test();synchronization_test();surface_test();if(!getenv("VMD_WAIT_ONLY"))allocation_test();CHECK(!live_bytes);printf("Decoder production checks: %u passed (%s).\n",checks,
#ifdef TEST_HW
"HW adapter"
#else
"SW adapter"
#endif
);return 0;}
