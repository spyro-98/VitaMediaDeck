#include "network/network_source.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <libssh2.h>

#include "app_paths.h"
#include "network/network_internal.h"

#define SOURCE_DB_PATH VITAMEDIADECK_NETWORK_DIR "/sources.bin"
#define SOURCE_DB_TEMP VITAMEDIADECK_NETWORK_DIR "/sources.tmp"
#define SOURCE_DB_BACKUP VITAMEDIADECK_NETWORK_DIR "/sources.bak"
#define SOURCE_DB_VERSION 2U
#define PASSWORDS_TEMP VITAMEDIADECK_NETWORK_DIR "/passwords.tmp"
#define PASSWORDS_BACKUP VITAMEDIADECK_NETWORK_DIR "/passwords.bak"

static const char g_passwords_header[] = "# VitaMediaDeck network passwords v1\n";

typedef struct {
	char magic[8];
	uint32_t version;
	uint32_t record_size;
	uint32_t count;
} SourceDbHeader;

/* On-disk v1 record, kept only for a lossless one-time migration. Passwords
 * were not part of this structure. */
typedef struct {
	VtNetworkProtocol protocol;
	char name[VT_NETWORK_NAME_MAX];
	char host[VT_NETWORK_HOST_MAX];
	uint16_t port;
	char root_path[VT_NETWORK_PATH_MAX];
	char share[VT_NETWORK_NAME_MAX];
	char username[VT_NETWORK_USER_MAX];
	char domain[VT_NETWORK_NAME_MAX];
	char host_key_sha256[VT_NETWORK_FINGERPRINT_MAX];
} VtNetworkSourceV1;

static int g_initialized;

static int path_exists(const char *path) {
	SceIoStat status;
	return path && sceIoGetstat(path, &status) >= 0;
}

static int read_all(SceUID fd, void *buffer, size_t size) {
	unsigned char *out = buffer;
	while (size) {
		int ret = sceIoRead(fd, out, size);
		if (ret <= 0) return ret < 0 ? ret : -1;
		out += ret;
		size -= (size_t)ret;
	}
	return 0;
}

static int write_all(SceUID fd, const void *buffer, size_t size) {
	const unsigned char *data = buffer;
	while (size) {
		int ret = sceIoWrite(fd, data, size);
		if (ret <= 0) return ret < 0 ? ret : -1;
		data += ret;
		size -= (size_t)ret;
	}
	return 0;
}

int vt_network_init(void) {
	if (g_initialized) return 0;
	if (libssh2_init(0) != 0) return -1;
	sceIoMkdir(VITAMEDIADECK_DATA_DIR, 0777);
	sceIoMkdir(VITAMEDIADECK_NETWORK_DIR, 0777);
	g_initialized = 1;
	return 0;
}

void vt_network_shutdown(void) {
	if (!g_initialized) return;
	libssh2_exit();
	g_initialized = 0;
}

int vt_network_sources_load(VtNetworkSource *sources, int capacity) {
	if (!sources || capacity <= 0) return -1;
	/* Recover a database left between the two rename operations. */
	if (!path_exists(SOURCE_DB_PATH) && path_exists(SOURCE_DB_BACKUP))
		sceIoRename(SOURCE_DB_BACKUP, SOURCE_DB_PATH);
	SceUID fd = sceIoOpen(SOURCE_DB_PATH, SCE_O_RDONLY, 0);
	if (fd < 0) return 0;
	SourceDbHeader header;
	int ret = read_all(fd, &header, sizeof(header));
	if (ret < 0 || memcmp(header.magic, "VTSRC001", 8) != 0 ||
	    header.count > VT_NETWORK_MAX_SOURCES) {
		sceIoClose(fd);
		return -1;
	}
	int count = header.count > (uint32_t)capacity ? capacity : (int)header.count;
	if (header.version == SOURCE_DB_VERSION &&
	    header.record_size == sizeof(VtNetworkSource)) {
		ret = read_all(fd, sources, (size_t)count * sizeof(*sources));
	} else if (header.version == 1U &&
	           header.record_size == sizeof(VtNetworkSourceV1)) {
		for (int i = 0; i < count; i++) {
			VtNetworkSourceV1 old;
			ret = read_all(fd, &old, sizeof(old));
			if (ret < 0) break;
			memset(&sources[i], 0, sizeof(sources[i]));
			sources[i].protocol = old.protocol;
			snprintf(sources[i].name, sizeof(sources[i].name), "%s", old.name);
			snprintf(sources[i].host, sizeof(sources[i].host), "%s", old.host);
			sources[i].port = old.port;
			snprintf(sources[i].root_path, sizeof(sources[i].root_path), "%s",
			         old.root_path);
			snprintf(sources[i].share, sizeof(sources[i].share), "%s", old.share);
			snprintf(sources[i].username, sizeof(sources[i].username), "%s",
			         old.username);
			snprintf(sources[i].domain, sizeof(sources[i].domain), "%s", old.domain);
			snprintf(sources[i].host_key_sha256,
			         sizeof(sources[i].host_key_sha256), "%s",
			         old.host_key_sha256);
		}
	} else {
		ret = -1;
	}
	sceIoClose(fd);
	return ret < 0 ? ret : count;
}

