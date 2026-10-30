#ifndef VMD_PLAYBACK_CHECKPOINT_H
#define VMD_PLAYBACK_CHECKPOINT_H
#include "app_paths.h"
#include "common/text_log.h"
#include <stdint.h>
#include <string.h>
#include <psp2/kernel/processmgr.h>
#ifdef __vita__
#include <psp2/kernel/sysmem.h>
#endif

/* Transition-only, never per frame. Preserve the last attempted operation even
 * if a system reset prevents the usual end-of-session log save. No URL/title
 * is accepted as a media identifier, and disabled disk logging stays disabled. */
static inline void vt_playback_checkpoint(const char *phase,const char *media_id,
                                          int backend,unsigned width,unsigned height,int result) {
    const char *id="unknown";
    if(media_id&&strnlen(media_id,16)==15&&media_id[0]=='u'&&
       strspn(media_id+1,"0123456789abcdef")==14)id=media_id;
    int memory_result=-1,main_bytes=-1,cdram_bytes=-1,phycont_bytes=-1;
#ifdef __vita__
    SceKernelFreeMemorySizeInfo memory={.size=sizeof(memory)};
    memory_result=sceKernelGetFreeMemorySize(&memory);
    if(memory_result>=0){main_bytes=memory.size_user;cdram_bytes=memory.size_cdram;phycont_bytes=memory.size_phycont;}
#endif
    log_printf("playback checkpoint: phase=%.32s media=%s backend=%d size=%ux%u result=%d process_us=%llu memory_result=%d main_free=%d cdram_free=%d phycont_free=%d",
               phase?phase:"unknown",id,backend,width,height,result,
               (unsigned long long)sceKernelGetProcessTimeWide(),memory_result,
               main_bytes,cdram_bytes,phycont_bytes);
    log_save(VITAMEDIADECK_SESSION_LOG_PATH);
}
#endif
