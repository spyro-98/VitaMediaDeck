#ifndef VMD_UI_FRAME_PACING_H
#define VMD_UI_FRAME_PACING_H
#include <stdint.h>
#include <psp2/kernel/processmgr.h>
/* Include rendering/vsync time in the budget instead of adding 16 ms to it. */
static inline unsigned ui_frame_remaining_us(uint64_t start,uint64_t now) {
    if(now<start || now-start>=16667ULL)return 0;
    return (unsigned)(16667ULL-(now-start));
}
static inline void ui_frame_pace(uint64_t start) {
    unsigned remaining=ui_frame_remaining_us(start,sceKernelGetProcessTimeWide());
    if(remaining)sceKernelDelayThread(remaining);
}
#endif