int vt_network_sources_save(const VtNetworkSource *sources, int count) {
	if (count < 0 || count > VT_NETWORK_MAX_SOURCES || (count && !sources))
		return -1;
	sceIoMkdir(VITAMEDIADECK_DATA_DIR, 0777);
	sceIoMkdir(VITAMEDIADECK_NETWORK_DIR, 0777);
	sceIoRemove(SOURCE_DB_TEMP);
	SceUID fd = sceIoOpen(SOURCE_DB_TEMP,
	                      SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) return fd;
	SourceDbHeader header = { { 0 }, SOURCE_DB_VERSION,
	                          sizeof(VtNetworkSource), (uint32_t)count };
	memcpy(header.magic, "VTSRC001", 8);
	int ret = write_all(fd, &header, sizeof(header));
	if (ret == 0 && count)
		ret = write_all(fd, sources, (size_t)count * sizeof(*sources));
	if (ret == 0) ret = sceIoSyncByFd(fd, 0);
	int close_ret = sceIoClose(fd);
	if (ret == 0 && close_ret < 0) ret = close_ret;
	if (ret < 0) {
		sceIoRemove(SOURCE_DB_TEMP);
		return ret;
	}

	/* Vita filesystems do not guarantee POSIX rename-overwrite semantics.
	 * Keep the previous database recoverable until the replacement is in place. */
	sceIoRemove(SOURCE_DB_BACKUP);
	int had_previous = path_exists(SOURCE_DB_PATH);
	if (had_previous) {
		ret = sceIoRename(SOURCE_DB_PATH, SOURCE_DB_BACKUP);
		if (ret < 0) {
			sceIoRemove(SOURCE_DB_TEMP);
			return ret;
		}
	}
	ret = sceIoRename(SOURCE_DB_TEMP, SOURCE_DB_PATH);
	if (ret < 0) {
		if (had_previous) sceIoRename(SOURCE_DB_BACKUP, SOURCE_DB_PATH);
		sceIoRemove(SOURCE_DB_TEMP);
		return ret;
	}
	sceIoSync("ux0:", 0);
	if (had_previous) sceIoRemove(SOURCE_DB_BACKUP);
	return 0;
}

static void escape_password(const char *password, char *out, size_t out_size) {
	size_t used = 0;
	for (const unsigned char *cursor = (const unsigned char *)password;
	     cursor && *cursor && used + 1 < out_size; cursor++) {
		const char *escape = NULL;
		if (*cursor == '\\') escape = "\\\\";
		else if (*cursor == '\n') escape = "\\n";
		else if (*cursor == '\r') escape = "\\r";
		else if (*cursor == '\t') escape = "\\t";
		if (escape) {
			if (used + 2 >= out_size) break;
			out[used++] = escape[0];
			out[used++] = escape[1];
		} else out[used++] = (char)*cursor;
	}
	out[used] = '\0';
}

static void unescape_password(const char *text, size_t length,
	                          char out[VT_NETWORK_SECRET_MAX]) {
	size_t used = 0;
	for (size_t i = 0; i < length && used + 1 < VT_NETWORK_SECRET_MAX; i++) {
		char value = text[i];
		if (value == '\\' && i + 1 < length) {
			char escaped = text[++i];
			if (escaped == 'n') value = '\n';
			else if (escaped == 'r') value = '\r';
			else if (escaped == 't') value = '\t';
			else value = escaped;
		}
		out[used++] = value;
	}
	out[used] = '\0';
}

