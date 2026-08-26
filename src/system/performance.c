#include "system/performance.h"

#include <string.h>

#include <psp2/power.h>

#include "common/text_log.h"
#include "settings/preferences.h"

void vt_performance_begin_video(VtPerformanceClockGuard *guard,
	                            int quality_height, int fps) {
	if (!guard) return;
	memset(guard, 0, sizeof(*guard));
	guard->previous_arm_mhz = scePowerGetArmClockFrequency();
	guard->previous_bus_mhz = scePowerGetBusClockFrequency();
	guard->previous_gpu_mhz = scePowerGetGpuClockFrequency();
	guard->previous_xbar_mhz = scePowerGetGpuXbarClockFrequency();
	guard->high_fps_profile = quality_height >= 720 && fps >= 50;
	guard->app_owns_clocks =
	    vt_preferences_clock_source() == VT_CLOCK_SOURCE_APP;

	int arm_ret = 0, bus_ret = 0, gpu_ret = 0, xbar_ret = 0;
	/* App mode is an exact, reproducible profile, not a lower-bound hint.  The
	 * previous implementation only raised clocks, so selecting VitaWave while
	 * a stale PSVshell value was higher looked as if the preference had been
	 * ignored.  In PSVshell mode this whole block is skipped: no detection or
	 * heuristic is allowed to change ownership behind the user's back. */
	if (guard->app_owns_clocks && guard->previous_arm_mhz > 0 &&
	    guard->previous_arm_mhz != VT_VIDEO_DEFAULT_ARM_MHZ) {
		arm_ret = scePowerSetArmClockFrequency(VT_VIDEO_DEFAULT_ARM_MHZ);
		guard->changed_arm_clock = arm_ret >= 0;
	}
	/* VitaWave's deterministic profile is the same at every selectable quality:
	 * CPU 444, ES4/GPU 222, BUS 222, XBAR 111 MHz. This avoids a session
	 * silently inheriting a slower firmware clock merely because it is 30 fps. */
	if (guard->app_owns_clocks) {
		if (guard->previous_bus_mhz > 0 && guard->previous_bus_mhz != VT_720P60_BUS_MHZ) {
			bus_ret = scePowerSetBusClockFrequency(VT_720P60_BUS_MHZ);
			guard->changed_bus_clock = bus_ret >= 0;
		}
		if (guard->previous_gpu_mhz > 0 && guard->previous_gpu_mhz != VT_720P60_GPU_MHZ) {
			gpu_ret = scePowerSetGpuClockFrequency(VT_720P60_GPU_MHZ);
			guard->changed_gpu_clock = gpu_ret >= 0;
		}
		if (guard->previous_xbar_mhz > 0 && guard->previous_xbar_mhz != VT_720P60_XBAR_MHZ) {
			xbar_ret = scePowerSetGpuXbarClockFrequency(VT_720P60_XBAR_MHZ);
			guard->changed_xbar_clock = xbar_ret >= 0;
		}
	}
	log_printf("video clocks: profile=%s highfps=%d ARM %d->%d ret=%08X BUS %d->%d ret=%08X GPU %d->%d ret=%08X XBAR %d->%d ret=%08X",
	           guard->app_owns_clocks ? "APP_FIXED" : "PSVSHELL",
	           guard->high_fps_profile,
	           guard->previous_arm_mhz,
	           scePowerGetArmClockFrequency(), (unsigned)arm_ret,
	           guard->previous_bus_mhz, scePowerGetBusClockFrequency(), (unsigned)bus_ret,
	           guard->previous_gpu_mhz, scePowerGetGpuClockFrequency(), (unsigned)gpu_ret,
	           guard->previous_xbar_mhz, scePowerGetGpuXbarClockFrequency(), (unsigned)xbar_ret);
}

void vt_performance_end_video(VtPerformanceClockGuard *guard) {
	if (!guard) return;
	if (!guard->app_owns_clocks) {
		log_printf("video clocks: PSVshell selected, no application clocks to restore");
		return;
	}
	int current_arm_mhz = scePowerGetArmClockFrequency();
	if (guard->changed_arm_clock && current_arm_mhz == VT_VIDEO_DEFAULT_ARM_MHZ)
		scePowerSetArmClockFrequency(guard->previous_arm_mhz);
	if (guard->changed_bus_clock &&
	    scePowerGetBusClockFrequency() == VT_720P60_BUS_MHZ)
		scePowerSetBusClockFrequency(guard->previous_bus_mhz);
	if (guard->changed_gpu_clock &&
	    scePowerGetGpuClockFrequency() == VT_720P60_GPU_MHZ)
		scePowerSetGpuClockFrequency(guard->previous_gpu_mhz);
	if (guard->changed_xbar_clock &&
	    scePowerGetGpuXbarClockFrequency() == VT_720P60_XBAR_MHZ)
		scePowerSetGpuXbarClockFrequency(guard->previous_xbar_mhz);
	log_printf("video clocks restored: ARM=%d BUS=%d GPU=%d XBAR=%d",
	           scePowerGetArmClockFrequency(), scePowerGetBusClockFrequency(),
	           scePowerGetGpuClockFrequency(), scePowerGetGpuXbarClockFrequency());
	guard->changed_arm_clock = 0;
	guard->changed_bus_clock = 0;
	guard->changed_gpu_clock = 0;
	guard->changed_xbar_clock = 0;
}
