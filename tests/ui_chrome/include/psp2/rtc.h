#include <stdint.h>
typedef struct {uint16_t year,month,day,hour,minute,second;uint32_t microsecond;} SceDateTime;
int sceRtcGetCurrentClockLocalTime(SceDateTime*);