int vt_network_credentials_load(VtNetworkCredential *credentials, int capacity) {
	if (!credentials || capacity <= 0) return -1;
	memset(credentials, 0, sizeof(*credentials) * (size_t)capacity);
	SceIoStat status;
	memset(&status, 0, sizeof(status));
	if (sceIoGetstat(VITAMEDIADECK_NETWORK_PASSWORDS_PATH, &status) < 0) return 0;
	if (status.st_size <= 0 || status.st_size > 32768) return -1;
	size_t size = (size_t)status.st_size;
	char *data = malloc(size + 1);
	if (!data) return -1;
	SceUID fd = sceIoOpen(VITAMEDIADECK_NETWORK_PASSWORDS_PATH, SCE_O_RDONLY, 0);
	if (fd < 0) { free(data); return fd; }
	int ret = read_all(fd, data, size);
	int close_ret = sceIoClose(fd);
	if (ret == 0 && close_ret < 0) ret = close_ret;
	data[size] = '\0';
	size_t header_size = strlen(g_passwords_header);
	if (ret == 0 && (size < header_size ||
	    memcmp(data, g_passwords_header, header_size) != 0)) ret = -1;
	char *cursor = data + (ret == 0 ? header_size : size);
	char *end = data + size;
	while (ret == 0 && cursor < end) {
		char *newline = memchr(cursor, '\n', (size_t)(end - cursor));
		char *line_end = newline ? newline : end;
		char *tab = memchr(cursor, '\t', (size_t)(line_end - cursor));
		if (!tab) { ret = -1; break; }
		char saved = *tab;
		*tab = '\0';
		char *number_end = NULL;
		long index = strtol(cursor, &number_end, 10);
		*tab = saved;
		if (!number_end || number_end != tab || index < 0 || index >= capacity) {
			ret = -1;
			break;
		}
		unescape_password(tab + 1, (size_t)(line_end - tab - 1),
		                  credentials[index].password);
		cursor = newline ? newline + 1 : end;
	}
	memset(data, 0, size);
	free(data);
	if (ret < 0)
		memset(credentials, 0, sizeof(*credentials) * (size_t)capacity);
	return ret;
}

int vt_network_credentials_save(const VtNetworkCredential *credentials, int count) {
	if (count < 0 || count > VT_NETWORK_MAX_SOURCES || (count && !credentials))
		return -1;
	sceIoMkdir(VITAMEDIADECK_DATA_DIR, 0777);
	sceIoMkdir(VITAMEDIADECK_NETWORK_DIR, 0777);
	sceIoRemove(PASSWORDS_TEMP);
	SceUID fd = sceIoOpen(PASSWORDS_TEMP,
	                      SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
	if (fd < 0) return fd;
	int ret = write_all(fd, g_passwords_header, strlen(g_passwords_header));
	for (int i = 0; ret == 0 && i < count; i++) {
		if (!credentials[i].password[0]) continue;
		char escaped[VT_NETWORK_SECRET_MAX * 2 + 1];
		char line[sizeof(escaped) + 24];
		escape_password(credentials[i].password, escaped, sizeof(escaped));
		int length = snprintf(line, sizeof(line), "%d\t%s\n", i, escaped);
		if (length < 0 || (size_t)length >= sizeof(line)) ret = -1;
		else ret = write_all(fd, line, (size_t)length);
		memset(escaped, 0, sizeof(escaped));
		memset(line, 0, sizeof(line));
	}
	if (ret == 0) ret = sceIoSyncByFd(fd, 0);
	int close_ret = sceIoClose(fd);
	if (ret == 0 && close_ret < 0) ret = close_ret;
	if (ret < 0) { sceIoRemove(PASSWORDS_TEMP); return ret; }
	sceIoRemove(PASSWORDS_BACKUP);
	int had_previous = path_exists(VITAMEDIADECK_NETWORK_PASSWORDS_PATH);
	if (had_previous) {
		ret = sceIoRename(VITAMEDIADECK_NETWORK_PASSWORDS_PATH, PASSWORDS_BACKUP);
		if (ret < 0) { sceIoRemove(PASSWORDS_TEMP); return ret; }
	}
	ret = sceIoRename(PASSWORDS_TEMP, VITAMEDIADECK_NETWORK_PASSWORDS_PATH);
	if (ret < 0) {
		if (had_previous)
			sceIoRename(PASSWORDS_BACKUP, VITAMEDIADECK_NETWORK_PASSWORDS_PATH);
		sceIoRemove(PASSWORDS_TEMP);
		return ret;
	}
	sceIoSync("ux0:", 0);
	if (had_previous) sceIoRemove(PASSWORDS_BACKUP);
	return 0;
}

int vt_network_credentials_clear(void) {
	int ret = 0;
	const char *paths[] = {
		VITAMEDIADECK_NETWORK_PASSWORDS_PATH, PASSWORDS_TEMP, PASSWORDS_BACKUP
	};
	for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
		if (!path_exists(paths[i])) continue;
		int remove_ret = sceIoRemove(paths[i]);
		if (ret == 0 && remove_ret < 0) ret = remove_ret;
	}
	if (ret == 0) sceIoSync("ux0:", 0);
	return ret;
}

