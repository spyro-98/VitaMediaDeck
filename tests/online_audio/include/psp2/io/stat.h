#include <stdint.h>
typedef struct { int64_t st_size; } SceIoStat;
int sceIoGetstat(const char *,SceIoStat *);
