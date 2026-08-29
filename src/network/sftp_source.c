#include "network/network_internal.h"

#include <arpa/inet.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include <libssh2.h>
#include <libssh2_sftp.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>

#include "common/text_log.h"

#define SFTP_IO_TIMEOUT_MS 5000
#define SFTP_FORCED_CLEANUP_TIMEOUT_MS 100
#define SFTP_CLEANUP_GRACE_MS 250
#define SFTP_CONNECT_POLL_MS 100
#define SFTP_DNS_POLL_MS 10
#define SFTP_DNS_THREAD_STACK (64 * 1024)

typedef struct {
	int socket_fd;
	LIBSSH2_SESSION *session;
	LIBSSH2_SFTP *sftp;
	LIBSSH2_SFTP_HANDLE *file;
	uint64_t position;
	uint64_t size;
	uint64_t open_deadline_us;
	volatile int *cancel;
	volatile int aborted;
} SftpConnection;

typedef struct {
	int resolver_id;
	char host[VT_NETWORK_HOST_MAX];
	SceNetInAddr address;
	volatile int result;
	volatile int done;
} SftpDnsJob;

static void *sftp_dns_worker(void *opaque) {
	SftpDnsJob *job = opaque;
	/* The resolver timeout is per attempt. The caller additionally owns a hard
	 * wall deadline and can abort this resolver ID before joining the worker. */
	job->result = sceNetResolverStartNtoa(job->resolver_id, job->host,
	                                      &job->address, 1000 * 1000, 4, 0);
	__sync_synchronize();
	job->done = 1;
	__sync_synchronize();
	return NULL;
}

static int sftp_resolve_cancelable(const char *host, volatile int *cancel,
	                               SceNetInAddr *out, uint64_t deadline) {
	if (!host || !out || (cancel && *cancel)) return -1;
	struct in_addr numeric;
	if (inet_pton(AF_INET, host, &numeric) == 1) {
		out->s_addr = numeric.s_addr;
		return 0;
	}
	SftpDnsJob job;
	memset(&job, 0, sizeof(job));
	int host_length = snprintf(job.host, sizeof(job.host), "%s", host);
	if (host_length < 0 || (size_t)host_length >= sizeof(job.host)) return -1;
	job.resolver_id = sceNetResolverCreate("VMD SFTP DNS", NULL, 0);
	if (job.resolver_id < 0) return job.resolver_id;
	pthread_attr_t attributes;
	int attributes_ready = 0;
	int result = pthread_attr_init(&attributes);
	if (result == 0) attributes_ready = 1;
	if (result == 0)
		result = pthread_attr_setstacksize(&attributes,
		                                   SFTP_DNS_THREAD_STACK);
	pthread_t thread;
	if (result == 0) result = pthread_create(&thread, &attributes,
	                                        sftp_dns_worker, &job);
	if (attributes_ready) pthread_attr_destroy(&attributes);
	if (result != 0) {
		sceNetResolverDestroy(job.resolver_id);
		return -1;
	}
	while (!job.done && !(cancel && *cancel) &&
	       sceKernelGetProcessTimeWide() < deadline)
		sceKernelDelayThread(SFTP_DNS_POLL_MS * 1000);
	__sync_synchronize();
	int completed_in_time = job.done && !(cancel && *cancel) &&
	                        sceKernelGetProcessTimeWide() < deadline;
	if (!job.done) sceNetResolverAbort(job.resolver_id, 0);
	pthread_join(thread, NULL);
	__sync_synchronize();
	result = completed_in_time && job.result >= 0 ? 0 : -1;
	if (result == 0) *out = job.address;
	sceNetResolverDestroy(job.resolver_id);
	return result;
}

static int sftp_cancelled(const SftpConnection *connection) {
	return connection && (connection->aborted ||
	       (connection->cancel && *connection->cancel));
}

