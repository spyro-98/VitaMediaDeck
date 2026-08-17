#ifndef VITATUBE_UI_LOADING_SCREEN_H
#define VITATUBE_UI_LOADING_SCREEN_H

#include <stddef.h>
#include <stdint.h>

/* Operation run on a separate thread while the main thread keeps redrawing
 * the loading screen. */
typedef int (*UiLoadingTaskFn)(void *ctx);
typedef void (*UiLoadingProgressTickFn)(long current, long total);
typedef void (*UiPlayerLoadingDetailFn)(char *out, size_t out_size,
	                                    void *ctx, uint64_t now_us);

/* Minimal data for the player canvas. The same composition is used while a
 * local or authenticated remote source opens and while the decoder waits for
 * its first frame. */
typedef struct {
	const char *title;
	const char *channel;
	const char *status;
	int quality_height; /* 0 = quality still in automatic resolution */
	int cancellable;
	UiLoadingProgressTickFn progress_tick; /* optional, run on the UI thread */
	/* Optional one-line live diagnostics, formatted on the UI thread from
	 * atomic/volatile counters owned by the worker. */
	UiPlayerLoadingDetailFn detail_text;
	void *detail_ctx;
} UiPlayerLoadingInfo;

/* Presents a single loading frame immediately. Needed before system
 * bootstrap steps that must stay on the main thread. */
void ui_loading_present(const char *message);

/* Runs task(ctx) while showing an animated graphical screen. progress_current
 * and progress_total are optional and, when set, enable a percentage and
 * progress bar. cancel_flag is optional: CIRCLE sets it to 1 and the
 * task can stop cooperatively. */
int ui_loading_run(const char *message,
                   UiLoadingTaskFn task,
                   void *ctx,
                   volatile int *cancel_flag,
                   const volatile long *progress_current,
                   const volatile long *progress_total);

/* Like ui_loading_run(), but keeps caller-provided context in the header. */
int ui_loading_run_with_query(const char *query,
                              const char *message,
                              UiLoadingTaskFn task,
                              void *ctx,
                              volatile int *cancel_flag,
                              const volatile long *progress_current,
                              const volatile long *progress_total);

/* Full-screen version dedicated to video. No panel or dialog: title top
 * left, quality bottom right, centered spinner, and cyan progress on the
 * timeline. */
void ui_player_loading_draw(const UiPlayerLoadingInfo *info,
                            const volatile long *progress_current,
                            const volatile long *progress_total,
                            uint64_t now_us);
void ui_player_loading_present(const UiPlayerLoadingInfo *info,
                               const volatile long *progress_current,
                               const volatile long *progress_total);
int ui_player_loading_run(const UiPlayerLoadingInfo *info,
                          UiLoadingTaskFn task,
                          void *ctx,
                          volatile int *cancel_flag,
                          const volatile long *progress_current,
                          const volatile long *progress_total);

/* Short graphical message for errors or final states. */
void ui_message_show(const char *message, const char *detail, int duration_ms);

/* Primitive shared with the grid: VitaTube orbital indicator. Must be
 * called inside an already-open vita2d scene. */
void ui_draw_spinner(float center_x, float center_y, uint64_t now_us);
void ui_draw_spinner_compact(float center_x, float center_y, uint64_t now_us);

#endif /* VITATUBE_UI_LOADING_SCREEN_H */
