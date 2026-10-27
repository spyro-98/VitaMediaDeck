#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "system/performance.h"
#include "settings/preferences.h"
#include "../../src/system/performance.c"
static int clocks[4], calls[4], owner=VT_CLOCK_SOURCE_APP, fail_mask, manual_mask;
static int hw_mhz=333, sw_mhz=444;
int vt_preferences_clock_source(void){return owner;}
int vt_preferences_decoder_cpu_mhz(int hw){return hw?hw_mhz:sw_mhz;}
void log_printf(const char*f,...){(void)f;}
int scePowerGetArmClockFrequency(void){return clocks[0];}
int scePowerGetBusClockFrequency(void){return clocks[1];}
int scePowerGetGpuClockFrequency(void){return clocks[2];}
int scePowerGetGpuXbarClockFrequency(void){return clocks[3];}
static int change(int i,int n){calls[i]++;if(fail_mask&(1<<i))return -1;if(!(manual_mask&(1<<i)))clocks[i]=n;return 0;}
int scePowerSetArmClockFrequency(int n){return change(0,n);}
int scePowerSetBusClockFrequency(int n){return change(1,n);}
int scePowerSetGpuClockFrequency(int n){return change(2,n);}
int scePowerSetGpuXbarClockFrequency(int n){return change(3,n);}
static void reset(void){const int original[]={222,111,111,83};memcpy(clocks,original,sizeof(clocks));memset(calls,0,sizeof(calls));owner=VT_CLOCK_SOURCE_APP;fail_mask=manual_mask=0;hw_mhz=333;sw_mhz=444;}
int main(void){
 VtPerformanceClockGuard g;
 reset();vt_performance_begin_video(&g,720,60);assert(clocks[0]==222&&calls[0]==0);
 vt_performance_select_backend(&g,1);assert(clocks[0]==333&&calls[0]==1);
 vt_performance_select_backend(&g,1);assert(calls[0]==1);
 vt_performance_select_backend(&g,0);assert(clocks[0]==444&&calls[0]==2);
 vt_performance_select_backend(&g,1);assert(clocks[0]==333&&calls[0]==3);
 vt_performance_end_video(&g);assert(clocks[0]==222&&calls[0]==4);
 assert(clocks[1]==111&&clocks[2]==111&&clocks[3]==83);
 vt_performance_end_video(&g);vt_performance_select_backend(&g,0);assert(calls[0]==4);
 /* Explicit external ownership means zero setters, including exit. */
 reset();owner=VT_CLOCK_SOURCE_PSVSHELL;vt_performance_begin_video(&g,720,60);
 vt_performance_select_backend(&g,1);vt_performance_select_backend(&g,0);vt_performance_end_video(&g);
 for(int i=0;i<4;i++)assert(calls[i]==0);
 /* Model PSVshell's per-domain manual hooks: success with unchanged readback.
  * All 16 CPU/BUS/GPU/XBAR combinations retain priority and no retry/restore. */
 for(int mask=1;mask<16;mask++){
  reset();manual_mask=mask;vt_performance_begin_video(&g,720,30);
  vt_performance_select_backend(&g,1);vt_performance_select_backend(&g,0);vt_performance_end_video(&g);
  for(int i=0;i<4;i++)if(mask&(1<<i))assert(calls[i]==1);
  assert(clocks[0]==222&&clocks[1]==111&&clocks[2]==111&&clocks[3]==83);
 }
 /* User changes the plugin during playback: don't change or restore that CPU. */
 reset();vt_performance_begin_video(&g,720,30);vt_performance_select_backend(&g,1);clocks[0]=500;
 vt_performance_select_backend(&g,0);vt_performance_end_video(&g);assert(clocks[0]==500&&calls[0]==1);
 reset();clocks[0]=500;vt_performance_begin_video(&g,720,30);vt_performance_select_backend(&g,1);vt_performance_end_video(&g);assert(clocks[0]==500&&calls[0]==0);
 reset();fail_mask=15;vt_performance_begin_video(&g,360,30);vt_performance_select_backend(&g,1);vt_performance_select_backend(&g,0);vt_performance_end_video(&g);for(int i=0;i<4;i++)assert(calls[i]==1);
 /* Custom pairs, software first, equal pairs, and preferences read only at begin. */
 reset();hw_mhz=222;sw_mhz=333;vt_performance_begin_video(&g,720,30);hw_mhz=444;sw_mhz=444;
 vt_performance_select_backend(&g,0);assert(clocks[0]==333);vt_performance_select_backend(&g,1);assert(clocks[0]==222);vt_performance_end_video(&g);assert(clocks[0]==222);
 reset();hw_mhz=sw_mhz=333;vt_performance_begin_video(&g,720,30);vt_performance_select_backend(&g,1);vt_performance_select_backend(&g,0);assert(calls[0]==1);vt_performance_end_video(&g);
 puts("Decoder clocks: HW333/SW444, AUTO transitions, configurable pairs, repeated opens, 16 plugin ownership combinations, manual changes, failures, restoration and zero-write PSVshell mode passed.");
}
