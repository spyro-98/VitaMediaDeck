#include "network/network_source.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <libssh2.h>

#include "app_paths.h"
#include "network/network_internal.h"

#define SOURCE_DB_PATH VITAWAVE_NETWORK_DIR "/sources.bin"
#define SOURCE_DB_TEMP VITAWAVE_NETWORK_DIR "/sources.tmp"
#define SOURCE_DB_VERSION 2U

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
	sceIoMkdir(VITAWAVE_DATA_DIR, 0777);
	sceIoMkdir(VITAWAVE_NETWORK_DIR, 0777);
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
	sceIoMkdir(VITAWAVE_DATA_DIR, 0777);
	sceIoMkdir(VITAWAVE_NETWORK_DIR, 0777);
	SceUID fd = sceIoOpen(SOURCE_DB_TEMP,
	                      SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0600);
	if (fd < 0) return fd;
	SourceDbHeader header = { { 0 }, SOURCE_DB_VERSION,
	                          sizeof(VtNetworkSource), (uint32_t)count };
	memcpy(header.magic, "VTSRC001", 8);
	int ret = write_all(fd, &header, sizeof(header));
	if (ret == 0 && count)
		ret = write_all(fd, sources, (size_t)count * sizeof(*sources));
	if (ret == 0) ret = sceIoSyncByFd(fd, 0);
	sceIoClose(fd);
	if (ret < 0) {
		sceIoRemove(SOURCE_DB_TEMP);
		return ret;
	}
	sceIoRemove(SOURCE_DB_PATH);
	return sceIoRename(SOURCE_DB_TEMP, SOURCE_DB_PATH);
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
		default: return -1;
	}
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

static int network_factory_open(void *opaque, VtDecoderStreamHandle *out) {
	VtNetworkStreamFactory *factory = opaque;
	if (!factory || !out) return -1;
	switch (factory->source.protocol) {
		case VT_NETWORK_WEBDAV:
			return vt_webdav_open_stream(&factory->source,
			                             &factory->credential, factory->path, out);
		case VT_NETWORK_SFTP:
			return vt_sftp_open_stream(&factory->source,
			                           &factory->credential, factory->path, out);
		case VT_NETWORK_SMB:
			return vt_smb_open_stream(&factory->source,
			                          &factory->credential, factory->path, out);
		default: return -1;
	}
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
	return 0;
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
	    suffix_ci(name, ".mov")) return 1;
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
		default: return "?";
	}
}
