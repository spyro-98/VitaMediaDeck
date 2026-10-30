#ifndef VMD_UI_PROGRAM_TIME_H
#define VMD_UI_PROGRAM_TIME_H
#include <time.h>
#include <string.h>
#ifdef __vita__
#include <psp2/rtc.h>
#endif
static inline void ui_program_local_time(time_t value,struct tm *out){
#ifdef __vita__
 SceDateTime date;SceRtcTick utc,local;
 if(value>=0&&sceRtcSetTime64_t(&date,(SceUInt64)value)>=0&&sceRtcGetTick(&date,&utc)>=0&&
    sceRtcConvertUtcToLocalTime(&utc,&local)>=0&&sceRtcSetTick(&date,&local)>=0){
  memset(out,0,sizeof(*out));out->tm_year=date.year-1900;out->tm_mon=date.month-1;out->tm_mday=date.day;
  out->tm_hour=date.hour;out->tm_min=date.minute;out->tm_sec=date.second;return;
 }
 gmtime_r(&value,out);
#else
 localtime_r(&value,out);
#endif
}
#endif
