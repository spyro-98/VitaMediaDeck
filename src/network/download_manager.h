#ifndef VITAMEDIADECK_NETWORK_DOWNLOAD_MANAGER_H
#define VITAMEDIADECK_NETWORK_DOWNLOAD_MANAGER_H

#include "network/network_source.h"

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
	volatile long progress_current;
	volatile long progress_total;
	volatile int paused;
	volatile int cancel;
	int direct_url;
} VtDownloadJob;

void vt_download_job_init_network(VtDownloadJob *job,
	                              const VtNetworkSource *source,
	                              const VtNetworkCredential *credential,
	                              const char *path);
void vt_download_job_init_url(VtDownloadJob *job, const char *url);
void vt_download_job_set_destination(VtDownloadJob *job, const char *directory);

/* Suitable for ui_loading_run(). Returns zero only when destination contains
 * a fully flushed file. Direct URLs support explicit HTTP and HTTPS. */
int vt_download_run(void *opaque);

#endif /* VITAMEDIADECK_NETWORK_DOWNLOAD_MANAGER_H */