static void sftp_force_transport_error(SftpConnection *connection) {
	if (!connection) return;
	connection->aborted = 1;
	__sync_synchronize();
	if (connection->socket_fd >= 0)
		shutdown(connection->socket_fd, SHUT_RDWR);
}

static int sftp_wait_session(SftpConnection *connection, uint64_t deadline) {
	if (!connection || connection->socket_fd < 0 || !connection->session)
		return -1;
	while (!sftp_cancelled(connection) &&
	       sceKernelGetProcessTimeWide() < deadline) {
		int directions = libssh2_session_block_directions(connection->session);
		fd_set readable, writable;
		FD_ZERO(&readable);
		FD_ZERO(&writable);
		if (directions & LIBSSH2_SESSION_BLOCK_INBOUND)
			FD_SET(connection->socket_fd, &readable);
		if (directions & LIBSSH2_SESSION_BLOCK_OUTBOUND)
			FD_SET(connection->socket_fd, &writable);
		if (!directions) {
			sceKernelDelayThread(1000);
			return 0;
		}
		struct timeval poll = {
			.tv_sec = 0,
			.tv_usec = SFTP_CONNECT_POLL_MS * 1000
		};
		int result = select(connection->socket_fd + 1,
		                    &readable, &writable, NULL, &poll);
		if (result < 0 && errno == EINTR) continue;
		return result < 0 ? -1 : 0;
	}
	return -1;
}

static int sftp_handshake_cancelable(SftpConnection *connection,
	                                 uint64_t deadline) {
	int result;
	do {
		result = libssh2_session_handshake(connection->session,
		                                   connection->socket_fd);
		if (result != LIBSSH2_ERROR_EAGAIN) return result;
	} while (sftp_wait_session(connection, deadline) == 0);
	return -1;
}

static int sftp_auth_cancelable(SftpConnection *connection,
	                            const char *username, const char *password,
	                            uint64_t deadline) {
	int result;
	do {
		result = libssh2_userauth_password(connection->session, username,
		                                    password ? password : "");
		if (result != LIBSSH2_ERROR_EAGAIN) return result;
	} while (sftp_wait_session(connection, deadline) == 0);
	return -1;
}

static LIBSSH2_SFTP *sftp_init_cancelable(SftpConnection *connection,
	                                      uint64_t deadline) {
	for (;;) {
		LIBSSH2_SFTP *sftp = libssh2_sftp_init(connection->session);
		if (sftp) return sftp;
		if (libssh2_session_last_errno(connection->session) !=
		        LIBSSH2_ERROR_EAGAIN ||
		    sftp_wait_session(connection, deadline) < 0) return NULL;
	}
}

static int sftp_stat_cancelable(SftpConnection *connection, const char *path,
	                            LIBSSH2_SFTP_ATTRIBUTES *attributes,
	                            uint64_t deadline) {
	int result;
	do {
		result = libssh2_sftp_stat(connection->sftp, path, attributes);
		if (result != LIBSSH2_ERROR_EAGAIN) return result;
	} while (sftp_wait_session(connection, deadline) == 0);
	return -1;
}

static LIBSSH2_SFTP_HANDLE *sftp_open_cancelable(
	SftpConnection *connection, const char *path, uint64_t deadline) {
	for (;;) {
		LIBSSH2_SFTP_HANDLE *file = libssh2_sftp_open(
			connection->sftp, path, LIBSSH2_FXF_READ, 0);
		if (file) return file;
		if (libssh2_session_last_errno(connection->session) !=
		        LIBSSH2_ERROR_EAGAIN ||
		    sftp_wait_session(connection, deadline) < 0) return NULL;
	}
}

static void format_fingerprint(const unsigned char *hash,
	                           char *out, size_t out_size) {
	if (!out || !out_size) return;
	out[0] = '\0';
	if (!hash || out_size < 96) return;
	char *cursor = out;
	for (int i = 0; i < 32; i++) {
		int written = snprintf(cursor, out_size - (size_t)(cursor - out),
		                       i ? ":%02X" : "%02X", hash[i]);
		if (written < 0) break;
		cursor += written;
	}
}