int vt_network_list(const VtNetworkSource *source,
	                const VtNetworkCredential *credential,
	                const char *path, VtNetworkEntry *entries,
	                int capacity, char *detail, size_t detail_size) {
	if (!source || !entries || capacity <= 0) return -1;
	if (detail && detail_size) detail[0] = '\0';
	switch (source->protocol) {
		case VT_NETWORK_WEBDAV:
			return vt_webdav_list(source, credential, path, entries, capacity,
			                      detail, detail_size);
		case VT_NETWORK_SFTP:
			return vt_sftp_list(source, credential, path, entries, capacity,
			                    detail, detail_size);
		case VT_NETWORK_SMB:
			return vt_smb_list(source, credential, path, entries, capacity,
			                   detail, detail_size);
		case VT_NETWORK_JELLYFIN:
			return vt_jellyfin_list(source, credential, path, entries, capacity,
			                        detail, detail_size);
		default: return -1;
	}
}

int vt_network_prepare_source(const VtNetworkSource *source,
	                          VtNetworkCredential *credential,
	                          char *detail, size_t detail_size) {
	if (!source || !credential) return -1;
	if (detail && detail_size) detail[0] = '\0';
	if (source->protocol == VT_NETWORK_JELLYFIN)
		return vt_jellyfin_authenticate(source, credential, detail, detail_size);
	return 0;
}

int vt_network_sftp_probe_fingerprint(const VtNetworkSource *source,
	                                  char *fingerprint,
	                                  size_t fingerprint_size,
	                                  char *detail, size_t detail_size) {
	return vt_sftp_probe_fingerprint(source, fingerprint, fingerprint_size,
	                                detail, detail_size);
}

int vt_network_webdav_probe_public_key(const VtNetworkSource *source,
	                                   const VtNetworkCredential *credential,
	                                   char *pin, size_t pin_size,
	                                   char *detail, size_t detail_size) {
	return vt_webdav_probe_public_key(source, credential, pin, pin_size,
	                                  detail, detail_size);
}

int vt_network_https_probe_public_key(const VtNetworkSource *source,
	                                  const VtNetworkCredential *credential,
	                                  char *pin, size_t pin_size,
	                                  char *detail, size_t detail_size) {
	if (!source) return -1;
	if (source->protocol == VT_NETWORK_WEBDAV)
		return vt_webdav_probe_public_key(source, credential, pin, pin_size,
		                                  detail, detail_size);
	if (source->protocol == VT_NETWORK_JELLYFIN)
		return vt_jellyfin_probe_public_key(source, pin, pin_size,
		                                    detail, detail_size);
	return -1;
}

static int network_factory_open_cancelable(void *opaque,
	                                       VtDecoderStreamHandle *out,
	                                       volatile int *cancel_flag) {
	VtNetworkStreamFactory *factory = opaque;
	if (!factory || !out) return -1;
	switch (factory->source.protocol) {
		case VT_NETWORK_WEBDAV:
			return vt_webdav_open_stream(&factory->source,
			                             &factory->credential, factory->path, out,
			                             cancel_flag);
		case VT_NETWORK_SFTP:
			return vt_sftp_open_stream(&factory->source,
			                           &factory->credential, factory->path, out,
			                           cancel_flag);
		case VT_NETWORK_SMB:
			return vt_smb_open_stream(&factory->source,
			                          &factory->credential, factory->path, out,
			                          cancel_flag);
		case VT_NETWORK_JELLYFIN:
			if (factory->jellyfin_subtitle_stream)
				return vt_jellyfin_open_subtitle_stream(
				    &factory->source, &factory->credential, factory->path,
				    factory->jellyfin_media_source_id,
				    factory->jellyfin_subtitle_index, out, cancel_flag);
			return vt_jellyfin_open_stream(&factory->source,
			                               &factory->credential, factory->path, out,
			                               cancel_flag, &factory->buffer);
		default: return -1;
	}
}

static int network_factory_open(void *opaque, VtDecoderStreamHandle *out) {
	return network_factory_open_cancelable(opaque, out, NULL);
}

