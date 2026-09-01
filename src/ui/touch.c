#include "ui/touch.h"

#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/touch.h>

/* Thresholds in framebuffer pixels, deliberately independent from the
 * panel's raw resolution: 12 px prevents normal jitter from becoming a swipe. */
#define UI_TOUCH_TAP_DISTANCE_PX 12
#define UI_TOUCH_TAP_TIMEOUT_US  (350 * 1000ULL)

typedef struct {
	int initialized;
	int sampling_started_here;
	SceTouchPanelInfo panel;
	int active;
	unsigned int active_id;
	int x;
	int y;
	int down_x;
	int down_y;
	uint64_t down_time_us;
	/* Screen/player transitions can happen while a finger is still down. Do
	 * not turn that inherited contact (or its later release) into a tap in the
	 * newly-entered screen. */
	int suppress_until_release;
} UiTouchState;

static UiTouchState g_touch;

static int clamp_int(int value, int low, int high) {
	if (value < low) return low;
	if (value > high) return high;
	return value;
}

static int map_axis(SceInt16 value, SceInt16 min_value, SceInt16 max_value,
	                int screen_size) {
	int span = (int)max_value - (int)min_value;
	if (span <= 0 || screen_size <= 1) return 0;
	int mapped = ((int)value - (int)min_value) * (screen_size - 1) / span;
	return clamp_int(mapped, 0, screen_size - 1);
}

static void map_report(const SceTouchReport *report, int *x, int *y) {
	*x = map_axis(report->x, g_touch.panel.minDispX, g_touch.panel.maxDispX,
	              UI_TOUCH_SCREEN_WIDTH);
	*y = map_axis(report->y, g_touch.panel.minDispY, g_touch.panel.maxDispY,
	              UI_TOUCH_SCREEN_HEIGHT);
}

/* Returns only contacts the app can use. A report marked as hidden by an
 * upper layer must not trigger buttons underneath it. */
static const SceTouchReport *find_report(const SceTouchData *data,
	                                      unsigned int id, int match_id) {
	for (unsigned int i = 0; i < data->reportNum && i < SCE_TOUCH_MAX_REPORT; i++) {
		const SceTouchReport *report = &data->report[i];
		if (report->info & SCE_TOUCH_REPORT_INFO_HIDE_UPPER_LAYER) continue;
		if (!match_id || report->id == id) return report;
	}
	return NULL;
}

int ui_touch_init(void) {
	if (g_touch.initialized) return 0;

	memset(&g_touch, 0, sizeof(g_touch));
	int ret = sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &g_touch.panel);
	if (ret < 0) return ret;

	SceTouchSamplingState state;
	ret = sceTouchGetSamplingState(SCE_TOUCH_PORT_FRONT, &state);
	if (ret < 0) return ret;
	if (state != SCE_TOUCH_SAMPLING_STATE_START) {
		ret = sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,
		                               SCE_TOUCH_SAMPLING_STATE_START);
		if (ret < 0) return ret;
		g_touch.sampling_started_here = 1;
	}

	g_touch.initialized = 1;
	return 0;
}

void ui_touch_term(void) {
	if (!g_touch.initialized) return;
	if (g_touch.sampling_started_here) {
		sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,
		                         SCE_TOUCH_SAMPLING_STATE_STOP);
	}
	memset(&g_touch, 0, sizeof(g_touch));
}

void ui_touch_reset(void) {
	if (!g_touch.initialized) return;
	g_touch.active = 0;
	g_touch.active_id = 0;
	g_touch.x = g_touch.y = 0;
	g_touch.down_x = g_touch.down_y = 0;
	g_touch.down_time_us = 0;

	SceTouchData data;
	memset(&data, 0, sizeof(data));
	int ret = sceTouchPeek(SCE_TOUCH_PORT_FRONT, &data, 1);
	g_touch.suppress_until_release =
	    ret >= 0 && find_report(&data, 0, 0) != NULL;
}

