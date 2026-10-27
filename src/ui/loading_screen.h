#ifndef VITAMEDIADECK_UI_LOADING_SCREEN_H
#define VITAMEDIADECK_UI_LOADING_SCREEN_H

#include <stddef.h>
#include <stdint.h>

/* Operation run on a separate thread while the main thread keeps redrawing
 * the loading screen. */
typedef int (*UiLoadingTaskFn)(void *ctx);
/* Infrastructure failure, outside provider/transport result-code ranges.
 * The task was not run; callers may report it without confusing it with DNS,
 * TLS, a malformed playlist or a decoder error. */
enum { UI_LOADING_TASK_FAILED = -0x7401 };
typedef void (*UiLoadingProgressTickFn)(long current, long total);
typedef void (*UiDownloadProgressReadFn)(void *ctx, uint64_t *current,
	                                     uint64_t *total);
typedef void (*UiPlayerLoadingDetailFn)(char *out, size_t out_size,
	                                    void *ctx, uint64_t now_us);
typedef void (*UiPlayerLoadingCancelFn)(void *ctx);

/* Minimal data for the player canvas. The same composition is used while a
 * local or authenticated remote source opens and while the decoder waits for
 * its first frame. */
typedef struct {
	const char *title;
	const char *channel;
	const char *status;
	int quality_height; /* 0 = quality still in automatic resolution */
	int cancellable;
	/* Optional action shown only for a recovered video. CROSS requests that the
	 * current open be cancelled and retried from timestamp zero. */
	volatile int *start_over_requested;
	UiLoadingProgressTickFn progress_tick; /* optional, run on the UI thread */
	/* Optional one-line live diagnostics, formatted on the UI thread from
	 * atomic/volatile counters owned by the worker. */
	UiPlayerLoadingDetailFn detail_text;
	void *detail_ctx;
	/* Optional immediate transport interrupt. The UI still sets cancel_flag;
	 * this hook wakes a decoder blocked inside a range/socket read. */
	UiPlayerLoadingCancelFn cancel_action;
	void *cancel_ctx;
	/* Optional UI-owned frozen canvas. This callback must never touch a decoder
	 * while its seek/restart worker is active. */
	void (*draw_backdrop)(void *ctx);
	void *backdrop_ctx;
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

/* Download-specific loading view. START toggles pause/resume; CIRCLE aborts.
 * It displays byte progress, transfer speed and ETA when the total is known,
 * and keeps the display/automatic-suspend timers awake until the task ends.
 * The worker is responsible for observing pause_flag cooperatively. */
int ui_loading_run_download(const char *message,
                            UiLoadingTaskFn task,
                            void *ctx,
                            volatile int *pause_flag,
                            volatile int *cancel_flag,
                            UiDownloadProgressReadFn progress_read,
                            void *progress_ctx);

/* Opens the app-level transfer page for the single active download. TRIANGLE
 * returns to navigation without stopping it; START pauses/resumes and CIRCLE
 * aborts. L1 opens global section navigation. Returns 1 when detached, 0 after
 * a terminal result, UI_LOADING_DOWNLOAD_SECTION_BASE + section after global
 * navigation, and -1 when no transfer exists. */
#define UI_LOADING_DOWNLOAD_SECTION_BASE 10
int ui_loading_monitor_background_download(void);

/* Full-screen version dedicated to video. No panel or dialog: title top
 * left, quality bottom right, centered acquisition mark, and signal progress on the
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

/* Short, interruptible full-page error message. */
void ui_message_show(const char *message, const char *detail, int duration_ms);
void ui_message_show_success(const char *message, const char *detail,
	                         int duration_ms);

/* Primitive shared with the grid: VitaMediaDeck orbital indicator. Must be
 * called inside an already-open vita2d scene. */
void ui_draw_spinner(float center_x, float center_y, uint64_t now_us);
void ui_draw_spinner_compact(float center_x, float center_y, uint64_t now_us);

#endif /* VITAMEDIADECK_UI_LOADING_SCREEN_H */