static void sftp_disconnect(SftpConnection *connection) {
	if (!connection) return;
	uint64_t deadline = sceKernelGetProcessTimeWide() +
	                    SFTP_CLEANUP_GRACE_MS * 1000ULL;
	int forced = sftp_cancelled(connection);
	if (connection->session) libssh2_session_set_blocking(connection->session, 0);
	if (forced) sftp_force_transport_error(connection);

	int result = 0;
	if (connection->file) {
		do {
			result = libssh2_sftp_close(connection->file);
			if (result != LIBSSH2_ERROR_EAGAIN) break;
		} while (!forced && sftp_wait_session(connection, deadline) == 0);
		if (result == LIBSSH2_ERROR_EAGAIN && connection->session) {
			forced = 1;
			sftp_force_transport_error(connection);
			libssh2_session_set_timeout(connection->session,
			                            SFTP_FORCED_CLEANUP_TIMEOUT_MS);
			libssh2_session_set_blocking(connection->session, 1);
			result = libssh2_sftp_close(connection->file);
			libssh2_session_set_blocking(connection->session, 0);
		}
		if (result != LIBSSH2_ERROR_EAGAIN) connection->file = NULL;
		else log_printf("sftp cleanup: file handle still busy after forced unwind\n");
	}
	if (connection->sftp && !connection->file) {
		do {
			result = libssh2_sftp_shutdown(connection->sftp);
			if (result != LIBSSH2_ERROR_EAGAIN) break;
		} while (!forced && sftp_wait_session(connection, deadline) == 0);
		if (result == LIBSSH2_ERROR_EAGAIN && connection->session) {
			forced = 1;
			sftp_force_transport_error(connection);
			libssh2_session_set_timeout(connection->session,
			                            SFTP_FORCED_CLEANUP_TIMEOUT_MS);
			libssh2_session_set_blocking(connection->session, 1);
			result = libssh2_sftp_shutdown(connection->sftp);
			libssh2_session_set_blocking(connection->session, 0);
		}
		if (result != LIBSSH2_ERROR_EAGAIN) connection->sftp = NULL;
		else log_printf("sftp cleanup: subsystem still busy after forced unwind\n");
	}
	if (connection->session && !forced && !connection->sftp) {
		do {
			result = libssh2_session_disconnect(
			    connection->session, "VitaMediaDeck closed the connection");
			if (result != LIBSSH2_ERROR_EAGAIN) break;
		} while (sftp_wait_session(connection, deadline) == 0);
		if (result == LIBSSH2_ERROR_EAGAIN) {
			forced = 1;
			sftp_force_transport_error(connection);
		}
	}
	if (connection->session && !connection->file && !connection->sftp) {
		do {
			result = libssh2_session_free(connection->session);
			if (result != LIBSSH2_ERROR_EAGAIN) break;
		} while (!forced && sftp_wait_session(connection, deadline) == 0);
		if (result == LIBSSH2_ERROR_EAGAIN) {
			forced = 1;
			sftp_force_transport_error(connection);
			libssh2_session_set_timeout(connection->session,
			                            SFTP_FORCED_CLEANUP_TIMEOUT_MS);
			libssh2_session_set_blocking(connection->session, 1);
			result = libssh2_session_free(connection->session);
		}
		if (result != LIBSSH2_ERROR_EAGAIN) connection->session = NULL;
		else log_printf("sftp cleanup: session still busy after forced unwind\n");
	}
	if (connection->socket_fd >= 0) close(connection->socket_fd);
	connection->socket_fd = -1;
}

