#include "network/download_manager.h"

#include <ctype.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>

#include <vita_https.h>

#include "app_paths.h"
#include "common/text_log.h"
#include "media/podcast_metadata.h"

#define DOWNLOAD_ROOT "ux0:download"
#define DOWNLOAD_BUFFER_SIZE (64 * 1024)
#define DOWNLOAD_BACKGROUND_PRIORITY 0xB0
#define DOWNLOAD_AWAKE_INTERVAL_US (750ULL * 1000ULL)

static void secure_zero(void *memory, size_t size) {
	volatile unsigned char *bytes = memory;
	while (size--) *bytes++ = 0;
}

static void keep_awake(VtDownloadJob *job) {
	if (!job) return;
	uint64_t now = sceKernelGetProcessTimeWide();
	if (job->keep_awake_last_us &&
	    now - job->keep_awake_last_us < DOWNLOAD_AWAKE_INTERVAL_US) return;
	sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_AUTO_SUSPEND);
	sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_OLED_OFF);
	sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DISABLE_OLED_DIMMING);
	job->keep_awake_last_us = now;
}

static void background_cooperate(VtDownloadJob *job) {
	keep_awake(job);
	/* Playback and its network prefetcher remain latency-sensitive. A tiny yield
	 * after each disk chunk prevents a detached bulk transfer from monopolising
	 * CPU time when the mini-player is also decoding. */
	if (job && job->background) sceKernelDelayThread(1000);
}

static void log_url_summary(const char *event, const char *url) {
	char safe[320];
	const char *end = url ? strpbrk(url, "?#") : NULL;
	size_t length = url ? (end ? (size_t)(end - url) : strlen(url)) : 0;
	if (length >= sizeof(safe)) length = sizeof(safe) - 1;
	if (length) memcpy(safe, url, length);
	safe[length] = '\0';
	log_printf("download http: %s url=%s%s\n", event ? event : "event",
	           safe[0] ? safe : "(empty)", end ? " [query redacted]" : "");
}

static void publish_progress(VtDownloadJob *job, int set_current,
	                         uint64_t current, int set_total, uint64_t total) {
	if (!job) return;
	uint32_t generation = job->progress_sequence;
	job->progress_sequence = generation + 1U;
	__sync_synchronize();
	if (set_current) job->progress_current = current;
	if (set_total) job->progress_total = total;
	__sync_synchronize();
	job->progress_sequence = generation + 2U;
}

static void publish_metadata(VtDownloadJob *job, const char *title,
	                         const char *destination) {
	if (!job) return;
	uint32_t generation = job->metadata_sequence;
	job->metadata_sequence = generation + 1U;
	__sync_synchronize();
	if (title)
		snprintf(job->display_title, sizeof(job->display_title), "%s", title);
	if (destination)
		snprintf(job->published_destination,
		         sizeof(job->published_destination), "%s", destination);
	__sync_synchronize();
	job->metadata_sequence = generation + 2U;
}

static void snapshot_metadata(VtDownloadJob *job,
	                          char *title, size_t title_size,
	                          char *destination, size_t destination_size) {
	if (title && title_size) title[0] = '\0';
	if (destination && destination_size) destination[0] = '\0';
	if (!job) return;
	uint32_t before, after;
	do {
		before = job->metadata_sequence;
		__sync_synchronize();
		if (title && title_size)
			snprintf(title, title_size, "%s", job->display_title);
		if (destination && destination_size)
			snprintf(destination, destination_size, "%s",
			         job->published_destination);
		__sync_synchronize();
		after = job->metadata_sequence;
	} while ((before & 1U) || before != after);
}

static void update_progress(VtDownloadJob *job, int64_t value) {
	publish_progress(job, 1, value > 0 ? (uint64_t)value : 0ULL, 0, 0ULL);
}

static void update_progress_total(VtDownloadJob *job, int64_t value) {
	publish_progress(job, 0, 0ULL, 1, value > 0 ? (uint64_t)value : 0ULL);
}

