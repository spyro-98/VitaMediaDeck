#ifndef VITAMEDIADECK_SYSTEM_PERFORMANCE_H
#define VITAMEDIADECK_SYSTEM_PERFORMANCE_H

#ifdef VT_VIDEO_STOCK_CLOCKS
#define VT_720P60_BUS_MHZ 166
#define VT_720P60_GPU_MHZ 166
#else
#define VT_720P60_BUS_MHZ 222
#define VT_720P60_GPU_MHZ 222
#endif
#define VT_720P60_XBAR_MHZ 111

typedef struct VtPerformanceClockSlot {
 int previous, expected, changed, external;
} VtPerformanceClockSlot;
typedef struct VtPerformanceClockGuard {
 VtPerformanceClockSlot clocks[4]; /* CPU, BUS, GPU, XBAR */
 int hardware_cpu_mhz, software_cpu_mhz;
 int app_owns_clocks, ended;
} VtPerformanceClockGuard;

void vt_performance_begin_video(VtPerformanceClockGuard *guard, int quality_height, int fps);
/* Called before each backend open, including AUTO fallback and session changes.
 * Preferences are snapshotted before worker startup; this callback performs no I/O. */
void vt_performance_select_backend(VtPerformanceClockGuard *guard, int hardware);
void vt_performance_end_video(VtPerformanceClockGuard *guard);
#endif
