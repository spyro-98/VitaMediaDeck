#include "system/display_awake.h"

#include <stdint.h>

#include <psp2/kernel/processmgr.h>

#define VT_DISPLAY_AWAKE_INTERVAL_US (750ULL * 1000ULL)

static uint64_t g_last_tick_us;

void vt_display_keep_awake_tick(void) {
	uint64_t now_us = sceKernelGetProcessTimeWide();
	if (g_last_tick_us && now_us - g_last_tick_us < VT_DISPLAY_AWAKE_INTERVAL_US)
		return;

	/* These are distinct Vita timers. Disabling dimming alone does not reset
	 * the later OLED-off deadline, which is why foreground playback could
	 * still end with a black display. */
	sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
	sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_OLED_OFF);
	sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_OLED_DIMMING);
	g_last_tick_us = now_us;
}