void vt_download_progress_snapshot(void *opaque, uint64_t *current,
	                               uint64_t *total) {
	VtDownloadJob *job = opaque;
	uint64_t current_value = 0, total_value = 0;
	if (job) {
		uint32_t before, after;
		do {
			before = job->progress_sequence;
			__sync_synchronize();
			current_value = job->progress_current;
			total_value = job->progress_total;
			__sync_synchronize();
			after = job->progress_sequence;
		} while ((before & 1U) || before != after);
	}
	if (current) *current = current_value;
	if (total) *total = total_value;
}

/* A pause is cooperative: never begin another transport read while paused,
 * but allow Abort to wake the worker even when the user pauses mid-transfer. */
static int wait_if_paused(VtDownloadJob *job) {
	while (job && job->paused && !job->cancel) {
		keep_awake(job);
		sceKernelDelayThread(20 * 1000);
	}
	return job && job->cancel ? -1 : 0;
}

static int hex_value(unsigned char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static void percent_decode(const char *source, char *out, size_t out_size) {
	if (!out || !out_size) return;
	size_t used = 0;
	while (source && *source && used + 1 < out_size) {
		int high = source[0] == '%' ? hex_value((unsigned char)source[1]) : -1;
		int low = high >= 0 && source[2]
		        ? hex_value((unsigned char)source[2]) : -1;
		if (high >= 0 && low >= 0) {
			unsigned char decoded = (unsigned char)((high << 4) | low);
			out[used++] = decoded ? (char)decoded : '_';
			source += 3;
		} else out[used++] = *source++;
	}
	out[used] = '\0';
}

static void sanitize_filename(const char *source, char *out, size_t out_size) {
	if (!out || !out_size) return;
	const char *name = source ? source : "";
	for (const char *cursor = name; *cursor; cursor++)
		if (*cursor == '/' || *cursor == '\\') name = cursor + 1;
	while (*name == ' ' || *name == '\t') name++;
	size_t used = 0;
	while (*name && used + 1 < out_size) {
		unsigned char c = (unsigned char)*name++;
		if (c < 32 || c == 127 || strchr("<>:\"/\\|?*", c)) c = '_';
		out[used++] = (char)c;
	}
	while (used && (out[used - 1] == ' ' || out[used - 1] == '.')) used--;
	out[used] = '\0';
	if (!strcmp(out, ".") || !strcmp(out, "..")) out[0] = '\0';
}

static int disposition_parameter(const char *header, const char *wanted,
	                             char *out, size_t out_size) {
	if (!header || !wanted || !out || !out_size) return 0;
	out[0] = '\0';
	const char *cursor = header;
	while ((cursor = strchr(cursor, ';')) != NULL) {
		cursor++;
		while (*cursor == ' ' || *cursor == '\t') cursor++;
		const char *name = cursor;
		while (*cursor && *cursor != '=' && *cursor != ';') cursor++;
		const char *name_end = cursor;
		while (name_end > name && (name_end[-1] == ' ' || name_end[-1] == '\t'))
			name_end--;
		if (*cursor != '=') continue;
		int matches = (size_t)(name_end - name) == strlen(wanted) &&
		              strncasecmp(name, wanted, (size_t)(name_end - name)) == 0;
		cursor++;
		while (*cursor == ' ' || *cursor == '\t') cursor++;
		char quote = *cursor == '\'' || *cursor == '"' ? *cursor++ : '\0';
		size_t used = 0;
		while (*cursor && used + 1 < out_size) {
			if (quote && *cursor == quote) break;
			if (!quote && *cursor == ';') break;
			if (quote && *cursor == '\\' && cursor[1]) cursor++;
			if (matches) out[used++] = *cursor;
			cursor++;
		}
		if (matches) {
			while (used && (out[used - 1] == ' ' || out[used - 1] == '\t')) used--;
			out[used] = '\0';
			return used > 0;
		}
	}
	return 0;
}

static int filename_from_disposition(const char *header, char *out,
	                                 size_t out_size) {
	char raw[384], decoded[384];
	if (disposition_parameter(header, "filename*", raw, sizeof(raw))) {
		const char *encoded = raw;
		const char *first_quote = strchr(raw, '\'');
		const char *second_quote = first_quote ? strchr(first_quote + 1, '\'') : NULL;
		if (second_quote) encoded = second_quote + 1;
		percent_decode(encoded, decoded, sizeof(decoded));
		sanitize_filename(decoded, out, out_size);
		if (out[0]) return 1;
	}
	if (disposition_parameter(header, "filename", raw, sizeof(raw))) {
		sanitize_filename(raw, out, out_size);
		return out[0] != '\0';
	}
	return 0;
}

static void filename_from_value(const char *value, char *out, size_t out_size) {
	const char *name = value ? strrchr(value, '/') : NULL;
	name = name ? name + 1 : value;
	if (!name || !name[0]) name = "download";
	char encoded[384], decoded[384];
	size_t used = 0;
	while (name[used] && name[used] != '?' && name[used] != '#' &&
	       used + 1 < sizeof(encoded)) {
		encoded[used] = name[used];
		used++;
	}
	encoded[used] = '\0';
	percent_decode(encoded, decoded, sizeof(decoded));
	sanitize_filename(decoded, out, out_size);
	if (!out[0]) snprintf(out, out_size, "download");
}

static int make_destination(VtDownloadJob *job, const char *value,
	                        const char *preferred_filename,
	                        char *part, size_t part_size) {
	char filename[128];
	if (preferred_filename && preferred_filename[0])
		sanitize_filename(preferred_filename, filename, sizeof(filename));
	else filename[0] = '\0';
	if (!filename[0]) filename_from_value(value, filename, sizeof(filename));
	const char *directory = job->destination_directory[0]
	                      ? job->destination_directory : DOWNLOAD_ROOT;
	for (int suffix = 0; suffix < 1000; suffix++) {
		char candidate[VT_NETWORK_PATH_MAX];
		int candidate_length = suffix == 0
		                     ? snprintf(candidate, sizeof(candidate), "%s/%s", directory, filename)
		                     : snprintf(candidate, sizeof(candidate), "%s/%d_%s", directory,
		                                suffix, filename);
		if (candidate_length <= 0 || candidate_length >= (int)sizeof(candidate)) {
			snprintf(job->detail, sizeof(job->detail), "Destination path is too long");
			log_printf("download destination rejected: directory_length=%u filename_length=%u\n",
			           (unsigned)strlen(directory), (unsigned)strlen(filename));
			return -1;
		}
		SceIoStat stat;
		memset(&stat, 0, sizeof(stat));
		if (sceIoGetstat(candidate, &stat) >= 0) continue;
		if (!vt_podcast_destination_available(candidate)) continue;
		snprintf(job->destination, sizeof(job->destination), "%s", candidate);
		publish_metadata(job, filename, candidate);
		int written = snprintf(part, part_size, "%s.part", candidate);
		return written > 0 && written < (int)part_size ? 0 : -1;
	}
	snprintf(job->detail, sizeof(job->detail), "No free destination filename is available");
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
	int sync_result = result == 0 ? sceIoSyncByFd(fd, 0) : 0;
	if (sync_result < 0) result = sync_result;
	int close_result = sceIoClose(fd);
	if (close_result < 0 && result == 0) result = close_result;
	int rename_result = result == 0 ? sceIoRename(part, job->destination) : 0;
	if (rename_result < 0) result = rename_result;
	if (result != 0) sceIoRemove(part);
	if (result < 0 && !job->detail[0])
		snprintf(job->detail, sizeof(job->detail), job->cancel ? "Download aborted" : "Could not save the download");
	log_printf("download file: result=0x%08X sync=0x%08X close=0x%08X rename=0x%08X destination=%s\n",
	           (unsigned)result, (unsigned)sync_result, (unsigned)close_result,
	           (unsigned)rename_result,
	           job && job->destination[0] ? job->destination : "(unset)");
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
	update_progress_total(job, stream.size > INT64_MAX ? INT64_MAX
	                                            : (int64_t)stream.size);
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
		background_cooperate(job);
	}
	if (job->cancel) {
		if (stream.abort) stream.abort(stream.opaque);
		result = -1;
	}
	stream.close(stream.opaque);
	return finish_file(job, fd, part, result);
}

