#include "network/network_internal.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>

#include <smb2/smb2.h>
#include <smb2/libsmb2.h>

#define SMB_IO_TIMEOUT_SECONDS 5
#define SMB_ASYNC_SLICE_MS 100
#define SMB_DNS_SLICE_MS 10
#define SMB_DNS_THREAD_STACK (64 * 1024)

typedef struct {
	struct smb2_context *context;
	struct smb2fh *file;
	uint64_t position;
	uint64_t size;
	uint64_t open_deadline_us;
	volatile int *cancel;
	volatile int aborted;
} SmbStream;

typedef struct {
	volatile int done;
	int status;
	void *command_data;
} SmbAsyncResult;

typedef struct {
	int resolver_id;
	char host[VT_NETWORK_HOST_MAX];
	SceNetInAddr address;
	volatile int result;
	volatile int done;
} SmbDnsJob;

static int smb_cancelled(const SmbStream *stream) {
	return stream && (stream->aborted ||
	       (stream->cancel && *stream->cancel));
}

static void *smb_dns_worker(void *opaque) {
	SmbDnsJob *job = opaque;
	job->result = sceNetResolverStartNtoa(job->resolver_id, job->host,
	                                      &job->address, 1000 * 1000, 4, 0);
	__sync_synchronize();
	job->done = 1;
	__sync_synchronize();
	return NULL;
}

static int smb_resolve_cancelable(const char *host, volatile int *cancel,
	                              SceNetInAddr *out, uint64_t deadline_us) {
	if (!host || !host[0] || !out || (cancel && *cancel)) return -1;
	struct in_addr numeric;
	if (inet_pton(AF_INET, host, &numeric) == 1) {
		out->s_addr = numeric.s_addr;
		return 0;
	}
	SmbDnsJob job;
	memset(&job, 0, sizeof(job));
	int host_length = snprintf(job.host, sizeof(job.host), "%s", host);
	if (host_length < 0 || (size_t)host_length >= sizeof(job.host)) return -1;
	job.resolver_id = sceNetResolverCreate("VMD SMB DNS", NULL, 0);
	if (job.resolver_id < 0) return job.resolver_id;
	pthread_attr_t attributes;
	int attributes_ready = 0;
	int result = pthread_attr_init(&attributes);
	if (result == 0) attributes_ready = 1;
	if (result == 0)
		result = pthread_attr_setstacksize(&attributes,
		                                   SMB_DNS_THREAD_STACK);
	pthread_t thread;
	if (result == 0)
		result = pthread_create(&thread, &attributes, smb_dns_worker, &job);
	if (attributes_ready) pthread_attr_destroy(&attributes);
	if (result != 0) {
		sceNetResolverDestroy(job.resolver_id);
		return -1;
	}
	while (!job.done && !(cancel && *cancel) &&
	       sceKernelGetProcessTimeWide() < deadline_us)
		sceKernelDelayThread(SMB_DNS_SLICE_MS * 1000);
	__sync_synchronize();
	int completed = job.done && !(cancel && *cancel) && job.result >= 0;
	if (!job.done) sceNetResolverAbort(job.resolver_id, 0);
	pthread_join(thread, NULL);
	__sync_synchronize();
	if (completed) *out = job.address;
	sceNetResolverDestroy(job.resolver_id);
	return completed ? 0 : -1;
}

static void smb_async_complete(struct smb2_context *context, int status,
	                           void *command_data, void *opaque) {
	(void)context;
	SmbAsyncResult *result = opaque;
	if (!result) return;
	result->status = status;
	result->command_data = command_data;
	__sync_synchronize();
	result->done = 1;
}

static void smb_abort_context(struct smb2_context *context) {
	if (!context) return;
	t_socket fd = smb2_get_fd(context);
	if (fd >= 0) shutdown(fd, SHUT_RDWR);
}

/* Drive one asynchronous libsmb2 command in short slices so Circle/scene
 * teardown never has to wait for a synchronous DNS, connect, stat, or open.
 * libsmb2 callbacks run from smb2_service(), so the stack-owned result remains
 * valid until this function returns. */