static int network_factory_buffer_status(void *opaque,
	                                     VtDecoderBufferStatus *out) {
	VtNetworkStreamFactory *factory = opaque;
	if (!factory || !out || factory->source.protocol != VT_NETWORK_JELLYFIN)
		return -1;
	for (int attempt = 0; attempt < 4; attempt++) {
		uint32_t before = factory->buffer.sequence;
		__sync_synchronize();
		if (before & 1U) continue;
		VtDecoderBufferStatus snapshot = {
			.source_size = factory->buffer.source_size,
			.range_start = factory->buffer.range_start,
			.range_end = factory->buffer.range_end,
			.resident_bytes = factory->buffer.resident_bytes,
			.capacity_bytes = factory->buffer.capacity_bytes
		};
		__sync_synchronize();
		if (before == factory->buffer.sequence) {
			*out = snapshot;
			return snapshot.source_size > 0 ? 0 : -1;
		}
	}
	return -1;
}

int vt_network_stream_factory_init(VtNetworkStreamFactory *factory,
	                               const VtNetworkSource *source,
	                               const VtNetworkCredential *credential,
	                               const char *path) {
	if (!factory || !source || !path) return -1;
	memset(factory, 0, sizeof(*factory));
	factory->source = *source;
	if (credential) factory->credential = *credential;
	snprintf(factory->path, sizeof(factory->path), "%s", path);
	factory->factory.opaque = factory;
	factory->factory.open = network_factory_open;
	factory->factory.open_cancelable = network_factory_open_cancelable;
	if (source->protocol == VT_NETWORK_JELLYFIN)
		factory->factory.buffer_status = network_factory_buffer_status;
	return 0;
}

int vt_network_jellyfin_subtitle_stream_factory_init(
	VtNetworkStreamFactory *factory, const VtNetworkSource *source,
	const VtNetworkCredential *credential, const char *item_path,
	const char *media_source_id, int subtitle_stream_index) {
	if (!factory || !source || source->protocol != VT_NETWORK_JELLYFIN ||
	    !credential || !item_path || !media_source_id || !media_source_id[0] ||
	    subtitle_stream_index < 0 ||
	    strlen(media_source_id) >= sizeof(factory->jellyfin_media_source_id))
		return -1;
	int ret = vt_network_stream_factory_init(factory, source, credential,
	                                         item_path);
	if (ret < 0) return ret;
	snprintf(factory->jellyfin_media_source_id,
	         sizeof(factory->jellyfin_media_source_id), "%s", media_source_id);
	factory->jellyfin_subtitle_index = subtitle_stream_index;
	factory->jellyfin_subtitle_stream = 1;
	/* Subtitle delivery is a bounded in-memory text response, not a media-range
	 * cache, so the video timeline must keep reporting the video factory. */
	factory->factory.buffer_status = NULL;
	return 0;
}

int vt_network_fetch_artwork(const VtNetworkSource *source,
	                         const VtNetworkCredential *credential,
	                         const char *path, unsigned char **data,
	                         size_t *size, volatile int *cancel_flag) {
	if (!source || !data || !size) return -1;
	*data = NULL;
	*size = 0;
	if (source->protocol == VT_NETWORK_JELLYFIN)
		return vt_jellyfin_fetch_primary_image(source, credential, path,
		                                       data, size, cancel_flag);
	return VT_NETWORK_UNSUPPORTED_MEDIA;
}

static int suffix_ci(const char *name, const char *suffix) {
	size_t a = strlen(name), b = strlen(suffix);
	if (a < b) return 0;
	name += a - b;
	for (size_t i = 0; i < b; i++)
		if (tolower((unsigned char)name[i]) !=
		    tolower((unsigned char)suffix[i])) return 0;
	return 1;
}

int vt_network_is_supported_media(const char *name, int *is_audio) {
	if (is_audio) *is_audio = 0;
	if (!name) return 0;
	if (suffix_ci(name, ".mp4") || suffix_ci(name, ".m4v") ||
	    suffix_ci(name, ".mov") || suffix_ci(name, ".mkv")) return 1;
	if (suffix_ci(name, ".mp3") || suffix_ci(name, ".m4a") ||
	    suffix_ci(name, ".aac") || suffix_ci(name, ".wav")) {
		if (is_audio) *is_audio = 1;
		return 1;
	}
	return 0;
}

const char *vt_network_protocol_name(VtNetworkProtocol protocol) {
	switch (protocol) {
		case VT_NETWORK_WEBDAV: return "WebDAV";
		case VT_NETWORK_SFTP: return "SFTP";
		case VT_NETWORK_SMB: return "SMB";
		case VT_NETWORK_JELLYFIN: return "Jellyfin";
		default: return "?";
	}
}
