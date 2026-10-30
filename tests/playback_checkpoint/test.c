#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define __vita__ 1
#include "common/playback_checkpoint.h"
static int failure,saved;
static char recorded[1024];
int sceKernelGetFreeMemorySize(SceKernelFreeMemorySizeInfo *out){
 assert(out->size==sizeof(*out));out->size_user=123456;out->size_cdram=78910;out->size_phycont=1112;return failure?-42:0;
}
uint64_t sceKernelGetProcessTimeWide(void){return 999000;}
void log_printf(const char *format,...){va_list args;va_start(args,format);vsnprintf(recorded,sizeof(recorded),format,args);va_end(args);}
int log_save(const char *path){assert(!strcmp(path,VITAMEDIADECK_SESSION_LOG_PATH));assert(strstr(recorded,"playback checkpoint:"));saved++;return 0;}
int main(void){
 vt_playback_checkpoint("decoder.hw.open.begin","u123456789abcde",1,1920,1080,0);
 assert(saved==1&&strstr(recorded,"media=u123456789abcde")&&strstr(recorded,"size=1920x1080"));
 assert(strstr(recorded,"process_us=999000")&&strstr(recorded,"cdram_free=78910"));
 failure=1;vt_playback_checkpoint("decoder.hw.open.complete","https://private.invalid/?secret",1,1920,1080,-12);
 assert(saved==2&&strstr(recorded,"result=-12")&&strstr(recorded,"memory_result=-42"));
 assert(strstr(recorded,"main_free=-1")&&strstr(recorded,"cdram_free=-1")&&strstr(recorded,"media=unknown"));
 assert(!strstr(recorded,"private")&&!strstr(recorded,"secret"));
 vt_playback_checkpoint(NULL,NULL,0,0,0,0);assert(saved==3&&strstr(recorded,"phase=unknown"));
 puts("Playback checkpoints: transition saved after complete record; valid hashed identity, no raw URL, memory snapshot/error boundaries and null labels passed.");
}