unsigned int ui_touch_poll(UiTouchEvent *event) {
	UiTouchEvent local;
	memset(&local, 0, sizeof(local));
	if (event) *event = local;
	if (!g_touch.initialized) return UI_TOUCH_EVENT_NONE;

	SceTouchData data;
	memset(&data, 0, sizeof(data));
	int ret = sceTouchPeek(SCE_TOUCH_PORT_FRONT, &data, 1);
	if (ret < 0) return UI_TOUCH_EVENT_NONE;
	if (g_touch.suppress_until_release) {
		if (!find_report(&data, 0, 0)) g_touch.suppress_until_release = 0;
		return UI_TOUCH_EVENT_NONE;
	}

	const SceTouchReport *report = find_report(&data, g_touch.active_id,
	                                           g_touch.active);
	uint64_t now = sceKernelGetProcessTimeWide();
	if (!g_touch.active) {
		if (!report) return UI_TOUCH_EVENT_NONE;
		map_report(report, &g_touch.x, &g_touch.y);
		g_touch.active = 1;
		g_touch.active_id = report->id;
		g_touch.down_x = g_touch.x;
		g_touch.down_y = g_touch.y;
		g_touch.down_time_us = now;
		local.flags = UI_TOUCH_EVENT_DOWN;
	} else if (report) {
		int old_x = g_touch.x;
		int old_y = g_touch.y;
		map_report(report, &g_touch.x, &g_touch.y);
		if (g_touch.x != old_x || g_touch.y != old_y) {
			local.flags = UI_TOUCH_EVENT_MOVE;
		} else {
			local.flags = UI_TOUCH_EVENT_HOLD;
		}
	} else {
		int dx = g_touch.x - g_touch.down_x;
		int dy = g_touch.y - g_touch.down_y;
		uint64_t duration = now - g_touch.down_time_us;
		local.flags = UI_TOUCH_EVENT_UP;
		if (dx * dx + dy * dy <= UI_TOUCH_TAP_DISTANCE_PX * UI_TOUCH_TAP_DISTANCE_PX &&
		    duration <= UI_TOUCH_TAP_TIMEOUT_US) {
			local.flags |= UI_TOUCH_EVENT_TAP;
		}
		local.duration_us = duration;
		g_touch.active = 0;
	}

	if (local.flags) {
		local.x = g_touch.x;
		local.y = g_touch.y;
		local.down_x = g_touch.down_x;
		local.down_y = g_touch.down_y;
		local.id = g_touch.active_id;
		if (!local.duration_us && g_touch.active) {
			local.duration_us = now - g_touch.down_time_us;
		}
	}
	if (event) *event = local;
	return local.flags;
}

int ui_touch_peek_points(UiTouchPoints *points) {
	UiTouchPoints local;
	memset(&local, 0, sizeof(local));
	if (points) *points = local;
	if (!g_touch.initialized) return 0;
	SceTouchData data;
	memset(&data, 0, sizeof(data));
	if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &data, 1) < 0) return 0;
	if (g_touch.suppress_until_release) {
		if (!find_report(&data, 0, 0)) g_touch.suppress_until_release = 0;
		return 0;
	}
	for (unsigned int i = 0;
	     i < data.reportNum && i < SCE_TOUCH_MAX_REPORT && local.count < 2; i++) {
		const SceTouchReport *report = &data.report[i];
		if (report->info & SCE_TOUCH_REPORT_INFO_HIDE_UPPER_LAYER) continue;
		map_report(report, &local.x[local.count], &local.y[local.count]);
		local.id[local.count] = report->id;
		local.count++;
	}
	if (points) *points = local;
	return local.count;
}

int ui_touch_hit_rect(int x, int y, int rect_x, int rect_y,
	                  int rect_w, int rect_h) {
	if (rect_w <= 0 || rect_h <= 0) return 0;
	return x >= rect_x && x < rect_x + rect_w &&
	       y >= rect_y && y < rect_y + rect_h;
}