static int connect_cancelable(int fd, const struct sockaddr *address,
	                          socklen_t address_size,
	                          volatile int *cancel, uint64_t deadline) {
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
	int result = connect(fd, address, address_size);
	if (result == 0) {
		fcntl(fd, F_SETFL, flags);
		return 0;
	}
	if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
		fcntl(fd, F_SETFL, flags);
		return -1;
	}
	while (!(cancel && *cancel) && sceKernelGetProcessTimeWide() < deadline) {
		fd_set writable, failed;
		FD_ZERO(&writable);
		FD_ZERO(&failed);
		FD_SET(fd, &writable);
		FD_SET(fd, &failed);
		struct timeval poll = {
			.tv_sec = 0,
			.tv_usec = SFTP_CONNECT_POLL_MS * 1000
		};
		result = select(fd + 1, NULL, &writable, &failed, &poll);
		if (result < 0 && errno == EINTR) continue;
		if (result < 0) break;
		if (result == 0) continue;
		int socket_error = 0;
		socklen_t error_size = sizeof(socket_error);
		if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_size) == 0 &&
		    socket_error == 0) {
			fcntl(fd, F_SETFL, flags);
			return 0;
		}
		break;
	}
	fcntl(fd, F_SETFL, flags);
	return -1;
}

static int open_tcp(const char *host, uint16_t port, volatile int *cancel,
	                uint64_t deadline) {
	SceNetInAddr address;
	if (sftp_resolve_cancelable(host, cancel, &address, deadline) < 0) return -1;
	int fd = -1;
	if (cancel && *cancel) return -1;
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	struct timeval timeout = {
		.tv_sec = SFTP_IO_TIMEOUT_MS / 1000,
		.tv_usec = (SFTP_IO_TIMEOUT_MS % 1000) * 1000
	};
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
	struct sockaddr_in endpoint;
	memset(&endpoint, 0, sizeof(endpoint));
	endpoint.sin_family = AF_INET;
	endpoint.sin_port = htons(port ? port : 22);
	endpoint.sin_addr.s_addr = address.s_addr;
	if (connect_cancelable(fd, (const struct sockaddr *)&endpoint,
	                       sizeof(endpoint), cancel, deadline) < 0) {
		close(fd);
		fd = -1;
	}
	return fd;
}

static int sftp_connect(const VtNetworkSource *source,
	                    const VtNetworkCredential *credential,
	                    int authenticate, SftpConnection *out,
	                    char *fingerprint, size_t fingerprint_size,
	                    char *detail, size_t detail_size,
	                    volatile int *cancel) {
	if (!source || !out || !source->host[0]) return -1;
	memset(out, 0, sizeof(*out));
	out->socket_fd = -1;
	out->cancel = cancel;
	out->open_deadline_us = sceKernelGetProcessTimeWide() +
	                        (uint64_t)SFTP_IO_TIMEOUT_MS * 1000ULL;
	if (sftp_cancelled(out)) return -1;
	out->socket_fd = open_tcp(source->host, source->port ? source->port : 22,
	                          cancel, out->open_deadline_us);
	if (out->socket_fd < 0) {
		if (detail && detail_size) snprintf(detail, detail_size, "TCP connection failed");
		return -1;
	}
	out->session = libssh2_session_init();
	if (!out->session) { sftp_disconnect(out); return -1; }
	libssh2_session_set_blocking(out->session, 0);
	libssh2_session_set_timeout(out->session, SFTP_IO_TIMEOUT_MS);
	if (sftp_handshake_cancelable(out, out->open_deadline_us) != 0) {
		if (detail && detail_size) snprintf(detail, detail_size, "SSH handshake failed");
		sftp_disconnect(out);
		return -1;
	}
	if (sftp_cancelled(out)) { sftp_disconnect(out); return -1; }
	const unsigned char *hash = (const unsigned char *)
		libssh2_hostkey_hash(out->session, LIBSSH2_HOSTKEY_HASH_SHA256);
	char actual[VT_NETWORK_FINGERPRINT_MAX];
	format_fingerprint(hash, actual, sizeof(actual));
	if (fingerprint && fingerprint_size)
		snprintf(fingerprint, fingerprint_size, "%s", actual);
	if (!actual[0]) {
		sftp_disconnect(out);
		return -1;
	}
	if (authenticate) {
		if (sftp_cancelled(out)) { sftp_disconnect(out); return -1; }
		if (!source->host_key_sha256[0]) {
			if (detail && detail_size) snprintf(detail, detail_size, "%s", actual);
			sftp_disconnect(out);
			return VT_NETWORK_HOST_KEY_REQUIRED;
		}
		if (strcasecmp(actual, source->host_key_sha256) != 0) {
			if (detail && detail_size) snprintf(detail, detail_size, "%s", actual);
			sftp_disconnect(out);
			return VT_NETWORK_HOST_KEY_MISMATCH;
		}
		if (!source->username[0] ||
		    sftp_auth_cancelable(out, source->username,
		                         credential ? credential->password : "",
		                         out->open_deadline_us) != 0) {
			if (detail && detail_size) snprintf(detail, detail_size, "SFTP authentication failed");
			sftp_disconnect(out);
			return VT_NETWORK_AUTH_FAILED;
		}
		out->sftp = sftp_init_cancelable(out, out->open_deadline_us);
		if (!out->sftp) { sftp_disconnect(out); return -1; }
	}
	return 0;
}

