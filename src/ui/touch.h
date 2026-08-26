#ifndef VITAWAVE_UI_TOUCH_H
#define VITAWAVE_UI_TOUCH_H

/*
 * Front-panel touch input for vita2d screens. This module doesn't draw and
 * doesn't create threads: ui_touch_poll() must be called from the main
 * thread, once per frame, before updating the UI state.
 */

#define UI_TOUCH_SCREEN_WIDTH  960
#define UI_TOUCH_SCREEN_HEIGHT 544

typedef enum {
	UI_TOUCH_EVENT_NONE = 0,
	UI_TOUCH_EVENT_DOWN = 1 << 0,
	UI_TOUCH_EVENT_MOVE = 1 << 1,
	UI_TOUCH_EVENT_UP   = 1 << 2,
	/* TAP always accompanies UP: a brief, nearly still touch. */
	UI_TOUCH_EVENT_TAP  = 1 << 3,
	/* Contact still active and still: allows long-press without waiting for UP. */
	UI_TOUCH_EVENT_HOLD = 1 << 4
} UiTouchEventFlags;

typedef struct {
	unsigned int flags;
	int x;
	int y;
	int down_x;
	int down_y;
	unsigned int id;
	unsigned long long duration_us;
} UiTouchEvent;

/* Starts sampling on the front panel and reads its calibration.
 * Returns 0 on success, an SceTouch error otherwise. Idempotent. */
int ui_touch_init(void);

/* Restores the sampling state only if it was started by ui_touch_init(). */
void ui_touch_term(void);

/* Clears gesture state at a screen/player boundary. If a contact is already
 * held, it is ignored until release so it cannot become a stale TAP/seek in
 * the destination screen. */
void ui_touch_reset(void);

/* Non-blocking poll. Returns the event flags (0 if nothing changed), and,
 * if not NULL, always initializes `event` even when there's no touch. */
unsigned int ui_touch_poll(UiTouchEvent *event);

/* Hit-test helper in vita2d coordinates. Left/top edge included,
 * right/bottom edge excluded: adjacent rectangles don't overlap. */
int ui_touch_hit_rect(int x, int y, int rect_x, int rect_y,
	                  int rect_w, int rect_h);

#endif /* VITAWAVE_UI_TOUCH_H */