static int smb_wait_async(struct smb2_context *context,
	                      SmbAsyncResult *result,
	                      volatile int *cancel,
	                      uint64_t deadline_us) {
	if (!context || !result) return -1;
	while (!result->done) {
		if ((cancel && *cancel) ||
		    sceKernelGetProcessTimeWide() >= deadline_us) {
			smb_abort_context(context);
			return -1;
		}
		t_socket fd = smb2_get_fd(context);
		if (fd < 0) {
			/* Let libsmb2 advance resolver/connect timeout state even before a
			 * socket has become visible to the application. */
			if (smb2_service(context, 0) < 0) return -1;
			sceKernelDelayThread(SMB_ASYNC_SLICE_MS * 1000);
			continue;
		}
		struct pollfd descriptor;
		descriptor.fd = fd;
		descriptor.events = (short)smb2_which_events(context);
		descriptor.revents = 0;
		int poll_result = poll(&descriptor, 1, SMB_ASYNC_SLICE_MS);
		if (poll_result < 0) return -1;
		if (smb2_service(context,
		                 poll_result > 0 ? descriptor.revents : 0) < 0)
			return -1;
	}
	__sync_synchronize();
	return result->status;
}

static int smb_server_address(const VtNetworkSource *source,
	                          volatile int *cancel,
	                          char *out, size_t out_size,
	                          uint64_t deadline_us) {
	char host[VT_NETWORK_HOST_MAX];
	int host_length = snprintf(host, sizeof(host), "%s", source->host);
	if (host_length < 0 || (size_t)host_length >= sizeof(host)) return -1;
	uint16_t port = source->port ? source->port : 445;
	/* Preserve the legacy host:port storage format without letting libsmb2 do
	 * an uninterruptible getaddrinfo() internally. IPv6 was never supported by
	 * the Vita network-source editor. */
	char *separator = strrchr(host, ':');
	if (separator) {
		char *end = NULL;
		long parsed = strtol(separator + 1, &end, 10);
		if (!separator[1] || !end || *end || parsed < 1 || parsed > 65535)
			return -1;
		*separator = '\0';
		port = (uint16_t)parsed;
	}
	SceNetInAddr address;
	if (smb_resolve_cancelable(host, cancel, &address, deadline_us) < 0)
		return -1;
	struct in_addr numeric = { .s_addr = address.s_addr };
	char numeric_host[INET_ADDRSTRLEN];
	if (!inet_ntop(AF_INET, &numeric, numeric_host, sizeof(numeric_host)))
		return -1;
	if (port == 445) return snprintf(out, out_size, "%s", numeric_host) > 0 ? 0 : -1;
	return snprintf(out, out_size, "%s:%u", numeric_host, port) > 0 ? 0 : -1;
}

static int smb_connect(const VtNetworkSource *source,
	                   const VtNetworkCredential *credential,
	                   struct smb2_context **out,
	                   char *detail, size_t detail_size,
	                   volatile int *cancel, uint64_t deadline_us) {
	if (!source || !out || !source->host[0] || !source->share[0] ||
	    !source->username[0]) return VT_NETWORK_AUTH_FAILED;
	struct smb2_context *context = smb2_init_context();
	if (!context) return -1;
	if (cancel && *cancel) { smb2_destroy_context(context); return -1; }
	/* libsmb2 otherwise permits an unbounded synchronous wait. A finite
	 * transport deadline guarantees that cancellation/teardown can progress. */
	smb2_set_timeout(context, SMB_IO_TIMEOUT_SECONDS);
	/* Authenticated sources must also protect SMB2/3 messages against
	 * tampering. Encryption remains a server/share policy so older SMB2
	 * servers can still be used on trusted networks. */
	smb2_set_security_mode(context, SMB2_NEGOTIATE_SIGNING_ENABLED |
	                                 SMB2_NEGOTIATE_SIGNING_REQUIRED);
	smb2_set_sign(context, 1);
	smb2_set_user(context, source->username);
	smb2_set_domain(context, source->domain);
	smb2_set_password(context, credential ? credential->password : "");
	/* Anonymous/guest access is deliberately outside VitaMediaDeck's network-source
	 * contract. */
	char server[VT_NETWORK_HOST_MAX + 16];
	if (smb_server_address(source, cancel, server, sizeof(server),
	                       deadline_us) < 0) {
		smb2_destroy_context(context);
		return -1;
	}
	SmbAsyncResult result = {0};
	int ret = smb2_connect_share_async(context, server, source->share,
	                                   source->username,
	                                   smb_async_complete, &result);
	if (ret == 0)
		ret = smb_wait_async(context, &result, cancel, deadline_us);
	if (ret < 0) {
		if (detail && detail_size)
			snprintf(detail, detail_size, "%s", smb2_get_error(context));
		smb_abort_context(context);
		smb2_destroy_context(context);
		return cancel && *cancel ? -1 : VT_NETWORK_AUTH_FAILED;
	}
	*out = context;
	return 0;
}

