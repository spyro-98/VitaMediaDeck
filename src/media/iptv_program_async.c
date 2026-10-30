#include "media/iptv_program_async.h"
#include "network/iptv_manifest.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#endif
struct VtIptvProgramJob {
 VtIptvProgramFetch fetch;
 unsigned refs;
 volatile int cancel;
 int done,result,refresh;
 unsigned count;size_t stride;
 int ready[VT_IPTV_GUIDE_BATCH_MAX],results[VT_IPTV_GUIDE_BATCH_MAX];
 VtIptvProgram program[VT_IPTV_GUIDE_BATCH_MAX];
 unsigned char context[];
};
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static pthread_t worker_thread;
static int started,stopping;
static VtIptvProgramJob *pending,*running;
static void drop(VtIptvProgramJob *job){if(job&&__atomic_sub_fetch(&job->refs,1,__ATOMIC_ACQ_REL)==0)free(job);}
static void *worker(void *unused){
 (void)unused;
#ifdef __vita__
 sceKernelChangeThreadPriority(sceKernelGetThreadId(),192);
#endif
 for(;;){
  pthread_mutex_lock(&mutex);
  while(!stopping&&!pending)pthread_cond_wait(&changed,&mutex);
  if(stopping){pthread_mutex_unlock(&mutex);break;}
  VtIptvProgramJob *job=pending;pending=NULL;running=job;
  pthread_mutex_unlock(&mutex);
  for(unsigned i=0;i<job->count;i++){
   int result=__atomic_load_n(&job->cancel,__ATOMIC_ACQUIRE)?VT_IPTV_CANCELLED:
    job->fetch(job->context+i*job->stride,&job->program[i],&job->cancel,job->refresh);
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
 pthread_mutex_lock(&mutex);
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
 if(__atomic_load_n(&job->cancel,__ATOMIC_ACQUIRE))return VT_IPTV_CANCELLED;
 if(!__atomic_load_n(&job->ready[row],__ATOMIC_ACQUIRE))return 0;
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
 pthread_mutex_lock(&mutex);started=stopping=0;pthread_mutex_unlock(&mutex);
}
