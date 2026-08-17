#ifndef VITATUBE_NETWORK_SOURCE_H
#define VITATUBE_NETWORK_SOURCE_H

#include <stddef.h>
#include <stdint.h>

#include "media/vita_decoder.h"

#define VT_NETWORK_MAX_SOURCES 32
#define VT_NETWORK_MAX_ENTRIES 256
#define VT_NETWORK_NAME_MAX 64
#define VT_NETWORK_HOST_MAX 192
#define VT_NETWORK_PATH_MAX 768
#define VT_NETWORK_USER_MAX 96
#define VT_NETWORK_SECRET_MAX 192
#define VT_NETWORK_FINGERPRINT_MAX 96

typedef enum {
	VT_NETWORK_WEBDAV = 1,
	VT_NETWORK_SFTP = 2,
	VT_NETWORK_SMB = 3
} VtNetworkProtocol;

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
} VtNetworkSource;

/* Secrets are session-only. The source database never serializes this
 * structure. */
typedef struct {
	char password[VT_NETWORK_SECRET_MAX];
} VtNetworkCredential;

typedef struct {
	char name[256];
	char path[VT_NETWORK_PATH_MAX];
	uint64_t size;
	int is_directory;
	int is_video;
	int is_audio;
} VtNetworkEntry;

typedef struct {
	VtDecoderStreamFactory factory;
	VtNetworkSource source;
	VtNetworkCredential credential;
	char path[VT_NETWORK_PATH_MAX];
} VtNetworkStreamFactory;

enum {
	VT_NETWORK_OK = 0,
	VT_NETWORK_ERROR = -1,
	VT_NETWORK_AUTH_FAILED = -2,
	VT_NETWORK_HOST_KEY_REQUIRED = -3,
	VT_NETWORK_HOST_KEY_MISMATCH = -4,
	VT_NETWORK_RANGE_UNSUPPORTED = -5,
	VT_NETWORK_UNSUPPORTED_MEDIA = -6
};

int vt_network_init(void);
void vt_network_shutdown(void);

int vt_network_sources_load(VtNetworkSource *sources, int capacity);
int vt_network_sources_save(const VtNetworkSource *sources, int count);

int vt_network_list(const VtNetworkSource *source,
	                const VtNetworkCredential *credential,
	                const char *path, VtNetworkEntry *entries,
	                int capacity, char *detail, size_t detail_size);

/* When SFTP has no trusted key yet, this returns the SHA-256 fingerprint to
 * show to the user before saving it into source.host_key_sha256. */
int vt_network_sftp_probe_fingerprint(const VtNetworkSource *source,
	                                  char *fingerprint,
	                                  size_t fingerprint_size,
	                                  char *detail, size_t detail_size);

int vt_network_stream_factory_init(VtNetworkStreamFactory *factory,
	                               const VtNetworkSource *source,
	                               const VtNetworkCredential *credential,
	                               const char *path);

int vt_network_is_supported_media(const char *name, int *is_audio);
const char *vt_network_protocol_name(VtNetworkProtocol protocol);

#endif
