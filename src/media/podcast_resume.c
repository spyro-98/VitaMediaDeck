#include "media/podcast_resume.h"
#include "app_paths.h"
#include "common/text_log.h"
#include "history/playback_history.h"
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>

#define RESUME_PATH VITAMEDIADECK_DATA_DIR "/podcast_resume.bin"
#define RESUME_TEMP VITAMEDIADECK_DATA_DIR "/podcast_resume.tmp"
#define RESUME_BACKUP VITAMEDIADECK_DATA_DIR "/podcast_resume.bak"
#define SAVE_INTERVAL_US 15000000ULL
#define RETRY_INTERVAL_US 5000000ULL

typedef struct {
    char magic[8];
    uint32_t version, crc;
    VtPodcastResumePoint point;
} ResumeDisk;
_Static_assert(sizeof(ResumeDisk)==56,"Compact podcast checkpoint size");
static const char magic[8]={'V','M','D','P','O','D','C','P'};
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static VtPodcastResumeSample sampler;
static VtPodcastResumePoint pending, saved;
static uint64_t last_write_us, retry_at_us;
static unsigned revision, saved_revision;
static int have_pending, have_saved;
static SceUID saver=-1;
static int stopping, urgent;

static uint32_t resume_checksum(const ResumeDisk *disk) {
    uint32_t crc=0xffffffffu;
    const unsigned char *bytes=(const unsigned char *)disk;
    for(size_t i=0;i<sizeof(*disk);i++) {
        unsigned byte=i>=offsetof(ResumeDisk,crc)&&i<offsetof(ResumeDisk,crc)+4?0:bytes[i];
        crc^=byte;
        for(int j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u & (uint32_t)-(int32_t)(crc&1));
    }
    return ~crc;
}
static int resume_valid_id(const char id[16]) {
    if(memcmp(id,"podcs",5))return 0;
    for(int i=5;i<13;i++)if(!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f')))return 0;
    return !id[13]&&!id[14]&&!id[15];
}
static int load(const char *path,VtPodcastResumePoint *point) {
    SceIoStat stat;ResumeDisk disk;
    if(sceIoGetstat(path,&stat)<0 || stat.st_size!=sizeof(disk))return -1;
    SceUID fd=sceIoOpen(path,SCE_O_RDONLY,0);if(fd<0)return fd;
    size_t done=0;
    while(done<sizeof(disk)) {
        int n=sceIoRead(fd,(unsigned char *)&disk+done,sizeof(disk)-done);
        if(n<=0)break;
        done+=(size_t)n;
    }
    sceIoClose(fd);
    if(done!=sizeof(disk)||memcmp(disk.magic,magic,8)||disk.version!=1||disk.crc!=resume_checksum(&disk)||!resume_valid_id(disk.point.id))return -1;
    *point=disk.point;return 0;
}
static int store(const VtPodcastResumePoint *point) {
    ResumeDisk disk={0};memcpy(disk.magic,magic,8);disk.version=1;disk.point=*point;disk.crc=resume_checksum(&disk);
    sceIoMkdir(VITAMEDIADECK_DATA_DIR,0777);
    sceIoRemove(RESUME_TEMP);
    SceUID fd=sceIoOpen(RESUME_TEMP,SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0777);
    if(fd<0)return fd;
    size_t done=0;int ret=0;
    while(done<sizeof(disk)) {
        int n=sceIoWrite(fd,(const unsigned char *)&disk+done,sizeof(disk)-done);
        if(n<=0){ret=n<0?n:-1;break;}done+=(size_t)n;
    }
    if(!ret)ret=sceIoSyncByFd(fd,0);
    int closed=sceIoClose(fd);if(!ret&&closed<0)ret=closed;
    if(ret<0){sceIoRemove(RESUME_TEMP);return ret;}
    sceIoRemove(RESUME_BACKUP);
    int had_previous=sceIoRename(RESUME_PATH,RESUME_BACKUP)>=0;
    ret=sceIoRename(RESUME_TEMP,RESUME_PATH);
    if(ret<0) {
        if(had_previous)sceIoRename(RESUME_BACKUP,RESUME_PATH);
        sceIoRemove(RESUME_TEMP);
    }
    /* Sync only this 56-byte record; never flush the entire ux0 volume. */
    return ret;
}
/* mutex is held only around a tiny snapshot/copy, never around storage I/O. */
static void capture_locked(void) {
    VtPodcastResumePoint point={0};
    if(!sampler||!sampler(&point)||!resume_valid_id(point.id))return;
    if(have_pending && !strcmp(point.id,pending.id) && !point.duration_ms)
        point.duration_ms=pending.duration_ms;
    if(have_pending && !memcmp(&point,&pending,sizeof(point)))return;
    pending=point;have_pending=1;revision++;
}
static void checkpoint(int force) {
    uint64_t now=sceKernelGetProcessTimeWide();
    VtPodcastResumePoint point;unsigned serial;
    pthread_mutex_lock(&mutex);
    capture_locked();
    if(!have_pending || revision==saved_revision ||
       (!force&&have_saved&&now>=last_write_us&&now-last_write_us<SAVE_INTERVAL_US)||
       (retry_at_us&&now<retry_at_us)) {pthread_mutex_unlock(&mutex);return;}
    point=pending;serial=revision;
    pthread_mutex_unlock(&mutex);
    int ret=store(&point);
    pthread_mutex_lock(&mutex);
    if(ret>=0) {
        saved=point;have_saved=1;saved_revision=serial;last_write_us=now;retry_at_us=0;
    } else retry_at_us=now+RETRY_INTERVAL_US;
    pthread_mutex_unlock(&mutex);
    if(ret<0)log_printf("podcast checkpoint: save failed 0x%08X; retry pending",(unsigned)ret);
}
static int power_event(int id,int count,int info,void *ctx) {
    (void)id;(void)count;(void)ctx;
    if(info&(SCE_POWER_CB_SYSTEM_SUSPEND|SCE_POWER_CB_APP_SUSPEND|
             SCE_POWER_CB_SYSTEM_RESUME|SCE_POWER_CB_APP_RESUME))
        checkpoint(1); /* Callback belongs to the saver, never the audio/UI thread. */
    return 0;
}
static int worker(SceSize size,void *args) {
    (void)size;(void)args;
    SceUID callback=sceKernelCreateCallback("VMDPodcastPower",0,power_event,NULL);
    int registered=callback>=0?scePowerRegisterCallback(callback):-1;
    if(registered<0)log_printf("podcast checkpoint: power callback unavailable 0x%08X",(unsigned)registered);
    while(!__atomic_load_n(&stopping,__ATOMIC_ACQUIRE)) {
        checkpoint(__atomic_exchange_n(&urgent,0,__ATOMIC_ACQ_REL));
        sceKernelDelayThreadCB(1000000);
    }
    checkpoint(1);
    if(registered>=0)scePowerUnregisterCallback(callback);
    if(callback>=0)sceKernelDeleteCallback(callback);
    return sceKernelExitThread(0);
}
int vt_podcast_resume_init(VtPodcastResumeSample sample) {
    sampler=sample;have_pending=have_saved=0;revision=saved_revision=0;
    last_write_us=retry_at_us=0;stopping=urgent=0;
    memset(&pending,0,sizeof(pending));memset(&saved,0,sizeof(saved));
    const char *paths[]={RESUME_PATH,RESUME_BACKUP,RESUME_TEMP};
    for(unsigned i=0;i<sizeof(paths)/sizeof(paths[0]);i++)if(load(paths[i],&saved)==0) {
        /* A normal stop may have committed a newer history record after this
         * checkpoint. Never overwrite it, including a rewind to the beginning. */
        if(vt_playback_history_order(saved.id)==saved.history_order) {
            have_saved=have_pending=1;pending=saved;
            if(vt_playback_history_update(saved.id,saved.position_ms,saved.duration_ms)<0)
                log_printf("podcast checkpoint: history import failed; memory recovery retained");
        }
        break;
    }
    saver=sceKernelCreateThread("VMDPodcastResume",worker,0x10000140,0x8000,0,0,NULL);
    if(saver<0)return saver;
    int ret=sceKernelStartThread(saver,0,NULL);
    if(ret<0){sceKernelDeleteThread(saver);saver=-1;}
    return ret;
}
int vt_podcast_resume_restore(const char *id,uint64_t order,uint64_t duration_ms,uint64_t *position_ms) {
    pthread_mutex_lock(&mutex);
    uint64_t position=0;
    int found=have_pending&&id&&!strcmp(id,pending.id)&&pending.history_order==order;
    if(found) {
        uint64_t duration=duration_ms?duration_ms:pending.duration_ms;
        position=pending.position_ms;
        if(position<10000ULL || (duration>15000ULL && position>=duration-15000ULL))position=0;
    }
    pthread_mutex_unlock(&mutex);
    if(found&&position_ms)*position_ms=position;
    return found;
}
void vt_podcast_resume_request(void) { __atomic_store_n(&urgent,1,__ATOMIC_RELEASE); }
int vt_podcast_resume_finish(const char *id,VtPodcastResumePoint *out) {
    pthread_mutex_lock(&mutex);capture_locked();
    int have=have_pending&&id&&!strcmp(id,pending.id);
    if(have&&out)*out=pending;
    pthread_mutex_unlock(&mutex);
    vt_podcast_resume_request();return have;
}
void vt_podcast_resume_shutdown(void) {
    if(saver<0)return;
    __atomic_store_n(&stopping,1,__ATOMIC_RELEASE);
    sceKernelWaitThreadEnd(saver,NULL,NULL);sceKernelDeleteThread(saver);saver=-1;
}
