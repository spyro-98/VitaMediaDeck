#include "network/network_internal.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <smb2/smb2.h>
#include <smb2/libsmb2.h>

typedef struct {
	struct smb2_context *context;
	struct smb2fh *file;
	uint64_t position;
	uint64_t size;
} SmbStream;

static int smb_connect(const VtNetworkSource *source,
	                   const VtNetworkCredential *credential,
	                   struct smb2_context **out,
	                   char *detail, size_t detail_size) {
	if (!source || !out || !source->host[0] || !source->share[0] ||
	    !source->username[0]) return VT_NETWORK_AUTH_FAILED;
	struct smb2_context *context = smb2_init_context();
	if (!context) return -1;
	/* Authenticated sources must also protect SMB2/3 messages against
	 * tampering. Encryption remains a server/share policy so older SMB2
	 * servers can still be used on trusted networks. */
	smb2_set_security_mode(context, SMB2_NEGOTIATE_SIGNING_ENABLED |
	                                 SMB2_NEGOTIATE_SIGNING_REQUIRED);
	smb2_set_sign(context, 1);
	smb2_set_user(context, source->username);
	smb2_set_domain(context, source->domain);
	smb2_set_password(context, credential ? credential->password : "");
	/* Anonymous/guest access is deliberately outside VitaTube's network-source
	 * contract. */
	int ret = smb2_connect_share(context, source->host, source->share,
	                             source->username);
	if (ret < 0) {
		if (detail && detail_size)
			snprintf(detail, detail_size, "%s", smb2_get_error(context));
		smb2_destroy_context(context);
		return VT_NETWORK_AUTH_FAILED;
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
	int ret = smb_connect(source, credential, &context, detail, detail_size);
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
		if (!is_dir && !vt_network_is_supported_media(entry->name, &is_audio))
			continue;
		if (!is_dir && is_audio) continue;
		VtNetworkEntry *out = &entries[count++];
		memset(out, 0, sizeof(*out));
		snprintf(out->name, sizeof(out->name), "%s", entry->name);
		if (path && path[0])
			snprintf(out->path, sizeof(out->path), "%s/%s", path, entry->name);
		else snprintf(out->path, sizeof(out->path), "%s", entry->name);
		out->size = entry->st.smb2_size;
		out->is_directory = is_dir;
		out->is_audio = !is_dir && is_audio;
		out->is_video = !is_dir && !is_audio;
	}
	smb2_closedir(context, directory);
	smb2_disconnect_share(context);
	smb2_destroy_context(context);
	return count;
}

static int smb_stream_read(void *opaque, void *buffer, size_t size) {
	SmbStream *stream = opaque;
	if (!stream || !stream->file) return -1;
	uint32_t max_read = smb2_get_max_read_size(stream->context);
	if (!max_read || max_read > size) max_read = (uint32_t)size;
	int ret = smb2_pread(stream->context, stream->file, buffer, max_read,
	                     stream->position);
	if (ret > 0) stream->position += (uint64_t)ret;
	return ret;
}

static int64_t smb_stream_seek(void *opaque, int64_t offset, int whence) {
	SmbStream *stream = opaque;
	if (!stream) return -1;
	int64_t target = whence == SEEK_SET ? offset
	               : whence == SEEK_CUR ? (int64_t)stream->position + offset
	               : whence == SEEK_END ? (int64_t)stream->size + offset : -1;
	if (target < 0 || (uint64_t)target > stream->size) return -1;
	stream->position = (uint64_t)target;
	return target;
}

static void smb_stream_close(void *opaque) {
	SmbStream *stream = opaque;
	if (!stream) return;
	if (stream->file) smb2_close(stream->context, stream->file);
	if (stream->context) {
		smb2_disconnect_share(stream->context);
		smb2_destroy_context(stream->context);
	}
	free(stream);
}

int vt_smb_open_stream(const VtNetworkSource *source,
	                   const VtNetworkCredential *credential,
	                   const char *path, VtDecoderStreamHandle *out) {
	if (!out) return -1;
	SmbStream *stream = calloc(1, sizeof(*stream));
	if (!stream) return -1;
	int ret = smb_connect(source, credential, &stream->context, NULL, 0);
	if (ret < 0) { free(stream); return ret; }
	char remote[VT_NETWORK_PATH_MAX];
	normalized_path(source, path, remote, sizeof(remote));
	struct smb2_stat_64 stat;
	memset(&stat, 0, sizeof(stat));
	if (smb2_stat(stream->context, remote, &stat) < 0 ||
	    stat.smb2_type != SMB2_TYPE_FILE) {
		smb_stream_close(stream);
		return -1;
	}
	stream->file = smb2_open(stream->context, remote, O_RDONLY);
	if (!stream->file) { smb_stream_close(stream); return -1; }
	stream->size = stat.smb2_size;
	memset(out, 0, sizeof(*out));
	out->opaque = stream;
	out->read = smb_stream_read;
	out->seek = smb_stream_seek;
	out->close = smb_stream_close;
	out->size = (int64_t)stream->size;
	return 0;
}
