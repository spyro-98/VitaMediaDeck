#ifndef VMD_IPTV_TIME_H
#define VMD_IPTV_TIME_H
#include <stdint.h>
#include <time.h>
#ifdef __vita__
#include <psp2/rtc.h>
#endif
/* Guide selection/cache boundaries use a UTC epoch, never local wall time. */
static inline int64_t vt_iptv_time_now(void){
#ifdef __vita__
 SceDateTime utc;SceUInt64 seconds;
 if(sceRtcGetCurrentClock(&utc,0)>=0&&sceRtcGetTime64_t(&utc,&seconds)>=0&&seconds<=INT64_MAX)return (int64_t)seconds;
#endif
 return (int64_t)time(NULL);
}
#endif
