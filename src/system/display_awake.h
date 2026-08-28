#ifndef VITAMEDIADECK_SYSTEM_DISPLAY_AWAKE_H
#define VITAMEDIADECK_SYSTEM_DISPLAY_AWAKE_H

/* Refreshes every display-related idle timer while video is visibly playing.
 * Internally rate-limited, so callers may invoke it once per rendered frame. */
void vt_display_keep_awake_tick(void);

#endif /* VITAMEDIADECK_SYSTEM_DISPLAY_AWAKE_H */
