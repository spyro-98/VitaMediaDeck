#include <assert.h>
#include <stdio.h>
#include "system/performance.h"
#include "settings/preferences.h"
#include "../../src/system/performance.c"
static int clocks[4],calls,owner=VT_CLOCK_SOURCE_APP,fail;
int vt_preferences_clock_source(void){return owner;}
void log_printf(const char*f,...){(void)f;}
int scePowerGetArmClockFrequency(void){return clocks[0];}
int scePowerGetBusClockFrequency(void){return clocks[1];}
int scePowerGetGpuClockFrequency(void){return clocks[2];}
int scePowerGetGpuXbarClockFrequency(void){return clocks[3];}
static int change(int i,int n){calls++;if(fail)return -1;clocks[i]=n;return 0;}
int scePowerSetArmClockFrequency(int n){return change(0,n);}
int scePowerSetBusClockFrequency(int n){return change(1,n);}
int scePowerSetGpuClockFrequency(int n){return change(2,n);}
int scePowerSetGpuXbarClockFrequency(int n){return change(3,n);}
int main(void){
 const int target[]={VT_VIDEO_DEFAULT_ARM_MHZ,VT_720P60_BUS_MHZ,VT_720P60_GPU_MHZ,VT_720P60_XBAR_MHZ};
 VtPerformanceClockGuard g;int original[]={222,111,111,83};
 for(int i=0;i<4;i++)clocks[i]=original[i];vt_performance_begin_video(&g,720,60);assert(calls==4&&g.high_fps_profile);
 for(int i=0;i<4;i++)assert(clocks[i]==target[i]);
 vt_performance_end_video(&g);for(int i=0;i<4;i++)assert(clocks[i]==original[i]);assert(calls==8);
 vt_performance_end_video(&g);assert(calls==8);
 owner=VT_CLOCK_SOURCE_PSVSHELL;vt_performance_begin_video(&g,720,60);vt_performance_end_video(&g);assert(calls==8);
 owner=VT_CLOCK_SOURCE_APP;fail=1;vt_performance_begin_video(&g,360,30);assert(!g.changed_arm_clock&&!g.changed_bus_clock&&!g.changed_gpu_clock&&!g.changed_xbar_clock);int before=calls;vt_performance_end_video(&g);assert(calls==before);fail=0;
 vt_performance_begin_video(&g,720,60);clocks[0]=111;vt_performance_end_video(&g);assert(clocks[0]==111);for(int i=1;i<4;i++)assert(clocks[i]==original[i]);
 printf("Clock ownership/apply/restore/failure/external change passed: %d/%d/%d/%d MHz.\n",target[0],target[1],target[2],target[3]);
}
