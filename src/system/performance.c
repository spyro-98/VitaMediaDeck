#include "system/performance.h"
#include <string.h>
#include <psp2/power.h>
#include "common/text_log.h"
#include "settings/preferences.h"

static int clock_read(int slot) {
 switch (slot) {
 case 0: return scePowerGetArmClockFrequency();
 case 1: return scePowerGetBusClockFrequency();
 case 2: return scePowerGetGpuClockFrequency();
 default: return scePowerGetGpuXbarClockFrequency();
 }
}
static int clock_write(int slot, int mhz) {
 /* Use only the public power API: PSVshell's manual frequency hooks retain
  * priority. Never unhook a plugin, write its profile or bypass it in kernel. */
 switch (slot) {
 case 0: return scePowerSetArmClockFrequency(mhz);
 case 1: return scePowerSetBusClockFrequency(mhz);
 case 2: return scePowerSetGpuClockFrequency(mhz);
 default: return scePowerSetGpuXbarClockFrequency(mhz);
 }
}
static void clock_apply(VtPerformanceClockGuard *guard, int slot, int mhz) {
 VtPerformanceClockSlot *clock = &guard->clocks[slot];
 if (!guard->app_owns_clocks || guard->ended || clock->external || clock->previous <= 0) return;
 int current = clock_read(slot);
 if (current != clock->expected) {
  clock->external = 1;
  clock->changed = 0;
  log_printf("video clocks: external change slot=%d expected=%d actual=%d; ownership released", slot, clock->expected, current);
  return;
 }
 if (current == mhz) return;
 int ret = clock_write(slot, mhz);
 int actual = clock_read(slot);
 clock->expected = actual;
 if (ret < 0 || actual != mhz) {
  /* A plugin may return success while keeping its manual value. Readback is
   * mandatory; do not retry and do not restore over that owner's choice. */
  clock->external = 1;
  clock->changed = 0;
 } else clock->changed = actual != clock->previous;
 log_printf("video clocks: slot=%d requested=%d actual=%d ret=%08X external=%d", slot, mhz, actual, (unsigned)ret, clock->external);
}
void vt_performance_begin_video(VtPerformanceClockGuard *guard, int quality_height, int fps) {
 if (!guard) return;
 (void)quality_height; (void)fps;
 memset(guard, 0, sizeof(*guard));
 guard->app_owns_clocks = vt_preferences_clock_source() == VT_CLOCK_SOURCE_APP;
 guard->hardware_cpu_mhz = vt_preferences_decoder_cpu_mhz(1);
 guard->software_cpu_mhz = vt_preferences_decoder_cpu_mhz(0);
 for (int i=0; i<4; i++) {
  guard->clocks[i].previous = guard->clocks[i].expected = clock_read(i);
 }
 /* A CPU value outside the app's supported range already belongs to an
  * external owner. Preserve it even if the plugin has no manual hooks. */
 if (guard->clocks[0].previous > 444) guard->clocks[0].external = 1;
 clock_apply(guard, 1, VT_720P60_BUS_MHZ);
 clock_apply(guard, 2, VT_720P60_GPU_MHZ);
 clock_apply(guard, 3, VT_720P60_XBAR_MHZ);
 log_printf("video clocks: profile=%s HW_CPU=%d SW_CPU=%d actual_CPU=%d", guard->app_owns_clocks ? "APP_DECODER" : "PSVSHELL", guard->hardware_cpu_mhz, guard->software_cpu_mhz, clock_read(0));
}
void vt_performance_select_backend(VtPerformanceClockGuard *guard, int hardware) {
 if (!guard) return;
 clock_apply(guard, 0, hardware ? guard->hardware_cpu_mhz : guard->software_cpu_mhz);
 log_printf("video clocks: backend=%s requested_CPU=%d actual_CPU=%d owner=%s", hardware ? "HW" : "SW", hardware ? guard->hardware_cpu_mhz : guard->software_cpu_mhz, clock_read(0), !guard->app_owns_clocks || guard->clocks[0].external ? "external" : "app");
}
void vt_performance_end_video(VtPerformanceClockGuard *guard) {
 if (!guard || guard->ended) return;
 if (guard->app_owns_clocks) for (int i=0; i<4; i++) {
  VtPerformanceClockSlot *clock = &guard->clocks[i];
  if (clock->changed && !clock->external && clock_read(i) == clock->expected)
   (void)clock_write(i, clock->previous);
  clock->changed = 0;
 }
 guard->ended = 1;
}
