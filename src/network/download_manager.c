#include "network/download_manager.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>

#include <vita_https.h>

#define DOWNLOAD_ROOT "ux0:download"
#define DOWNLOAD_BUFFER_SIZE (64 * 1024)

static void secure_zero(void *memory, size_t size) {
	volatile unsigned char *bytes = memory;
	while (size--) *bytes++ = 0;
}

static void update_progress(VtDownloadJob *job, int64_t value) {
	if (!job) return;
	job->progress_current = value > LONG_MAX ? LONG_MAX : (long)value;
}

/* A pause is cooperative: never begin another transport read while paused,
 * but allow Abort to wake the worker even when the user pauses mid-transfer. */
static int wait_if_paused(VtDownloadJob *job) {
	while (job && job->paused && !job->cancel)
		sceKernelDelayThread(20 * 1000);
	return job && job->cancel ? -1 : 0;
}

static void filename_from_value(const char *value, char *out, size_t out_size) {
	const char *name = value ? strrchr(value, '/') : NULL;
	name = name ? name + 1 : value;
	if (!name || !name[0]) name = "download";
	char plain[128];
	size_t n = 0;
	while (*name && *name != '?' && *name != '#' && n + 1 < sizeof(plain)) {
		unsigned char c = (unsigned char)*name++;
		plain[n++] = (isalnum(c) || c == '.' || c == '-' || c == '_') ? c : '_';
	}
	plain[n] = '\0';
	if (!plain[0] || !strcmp(plain, ".") || !strcmp(plain, ".."))
		snprintf(plain, sizeof(plain), "download");
	snprintf(out, out_size, "%s", plain);
}

static int make_destination(VtDownloadJob *job, const char *value,
	                        char *part, size_t part_size) {
	char filename[128];
	filename_from_value(value, filename, sizeof(filename));
	const char *directory = job->destination_directory[0]
	                      ? job->destination_directory : DOWNLOAD_ROOT;
	sceIoMkdir("ux0:download", 0777);
	for (int suffix = 0; suffix < 1000; suffix++) {
		char candidate[VT_NETWORK_PATH_MAX];
		if (suffix == 0) snprintf(candidate, sizeof(candidate), "%s/%s", directory, filename);
		else snprintf(candidate, sizeof(candidate), "%s/%d_%s", directory, suffix, filename);
		SceIoStat stat;
		memset(&stat, 0, sizeof(stat));
		if (sceIoGetstat(candidate, &stat) >= 0) continue;
		snprintf(job->destination, sizeof(job->destination), "%s", candidate);
		int written = snprintf(part, part_size, "%s.part", candidate);
		return written > 0 && written < (int)part_size ? 0 : -1;
	}
	snprintf(job->detail, sizeof(job->detail), "No free filename in %s", directory);
	return -1;
}

static int write_chunk(SceUID fd, const void *data, size_t size) {
	const unsigned char *cursor = data;
	while (size) {
		int wrote = sceIoWrite(fd, cursor, size);
		if (wrote <= 0) return -1;
		cursor += wrote;
		size -= (size_t)wrote;
	}
	return 0;
}

static int finish_file(VtDownloadJob *job, SceUID fd, const char *part, int result) {
	if (result == 0 && sceIoSyncByFd(fd, 0) < 0) result = -1;
	if (sceIoClose(fd) < 0 && result == 0) result = -1;
	if (result == 0 && sceIoRename(part, job->destination) < 0) result = -1;
	if (result != 0) sceIoRemove(part);
	if (result < 0 && !job->detail[0])
		snprintf(job->detail, sizeof(job->detail), job->cancel ? "Download aborted" : "Could not save the download");
	return result;
}