static void normalized_path(const VtNetworkSource *source, const char *path,
	                        char *out, size_t out_size) {
	const char *root = source->root_path;
	while (*root == '/') root++;
	while (path && *path == '/') path++;
	if (root[0] && path && path[0]) snprintf(out, out_size, "%s/%s", root, path);
	else snprintf(out, out_size, "%s", root[0] ? root : (path ? path : ""));
}

int vt_smb_list(const VtNetworkSource *source,
	            const VtNetworkCredential *credential,
	            const char *path, VtNetworkEntry *entries, int capacity,
	               char *detail, size_t detail_size) {
	struct smb2_context *context = NULL;
	uint64_t deadline_us = sceKernelGetProcessTimeWide() +
	                       SMB_IO_TIMEOUT_SECONDS * 1000000ULL;
	int ret = smb_connect(source, credential, &context, detail, detail_size, NULL,
	                      deadline_us);
	if (ret < 0) return ret;
	char remote[VT_NETWORK_PATH_MAX];
	normalized_path(source, path, remote, sizeof(remote));
	struct smb2dir *directory = smb2_opendir(context, remote);
	if (!directory) {
		if (detail && detail_size)
			snprintf(detail, detail_size, "%s", smb2_get_error(context));
		smb2_disconnect_share(context);
		smb2_destroy_context(context);
		return -1;
	}
	int count = 0;
	struct smb2dirent *entry;
	while (count < capacity && (entry = smb2_readdir(context, directory))) {
		if (!strcmp(entry->name, ".") || !strcmp(entry->name, "..")) continue;
		int is_dir = entry->st.smb2_type == SMB2_TYPE_DIRECTORY;
		int is_audio = 0;
		int supported = !is_dir &&
		                vt_network_is_supported_media(entry->name, &is_audio);
		VtNetworkEntry *out = &entries[count++];
		memset(out, 0, sizeof(*out));
		snprintf(out->name, sizeof(out->name), "%s", entry->name);
		if (path && path[0])
			snprintf(out->path, sizeof(out->path), "%s/%s", path, entry->name);
		else snprintf(out->path, sizeof(out->path), "%s", entry->name);
		out->size = entry->st.smb2_size;
		out->is_directory = is_dir;
		out->is_audio = supported && is_audio;
		out->is_video = supported && !is_audio;
	}
	smb2_closedir(context, directory);
	smb2_disconnect_share(context);
	smb2_destroy_context(context);
	return count;
}

static int smb_stream_read(void *opaque, void *buffer, size_t size) {
	SmbStream *stream = opaque;
	if (!stream || !stream->file || smb_cancelled(stream)) return -1;
	uint32_t max_read = smb2_get_max_read_size(stream->context);
	if (!max_read || max_read > size) max_read = (uint32_t)size;
	int ret = smb2_pread(stream->context, stream->file, buffer, max_read,
	                     stream->position);
	if (ret > 0) stream->position += (uint64_t)ret;
	if (ret < 0) {
		stream->aborted = 1;
		__sync_synchronize();
	}
	return smb_cancelled(stream) ? -1 : ret;
}