static void sftp_path(const VtNetworkSource *source, const char *path,
	                  char *out, size_t out_size) {
	const char *root = source->root_path;
	if (!root[0]) root = "/";
	if (!path || !path[0]) snprintf(out, out_size, "%s", root);
	else if (root[strlen(root) - 1] == '/')
		snprintf(out, out_size, "%s%s", root, *path == '/' ? path + 1 : path);
	else snprintf(out, out_size, "%s/%s", root, *path == '/' ? path + 1 : path);
}

int vt_sftp_probe_fingerprint(const VtNetworkSource *source,
	                          char *fingerprint, size_t fingerprint_size,
	                          char *detail, size_t detail_size) {
	SftpConnection connection;
	int ret = sftp_connect(source, NULL, 0, &connection, fingerprint,
	                       fingerprint_size, detail, detail_size, NULL);
	if (ret == 0) sftp_disconnect(&connection);
	return ret;
}

int vt_sftp_list(const VtNetworkSource *source,
	             const VtNetworkCredential *credential,
	             const char *path, VtNetworkEntry *entries, int capacity,
	             char *detail, size_t detail_size) {
	SftpConnection connection;
	int ret = sftp_connect(source, credential, 1, &connection, NULL, 0,
	                       detail, detail_size, NULL);
	if (ret < 0) return ret;
	libssh2_session_set_blocking(connection.session, 1);
	char remote[VT_NETWORK_PATH_MAX];
	sftp_path(source, path, remote, sizeof(remote));
	LIBSSH2_SFTP_HANDLE *directory = libssh2_sftp_opendir(connection.sftp, remote);
	if (!directory) { sftp_disconnect(&connection); return -1; }
	int count = 0;
	char name[256];
	LIBSSH2_SFTP_ATTRIBUTES attributes;
	while (count < capacity) {
		memset(&attributes, 0, sizeof(attributes));
		int length = libssh2_sftp_readdir(directory, name, sizeof(name) - 1,
		                                  &attributes);
		if (length <= 0) break;
		name[length] = '\0';
		if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
		int is_dir = (attributes.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) &&
		             LIBSSH2_SFTP_S_ISDIR(attributes.permissions);
		int is_audio = 0;
		int supported = !is_dir && vt_network_is_supported_media(name, &is_audio);
		VtNetworkEntry *entry = &entries[count++];
		memset(entry, 0, sizeof(*entry));
		snprintf(entry->name, sizeof(entry->name), "%s", name);
		if (path && path[0])
			snprintf(entry->path, sizeof(entry->path), "%s/%s", path, name);
		else snprintf(entry->path, sizeof(entry->path), "%s", name);
		entry->size = (attributes.flags & LIBSSH2_SFTP_ATTR_SIZE)
		            ? attributes.filesize : 0;
		entry->is_directory = is_dir;
		entry->is_audio = supported && is_audio;
		entry->is_video = supported && !is_audio;
	}
	libssh2_sftp_closedir(directory);
	sftp_disconnect(&connection);
	return count;
}

