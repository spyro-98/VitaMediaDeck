#include "network/network_internal.h"

#include <limits.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

#include <libssh2.h>
#include <libssh2_sftp.h>

typedef struct {
	int socket_fd;
	LIBSSH2_SESSION *session;
	LIBSSH2_SFTP *sftp;
	LIBSSH2_SFTP_HANDLE *file;
	uint64_t position;
	uint64_t size;
} SftpConnection;

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
	if (connection->file) libssh2_sftp_close(connection->file);
	if (connection->sftp) libssh2_sftp_shutdown(connection->sftp);
	if (connection->session) {
		libssh2_session_disconnect(connection->session, "VitaMediaDeck closed the connection");
		libssh2_session_free(connection->session);
	}
	if (connection->socket_fd >= 0) close(connection->socket_fd);
	connection->file = NULL;
	connection->sftp = NULL;
	connection->session = NULL;
	connection->socket_fd = -1;
}

static int open_tcp(const char *host, uint16_t port) {
	char port_text[8];
	snprintf(port_text, sizeof(port_text), "%u", port ? port : 22);
	struct addrinfo hints, *addresses = NULL;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host, port_text, &hints, &addresses) != 0) return -1;
	int fd = -1;
	for (struct addrinfo *it = addresses; it; it = it->ai_next) {
		fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
		if (fd < 0) continue;
		if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(addresses);
	return fd;
}

static int sftp_connect(const VtNetworkSource *source,
	                    const VtNetworkCredential *credential,
	                    int authenticate, SftpConnection *out,
	                    char *fingerprint, size_t fingerprint_size,
	                    char *detail, size_t detail_size) {
	if (!source || !out || !source->host[0]) return -1;
	memset(out, 0, sizeof(*out));
	out->socket_fd = -1;
	out->socket_fd = open_tcp(source->host, source->port ? source->port : 22);
	if (out->socket_fd < 0) {
		if (detail && detail_size) snprintf(detail, detail_size, "TCP connection failed");
		return -1;
	}
	out->session = libssh2_session_init();
	if (!out->session) { sftp_disconnect(out); return -1; }
	libssh2_session_set_blocking(out->session, 1);
	libssh2_session_set_timeout(out->session, 15000);
	if (libssh2_session_handshake(out->session, out->socket_fd) != 0) {
		if (detail && detail_size) snprintf(detail, detail_size, "SSH handshake failed");
		sftp_disconnect(out);
		return -1;
	}
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
		    libssh2_userauth_password(out->session, source->username,
		                              credential ? credential->password : "") != 0) {
			if (detail && detail_size) snprintf(detail, detail_size, "SFTP authentication failed");
			sftp_disconnect(out);
			return VT_NETWORK_AUTH_FAILED;
		}
		out->sftp = libssh2_sftp_init(out->session);
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
	                       fingerprint_size, detail, detail_size);
	if (ret == 0) sftp_disconnect(&connection);
	return ret;
}

int vt_sftp_list(const VtNetworkSource *source,
	             const VtNetworkCredential *credential,
	             const char *path, VtNetworkEntry *entries, int capacity,
	             char *detail, size_t detail_size) {
	SftpConnection connection;
	int ret = sftp_connect(source, credential, 1, &connection, NULL, 0,
	                       detail, detail_size);
	if (ret < 0) return ret;
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
	if (!connection || !connection->file) return -1;
	if (size > 256 * 1024) size = 256 * 1024;
	ssize_t ret = libssh2_sftp_read(connection->file, buffer, size);
	if (ret > 0) connection->position += (uint64_t)ret;
	return ret < 0 ? -1 : (int)ret;
}

static int64_t sftp_stream_seek(void *opaque, int64_t offset, int whence) {
	SftpConnection *connection = opaque;
	if (!connection) return -1;
	int64_t target = whence == SEEK_SET ? offset
	               : whence == SEEK_CUR ? (int64_t)connection->position + offset
	               : whence == SEEK_END ? (int64_t)connection->size + offset : -1;
	if (target < 0 || (uint64_t)target > connection->size) return -1;
	libssh2_sftp_seek64(connection->file, (uint64_t)target);
	connection->position = (uint64_t)target;
	return target;
}

static void sftp_stream_close(void *opaque) {
	SftpConnection *connection = opaque;
	if (!connection) return;
	sftp_disconnect(connection);
	free(connection);
}

int vt_sftp_open_stream(const VtNetworkSource *source,
	                    const VtNetworkCredential *credential,
	                    const char *path, VtDecoderStreamHandle *out) {
	if (!out) return -1;
	SftpConnection *connection = calloc(1, sizeof(*connection));
	if (!connection) return -1;
	int ret = sftp_connect(source, credential, 1, connection, NULL, 0, NULL, 0);
	if (ret < 0) { free(connection); return ret; }
	char remote[VT_NETWORK_PATH_MAX];
	sftp_path(source, path, remote, sizeof(remote));
	LIBSSH2_SFTP_ATTRIBUTES attributes;
	memset(&attributes, 0, sizeof(attributes));
	if (libssh2_sftp_stat(connection->sftp, remote, &attributes) < 0 ||
	    !(attributes.flags & LIBSSH2_SFTP_ATTR_SIZE)) {
		sftp_stream_close(connection);
		return -1;
	}
	connection->file = libssh2_sftp_open(connection->sftp, remote,
	                                     LIBSSH2_FXF_READ, 0);
	if (!connection->file) { sftp_stream_close(connection); return -1; }
	connection->size = attributes.filesize;
	memset(out, 0, sizeof(*out));
	out->opaque = connection;
	out->read = sftp_stream_read;
	out->seek = sftp_stream_seek;
	out->close = sftp_stream_close;
	out->size = (int64_t)connection->size;
	return 0;
}