static int download_network(VtDownloadJob *job, const char *part) {
	VtNetworkStreamFactory factory;
	VtDecoderStreamHandle stream;
	if (vt_network_stream_factory_init(&factory, &job->source, &job->credential,
	                                  job->remote_path) < 0) return -1;
	memset(&stream, 0, sizeof(stream));
	int result = factory.factory.open_cancelable(factory.factory.opaque, &stream, &job->cancel);
	if (result < 0) {
		snprintf(job->detail, sizeof(job->detail), "Could not open the remote file");
		return result;
	}
	job->progress_total = stream.size > LONG_MAX ? LONG_MAX : (long)stream.size;
	SceUID fd = sceIoOpen(part, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (fd < 0) { stream.close(stream.opaque); return -1; }
	unsigned char buffer[DOWNLOAD_BUFFER_SIZE];
	int64_t transferred = 0;
	while (!job->cancel) {
		if (wait_if_paused(job) < 0) { result = -1; break; }
		int read = stream.read(stream.opaque, buffer, sizeof(buffer));
		if (read < 0) { result = -1; break; }
		if (read == 0) { result = 0; break; }
		if (write_chunk(fd, buffer, (size_t)read) < 0) { result = -1; break; }
		transferred += read;
		update_progress(job, transferred);
	}
	if (job->cancel) {
		if (stream.abort) stream.abort(stream.opaque);
		result = -1;
	}
	stream.close(stream.opaque);
	return finish_file(job, fd, part, result);
}

typedef struct { VtDownloadJob *job; SceUID fd; int64_t transferred; } UrlWrite;

static size_t url_write(const void *data, size_t size, void *opaque) {
	UrlWrite *writer = opaque;
	if (!writer || !writer->job || wait_if_paused(writer->job) < 0 ||
	    write_chunk(writer->fd, data, size) < 0) return 0;
	writer->transferred += (int64_t)size;
	update_progress(writer->job, writer->transferred);
	return size;
}

static int download_url(VtDownloadJob *job, const char *part) {
	VitaHttpsClientConfig config;
	memset(&config, 0, sizeof(config));
	/* Plain HTTP is an explicit direct-download choice. vita-https still keeps
	 * HTTPS verification enabled and forbids HTTPS-to-HTTP redirect downgrades. */
	config.allow_http = 1;
	VitaHttpsClient *client = vita_https_client_create(&config);
	if (!client) return -1;
	/* Probe metadata before GET so direct HTTP(S) downloads get a live progress
	 * bar rather than learning Content-Length only after the transfer ends. */
	VitaHttpsRequest head = { .method = "HEAD", .url = job->url,
		.cancel_flag = &job->cancel };
	VitaHttpsResponse head_response;
	memset(&head_response, 0, sizeof(head_response));
	if (vita_https_perform(client, &head, &head_response) == 0 &&
	    head_response.status_code >= 200 && head_response.status_code < 300 &&
	    head_response.content_length > 0)
		job->progress_total = head_response.content_length > LONG_MAX
		                    ? LONG_MAX : (long)head_response.content_length;
	if (job->cancel) {
		vita_https_client_destroy(client);
		return -1;
	}
	SceUID fd = sceIoOpen(part, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (fd < 0) { vita_https_client_destroy(client); return -1; }
	UrlWrite writer = { job, fd, 0 };
	VitaHttpsRequest request = { .method = "GET", .url = job->url,
		.write = url_write, .write_opaque = &writer, .cancel_flag = &job->cancel };
	VitaHttpsResponse response;
	memset(&response, 0, sizeof(response));
	int result = vita_https_perform(client, &request, &response);
	if (response.content_length > 0)
		job->progress_total = response.content_length > LONG_MAX ? LONG_MAX
		                    : (long)response.content_length;
	if (result < 0 || response.status_code < 200 || response.status_code >= 300) {
		if (!job->detail[0]) snprintf(job->detail, sizeof(job->detail), "%s",
		                              job->cancel ? "Download cancelled" : vita_https_error_string(result));
		result = -1;
	} else result = 0;
	vita_https_client_destroy(client);
	return finish_file(job, fd, part, result);
}

void vt_download_job_init_network(VtDownloadJob *job, const VtNetworkSource *source,
	                              const VtNetworkCredential *credential, const char *path) {
	if (!job) return;
	memset(job, 0, sizeof(*job));
	if (source) job->source = *source;
	if (credential) job->credential = *credential;
	if (path) snprintf(job->remote_path, sizeof(job->remote_path), "%s", path);
}

void vt_download_job_init_url(VtDownloadJob *job, const char *url) {
	if (!job) return;
	memset(job, 0, sizeof(*job));
	job->direct_url = 1;
	if (url) snprintf(job->url, sizeof(job->url), "%s", url);
}

void vt_download_job_set_destination(VtDownloadJob *job, const char *directory) {
	if (!job) return;
	job->destination_directory[0] = '\0';
	if (directory && directory[0])
		snprintf(job->destination_directory, sizeof(job->destination_directory), "%s",
		         directory);
}

int vt_download_run(void *opaque) {
	VtDownloadJob *job = opaque;
	if (!job) return -1;
	const char *value = job->direct_url ? job->url : job->remote_path;
	if (!value || !value[0]) return -1;
	if (job->direct_url && strncmp(value, "https://", 8) != 0 &&
	    strncmp(value, "http://", 7) != 0) {
		snprintf(job->detail, sizeof(job->detail), "Only HTTP and HTTPS URLs are accepted");
		return -1;
	}
	char part[VT_NETWORK_PATH_MAX + 8];
	if (make_destination(job, value, part, sizeof(part)) < 0) return -1;
	int result = job->direct_url ? download_url(job, part) : download_network(job, part);
	secure_zero(&job->credential, sizeof(job->credential));
	return result;
}
