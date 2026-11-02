#include "common/iptv_time.h"
#include "media/iptv_program_async.h"
#include "network/iptv_manifest.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#endif
struct VtIptvProgramJob {
 VtIptvProgramFetch fetch;
 unsigned refs;
 volatile int cancel;
 int done,result,refresh;
 unsigned count;size_t stride;
 uint64_t keys[VT_IPTV_GUIDE_BATCH_MAX];unsigned epoch;
 int ready[VT_IPTV_GUIDE_BATCH_MAX],results[VT_IPTV_GUIDE_BATCH_MAX];
 VtIptvProgram program[VT_IPTV_GUIDE_BATCH_MAX];
 unsigned char context[];
};
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static pthread_t worker_thread;
static int started,stopping;
static VtIptvProgramJob *pending,*running;
/* Shared by the player and grid. Completed results survive view cancellation;
 * missing matches and transient failures have separate retry windows. Contexts
 * are hashed only in memory: authenticated playlist URLs are never persisted. */
#define PROGRAM_CACHE_COUNT 64
typedef struct {
 uint64_t key,used;VtIptvProgramFetch fetch;time_t saved,until;
 int result;VtIptvProgram program;
} ProgramEntry;
static ProgramEntry entries[PROGRAM_CACHE_COUNT];
static uint64_t cache_clock;
static unsigned cache_epoch;
static uint64_t context_key(const void *context,size_t size){
 uint64_t key=1469598103934665603ULL;const unsigned char *p=context;
 for(size_t i=0;i<size;i++){key^=p[i];key*=1099511628211ULL;}return key?key:1;
}
static time_t program_until(const VtIptvProgram *p,int result,time_t now){
 time_t until=now+(result<0||p->stale?300:p->guide_status==VT_IPTV_GUIDE_MISSING||p->guide_status==VT_IPTV_GUIDE_NO_CHANNEL?21600:900);
 if(result>=0){
  if(p->available&&p->end>now&&p->end<until)until=(time_t)p->end;
  if(p->next_start>now&&p->next_start<until)until=(time_t)p->next_start;
 }
 return until;
}
static void remember(VtIptvProgramJob *job,unsigned row,int result){
 if(result==VT_IPTV_CANCELLED)return;
 pthread_mutex_lock(&mutex);
 if(job->epoch!=cache_epoch){pthread_mutex_unlock(&mutex);return;}
 ProgramEntry *slot=&entries[0];
 for(unsigned i=0;i<PROGRAM_CACHE_COUNT;i++){
  if(entries[i].key==job->keys[row]&&entries[i].fetch==job->fetch){slot=&entries[i];break;}
  if(entries[i].used<slot->used)slot=&entries[i];
 }
 *slot=(ProgramEntry){.key=job->keys[row],.fetch=job->fetch,.used=++cache_clock,
  .saved=vt_iptv_time_now(),.until=program_until(&job->program[row],result,vt_iptv_time_now()),.result=result,.program=job->program[row]};
 pthread_mutex_unlock(&mutex);
}
static void drop(VtIptvProgramJob *job){if(job&&__atomic_sub_fetch(&job->refs,1,__ATOMIC_ACQ_REL)==0)free(job);}
static void *worker(void *unused){
 (void)unused;
#ifdef __vita__
 sceKernelChangeThreadPriority(sceKernelGetThreadId(),191);
#endif
 for(;;){
  pthread_mutex_lock(&mutex);
  while(!stopping&&!pending)pthread_cond_wait(&changed,&mutex);
  if(stopping){pthread_mutex_unlock(&mutex);break;}
  VtIptvProgramJob *job=pending;pending=NULL;running=job;
  pthread_mutex_unlock(&mutex);
  for(unsigned i=0;i<job->count;i++){
   if(job->ready[i])continue;
   int result=__atomic_load_n(&job->cancel,__ATOMIC_ACQUIRE)?VT_IPTV_CANCELLED:
    job->fetch(job->context+i*job->stride,&job->program[i],&job->cancel,job->refresh);
   remember(job,i,result);
   job->results[i]=result;__atomic_store_n(&job->ready[i],1,__ATOMIC_RELEASE);
   if(i==0)job->result=result;
   if(result==VT_IPTV_CANCELLED)break;
  }
  __atomic_store_n(&job->done,1,__ATOMIC_RELEASE);
  pthread_mutex_lock(&mutex);running=NULL;pthread_mutex_unlock(&mutex);
  drop(job);
 }
 return NULL;
}
VtIptvProgramJob *vt_iptv_program_request_batch(VtIptvProgramFetch fetch,const void *context,size_t stride,unsigned count,int refresh){
 if(!fetch||stride>16384||!count||count>VT_IPTV_GUIDE_BATCH_MAX||
    (stride&&!context)||(context&&!stride))return NULL;
 size_t size=stride*count;
 VtIptvProgramJob *job=calloc(1,sizeof(*job)+size);if(!job)return NULL;
 job->refs=2;job->fetch=fetch;job->refresh=!!refresh;job->count=count;job->stride=stride;
 if(size)memcpy(job->context,context,size);
 for(unsigned i=0;i<count;i++)job->keys[i]=context_key(job->context+i*stride,stride);
 pthread_mutex_lock(&mutex);
 if(refresh){memset(entries,0,sizeof(entries));cache_epoch++;}
 job->epoch=cache_epoch;
 time_t now=vt_iptv_time_now();unsigned hits=0;
 for(unsigned row=0;row<count;row++)for(unsigned i=0;i<PROGRAM_CACHE_COUNT;i++){
  ProgramEntry *e=&entries[i];
  if(e->key==job->keys[row]&&e->fetch==fetch&&now<e->until&&now>=e->saved){
   job->program[row]=e->program;job->program[row].cached=1;job->results[row]=e->result;
   job->ready[row]=1;e->used=++cache_clock;hits++;if(!row)job->result=e->result;break;
  }
 }
 if(hits==count){job->done=1;job->refs=1;pthread_mutex_unlock(&mutex);return job;}
 if(!started){
  pthread_attr_t attr;int r=pthread_attr_init(&attr);
  if(!r){r=pthread_attr_setstacksize(&attr,0x40000);if(!r)r=pthread_create(&worker_thread,&attr,worker,NULL);pthread_attr_destroy(&attr);}
  if(r){pthread_mutex_unlock(&mutex);free(job);return NULL;}
  started=1;
 }
 if(stopping){pthread_mutex_unlock(&mutex);free(job);return NULL;}
 if(running)__atomic_store_n(&running->cancel,1,__ATOMIC_RELEASE);
 if(pending){__atomic_store_n(&pending->cancel,1,__ATOMIC_RELEASE);drop(pending);}
 pending=job;pthread_cond_signal(&changed);pthread_mutex_unlock(&mutex);
 return job;
}
VtIptvProgramJob *vt_iptv_program_request(VtIptvProgramFetch fetch,const void *ctx,size_t size,int refresh){
 return vt_iptv_program_request_batch(fetch,ctx,size,1,refresh);
}
int vt_iptv_program_poll_row(VtIptvProgramJob *job,unsigned row,VtIptvProgram *out){
 if(!job||!out||row>=job->count)return VT_IPTV_INVALID;
 if(!__atomic_load_n(&job->ready[row],__ATOMIC_ACQUIRE))return __atomic_load_n(&job->cancel,__ATOMIC_ACQUIRE)?VT_IPTV_CANCELLED:0;
 if(job->results[row]<0)return job->results[row];
 *out=job->program[row];return 1;
}
int vt_iptv_program_poll(VtIptvProgramJob *job,VtIptvProgram *out){
 if(!job||!out)return VT_IPTV_INVALID;
 if(__atomic_load_n(&job->cancel,__ATOMIC_ACQUIRE))return VT_IPTV_CANCELLED;
 if(!__atomic_load_n(&job->done,__ATOMIC_ACQUIRE))return 0;
 if(job->result<0)return job->result;
 *out=job->program[0];return 1;
}
void vt_iptv_program_release(VtIptvProgramJob *job){
 if(!job)return;
 __atomic_store_n(&job->cancel,1,__ATOMIC_RELEASE);drop(job);
}
void vt_iptv_program_shutdown(void){
 pthread_mutex_lock(&mutex);
 if(!started){pthread_mutex_unlock(&mutex);return;}
 stopping=1;
 if(running)__atomic_store_n(&running->cancel,1,__ATOMIC_RELEASE);
 if(pending){__atomic_store_n(&pending->cancel,1,__ATOMIC_RELEASE);drop(pending);pending=NULL;}
 pthread_cond_signal(&changed);pthread_mutex_unlock(&mutex);
 pthread_join(worker_thread,NULL);
 pthread_mutex_lock(&mutex);started=stopping=0;memset(entries,0,sizeof(entries));pthread_mutex_unlock(&mutex);
}