typedef struct {
	VtDownloadJob *job;
	SceUID fd;
	int64_t transferred;
	int write_error;
	VitaHttpsResponse *response;
	char head_filename[128];
	char part[VT_NETWORK_PATH_MAX + 8];
} UrlWrite;

static int prepare_url_output(UrlWrite *writer) {
	if (!writer || !writer->job) return -1;
	if (writer->fd >= 0) return 0;
	char response_filename[128] = "";
	const char *preferred = writer->head_filename;
	if (writer->response &&
	    filename_from_disposition(writer->response->content_disposition,
	                              response_filename, sizeof(response_filename)))
		preferred = response_filename;
	if (make_destination(writer->job, writer->job->url, preferred,
	                     writer->part, sizeof(writer->part)) < 0) {
		writer->write_error = 1;
		return -1;
	}
	writer->fd = sceIoOpen(writer->part,
	                       SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (writer->fd < 0) {
		snprintf(writer->job->detail, sizeof(writer->job->detail),
		         "Could not create temporary file (0x%08X)",
		         (unsigned)writer->fd);
		log_printf("download http: open part failed ret=0x%08X path=%s\n",
		           (unsigned)writer->fd, writer->part);
		writer->write_error = 1;
		return writer->fd;
	}
	log_printf("download http: filename source=%s name=%s destination=%s\n",
	           response_filename[0] ? "GET Content-Disposition"
	           : writer->head_filename[0] ? "HEAD Content-Disposition"
	                                      : "URL fallback",
	           preferred && preferred[0] ? preferred : "(URL basename)",
	           writer->job->destination);
	return 0;
}

static size_t url_write(const void *data, size_t size, void *opaque) {
	UrlWrite *writer = opaque;
	if (!writer || !writer->job || wait_if_paused(writer->job) < 0) return 0;
	if (prepare_url_output(writer) < 0) return 0;
	if (write_chunk(writer->fd, data, size) < 0) {
		writer->write_error = 1;
		return 0;
	}
	writer->transferred += (int64_t)size;
	update_progress(writer->job, writer->transferred);
	background_cooperate(writer->job);
	return size;
}

static int url_progress(int64_t downloaded, int64_t total, void *opaque) {
	UrlWrite *writer = opaque;
	if (!writer || !writer->job) return 1;
	if (total > 0) update_progress_total(writer->job, total);
	/* The write callback remains authoritative for bytes safely written to disk;
	 * this callback only publishes the response total before GET completes. */
	(void)downloaded;
	return writer->job->cancel ? 1 : 0;
}

static int download_url(VtDownloadJob *job) {
	VitaHttpsClientConfig config;
	memset(&config, 0, sizeof(config));
	/* Plain HTTP is an explicit direct-download choice. vita-https still keeps
	 * HTTPS verification enabled and forbids HTTPS-to-HTTP redirect downgrades. */
	config.allow_http = 1;
	config.user_agent = "VitaMediaDeck/1.4.1";
	/* Downloads are bounded by cancellation and the low-speed watchdog, not the
	 * generic request API's 20-second wall-clock timeout. LONG_MAX is roughly
	 * 24 days on Vita and leaves genuinely stalled transfers detectable. */
	config.request_timeout_ms = LONG_MAX;
	config.low_speed_bytes_per_second = 1;
	config.low_speed_seconds = 60;
	log_url_summary("start", job->url);
	VitaHttpsClient *client = vita_https_client_create(&config);
	if (!client) {
		snprintf(job->detail, sizeof(job->detail), "HTTP client unavailable");
		log_printf("download http: client creation failed\n");
		return -1;
	}
	/* Probe metadata before GET so direct HTTP(S) downloads get a live progress
	 * bar rather than learning Content-Length only after the transfer ends. */
	VitaHttpsRequest head = { .method = "HEAD", .url = job->url,
		.cancel_flag = &job->cancel };
	VitaHttpsResponse head_response;
	memset(&head_response, 0, sizeof(head_response));
	int head_result = vita_https_perform(client, &head, &head_response);
	log_printf("download http: HEAD ret=%d status=%ld length=%lld\n",
	           head_result, head_response.status_code,
	           (long long)head_response.content_length);
	if (head_result == 0 &&
	    head_response.status_code >= 200 && head_response.status_code < 300 &&
	    head_response.content_length > 0)
		update_progress_total(job, head_response.content_length);
	if (job->cancel) {
		vita_https_client_destroy(client);
		return -1;
	}
	UrlWrite writer;
	memset(&writer, 0, sizeof(writer));
	writer.job = job;
	writer.fd = -1;
	filename_from_disposition(head_response.content_disposition,
	                          writer.head_filename,
	                          sizeof(writer.head_filename));
	VitaHttpsRequest request = { .method = "GET", .url = job->url,
		.write = url_write, .write_opaque = &writer,
		.progress = url_progress, .progress_opaque = &writer,
		.cancel_flag = &job->cancel };
	VitaHttpsResponse response;
	memset(&response, 0, sizeof(response));
	writer.response = &response;
	int result = vita_https_perform(client, &request, &response);
	if (response.content_length > 0)
		update_progress_total(job, response.content_length);
	if (result == 0 && response.status_code >= 200 && response.status_code < 300 &&
	    writer.fd < 0 && prepare_url_output(&writer) < 0)
		result = -1;
	if (result < 0 || response.status_code < 200 || response.status_code >= 300) {
		if (!job->detail[0]) {
			if (job->cancel)
				snprintf(job->detail, sizeof(job->detail), "Download cancelled");
			else if (writer.write_error)
				snprintf(job->detail, sizeof(job->detail), "Could not write the downloaded data");
			else if (response.status_code > 0)
				snprintf(job->detail, sizeof(job->detail), "HTTP %ld: %s",
				         response.status_code, vita_https_error_string(result));
			else
				snprintf(job->detail, sizeof(job->detail), "%s",
				         vita_https_error_string(result));
		}
		result = -1;
	} else result = 0;
	log_printf("download http: GET ret=%d status=%ld length=%lld received=%lld cancel=%d write_error=%d detail=%s\n",
	           result, response.status_code, (long long)response.content_length,
	           (long long)writer.transferred, job->cancel, writer.write_error,
	           job->detail[0] ? job->detail : "ok");
	if (response.effective_url[0]) log_url_summary("effective", response.effective_url);
	vita_https_client_destroy(client);
	if (writer.fd >= 0) return finish_file(job, writer.fd, writer.part, result);
	return result;
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
	keep_awake(job);
	const char *value = job->direct_url ? job->url : job->remote_path;
	if (!value || !value[0]) return -1;
	if (job->direct_url && strncmp(value, "https://", 8) != 0 &&
	    strncmp(value, "http://", 7) != 0) {
		snprintf(job->detail, sizeof(job->detail), "Only HTTP and HTTPS URLs are accepted");
		return -1;
	}
	char part[VT_NETWORK_PATH_MAX + 8] = "";
	if (!job->direct_url &&
	    make_destination(job, value, NULL, part, sizeof(part)) < 0) return -1;
	log_printf("download start: type=%s destination=%s part=%s\n",
	           job->direct_url ? "http" : "network",
	           job->direct_url ? job->destination_directory : job->destination,
	           job->direct_url ? "(deferred until response headers)" : part);
	int result = job->direct_url ? download_url(job) : download_network(job, part);
	if (result == 0 && job->podcast.kind == VT_ONLINE_EPISODE) {
		VtPodcastMetadata metadata = {0};
		snprintf(metadata.title,sizeof(metadata.title),"%s",job->podcast.title);
		snprintf(metadata.author,sizeof(metadata.author),"%s",job->podcast.author);
		snprintf(metadata.show,sizeof(metadata.show),"%s",job->podcast_show);
		snprintf(metadata.description,sizeof(metadata.description),"%s",job->podcast.description);
		metadata.duration_ms=job->podcast.duration_ms;
		snprintf(metadata.published_date,sizeof(metadata.published_date),"%s",job->podcast.published_date);
		int cover_result = 0;
		keep_awake(job);
		if (job->podcast.artwork_url[0]) {
			cover_result = wait_if_paused(job) < 0 ? -1 :
			    vt_online_fetch_cover(&job->podcast, &job->cancel);
		}
		int metadata_result = vt_podcast_metadata_save(job->destination, &metadata,
		    strcmp(job->podcast.artwork_path,"-") ? job->podcast.artwork_path : "", &job->cancel);
		keep_awake(job);
		if (cover_result < 0 || metadata_result != 0)
			snprintf(job->detail,sizeof(job->detail),"Audio saved; podcast metadata or cover could not be saved");
		log_printf("podcast download extras: metadata=%d cover=%d cancelled=%d\n",
		    metadata_result,cover_result,job->cancel);
	}
	uint64_t progress_current = 0, progress_total = 0;
	vt_download_progress_snapshot(job, &progress_current, &progress_total);
	log_printf("download end: type=%s result=0x%08X current=%llu total=%llu paused=%d cancel=%d detail=%s\n",
	           job->direct_url ? "http" : "network", (unsigned)result,
	           (unsigned long long)progress_current,
	           (unsigned long long)progress_total, job->paused, job->cancel,
	           job->detail[0] ? job->detail : "ok");
	secure_zero(&job->credential, sizeof(job->credential));
	return result;
}

typedef struct {
	pthread_mutex_t mutex;
	pthread_t thread;
	int thread_valid;
	int done;
	int result;
	VtDownloadJob job;
} BackgroundDownload;

static BackgroundDownload g_background = {
	.mutex = PTHREAD_MUTEX_INITIALIZER,
	.result = -1
};

static void *background_download_thread(void *opaque) {
	BackgroundDownload *background = opaque;
	int priority_before = sceKernelGetThreadCurrentPriority();
	int priority_ret = sceKernelChangeThreadPriority(
	    sceKernelGetThreadId(), DOWNLOAD_BACKGROUND_PRIORITY);
	log_printf("download background: thread priority %d -> %d ret=0x%08X\n",
	           priority_before, sceKernelGetThreadCurrentPriority(),
	           (unsigned)priority_ret);
	int result = vt_download_run(&background->job);
	pthread_mutex_lock(&background->mutex);
	background->result = result;
	background->done = 1;
	pthread_mutex_unlock(&background->mutex);
	log_save(VITAMEDIADECK_SESSION_LOG_PATH);
	return NULL;
}

static void background_reap_finished(void) {
	pthread_t thread;
	int join = 0;
	pthread_mutex_lock(&g_background.mutex);
	if (g_background.thread_valid && g_background.done) {
		thread = g_background.thread;
		g_background.thread_valid = 0;
		join = 1;
	}
	pthread_mutex_unlock(&g_background.mutex);
	if (join) pthread_join(thread, NULL);
}

int vt_download_background_start(const VtDownloadJob *job) {
	if (!job) return -1;
	background_reap_finished();
	pthread_mutex_lock(&g_background.mutex);
	if (g_background.thread_valid) {
		pthread_mutex_unlock(&g_background.mutex);
		return -1;
	}
	/* A completed detached transfer has already been logged. Starting the next
	 * job acknowledges that terminal state without requiring a modal popup. */
	if (g_background.done) {
		secure_zero(&g_background.job, sizeof(g_background.job));
		g_background.done = 0;
	}
	g_background.job = *job;
	g_background.job.paused = 0;
	g_background.job.cancel = 0;
	g_background.job.background = 1;
	g_background.done = 0;
	g_background.result = -1;
	pthread_attr_t attr;
	int ret = pthread_attr_init(&attr);
	int attr_ready = ret == 0;
	if (attr_ready) ret = pthread_attr_setstacksize(&attr, 0x40000);
	if (ret == 0)
		ret = pthread_create(&g_background.thread, &attr,
		                     background_download_thread, &g_background);
	if (ret == 0) g_background.thread_valid = 1;
	if (attr_ready) pthread_attr_destroy(&attr);
	pthread_mutex_unlock(&g_background.mutex);
	return ret == 0 ? 0 : -ret;
}

void vt_download_background_status(VtDownloadBackgroundStatus *status) {
	if (!status) return;
	background_reap_finished();
	memset(status, 0, sizeof(*status));
	pthread_mutex_lock(&g_background.mutex);
	if (!g_background.thread_valid && !g_background.done) {
		status->state = VT_DOWNLOAD_BACKGROUND_IDLE;
		pthread_mutex_unlock(&g_background.mutex);
		return;
	}
	status->result = g_background.result;
	status->paused = g_background.job.paused;
	if (!g_background.done) status->state = VT_DOWNLOAD_BACKGROUND_RUNNING;
	else if (g_background.result == 0) status->state = VT_DOWNLOAD_BACKGROUND_COMPLETE;
	else if (g_background.job.cancel) status->state = VT_DOWNLOAD_BACKGROUND_ABORTED;
	else status->state = VT_DOWNLOAD_BACKGROUND_FAILED;
	vt_download_progress_snapshot(&g_background.job, &status->current, &status->total);
	snapshot_metadata(&g_background.job, status->title, sizeof(status->title),
	                  status->destination, sizeof(status->destination));
	/* detail is only stable after the worker has stopped. */
	if (g_background.done)
		snprintf(status->detail, sizeof(status->detail), "%s",
		         g_background.job.detail);
	if (!status->title[0])
		filename_from_value(g_background.job.direct_url ? g_background.job.url
		                                               : g_background.job.remote_path,
		                    status->title, sizeof(status->title));
	pthread_mutex_unlock(&g_background.mutex);
}

int vt_download_background_active(void) {
	VtDownloadBackgroundStatus status;
	vt_download_background_status(&status);
	return status.state == VT_DOWNLOAD_BACKGROUND_RUNNING;
}

void vt_download_background_toggle_pause(void) {
	pthread_mutex_lock(&g_background.mutex);
	if (g_background.thread_valid && !g_background.done)
		g_background.job.paused = !g_background.job.paused;
	pthread_mutex_unlock(&g_background.mutex);
}

void vt_download_background_abort(void) {
	pthread_mutex_lock(&g_background.mutex);
	if (g_background.thread_valid && !g_background.done) {
		g_background.job.paused = 0;
		g_background.job.cancel = 1;
	}
	pthread_mutex_unlock(&g_background.mutex);
}

void vt_download_background_acknowledge(void) {
	background_reap_finished();
	pthread_mutex_lock(&g_background.mutex);
	if (!g_background.thread_valid && g_background.done) {
		secure_zero(&g_background.job, sizeof(g_background.job));
		g_background.done = 0;
		g_background.result = -1;
	}
	pthread_mutex_unlock(&g_background.mutex);
}

void vt_download_background_shutdown(void) {
	vt_download_background_abort();
	pthread_t thread;
	int join = 0;
	pthread_mutex_lock(&g_background.mutex);
	if (g_background.thread_valid) {
		thread = g_background.thread;
		g_background.thread_valid = 0;
		join = 1;
	}
	pthread_mutex_unlock(&g_background.mutex);
	if (join) pthread_join(thread, NULL);
	vt_download_background_acknowledge();
}
