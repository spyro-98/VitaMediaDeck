#ifndef TEST_RTC_H
#define TEST_RTC_H
#include <stdint.h>
typedef uint64_t SceUInt64;
typedef struct { int64_t epoch; } SceDateTime;
int sceRtcGetCurrentClock(SceDateTime *,int);
int sceRtcGetCurrentClockLocalTime(SceDateTime *);
int sceRtcGetTime64_t(const SceDateTime *,SceUInt64 *);
#endif