static int64_t smb_stream_seek(void *opaque, int64_t offset, int whence) {
	SmbStream *stream = opaque;
	if (!stream || smb_cancelled(stream)) return -1;
	int64_t target = whence == SEEK_SET ? offset
	               : whence == SEEK_CUR ? (int64_t)stream->position + offset
	               : whence == SEEK_END ? (int64_t)stream->size + offset : -1;
	if (target < 0 || (uint64_t)target > stream->size) return -1;
	stream->position = (uint64_t)target;
	return target;
}

static void smb_stream_abort(void *opaque) {
	SmbStream *stream = opaque;
	if (!stream) return;
	stream->aborted = 1;
	__sync_synchronize();
	/* Closing ownership stays on the worker. Shutting down libsmb2's socket is
	 * enough to wake its synchronous pread/connect service loop. */
	if (stream->context) {
		t_socket fd = smb2_get_fd(stream->context);
		if (fd >= 0) shutdown(fd, SHUT_RDWR);
	}
}

static void smb_stream_close(void *opaque) {
	SmbStream *stream = opaque;
	if (!stream) return;
	int interrupted = smb_cancelled(stream);
	/* Once abort() has shut the socket down, protocol-level close/disconnect
	 * can only add another timeout. Context destruction releases local handles. */
	if (stream->file && !interrupted)
		smb2_close(stream->context, stream->file);
	if (stream->context) {
		if (!interrupted) smb2_disconnect_share(stream->context);
		smb2_destroy_context(stream->context);
	}
	free(stream);
}

int vt_smb_open_stream(const VtNetworkSource *source,
	                   const VtNetworkCredential *credential,
	                   const char *path, VtDecoderStreamHandle *out,
	                   volatile int *cancel_flag) {
	if (!out) return -1;
	SmbStream *stream = calloc(1, sizeof(*stream));
	if (!stream) return -1;
	stream->cancel = cancel_flag;
	stream->open_deadline_us = sceKernelGetProcessTimeWide() +
	                           SMB_IO_TIMEOUT_SECONDS * 1000000ULL;
	int ret = smb_connect(source, credential, &stream->context, NULL, 0,
	                      cancel_flag, stream->open_deadline_us);
	if (ret < 0) { free(stream); return ret; }
	if (smb_cancelled(stream)) { smb_stream_close(stream); return -1; }
	char remote[VT_NETWORK_PATH_MAX];
	normalized_path(source, path, remote, sizeof(remote));
	struct smb2_stat_64 stat;
	memset(&stat, 0, sizeof(stat));
	SmbAsyncResult stat_result = {0};
	ret = smb2_stat_async(stream->context, remote, &stat,
	                     smb_async_complete, &stat_result);
	if (ret == 0)
		ret = smb_wait_async(stream->context, &stat_result, cancel_flag,
		                     stream->open_deadline_us);
	if (ret < 0 || stat.smb2_type != SMB2_TYPE_FILE) {
		if (ret < 0) stream->aborted = 1;
		smb_stream_close(stream);
		return -1;
	}
	if (smb_cancelled(stream)) { smb_stream_close(stream); return -1; }
	SmbAsyncResult open_result = {0};
	ret = smb2_open_async(stream->context, remote, O_RDONLY,
	                     smb_async_complete, &open_result);
	if (ret == 0)
		ret = smb_wait_async(stream->context, &open_result, cancel_flag,
		                     stream->open_deadline_us);
	if (ret >= 0) stream->file = open_result.command_data;
	if (!stream->file) {
		if (ret < 0) stream->aborted = 1;
		smb_stream_close(stream);
		return -1;
	}
	stream->size = stat.smb2_size;
	memset(out, 0, sizeof(*out));
	out->opaque = stream;
	out->read = smb_stream_read;
	out->seek = smb_stream_seek;
	out->abort = smb_stream_abort;
	out->close = smb_stream_close;
	out->size = (int64_t)stream->size;
	return 0;
}
