#ifndef VITAMEDIADECK_NETWORK_SOURCE_H
#define VITAMEDIADECK_NETWORK_SOURCE_H

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
#define VT_NETWORK_TOKEN_MAX 256
#define VT_NETWORK_USER_ID_MAX 64
#define VT_NETWORK_FINGERPRINT_MAX 96
#define VT_NETWORK_TLS_PIN_MAX 64
#define VT_JELLYFIN_MAX_EXTERNAL_SUBTITLES 16

typedef enum {
	VT_NETWORK_WEBDAV = 1,
	VT_NETWORK_SFTP = 2,
	VT_NETWORK_SMB = 3,
	VT_NETWORK_JELLYFIN = 4
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
	/* Optional sha256// SPKI pin confirmed by the user for a private WebDAV
	 * server whose certificate is not issued by the bundled public CAs. */
	char tls_public_key_sha256[VT_NETWORK_TLS_PIN_MAX];
} VtNetworkSource;

/* Secrets are session-only. The source database never serializes this
 * structure. */
typedef struct {
	char password[VT_NETWORK_SECRET_MAX];
	/* Provider sessions remain memory-only. The optional credential store writes
	 * only password; access tokens and user ids are reacquired after launch. */
	char access_token[VT_NETWORK_TOKEN_MAX];
	char user_id[VT_NETWORK_USER_ID_MAX];
} VtNetworkCredential;

typedef struct {
	char name[256];
	char path[VT_NETWORK_PATH_MAX];
	uint64_t size;
	uint64_t runtime_ms;
	int production_year;
	float community_rating;
	int is_directory;
	int is_video;
	int is_audio;
} VtNetworkEntry;

typedef struct {
	int stream_index;
	int is_default;
	int is_forced;
	char language[16];
	char title[96];
	char codec[16];
} VtJellyfinSubtitleTrack;

/* Rich provider data is loaded only for the focused item. Keeping it outside
 * VtNetworkEntry avoids multiplying synopsis and cast storage by every item in
 * a large library page. */
typedef struct {
	char title[256];
	char original_title[256];
	char tagline[256];
	char overview[1536];
	char genres[256];
	char studios[192];
	char directors[192];
	char cast[384];
	char official_rating[32];
	char series_name[160];
	char media_source_id[VT_NETWORK_USER_ID_MAX];
	char audio_summary[256];
	char subtitle_summary[256];
	uint64_t runtime_ms;
	int production_year;
	float community_rating;
	float critic_rating;
	int audio_track_count;
	int subtitle_track_count;
	int favorite;
	int played;
	VtJellyfinSubtitleTrack external_subtitles[
		VT_JELLYFIN_MAX_EXTERNAL_SUBTITLES];
	int external_subtitle_count;
} VtJellyfinMetadata;

typedef struct {
	/* Sequence is odd while a cursor publishes a new cache snapshot. */
	volatile uint32_t sequence;
	volatile uint32_t writer_lock;
	uint64_t source_size;
	uint64_t range_start;
	uint64_t range_end;
	uint32_t resident_bytes;
	uint32_t capacity_bytes;
} VtNetworkBufferTelemetry;

typedef struct {
	VtDecoderStreamFactory factory;
	VtNetworkSource source;
	VtNetworkCredential credential;
	char path[VT_NETWORK_PATH_MAX];
	VtNetworkBufferTelemetry buffer;
	/* Exactly one live media cursor owns HUD telemetry. The decoder opens the
	 * video cursor first; keeping audio read-ahead out of this snapshot prevents
	 * the timeline trace from alternating between independent byte ranges. */
	volatile uintptr_t buffer_owner;
	char jellyfin_media_source_id[VT_NETWORK_USER_ID_MAX];
	int jellyfin_subtitle_index;
	int jellyfin_subtitle_stream;
} VtNetworkStreamFactory;

