/* Real player-open worker + real IPTV producer/session. No Vita code executes. */
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#ifdef VMD_CANCEL_BASELINE
#include VMD_CANCEL_BASELINE
#else
#include "../../../src/media/hw_player_screen.c"
#endif
static unsigned checks;
#define CHECK(x) do{checks++;assert(x);}while(0)
static int blocked,exited,decoder_entered,decoder_stopped,producer_chunks;
static VtDecoderStreamHandle reader;
static int atomic_read(const int *p){return __atomic_load_n(p,__ATOMIC_ACQUIRE);}
static void atomic_write(int *p,int v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
uint64_t sceKernelGetProcessTimeWide(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;}
int sceKernelDelayThread(unsigned us){struct timespec t={us/1000000,(us%1000000)*1000};return nanosleep(&t,NULL);}
void log_printf(const char *format,...){(void)format;}
static void *producer_open(const char *url,const VtIptvSourceConfig *cfg){(void)url;(void)cfg;return &producer_chunks;}
static int producer_next(void *ctx,uint64_t now,volatile int *cancel,unsigned char **data,size_t *size,int *finite,uint64_t *duration){
 (void)ctx;(void)now;*data=NULL;*size=0;*finite=0;*duration=0;
 if(producer_chunks>0){producer_chunks--;*data=calloc(1,188);assert(*data);(*data)[0]=0x47;*size=188;return 0;}
 atomic_write(&blocked,1);
 while(!__atomic_load_n(cancel,__ATOMIC_ACQUIRE))sceKernelDelayThread(1000);
 atomic_write(&exited,1);return VT_IPTV_CANCELLED;
}
static void producer_close(void *ctx){(void)ctx;}
int vt_iptv_http_fetch(void *ctx,const char *url,size_t limit,unsigned char **data,size_t *size,char *effective,size_t capacity,volatile int *cancel){
 (void)ctx;(void)url;(void)limit;(void)data;(void)size;(void)effective;(void)capacity;(void)cancel;return VT_IPTV_INVALID;
}
void vt_decoder_get_status(VtDecoderPlayer *p,VtDecoderPlayerStatus *s){(void)p;memset(s,0,sizeof(*s));}
void vt_decoder_request_stop(VtDecoderPlayer *p){(void)p;atomic_write(&decoder_stopped,1);}
int vt_decoder_open_live(VtDecoderPlayer *p,const VtDecoderPlayerConfig *cfg){
 (void)p;assert(!cfg->stream.open(cfg->stream.opaque,&reader));atomic_write(&decoder_entered,1);
 unsigned char bytes[188];while(reader.read(reader.opaque,bytes,sizeof(bytes))>0){}
 reader.close(reader.opaque);return AVERROR_EXIT;
}
int vt_decoder_open(VtDecoderPlayer *p,const VtDecoderPlayerConfig *cfg){(void)p;(void)cfg;return -1;}
static void wait_flag(const int *p){uint64_t end=sceKernelGetProcessTimeWide()+3000000;while(!atomic_read(p)&&sceKernelGetProcessTimeWide()<end)sceKernelDelayThread(1000);CHECK(atomic_read(p));}
static void *open_worker(void *ctx){return (void*)(intptr_t)open_task(ctx);}
static void cancel_at(int chunks){
 blocked=exited=decoder_entered=decoder_stopped=0;producer_chunks=chunks;
 VtIptvSourceConfig cfg={.buffer_limit=64*188,.segment_limit=188,.startup_segments=6,
  .producer={producer_open,producer_next,producer_close,NULL}};
 VtIptvSession *session=vt_iptv_session_create("https://example.org/live.m3u8",&cfg);CHECK(session);
 volatile int cancel=0;OpenTask task={.player=(void*)1,.live=1,.live_session=session};
 task.config.cancel_flag=&cancel;vt_iptv_session_factory(session,&task.config.stream);
 pthread_t worker;CHECK(!pthread_create(&worker,NULL,open_worker,&task));wait_flag(&blocked);
 if(chunks)wait_flag(&decoder_entered);else CHECK(!atomic_read(&decoder_entered));
 uint64_t start=sceKernelGetProcessTimeWide();
#ifdef VMD_CANCEL_BASELINE
 cancel_player_operation(task.player); /* The previous loading callback. */
#else
 cancel_open_operation(&task);
#endif
 __atomic_store_n(&cancel,1,__ATOMIC_RELEASE);void *result;CHECK(!pthread_join(worker,&result));
 if(!atomic_read(&exited)) {
  vt_iptv_session_stop_and_wait(session);vt_iptv_session_destroy(session);
  fprintf(stderr,"REGRESSION: player worker returned while producer still waits for network; UI cleanup would join it.\n");exit(1);
 }
 CHECK((intptr_t)result<0&&atomic_read(&exited)&&atomic_read(&decoder_stopped));
 CHECK(sceKernelGetProcessTimeWide()-start<500000); /* Synthetic cooperative I/O only. */
 CHECK(vt_iptv_session_event(session)==VT_IPTV_CANCELLED);CHECK(vt_iptv_session_stopped(session));
 vt_iptv_session_stop_and_wait(session); /* Idempotent: destroy must not rejoin. */
 start=sceKernelGetProcessTimeWide();vt_iptv_session_destroy(session);CHECK(sceKernelGetProcessTimeWide()-start<100000);
}
int main(void){for(int i=0;i<20;i++){cancel_at(0);cancel_at(6);}printf("IPTV actual player-open cancellation: %u checks; prebuffer and decoder read; producer joined before UI cleanup\n",checks);}