static int sftp_stream_read(void *opaque, void *buffer, size_t size) {
	SftpConnection *connection = opaque;
	if (!connection || !connection->file || sftp_cancelled(connection)) return -1;
	if (size > 256 * 1024) size = 256 * 1024;
	ssize_t ret = libssh2_sftp_read(connection->file, buffer, size);
	if (ret > 0) connection->position += (uint64_t)ret;
	if (ret < 0) {
		connection->aborted = 1;
		__sync_synchronize();
	}
	return ret < 0 || sftp_cancelled(connection) ? -1 : (int)ret;
}

static int64_t sftp_stream_seek(void *opaque, int64_t offset, int whence) {
	SftpConnection *connection = opaque;
	if (!connection || sftp_cancelled(connection)) return -1;
	int64_t target = whence == SEEK_SET ? offset
	               : whence == SEEK_CUR ? (int64_t)connection->position + offset
	               : whence == SEEK_END ? (int64_t)connection->size + offset : -1;
	if (target < 0 || (uint64_t)target > connection->size) return -1;
	libssh2_sftp_seek64(connection->file, (uint64_t)target);
	connection->position = (uint64_t)target;
	return target;
}

static void sftp_stream_abort(void *opaque) {
	SftpConnection *connection = opaque;
	if (!connection) return;
	connection->aborted = 1;
	__sync_synchronize();
	/* shutdown is non-owning and wakes a blocking libssh2 socket operation. The
	 * subtitle/decoder worker remains responsible for normal close/free. */
	if (connection->socket_fd >= 0)
		shutdown(connection->socket_fd, SHUT_RDWR);
}

static void sftp_stream_close(void *opaque) {
	SftpConnection *connection = opaque;
	if (!connection) return;
	sftp_disconnect(connection);
	free(connection);
}

int vt_sftp_open_stream(const VtNetworkSource *source,
	                    const VtNetworkCredential *credential,
	                    const char *path, VtDecoderStreamHandle *out,
	                    volatile int *cancel_flag) {
	if (!out) return -1;
	SftpConnection *connection = calloc(1, sizeof(*connection));
	if (!connection) return -1;
	int ret = sftp_connect(source, credential, 1, connection, NULL, 0, NULL, 0,
	                       cancel_flag);
	if (ret < 0) { free(connection); return ret; }
	if (sftp_cancelled(connection)) { sftp_stream_close(connection); return -1; }
	char remote[VT_NETWORK_PATH_MAX];
	sftp_path(source, path, remote, sizeof(remote));
	LIBSSH2_SFTP_ATTRIBUTES attributes;
	memset(&attributes, 0, sizeof(attributes));
	if (sftp_stat_cancelable(connection, remote, &attributes,
	                         connection->open_deadline_us) < 0 ||
	    !(attributes.flags & LIBSSH2_SFTP_ATTR_SIZE)) {
		sftp_stream_close(connection);
		return -1;
	}
	if (sftp_cancelled(connection)) { sftp_stream_close(connection); return -1; }
	connection->file = sftp_open_cancelable(connection, remote,
	                                        connection->open_deadline_us);
	if (!connection->file) { sftp_stream_close(connection); return -1; }
	libssh2_session_set_blocking(connection->session, 1);
	connection->size = attributes.filesize;
	memset(out, 0, sizeof(*out));
	out->opaque = connection;
	out->read = sftp_stream_read;
	out->seek = sftp_stream_seek;
	out->abort = sftp_stream_abort;
	out->close = sftp_stream_close;
	out->size = (int64_t)connection->size;
	return 0;
}