enum {
	VT_NETWORK_OK = 0,
	VT_NETWORK_ERROR = -1,
	VT_NETWORK_AUTH_FAILED = -2,
	VT_NETWORK_HOST_KEY_REQUIRED = -3,
	VT_NETWORK_HOST_KEY_MISMATCH = -4,
	VT_NETWORK_RANGE_UNSUPPORTED = -5,
	VT_NETWORK_UNSUPPORTED_MEDIA = -6,
	VT_NETWORK_TLS_TRUST_REQUIRED = -7,
	VT_NETWORK_TLS_PIN_MISMATCH = -8
};

int vt_network_init(void);
void vt_network_shutdown(void);

int vt_network_sources_load(VtNetworkSource *sources, int capacity);
int vt_network_sources_save(const VtNetworkSource *sources, int count);

/* Optional plaintext password store. Callers must gate load/save behind the
 * explicit remember-passwords preference. */
int vt_network_credentials_load(VtNetworkCredential *credentials, int capacity);
int vt_network_credentials_save(const VtNetworkCredential *credentials, int count);
int vt_network_credentials_clear(void);

int vt_network_list(const VtNetworkSource *source,
	                const VtNetworkCredential *credential,
	                const char *path, VtNetworkEntry *entries,
	                int capacity, char *detail, size_t detail_size);

/* Authenticates providers that issue a session token. File-oriented providers
 * need no preparation and return success immediately. */
int vt_network_prepare_source(const VtNetworkSource *source,
	                          VtNetworkCredential *credential,
	                          char *detail, size_t detail_size);

/* When SFTP has no trusted key yet, this returns the SHA-256 fingerprint to
 * show to the user before saving it into source.host_key_sha256. */
int vt_network_sftp_probe_fingerprint(const VtNetworkSource *source,
	                                  char *fingerprint,
	                                  size_t fingerprint_size,
	                                  char *detail, size_t detail_size);

/* Used only after normal CA validation reports an untrusted certificate. The
 * returned pin must be shown to and explicitly confirmed by the user. */
int vt_network_webdav_probe_public_key(const VtNetworkSource *source,
	                                   const VtNetworkCredential *credential,
	                                   char *pin, size_t pin_size,
	                                   char *detail, size_t detail_size);

/* Shared HTTPS trust probe for WebDAV and Jellyfin. */
int vt_network_https_probe_public_key(const VtNetworkSource *source,
	                                  const VtNetworkCredential *credential,
	                                  char *pin, size_t pin_size,
	                                  char *detail, size_t detail_size);

int vt_network_stream_factory_init(VtNetworkStreamFactory *factory,
	                               const VtNetworkSource *source,
	                               const VtNetworkCredential *credential,
	                               const char *path);

int vt_network_jellyfin_metadata(const VtNetworkSource *source,
	                              const VtNetworkCredential *credential,
	                              const char *path, VtJellyfinMetadata *metadata,
	                              char *detail, size_t detail_size,
	                              volatile int *cancel_flag);

int vt_network_jellyfin_subtitle_stream_factory_init(
	VtNetworkStreamFactory *factory, const VtNetworkSource *source,
	const VtNetworkCredential *credential, const char *item_path,
	const char *media_source_id, int subtitle_stream_index);

/* Returns provider artwork in an allocated buffer. The caller owns `data`.
 * File-oriented providers return VT_NETWORK_UNSUPPORTED_MEDIA. */
int vt_network_fetch_artwork(const VtNetworkSource *source,
	                         const VtNetworkCredential *credential,
	                         const char *path, unsigned char **data,
	                         size_t *size, volatile int *cancel_flag);

/* Explicit schemes take precedence. For a bare Jellyfin host, port 8096 is
 * plain HTTP and every other port is HTTPS. */
int vt_network_jellyfin_uses_https(const VtNetworkSource *source);

int vt_network_is_supported_media(const char *name, int *is_audio);
const char *vt_network_protocol_name(VtNetworkProtocol protocol);

/* Stable remote-media key shared by playback history and browser cells. */
void vt_network_media_history_id(const VtNetworkSource *source,
	                             const char *path, char out[16]);

#endif
