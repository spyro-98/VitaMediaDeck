#ifndef VMD_TEST_STAT_H
#define VMD_TEST_STAT_H
#include "psp2/io/fcntl.h"
typedef struct { SceOff st_size; } SceIoStat;
int sceIoGetstat(const char *,SceIoStat *);
#endif
