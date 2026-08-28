#ifndef VITAMEDIADECK_SYSTEM_PERFORMANCE_H
#define VITAMEDIADECK_SYSTEM_PERFORMANCE_H

#define VT_VIDEO_DEFAULT_ARM_MHZ 444
#define VT_720P60_BUS_MHZ 222
#define VT_720P60_GPU_MHZ 222
#define VT_720P60_XBAR_MHZ 111

typedef struct VtPerformanceClockGuard {
	int previous_arm_mhz;
	int previous_bus_mhz;
	int previous_gpu_mhz;
	int previous_xbar_mhz;
	int changed_arm_clock;
	int changed_bus_clock;
	int changed_gpu_clock;
	int changed_xbar_clock;
	int high_fps_profile;
	int app_owns_clocks;
} VtPerformanceClockGuard;

void vt_performance_begin_video(VtPerformanceClockGuard *guard,
	                            int quality_height, int fps);
void vt_performance_end_video(VtPerformanceClockGuard *guard);

#endif /* VITAMEDIADECK_SYSTEM_PERFORMANCE_H */
