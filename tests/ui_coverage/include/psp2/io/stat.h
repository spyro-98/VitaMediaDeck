#ifndef VMD_COVERAGE_STAT_H
#define VMD_COVERAGE_STAT_H
#include <stdint.h>
typedef struct { int64_t st_size; } SceIoStat;
int sceIoGetstat(const char *,SceIoStat *);
#endif
