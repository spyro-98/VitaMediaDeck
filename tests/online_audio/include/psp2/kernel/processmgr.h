#include <stdint.h>
#define SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND 1
uint64_t sceKernelGetProcessTimeWide(void);
int sceKernelPowerTick(int);
int sceKernelDelayThread(unsigned);
