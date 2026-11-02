#ifndef VMD_UI_PROGRAM_TIME_H
#define VMD_UI_PROGRAM_TIME_H
#include "common/iptv_time.h"
#include <string.h>
/* XMLTV already supplies absolute UTC instants. Apply the console's actual
 * displayed offset once, including its current DST setting. Country/channel
 * metadata never changes the viewer's timezone. Read both clocks each time so
 * changing the Vita clock/zone takes effect without restarting or refetching. */
static inline void ui_program_local_time(time_t value,struct tm *out){
#ifdef __vita__
 SceDateTime utc,local;SceUInt64 utc_seconds,local_seconds;
 if(sceRtcGetCurrentClock(&utc,0)>=0&&sceRtcGetCurrentClockLocalTime(&local)>=0&&
    sceRtcGetTime64_t(&utc,&utc_seconds)>=0&&sceRtcGetTime64_t(&local,&local_seconds)>=0&&
    utc_seconds<=INT64_MAX&&local_seconds<=INT64_MAX){
  int64_t offset=(int64_t)local_seconds-(int64_t)utc_seconds;
  /* The two clock reads can straddle a second. System zone offsets use minutes. */
  offset=(offset>=0?(offset+30)/60:(offset-30)/60)*60;
  if(offset>=-14*3600&&offset<=14*3600){time_t display=(time_t)((int64_t)value+offset);gmtime_r(&display,out);return;}
 }
 if(!localtime_r(&value,out))memset(out,0,sizeof(*out));
#else
 localtime_r(&value,out);
#endif
}
#endif
