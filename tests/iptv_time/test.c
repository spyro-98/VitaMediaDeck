/* Compile the actual Vita branch; only the firmware clock calls are replaced.
 * Deliberately skew libc time, then move the device through DST/zone changes. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static int64_t utc_now;
static int system_offset,read_skew,rtc_failure;
static time_t skewed_libc(time_t *out){time_t n=(time_t)(utc_now-3600);if(out)*out=n;return n;}
#define __vita__ 1
#define time skewed_libc
#include "ui/program_time.h"
#undef time
int sceRtcGetCurrentClock(SceDateTime *out,int zone){assert(zone==0);out->epoch=utc_now;return rtc_failure?-1:0;}
int sceRtcGetCurrentClockLocalTime(SceDateTime *out){out->epoch=utc_now+system_offset+read_skew;return rtc_failure?-1:0;}
int sceRtcGetTime64_t(const SceDateTime *date,SceUInt64 *out){*out=(SceUInt64)date->epoch;return 0;}
static int64_t instant(int y,int m,int d,int hour,int minute){struct tm date={.tm_year=y-1900,.tm_mon=m-1,.tm_mday=d,.tm_hour=hour,.tm_min=minute};return timegm(&date);}
static void local(int expected_hour,int expected_minute,int expected_day){struct tm date;ui_program_local_time((time_t)utc_now,&date);assert(date.tm_hour==expected_hour&&date.tm_min==expected_minute&&date.tm_mday==expected_day);}
int main(void){
 utc_now=instant(2026,10,31,12,0);system_offset=3600;
 assert(vt_iptv_time_now()==utc_now&&vt_iptv_time_now()!=skewed_libc(NULL));local(13,0,31);
 utc_now=instant(2026,7,15,12,0);system_offset=7200;local(14,0,15);
 utc_now=instant(2026,10,25,0,30);system_offset=7200;local(2,30,25);
 utc_now=instant(2026,10,25,1,30);system_offset=3600;local(2,30,25);
 system_offset=-4*3600;local(21,30,24);system_offset=-5*3600;local(20,30,24);
 utc_now=instant(2026,10,31,12,0);system_offset=19800;local(17,30,31);system_offset=20700;local(17,45,31);
 system_offset=3600;read_skew=1;local(13,0,31);read_skew=-1;local(13,0,31);read_skew=0;
 utc_now=instant(2026,10,31,23,30);local(0,30,1);
 /* Network/cache timestamps do not acquire the viewer's offset. */
 assert(vt_iptv_time_now()==utc_now);system_offset=9*3600;assert(vt_iptv_time_now()==utc_now);
 rtc_failure=1;assert(vt_iptv_time_now()==skewed_libc(NULL));
 puts("Actual Vita clock branch: UTC independent of libc skew, current console offset, CET/CEST transitions, US/Asian zones, fractional-hour offsets, midnight, second-read skew and fallback passed. Firmware calls mocked.");
}
