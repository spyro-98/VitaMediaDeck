#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
int sceIoSync(const char *,int);
#include "../../src/history/playback_history.c"
#include "../../src/media/podcast_resume.c"
static unsigned checks;
#define CHECK(x) do {__atomic_add_fetch(&checks,1,__ATOMIC_RELAXED);assert(x);}while(0)
static uint64_t clock_us;
static int thread_enabled,fail_write,fail_sync,fail_rename,partial_write;
static unsigned write_calls,write_bytes,volume_syncs,fd_syncs,callback_events;
static SceKernelThreadEntry entry;
static SceKernelCallbackFunction callback_fn;
static pthread_t worker_thread,caller_thread;
static int async_mode,io_on_caller;
static pthread_mutex_t source_mutex=PTHREAD_MUTEX_INITIALIZER;
static VtPodcastResumePoint source;
static int source_valid;
static void pause_ms(unsigned n){struct timespec t={n/1000,(long)(n%1000)*1000000};nanosleep(&t,NULL);}
void log_printf(const char *format,...){(void)format;}
uint64_t sceKernelGetProcessTimeWide(void){return __atomic_load_n(&clock_us,__ATOMIC_ACQUIRE);}
int sceIoOpen(const char *p,int flags,int mode){return open(p,flags,mode);}
int sceIoRead(int fd,void *p,unsigned n){return (int)read(fd,p,n);}
int sceIoWrite(int fd,const void *p,unsigned n){
 if(async_mode&&pthread_equal(pthread_self(),caller_thread))__atomic_store_n(&io_on_caller,1,__ATOMIC_RELEASE);
 if(fail_write)return -9;
 if(partial_write&&n>7)n=7;
 int ret=(int)write(fd,p,n);
 if(ret>0){__atomic_add_fetch(&write_calls,1,__ATOMIC_RELAXED);__atomic_add_fetch(&write_bytes,(unsigned)ret,__ATOMIC_RELAXED);}return ret;
}
int sceIoClose(int f){return close(f);}
int sceIoSyncByFd(int f,int flags){(void)flags;__atomic_add_fetch(&fd_syncs,1,__ATOMIC_RELAXED);return fail_sync?-10:fsync(f);}
int sceIoSync(const char *p,int flags){(void)p;(void)flags;volume_syncs++;return 0;}
int sceIoRemove(const char *p){return unlink(p);}
int sceIoRename(const char *a,const char *b){if(fail_rename&&strstr(a,"podcast_resume.tmp"))return -11;return rename(a,b);}
int sceIoMkdir(const char *p,int m){return mkdir(p,m);}
int sceIoGetstat(const char *p,SceIoStat *s){struct stat st;int ret=stat(p,&st);if(!ret)s->st_size=st.st_size;return ret;}
int sceKernelCreateThread(const char *n,SceKernelThreadEntry e,int priority,unsigned stack,unsigned a,int cpu,void *opt){
 (void)n;(void)a;(void)cpu;(void)opt;CHECK(priority==0x10000140&&stack==0x8000);entry=e;return thread_enabled?99:-1;
}
static void *thread_main(void *ctx){(void)ctx;entry(0,NULL);return NULL;}
int sceKernelStartThread(SceUID id,SceSize size,void *arg){(void)size;(void)arg;CHECK(id==99);return pthread_create(&worker_thread,NULL,thread_main,NULL);}
int sceKernelWaitThreadEnd(SceUID id,int *status,void *timeout){(void)status;(void)timeout;CHECK(id==99);return pthread_join(worker_thread,NULL);}
int sceKernelDeleteThread(SceUID id){CHECK(id==99);return 0;}
int sceKernelExitThread(int status){return status;}
int sceKernelDelayThreadCB(unsigned delay){
 (void)delay;pause_ms(1);unsigned flags=__atomic_exchange_n(&callback_events,0,__ATOMIC_ACQ_REL);
 if(flags&&callback_fn)callback_fn(1,1,(int)flags,NULL);return 0;
}
int sceKernelCreateCallback(const char *name,unsigned attr,SceKernelCallbackFunction cb,void *ctx){(void)name;(void)attr;(void)ctx;callback_fn=cb;return 3;}
int sceKernelDeleteCallback(SceUID id){CHECK(id==3);callback_fn=NULL;return 0;}
int scePowerRegisterCallback(int id){CHECK(id==3);return 0;}
int scePowerUnregisterCallback(int id){CHECK(id==3);return 0;}
static int sample(VtPodcastResumePoint *out){pthread_mutex_lock(&source_mutex);int valid=source_valid;*out=source;pthread_mutex_unlock(&source_mutex);return valid;}
static void set_source(const char *id,uint64_t position,uint64_t duration,uint64_t order,int valid){
 pthread_mutex_lock(&source_mutex);memset(&source,0,sizeof(source));snprintf(source.id,sizeof(source.id),"%s",id);
 source.position_ms=position;source.duration_ms=duration;source.history_order=order;source_valid=valid;pthread_mutex_unlock(&source_mutex);
}
static void tick(uint64_t us,int force){__atomic_store_n(&clock_us,us,__ATOMIC_RELEASE);checkpoint(force);}
static void clear_files(void){unlink(RESUME_PATH);unlink(RESUME_TEMP);unlink(RESUME_BACKUP);unlink(VT_PLAYBACK_HISTORY_PATH);unlink(VT_PLAYBACK_HISTORY_TEMP);unlink(VT_PLAYBACK_HISTORY_BACKUP);}
static void restart(void){CHECK(vt_playback_history_init()>=0);CHECK(vt_podcast_resume_init(sample)<0);}
static void file_put(const char *p,const void *data,size_t n){FILE*f=fopen(p,"wb");CHECK(f);CHECK(fwrite(data,1,n,f)==n);CHECK(!fclose(f));}
static void file_copy(const char *a,const char *b){unsigned char data[128];FILE*f=fopen(a,"rb");CHECK(f);size_t n=fread(data,1,sizeof(data),f);CHECK(!ferror(f));fclose(f);file_put(b,data,n);}
static int durable(uint64_t pos){pthread_mutex_lock(&mutex);int ok=have_saved&&saved.position_ms==pos&&saved_revision==revision;pthread_mutex_unlock(&mutex);return ok;}
int main(void){
 char fixture[]="/tmp/vmd-podcast-resume.XXXXXX";CHECK(mkdtemp(fixture));CHECK(!chdir(fixture));CHECK(!mkdir("ux0:data",0700));CHECK(!mkdir(VITAMEDIADECK_DATA_DIR,0700));
 caller_thread=pthread_self();const char *id="podcs12345678",*other="podcsabcdef01";
 restart();set_source(id,120000,3600000,0,1);tick(1000000,0);
 VtPodcastResumePoint point;CHECK(!load(RESUME_PATH,&point)&&point.position_ms==120000);CHECK(write_bytes==56&&fd_syncs==1&&volume_syncs==0);
 /* No UI pump: the saved point survives a process restart. */
 set_source(id,0,0,0,0);restart();CHECK(vt_playback_history_position(id,0)==120000);
 uint64_t order=vt_playback_history_order(id);CHECK(order>0);
 /* Slow/error startup and optimistic seek zero must never erase prior progress. */
 unsigned writes=write_calls;set_source(id,0,0,order,0);tick(30000000,1);CHECK(write_calls==writes);CHECK(vt_playback_history_position(id,0)==120000);
 CHECK(!vt_podcast_resume_finish(other,&point));
 set_source(id,125000,0,order,1);tick(31000000,0);CHECK(!load(RESUME_PATH,&point)&&point.duration_ms==3600000);
 /* No duplicate writes while paused; unchanged duration/position are coalesced. */
 writes=write_calls;for(unsigned i=0;i<500;i++)tick(32000000+i*1000000ULL,0);CHECK(write_calls==writes);
 /* Normal-stop generation wins over an older compact record, including rewind. */
 CHECK(!vt_playback_history_update(id,60000,3600000));set_source(id,0,0,0,0);restart();CHECK(vt_playback_history_position(id,0)==60000);
 order=vt_playback_history_order(id);set_source(id,65000,3600000,order,1);tick(600000000,1);
 CHECK(!vt_playback_history_update(id,0,3600000));set_source(id,0,0,0,0);restart();CHECK(vt_playback_history_position(id,0)==0);CHECK(vt_playback_history_order(id)>0);
 /* Rewind below the resume minimum remains a tombstone across repeated starts. */
 restart();CHECK(vt_playback_history_position(id,0)==0);
 /* Failed history persistence rolls back the generation, allowing recovery. */
 order=vt_playback_history_order(id);fail_sync=1;CHECK(vt_playback_history_update(id,70000,3600000)<0);fail_sync=0;CHECK(vt_playback_history_order(id)==order);
 set_source(id,80000,3600000,order,1);tick(700000000,1);set_source(id,0,0,0,0);restart();CHECK(vt_playback_history_position(id,0)==80000);
 /* Full storage at import still permits a memory-only resume. */
 clear_files();restart();set_source(id,85000,3600000,0,1);tick(750000000,1);
 set_source(id,0,0,0,0);fail_sync=1;restart();fail_sync=0;
 uint64_t restored=0;CHECK(vt_podcast_resume_restore(id,0,0,&restored)&&restored==85000);
 CHECK(!vt_podcast_resume_restore(other,0,0,&restored));CHECK(!vt_podcast_resume_restore(id,9,0,&restored));
 CHECK(vt_podcast_resume_restore(id,0,90000,&restored)&&!restored);
 order=vt_playback_history_order(id);
 /* A failed checkpoint is retried; it is never acknowledged as saved. */
 order=vt_playback_history_order(id);set_source(id,90000,3600000,order,1);fail_write=1;tick(800000000,1);CHECK(!durable(90000));fail_write=0;
 tick(804000000,1);CHECK(!durable(90000));tick(805000000,0);CHECK(durable(90000));
 set_source(id,95000,3600000,order,1);fail_rename=1;tick(830000000,1);CHECK(!load(RESUME_PATH,&point)&&point.position_ms==90000);fail_rename=0;tick(835000000,0);CHECK(durable(95000));
 /* Partial writes are completed; corrupt current record falls back to backup. */
 set_source(id,100000,3600000,order,1);partial_write=1;tick(860000000,1);partial_write=0;CHECK(!load(RESUME_PATH,&point)&&point.position_ms==100000);
 file_put(RESUME_PATH,"torn",4);set_source(id,0,0,0,0);restart();CHECK(vt_playback_history_position(id,0)==95000);
 /* Rename-window recovery, then CRC rejection with no valid older record. */
 clear_files();restart();set_source(id,200000,3600000,0,1);tick(900000000,1);file_copy(RESUME_PATH,RESUME_BACKUP);CHECK(!unlink(RESUME_PATH));set_source(id,0,0,0,0);restart();CHECK(vt_playback_history_position(id,0)==200000);
 clear_files();restart();set_source(id,210000,3600000,0,1);tick(930000000,1);file_copy(RESUME_PATH,RESUME_TEMP);CHECK(!unlink(RESUME_PATH));set_source(id,0,0,0,0);restart();CHECK(vt_playback_history_position(id,0)==210000);
 clear_files();restart();set_source(id,220000,3600000,0,1);tick(960000000,1);FILE*f=fopen(RESUME_PATH,"r+b");CHECK(f);CHECK(!fseek(f,40,SEEK_SET));CHECK(fputc(123,f)!=EOF);fclose(f);CHECK(load(RESUME_PATH,&point)<0);set_source(id,0,0,0,0);restart();CHECK(!vt_playback_history_position(id,0));
 /* A valid zero bookmark must override an older database even if import fails. */
 clear_files();restart();CHECK(!vt_playback_history_update(id,60000,3600000));order=vt_playback_history_order(id);
 set_source(id,0,3600000,order,1);tick(980000000,1);set_source(id,0,0,0,0);fail_sync=1;restart();fail_sync=0;
 CHECK(vt_playback_history_position(id,0)==60000);CHECK(vt_podcast_resume_restore(id,order,0,&restored)&&!restored);
 /* One hour of continuous playback at 1 Hz sampling: <=241 tiny writes. */
 clear_files();restart();unsigned start_bytes=write_bytes,start_calls=write_calls,start_syncs=volume_syncs;
 for(unsigned sec=0;sec<3600;sec++){set_source(id,20000+sec*1000ULL,7200000,0,1);tick((1000ULL+sec)*1000000,0);}
 CHECK(write_calls-start_calls<=241);CHECK(write_bytes-start_bytes<=241*56);CHECK(volume_syncs==start_syncs);
 unsigned periodic_calls=write_calls-start_calls,periodic_bytes=write_bytes-start_bytes;
 /* Real asynchronous worker: storage never executes on the caller thread.
  * Emulate callback dispatch on the owning saver thread, including suspend. */
 clear_files();CHECK(!vt_playback_history_init());thread_enabled=async_mode=1;
 set_source(other,300000,3600000,0,1);__atomic_store_n(&clock_us,5000000000ULL,__ATOMIC_RELEASE);CHECK(!vt_podcast_resume_init(sample));
 for(int i=0;i<2000&&!durable(300000);i++)pause_ms(1);CHECK(durable(300000));
 set_source(other,301000,3600000,0,1);vt_podcast_resume_request();
 for(int i=0;i<2000&&!durable(301000);i++)pause_ms(1);CHECK(durable(301000));
 set_source(other,302000,3600000,0,1);__atomic_store_n(&callback_events,SCE_POWER_CB_SYSTEM_SUSPEND,__ATOMIC_RELEASE);
 for(int i=0;i<2000&&!durable(302000);i++)pause_ms(1);CHECK(durable(302000));
 set_source(other,0,0,0,0);__atomic_store_n(&callback_events,SCE_POWER_CB_SYSTEM_RESUME,__ATOMIC_RELEASE);pause_ms(10);CHECK(durable(302000));
 vt_podcast_resume_shutdown();CHECK(!__atomic_load_n(&io_on_caller,__ATOMIC_ACQUIRE));async_mode=thread_enabled=0;restart();CHECK(vt_playback_history_position(other,0)==302000);
 /* Completed episodes and unknown-duration episodes remain compatible. */
 CHECK(!vt_playback_history_update(other,3590000,3600000));CHECK(!vt_playback_history_position(other,0));uint64_t pos,dur;CHECK(vt_playback_history_progress(other,&pos,&dur)&&pos==3600000);
 CHECK(!vt_playback_history_update(id,123000,0));CHECK(vt_playback_history_position(id,0)==123000);CHECK(!vt_playback_history_update(id,124000,3600000));CHECK(!vt_playback_history_update(id,125000,0));CHECK(vt_playback_history_get(id,&pos,&dur)&&dur==3600000&&pos==125000);
 CHECK(!vt_playback_history_update(id,2000,8000));CHECK(vt_playback_history_progress(id,&pos,&dur)&&pos==2000);
 printf("Podcast resume: %u checks; 1 hour = %u checkpoints / %u payload bytes; restart, suspend callback, CRC/backup/temp recovery, failed-write retry, seek-zero protection and asynchronous ownership passed. Fixture %s\n",checks,periodic_calls,periodic_bytes,fixture);
 return 0;
}
