#ifndef VITAMEDIADECK_NETWORK_DOWNLOAD_MANAGER_H
#define VITAMEDIADECK_NETWORK_DOWNLOAD_MANAGER_H

#include <stdint.h>

#include "network/network_source.h"
#include "network/online_audio.h"

/* Transfers first use a sibling .part file and only publish the final name
 * after a successful flush, so an interrupted transfer never looks complete
 * in the media browser. */
typedef struct {
	VtNetworkSource source;
	VtNetworkCredential credential;
	char remote_path[VT_NETWORK_PATH_MAX];
	char url[2048];
	char destination_directory[VT_NETWORK_PATH_MAX];
	char destination[VT_NETWORK_PATH_MAX];
	char detail[192];
	/* sequence protects the 64-bit counters from torn reads on Vita's 32-bit
	 * ARM CPU. Writers publish odd/even generations; the UI retries snapshots. */
	volatile uint32_t progress_sequence;
	uint64_t progress_current;
	uint64_t progress_total;
	/* The resolved response filename is published independently of progress.
	 * HTTP can replace the URL basename after GET response headers arrive. */
	volatile uint32_t metadata_sequence;
	char display_title[128];
	char published_destination[VT_NETWORK_PATH_MAX];
	volatile int paused;
	volatile int cancel;
	int direct_url;
	int background;
	uint64_t keep_awake_last_us;
	/* Owned by the background job; RSS data survives leaving its screen. */
	VtOnlineItem podcast;
	char podcast_show[160];
} VtDownloadJob;

typedef enum {
	VT_DOWNLOAD_BACKGROUND_IDLE = 0,
	VT_DOWNLOAD_BACKGROUND_RUNNING,
	VT_DOWNLOAD_BACKGROUND_COMPLETE,
	VT_DOWNLOAD_BACKGROUND_FAILED,
	VT_DOWNLOAD_BACKGROUND_ABORTED
} VtDownloadBackgroundState;

typedef struct {
	VtDownloadBackgroundState state;
	uint64_t current;
	uint64_t total;
	int paused;
	int result;
	char title[128];
	char destination[VT_NETWORK_PATH_MAX];
	char detail[192];
} VtDownloadBackgroundStatus;

void vt_download_job_init_network(VtDownloadJob *job,
	                              const VtNetworkSource *source,
	                              const VtNetworkCredential *credential,
	                              const char *path);
void vt_download_job_init_url(VtDownloadJob *job, const char *url);
void vt_download_job_set_destination(VtDownloadJob *job, const char *directory);
void vt_download_progress_snapshot(void *opaque, uint64_t *current,
	                               uint64_t *total);

/* Suitable for ui_loading_run(). Returns zero only when destination contains
 * a fully flushed file. Direct URLs support explicit HTTP and HTTPS. */
int vt_download_run(void *opaque);

/* One bounded background transfer. It owns a copy of job and its credentials
 * until completion, so callers may immediately leave the initiating screen. */
int vt_download_background_start(const VtDownloadJob *job);
void vt_download_background_status(VtDownloadBackgroundStatus *status);
int vt_download_background_active(void);
void vt_download_background_toggle_pause(void);
void vt_download_background_abort(void);
void vt_download_background_acknowledge(void);
void vt_download_background_shutdown(void);

#endif /* VITAMEDIADECK_NETWORK_DOWNLOAD_MANAGER_H */
