#include "network/network_internal.h"

#include "common/text_log.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <jansson.h>
#include <psp2/kernel/threadmgr.h>
#include <vita_https.h>

#define JELLYFIN_RESPONSE_MAX (2U * 1024U * 1024U)
#define JELLYFIN_CACHE_CHUNK (1024U * 1024U)
#define JELLYFIN_CACHE_SLOTS 8
#define JELLYFIN_RANGE_CACHE (JELLYFIN_CACHE_CHUNK * JELLYFIN_CACHE_SLOTS)
#define JELLYFIN_SUBTITLE_MAX (2U * 1024U * 1024U)
#define JELLYFIN_PREFETCH_THREAD_CREATE_PRIORITY  0x10000100
#define JELLYFIN_PREFETCH_THREAD_RUNTIME_PRIORITY 0xB0
#define JELLYFIN_PREFETCH_THREAD_STACK            0x20000
#define JELLYFIN_CLIENT_VERSION "1.1"
#define JELLYFIN_DEVICE_ID "VMDK00001"

typedef struct {
	unsigned char *data;
	size_t size;
	size_t capacity;
	size_t limit;
} JellyfinBuffer;

typedef struct {
	unsigned char *data;
	uint64_t start;
	size_t size;
	unsigned int generation;
	int state;
} JellyfinCacheSlot;

enum {
	JELLYFIN_SLOT_EMPTY = 0,
	JELLYFIN_SLOT_FILLING,
	JELLYFIN_SLOT_READY
};

typedef struct {
	VitaHttpsClient *client;
	char url[2048];
	char access_token[VT_NETWORK_TOKEN_MAX];
	volatile int *cancel;
	uint64_t size;
	uint64_t position;
	unsigned char *cache;
	JellyfinCacheSlot slots[JELLYFIN_CACHE_SLOTS];
	volatile int cache_lock;
	volatile int worker_stop;
	volatile int worker_cancel;
	volatile int worker_done;
	SceUID worker_thid;
	unsigned int generation;
	uint64_t requested_start;
	uint64_t next_fetch;
	int reset_pending;
	int worker_error;
	int worker_retries;
	VtNetworkBufferTelemetry *telemetry;
	volatile uintptr_t *telemetry_owner;
} JellyfinStream;

typedef struct {
	unsigned char *data;
	size_t size;
	size_t position;
} JellyfinSubtitleStream;

static void jellyfin_publish_cache(JellyfinStream *stream, uint64_t start,
	                               uint64_t end, size_t resident) {
	if (!stream || !stream->telemetry) return;
	VtNetworkBufferTelemetry *telemetry = stream->telemetry;
	while (__sync_lock_test_and_set(&telemetry->writer_lock, 1U)) { }
	__sync_add_and_fetch(&telemetry->sequence, 1U);
	__sync_synchronize();
	telemetry->source_size = stream->size;
	telemetry->range_start = start;
	telemetry->range_end = end;
	telemetry->resident_bytes = (uint32_t)resident;
	telemetry->capacity_bytes = JELLYFIN_RANGE_CACHE;
	__sync_synchronize();
	__sync_add_and_fetch(&telemetry->sequence, 1U);
	__sync_lock_release(&telemetry->writer_lock);
}

static void jellyfin_cache_lock(JellyfinStream *stream) {
	while (__sync_lock_test_and_set(&stream->cache_lock, 1))
		sceKernelDelayThread(100);
}

static void jellyfin_cache_unlock(JellyfinStream *stream) {
	__sync_lock_release(&stream->cache_lock);
}

static void jellyfin_publish_slots_locked(JellyfinStream *stream) {
	uint64_t start = stream->size;
	uint64_t end = 0;
	size_t resident = 0;
	for (int i = 0; i < JELLYFIN_CACHE_SLOTS; i++) {
		JellyfinCacheSlot *slot = &stream->slots[i];
		if (slot->state != JELLYFIN_SLOT_READY || !slot->size) continue;
		if (slot->start < start) start = slot->start;
		if (slot->start + slot->size > end) end = slot->start + slot->size;
		resident += slot->size;
	}
	if (!resident) start = end = stream->requested_start;
	jellyfin_publish_cache(stream, start, end, resident);
}

static void secure_zero(void *memory, size_t size) {
	volatile unsigned char *bytes = memory;
	while (size--) *bytes++ = 0;
}

static size_t buffer_write(const void *contents, size_t bytes, void *opaque) {
	JellyfinBuffer *buffer = opaque;
	if (!buffer || !bytes) return bytes;
	if (bytes > buffer->limit - buffer->size) return 0;
	if (buffer->size + bytes + 1 > buffer->capacity) {
		size_t capacity = buffer->capacity ? buffer->capacity * 2 : 8192;
		while (capacity < buffer->size + bytes + 1) capacity *= 2;
		if (capacity > buffer->limit + 1) capacity = buffer->limit + 1;
		unsigned char *next = realloc(buffer->data, capacity);
		if (!next) return 0;
		buffer->data = next;
		buffer->capacity = capacity;
	}
	memcpy(buffer->data + buffer->size, contents, bytes);
	buffer->size += bytes;
	buffer->data[buffer->size] = '\0';
	return bytes;
}

static size_t fixed_write(const void *contents, size_t bytes, void *opaque) {
	JellyfinBuffer *buffer = opaque;
	if (!buffer || bytes > buffer->capacity - buffer->size) return 0;
	memcpy(buffer->data + buffer->size, contents, bytes);
	buffer->size += bytes;
	return bytes;
}

static int append_url_path(char *url, size_t size, const char *path) {
	if (!path || !path[0]) return 0;
	if (strchr(path, '?') || strchr(path, '#')) return -1;
	size_t used = strlen(url);
	while (used > 0 && url[used - 1] == '/') url[--used] = '\0';
	while (*path == '/') path++;
	if (!path[0]) return 0;
	int written = snprintf(url + used, size - used, "/%s", path);
	return written < 0 || (size_t)written >= size - used ? -1 : 0;
}

int vt_network_jellyfin_uses_https(const VtNetworkSource *source) {
	if (!source || source->protocol != VT_NETWORK_JELLYFIN) return 0;
	if (!strncmp(source->host, "https://", 8)) return 1;
	if (!strncmp(source->host, "http://", 7)) return 0;
	return source->port != 8096;
}

static int jellyfin_base_url(const VtNetworkSource *source,
	                         char *out, size_t out_size) {
	if (!source || !out || !out_size) return -1;
	const char *host = source->host;
	if (!host || !host[0]) return -1;
	int https = vt_network_jellyfin_uses_https(source);
	int scheme_length = !strncmp(host, "https://", 8) ? 8
	                  : !strncmp(host, "http://", 7) ? 7 : 0;
	if (!scheme_length && strstr(host, "://")) return -1;
	const char *authority = host + scheme_length;
	if (!authority[0] || strchr(authority, '?') || strchr(authority, '#')) return -1;
	const char *base_path = strchr(authority, '/');
	size_t authority_length = base_path
	                        ? (size_t)(base_path - authority) : strlen(authority);
	if (!authority_length) return -1;
	int explicit_port = 0;
	if (authority[0] == '[') {
		const char *closing = memchr(authority, ']', authority_length);
		explicit_port = closing && closing + 1 < authority + authority_length &&
		                closing[1] == ':';
	} else explicit_port = memchr(authority, ':', authority_length) != NULL;
	int written = snprintf(out, out_size, "%s://%.*s",
	                       https ? "https" : "http",
	                       (int)authority_length, authority);
	if (written < 0 || (size_t)written >= out_size) return -1;
	uint16_t default_port = https ? 443 : 80;
	if (!explicit_port && source->port && source->port != default_port) {
		size_t used = strlen(out);
		written = snprintf(out + used, out_size - used, ":%u", source->port);
		if (written < 0 || (size_t)written >= out_size - used) return -1;
	}
	if (append_url_path(out, out_size, base_path) < 0 ||
	    append_url_path(out, out_size, source->root_path) < 0) return -1;
	return 0;
}

static int jellyfin_url(const VtNetworkSource *source, const char *endpoint,
	                    const char *query, char *out, size_t out_size) {
	if (jellyfin_base_url(source, out, out_size) < 0 ||
	    append_url_path(out, out_size, endpoint) < 0) return -1;
	if (query && query[0]) {
		size_t used = strlen(out);
		int written = snprintf(out + used, out_size - used, "?%s", query);
		if (written < 0 || (size_t)written >= out_size - used) return -1;
	}
	return 0;
}

static int safe_header_value(const char *value) {
	if (!value || !value[0]) return 0;
	for (const unsigned char *cursor = (const unsigned char *)value; *cursor; cursor++)
		if (*cursor < 0x21 || *cursor > 0x7e || *cursor == '"' ||
		    *cursor == '\\') return 0;
	return 1;
}

static int authorization_header(const char *token, char *out, size_t out_size) {
	if (token && token[0] && !safe_header_value(token)) return -1;
	int written = snprintf(
	    out, out_size,
	    "Authorization: MediaBrowser Client=\"VitaMediaDeck\", "
	    "Device=\"PlayStation Vita\", DeviceId=\"%s\", Version=\"%s\"%s%s%s",
	    JELLYFIN_DEVICE_ID, JELLYFIN_CLIENT_VERSION,
	    token && token[0] ? ", Token=\"" : "",
	    token && token[0] ? token : "",
	    token && token[0] ? "\"" : "");
	return written < 0 || (size_t)written >= out_size ? -1 : 0;
}

static void jellyfin_client_config(const VtNetworkSource *source,
	                               VitaHttpsClientConfig *config) {
	memset(config, 0, sizeof(*config));
	config->user_agent = "VitaMediaDeck/1.1";
	config->connect_timeout_ms = 5000;
	config->request_timeout_ms = 12000;
	config->low_speed_bytes_per_second = 1024;
	config->low_speed_seconds = 6;
	config->allow_http = !vt_network_jellyfin_uses_https(source);
	if (!config->allow_http && source->tls_public_key_sha256[0]) {
		config->pinned_public_key = source->tls_public_key_sha256;
		config->allow_untrusted_ca_with_pin = 1;
	}
}

static int transport_result(int result) {
	if (result == VITA_HTTPS_ERROR_UNTRUSTED_CERTIFICATE)
		return VT_NETWORK_TLS_TRUST_REQUIRED;
	if (result == VITA_HTTPS_ERROR_PIN_MISMATCH)
		return VT_NETWORK_TLS_PIN_MISMATCH;
	return VT_NETWORK_ERROR;
}

static int request_json(const VtNetworkSource *source, const char *url,
	                    const char *method, const char *authorization,
	                    const char *body, JellyfinBuffer *response,
	                    long *status, volatile int *cancel) {
	VitaHttpsClientConfig config;
	jellyfin_client_config(source, &config);
	VitaHttpsClient *client = vita_https_client_create(&config);
	if (!client) return VT_NETWORK_ERROR;
	const char *headers[] = {
		authorization,
		"Accept: application/json",
		"Content-Type: application/json; charset=utf-8",
		NULL
	};
	VitaHttpsRequest request = {
		.method = method,
		.url = url,
		.headers = headers,
		.body = body,
		.body_size = body ? strlen(body) : 0,
		.write = response ? buffer_write : NULL,
		.write_opaque = response,
		.cancel_flag = cancel
	};
	VitaHttpsResponse transport = { 0 };
	int result = vita_https_perform(client, &request, &transport);
	vita_https_client_destroy(client);
	if (status) *status = transport.status_code;
	if (transport.status_code == 401 || transport.status_code == 403)
		return VT_NETWORK_AUTH_FAILED;
	if (result < 0) return transport_result(result);
	return 0;
}

int vt_jellyfin_authenticate(const VtNetworkSource *source,
	                         VtNetworkCredential *credential,
	                         char *detail, size_t detail_size) {
	if (!source || !credential || !source->username[0]) return -1;
	if (credential->access_token[0] && credential->user_id[0]) return 0;
	char url[2048];
	if (jellyfin_url(source, "Users/AuthenticateByName", NULL,
	                 url, sizeof(url)) < 0) {
		if (detail && detail_size)
			snprintf(detail, detail_size,
			         "Jellyfin requires a valid HTTP or HTTPS endpoint");
		return -1;
	}
	char authorization[512];
	if (authorization_header(NULL, authorization, sizeof(authorization)) < 0)
		return -1;
	json_t *payload = json_pack("{s:s,s:s}", "Username", source->username,
	                           "Pw", credential->password);
	if (!payload) return -1;
	char *body = json_dumps(payload, JSON_COMPACT | JSON_ENSURE_ASCII);
	json_decref(payload);
	if (!body) return -1;
	JellyfinBuffer response = { .limit = JELLYFIN_RESPONSE_MAX };
	long status = 0;
	int ret = request_json(source, url, "POST", authorization, body,
	                       &response, &status, NULL);
	secure_zero(body, strlen(body));
	free(body);
	if (ret < 0) {
		if (detail && detail_size && ret == VT_NETWORK_AUTH_FAILED)
			snprintf(detail, detail_size, "Jellyfin authentication failed");
		free(response.data);
		return ret;
	}
	if (!response.data || response.size == 0) {
		if (detail && detail_size)
			snprintf(detail, detail_size, "Empty Jellyfin authentication response");
		free(response.data);
		return VT_NETWORK_AUTH_FAILED;
	}
	json_error_t error;
	json_t *root = json_loadb((const char *)response.data, response.size, 0, &error);
	free(response.data);
	const char *token = root
	                  ? json_string_value(json_object_get(root, "AccessToken"))
	                  : NULL;
	json_t *user = root ? json_object_get(root, "User") : NULL;
	const char *user_id = user
	                    ? json_string_value(json_object_get(user, "Id")) : NULL;
	if (!safe_header_value(token) || !safe_header_value(user_id) ||
	    strlen(token) >= sizeof(credential->access_token) ||
	    strlen(user_id) >= sizeof(credential->user_id)) {
		if (detail && detail_size)
			snprintf(detail, detail_size, "Invalid Jellyfin authentication response");
		ret = VT_NETWORK_AUTH_FAILED;
	} else {
		snprintf(credential->access_token, sizeof(credential->access_token), "%s", token);
		snprintf(credential->user_id, sizeof(credential->user_id), "%s", user_id);
		ret = 0;
	}
	if (root) json_decref(root);
	(void)status;
	return ret;
}

static int item_id_from_path(const char *path, char out[VT_NETWORK_USER_ID_MAX]) {
	if (!path || !path[0]) { out[0] = '\0'; return 0; }
	const char *id = strrchr(path, '/');
	id = id ? id + 1 : path;
	size_t length = strlen(id);
	if (!length || length >= VT_NETWORK_USER_ID_MAX) return -1;
	for (size_t i = 0; i < length; i++)
		if (!isalnum((unsigned char)id[i]) && id[i] != '-') return -1;
	memcpy(out, id, length + 1);
	return 0;
}

static int item_is_folder(json_t *item, const char *type) {
	json_t *folder = json_object_get(item, "IsFolder");
	if (json_is_true(folder)) return 1;
	return type && (!strcmp(type, "Folder") || !strcmp(type, "CollectionFolder") ||
	                !strcmp(type, "Series") || !strcmp(type, "Season") ||
	                !strcmp(type, "BoxSet") || !strcmp(type, "Playlist"));
}

static uint64_t item_size(json_t *item) {
	json_t *sources = json_object_get(item, "MediaSources");
	if (!json_is_array(sources) || json_array_size(sources) == 0) return 0;
	json_t *source = json_array_get(sources, 0);
	json_t *size = source ? json_object_get(source, "Size") : NULL;
	return json_is_integer(size) && json_integer_value(size) > 0
	     ? (uint64_t)json_integer_value(size) : 0;
}

static void copy_json_text(json_t *object, const char *key,
	                       char *out, size_t out_size) {
	if (!out || !out_size) return;
	out[0] = '\0';
	const char *value = object
	                  ? json_string_value(json_object_get(object, key)) : NULL;
	if (value) snprintf(out, out_size, "%s", value);
}

static void append_metadata_value(char *out, size_t out_size,
	                              const char *value) {
	if (!out || !out_size || !value || !value[0]) return;
	size_t used = strlen(out);
	if (used >= out_size - 1) return;
	snprintf(out + used, out_size - used, "%s%s", used ? "  /  " : "", value);
}

static void join_name_array(json_t *array, char *out, size_t out_size) {
	if (!out || !out_size) return;
	out[0] = '\0';
	if (!json_is_array(array)) return;
	size_t index;
	json_t *value;
	json_array_foreach(array, index, value) {
		const char *name = json_is_string(value) ? json_string_value(value)
		                 : json_is_object(value)
		                     ? json_string_value(json_object_get(value, "Name"))
		                     : NULL;
		append_metadata_value(out, out_size, name);
	}
}

static int jellyfin_text_subtitle_codec(const char *codec) {
	if (!codec) return 0;
	return !strcasecmp(codec, "srt") || !strcasecmp(codec, "subrip") ||
	       !strcasecmp(codec, "ass") || !strcasecmp(codec, "ssa") ||
	       !strcasecmp(codec, "webvtt") || !strcasecmp(codec, "vtt") ||
	       !strcasecmp(codec, "mov_text") || !strcasecmp(codec, "text") ||
	       !strcasecmp(codec, "microdvd");
}

static void parse_people(json_t *people, VtJellyfinMetadata *metadata) {
	if (!json_is_array(people) || !metadata) return;
	size_t index;
	json_t *person;
	json_array_foreach(people, index, person) {
		if (!json_is_object(person)) continue;
		const char *name = json_string_value(json_object_get(person, "Name"));
		const char *type = json_string_value(json_object_get(person, "Type"));
		if (!name || !type) continue;
		if (!strcmp(type, "Director"))
			append_metadata_value(metadata->directors,
			                      sizeof(metadata->directors), name);
		else if (!strcmp(type, "Actor"))
			append_metadata_value(metadata->cast, sizeof(metadata->cast), name);
	}
}

static void parse_media_streams(json_t *streams, VtJellyfinMetadata *metadata) {
	if (!json_is_array(streams) || !metadata) return;
	size_t array_index;
	json_t *stream;
	json_array_foreach(streams, array_index, stream) {
		if (!json_is_object(stream)) continue;
		const char *type = json_string_value(json_object_get(stream, "Type"));
		const char *codec = json_string_value(json_object_get(stream, "Codec"));
		const char *language = json_string_value(json_object_get(stream, "Language"));
		const char *display = json_string_value(
		    json_object_get(stream, "DisplayTitle"));
		const char *title = json_string_value(json_object_get(stream, "Title"));
		const char *summary = display && display[0] ? display
		                      : title && title[0] ? title
		                      : language && language[0] ? language : codec;
		if (type && !strcmp(type, "Audio")) {
			metadata->audio_track_count++;
			append_metadata_value(metadata->audio_summary,
			                      sizeof(metadata->audio_summary), summary);
			continue;
		}
		if (!type || strcmp(type, "Subtitle")) continue;
		metadata->subtitle_track_count++;
		append_metadata_value(metadata->subtitle_summary,
		                      sizeof(metadata->subtitle_summary), summary);
		const char *delivery = json_string_value(
		    json_object_get(stream, "DeliveryMethod"));
		int server_text_stream =
		    json_is_true(json_object_get(stream, "IsExternal")) ||
		    json_is_true(json_object_get(stream, "IsTextSubtitleStream")) ||
		    json_is_true(json_object_get(stream, "SupportsExternalStream")) ||
		    (delivery && !strcasecmp(delivery, "External"));
		if (!server_text_stream || !jellyfin_text_subtitle_codec(codec) ||
		    metadata->external_subtitle_count >=
		        VT_JELLYFIN_MAX_EXTERNAL_SUBTITLES)
			continue;
		json_t *index = json_object_get(stream, "Index");
		if (!json_is_integer(index) || json_integer_value(index) < 0) continue;
		VtJellyfinSubtitleTrack *track =
		    &metadata->external_subtitles[metadata->external_subtitle_count++];
		memset(track, 0, sizeof(*track));
		track->stream_index = (int)json_integer_value(index);
		track->is_default = json_is_true(json_object_get(stream, "IsDefault"));
		track->is_forced = json_is_true(json_object_get(stream, "IsForced"));
		if (language) snprintf(track->language, sizeof(track->language), "%s", language);
		if (summary) snprintf(track->title, sizeof(track->title), "%s [JF]", summary);
		if (codec) snprintf(track->codec, sizeof(track->codec), "%s", codec);
	}
}

int vt_network_jellyfin_metadata(const VtNetworkSource *source,
	                              const VtNetworkCredential *credential,
	                              const char *path, VtJellyfinMetadata *metadata,
	                              char *detail, size_t detail_size,
	                              volatile int *cancel_flag) {
	if (!source || !credential || !path || !metadata ||
	    source->protocol != VT_NETWORK_JELLYFIN ||
	    !credential->access_token[0] || !credential->user_id[0])
		return VT_NETWORK_AUTH_FAILED;
	memset(metadata, 0, sizeof(*metadata));
	if (detail && detail_size) detail[0] = '\0';
	char item_id[VT_NETWORK_USER_ID_MAX];
	if (item_id_from_path(path, item_id) < 0 || !item_id[0]) return -1;
	char endpoint[192];
	int endpoint_size = snprintf(endpoint, sizeof(endpoint), "Users/%s/Items/%s",
	                             credential->user_id, item_id);
	if (endpoint_size < 0 || (size_t)endpoint_size >= sizeof(endpoint)) return -1;
	const char *fields =
	    "fields=Overview,Genres,Studios,People,MediaSources,MediaStreams,"
	    "OriginalTitle,Taglines,DateCreated,ProviderIds";
	char url[2048];
	if (jellyfin_url(source, endpoint, fields, url, sizeof(url)) < 0) return -1;
	char authorization[512];
	if (authorization_header(credential->access_token, authorization,
	                         sizeof(authorization)) < 0) return -1;
	JellyfinBuffer response = { .limit = JELLYFIN_RESPONSE_MAX };
	long status = 0;
	int ret = request_json(source, url, "GET", authorization, NULL, &response,
	                       &status, cancel_flag);
	if (ret < 0) {
		if (detail && detail_size)
			snprintf(detail, detail_size, "%s",
			         ret == VT_NETWORK_AUTH_FAILED ? "Jellyfin session expired"
			                                       : "Unable to read item metadata");
		free(response.data);
		return ret;
	}
	json_error_t error;
	json_t *item = response.data
	             ? json_loadb((const char *)response.data, response.size, 0, &error)
	             : NULL;
	free(response.data);
	if (!json_is_object(item)) {
		if (detail && detail_size)
			snprintf(detail, detail_size, "Invalid Jellyfin metadata response");
		if (item) json_decref(item);
		return -1;
	}
	copy_json_text(item, "Name", metadata->title, sizeof(metadata->title));
	copy_json_text(item, "OriginalTitle", metadata->original_title,
	               sizeof(metadata->original_title));
	copy_json_text(item, "Tagline", metadata->tagline, sizeof(metadata->tagline));
	if (!metadata->tagline[0]) {
		json_t *taglines = json_object_get(item, "Taglines");
		const char *first = json_is_array(taglines) && json_array_size(taglines)
		                  ? json_string_value(json_array_get(taglines, 0)) : NULL;
		if (first) snprintf(metadata->tagline, sizeof(metadata->tagline), "%s", first);
	}
	copy_json_text(item, "Overview", metadata->overview,
	               sizeof(metadata->overview));
	copy_json_text(item, "OfficialRating", metadata->official_rating,
	               sizeof(metadata->official_rating));
	copy_json_text(item, "SeriesName", metadata->series_name,
	               sizeof(metadata->series_name));
	join_name_array(json_object_get(item, "Genres"), metadata->genres,
	                sizeof(metadata->genres));
	join_name_array(json_object_get(item, "Studios"), metadata->studios,
	                sizeof(metadata->studios));
	parse_people(json_object_get(item, "People"), metadata);
	json_t *runtime = json_object_get(item, "RunTimeTicks");
	if (json_is_integer(runtime) && json_integer_value(runtime) > 0)
		metadata->runtime_ms = (uint64_t)json_integer_value(runtime) / 10000ULL;
	json_t *year = json_object_get(item, "ProductionYear");
	if (json_is_integer(year)) metadata->production_year = (int)json_integer_value(year);
	json_t *community = json_object_get(item, "CommunityRating");
	if (json_is_number(community))
		metadata->community_rating = (float)json_number_value(community);
	json_t *critic = json_object_get(item, "CriticRating");
	if (json_is_number(critic)) metadata->critic_rating = (float)json_number_value(critic);
	json_t *user_data = json_object_get(item, "UserData");
	if (json_is_object(user_data)) {
		metadata->favorite = json_is_true(json_object_get(user_data, "IsFavorite"));
		metadata->played = json_is_true(json_object_get(user_data, "Played"));
	}
	json_t *sources = json_object_get(item, "MediaSources");
	json_t *media_source = json_is_array(sources) && json_array_size(sources)
	                     ? json_array_get(sources, 0) : NULL;
	const char *media_source_id = media_source
	    ? json_string_value(json_object_get(media_source, "Id")) : NULL;
	if (safe_header_value(media_source_id) &&
	    strlen(media_source_id) < sizeof(metadata->media_source_id))
		snprintf(metadata->media_source_id, sizeof(metadata->media_source_id),
		         "%s", media_source_id);
	json_t *streams = json_object_get(item, "MediaStreams");
	if (!json_is_array(streams) && media_source)
		streams = json_object_get(media_source, "MediaStreams");
	parse_media_streams(streams, metadata);
	json_decref(item);
	(void)status;
	return 0;
}

int vt_jellyfin_list(const VtNetworkSource *source,
	                 const VtNetworkCredential *credential,
	                 const char *path, VtNetworkEntry *entries, int capacity,
	                 char *detail, size_t detail_size) {
	if (!source || !credential || !entries || capacity <= 0 ||
	    !credential->access_token[0] || !credential->user_id[0])
		return VT_NETWORK_AUTH_FAILED;
	char parent_id[VT_NETWORK_USER_ID_MAX];
	if (item_id_from_path(path, parent_id) < 0) return -1;
	char query[512];
	int query_size = snprintf(
	    query, sizeof(query),
	    "userId=%s%s%s&limit=%d&recursive=false&sortBy=SortName&sortOrder=Ascending"
	    "&fields=MediaSources,MediaStreams&enableImages=true&enableTotalRecordCount=false",
	    credential->user_id, parent_id[0] ? "&parentId=" : "",
	    parent_id[0] ? parent_id : "", capacity);
	if (query_size < 0 || (size_t)query_size >= sizeof(query)) return -1;
	char url[2048];
	if (jellyfin_url(source, "Items", query, url, sizeof(url)) < 0) return -1;
	char authorization[512];
	if (authorization_header(credential->access_token, authorization,
	                         sizeof(authorization)) < 0) return -1;
	JellyfinBuffer response = { .limit = JELLYFIN_RESPONSE_MAX };
	long status = 0;
	int ret = request_json(source, url, "GET", authorization, NULL,
	                       &response, &status, NULL);
	if (ret < 0) {
		if (detail && detail_size && ret == VT_NETWORK_AUTH_FAILED)
			snprintf(detail, detail_size, "Jellyfin session expired");
		free(response.data);
		return ret;
	}
	if (!response.data || response.size == 0) {
		if (detail && detail_size)
			snprintf(detail, detail_size, "Empty Jellyfin library response");
		free(response.data);
		return -1;
	}
	json_error_t error;
	json_t *root = json_loadb((const char *)response.data, response.size, 0, &error);
	free(response.data);
	json_t *items = root ? json_object_get(root, "Items") : NULL;
	if (!json_is_array(items)) {
		if (detail && detail_size)
			snprintf(detail, detail_size, "Invalid Jellyfin library response");
		if (root) json_decref(root);
		return -1;
	}
	int count = 0;
	size_t index;
	json_t *item;
	json_array_foreach(items, index, item) {
		if (count >= capacity || !json_is_object(item)) break;
		const char *id = json_string_value(json_object_get(item, "Id"));
		const char *name = json_string_value(json_object_get(item, "Name"));
		const char *type = json_string_value(json_object_get(item, "Type"));
		const char *media_type = json_string_value(json_object_get(item, "MediaType"));
		if (!id || !name || !safe_header_value(id)) continue;
		int folder = item_is_folder(item, type);
		int video = media_type && !strcmp(media_type, "Video");
		if (!folder && !video) continue;
		VtNetworkEntry *entry = &entries[count];
		memset(entry, 0, sizeof(*entry));
		snprintf(entry->name, sizeof(entry->name), "%s", name);
		if (path && path[0])
			snprintf(entry->path, sizeof(entry->path), "%s/%s", path, id);
		else snprintf(entry->path, sizeof(entry->path), "%s", id);
		entry->size = video ? item_size(item) : 0;
		json_t *runtime = json_object_get(item, "RunTimeTicks");
		if (json_is_integer(runtime) && json_integer_value(runtime) > 0)
			entry->runtime_ms = (uint64_t)json_integer_value(runtime) / 10000ULL;
		json_t *year = json_object_get(item, "ProductionYear");
		if (json_is_integer(year))
			entry->production_year = (int)json_integer_value(year);
		json_t *rating = json_object_get(item, "CommunityRating");
		if (json_is_number(rating))
			entry->community_rating = (float)json_number_value(rating);
		entry->is_directory = folder;
		entry->is_video = video;
		count++;
	}
	json_decref(root);
	(void)status;
	return count;
}

int vt_jellyfin_probe_public_key(const VtNetworkSource *source,
	                             char *pin, size_t pin_size,
	                             char *detail, size_t detail_size) {
	if (!source || !pin || !pin_size) return -1;
	if (!vt_network_jellyfin_uses_https(source)) return -1;
	char url[2048];
	if (jellyfin_url(source, "System/Info/Public", NULL, url, sizeof(url)) < 0)
		return -1;
	VitaHttpsClientConfig config;
	jellyfin_client_config(source, &config);
	config.pinned_public_key = NULL;
	config.allow_untrusted_ca_with_pin = 0;
	VitaHttpsClient *client = vita_https_client_create(&config);
	if (!client) return -1;
	int result = vita_https_probe_public_key(client, url, pin, pin_size);
	if (result < 0 && detail && detail_size)
		snprintf(detail, detail_size, "%s", vita_https_error_string(result));
	vita_https_client_destroy(client);
	return result;
}

int vt_jellyfin_fetch_primary_image(const VtNetworkSource *source,
	                                const VtNetworkCredential *credential,
	                                const char *path, unsigned char **data,
	                                size_t *size, volatile int *cancel_flag) {
	if (!source || !credential || !path || !data || !size ||
	    !credential->access_token[0]) return VT_NETWORK_AUTH_FAILED;
	*data = NULL;
	*size = 0;
	char item_id[VT_NETWORK_USER_ID_MAX];
	if (item_id_from_path(path, item_id) < 0 || !item_id[0]) return -1;
	char endpoint[160];
	int endpoint_size = snprintf(endpoint, sizeof(endpoint),
	                             "Items/%s/Images/Primary", item_id);
	if (endpoint_size < 0 || (size_t)endpoint_size >= sizeof(endpoint)) return -1;
	char url[2048];
	if (jellyfin_url(source, endpoint,
	                 "maxWidth=480&maxHeight=272&quality=85&format=Jpg",
	                 url, sizeof(url)) < 0) return -1;
	char authorization[512];
	if (authorization_header(credential->access_token, authorization,
	                         sizeof(authorization)) < 0) return -1;
	VitaHttpsClientConfig config;
	jellyfin_client_config(source, &config);
	config.request_timeout_ms = 8000;
	VitaHttpsClient *client = vita_https_client_create(&config);
	if (!client) return -1;
	const char *headers[] = { authorization, "Accept: image/jpeg", NULL };
	JellyfinBuffer response = { .limit = JELLYFIN_RESPONSE_MAX };
	VitaHttpsRequest request = {
		.method = "GET",
		.url = url,
		.headers = headers,
		.write = buffer_write,
		.write_opaque = &response,
		.cancel_flag = cancel_flag
	};
	VitaHttpsResponse transport = { 0 };
	int result = vita_https_perform(client, &request, &transport);
	vita_https_client_destroy(client);
	if (transport.status_code == 401 || transport.status_code == 403)
		result = VT_NETWORK_AUTH_FAILED;
	else if (result < 0) result = transport_result(result);
	else if (transport.status_code != 200 || response.size < 4) result = -1;
	if (result < 0) {
		free(response.data);
		return result;
	}
	*data = response.data;
	*size = response.size;
	return 0;
}

static int jellyfin_stream_fetch_slot(JellyfinStream *stream,
	                                  JellyfinCacheSlot *slot,
	                                  uint64_t start) {
	if (!stream || !slot || start >= stream->size) return 0;
	uint64_t remaining = stream->size - start;
	uint64_t wanted = remaining < JELLYFIN_CACHE_CHUNK
	                ? remaining : JELLYFIN_CACHE_CHUNK;
	uint64_t end = start + wanted - 1;
	char authorization[512];
	char range[96];
	if (authorization_header(stream->access_token, authorization,
	                         sizeof(authorization)) < 0) return -1;
	snprintf(range, sizeof(range), "Range: bytes=%llu-%llu",
	         (unsigned long long)start, (unsigned long long)end);
	const char *headers[] = { authorization, range, NULL };
	JellyfinBuffer output = {
		.data = slot->data,
		.capacity = JELLYFIN_CACHE_CHUNK,
		.limit = JELLYFIN_CACHE_CHUNK
	};
	VitaHttpsRequest request = {
		.method = "GET",
		.url = stream->url,
		.headers = headers,
		.write = fixed_write,
		.write_opaque = &output,
		.cancel_flag = &stream->worker_cancel
	};
	VitaHttpsResponse response = { 0 };
	int result = vita_https_perform(stream->client, &request, &response);
	if (response.status_code == 401 || response.status_code == 403)
		return VT_NETWORK_AUTH_FAILED;
	if (result < 0 || response.status_code != 206 ||
	    output.size != (size_t)wanted ||
	    response.content_length != (int64_t)wanted)
		return result < 0 ? transport_result(result)
		                  : VT_NETWORK_RANGE_UNSUPPORTED;
	slot->start = start;
	slot->size = output.size;
	return 0;
}

static int jellyfin_prefetch_worker(SceSize args, void *argp) {
	(void)args;
	JellyfinStream *stream = *(JellyfinStream **)argp;
	sceKernelChangeThreadPriority(sceKernelGetThreadId(),
	                              JELLYFIN_PREFETCH_THREAD_RUNTIME_PRIORITY);
	while (!stream->worker_stop) {
		jellyfin_cache_lock(stream);
		if (stream->reset_pending) {
			for (int i = 0; i < JELLYFIN_CACHE_SLOTS; i++) {
				stream->slots[i].state = JELLYFIN_SLOT_EMPTY;
				stream->slots[i].size = 0;
			}
			stream->next_fetch = stream->requested_start;
			stream->worker_error = 0;
			stream->worker_retries = 0;
			stream->reset_pending = 0;
			stream->worker_cancel = 0;
			jellyfin_publish_slots_locked(stream);
		}
		/* The decoder raises its cancel flag briefly while joining demux threads
		 * for an in-place seek. That flag is reusable and must never terminate the
		 * long-lived prefetch worker; only close owns worker_stop. */
		if (stream->worker_stop) {
			jellyfin_cache_unlock(stream);
			break;
		}
		JellyfinCacheSlot *slot = NULL;
		for (int i = 0; i < JELLYFIN_CACHE_SLOTS; i++)
			if (stream->slots[i].state == JELLYFIN_SLOT_EMPTY) {
				slot = &stream->slots[i];
				break;
			}
		if (stream->worker_error || !slot ||
		    stream->next_fetch >= stream->size) {
			jellyfin_cache_unlock(stream);
			sceKernelDelayThread(1000);
			continue;
		}
		uint64_t start = stream->next_fetch;
		unsigned int generation = stream->generation;
		uint64_t remaining = stream->size - start;
		uint64_t planned = remaining < JELLYFIN_CACHE_CHUNK
		                 ? remaining : JELLYFIN_CACHE_CHUNK;
		stream->next_fetch += planned;
		slot->state = JELLYFIN_SLOT_FILLING;
		slot->start = start;
		slot->size = 0;
		slot->generation = generation;
		stream->worker_cancel = 0;
		jellyfin_cache_unlock(stream);

		int result = jellyfin_stream_fetch_slot(stream, slot, start);
		jellyfin_cache_lock(stream);
		if (stream->worker_stop || generation != stream->generation) {
			slot->state = JELLYFIN_SLOT_EMPTY;
			slot->size = 0;
		} else if (result < 0) {
			slot->state = JELLYFIN_SLOT_EMPTY;
			slot->size = 0;
			if (!stream->worker_cancel) {
				stream->next_fetch = start;
				if (stream->worker_retries < 2)
					stream->worker_retries++;
				else stream->worker_error = result;
			}
		} else {
			slot->state = JELLYFIN_SLOT_READY;
			stream->worker_error = 0;
			stream->worker_retries = 0;
		}
		stream->worker_cancel = 0;
		jellyfin_publish_slots_locked(stream);
		jellyfin_cache_unlock(stream);
	}
	stream->worker_done = 1;
	return sceKernelExitThread(0);
}

static void jellyfin_request_window_locked(JellyfinStream *stream,
	                                       uint64_t start) {
	uint64_t window_start = start - start % JELLYFIN_CACHE_CHUNK;
	stream->generation++;
	stream->requested_start = window_start;
	stream->reset_pending = 1;
	stream->worker_error = 0;
	stream->worker_retries = 0;
	stream->worker_cancel = 1;
	for (int i = 0; i < JELLYFIN_CACHE_SLOTS; i++)
		if (stream->slots[i].state == JELLYFIN_SLOT_READY) {
			stream->slots[i].state = JELLYFIN_SLOT_EMPTY;
			stream->slots[i].size = 0;
		}
	jellyfin_publish_slots_locked(stream);
}

static JellyfinCacheSlot *jellyfin_ready_slot_locked(
	JellyfinStream *stream, uint64_t position) {
	for (int i = 0; i < JELLYFIN_CACHE_SLOTS; i++) {
		JellyfinCacheSlot *slot = &stream->slots[i];
		if (slot->state == JELLYFIN_SLOT_READY &&
		    position >= slot->start && position < slot->start + slot->size)
			return slot;
	}
	return NULL;
}

static int jellyfin_position_pending_locked(JellyfinStream *stream,
	                                        uint64_t position) {
	if (stream->reset_pending && position >= stream->requested_start &&
	    position < stream->requested_start + JELLYFIN_CACHE_CHUNK) return 1;
	for (int i = 0; i < JELLYFIN_CACHE_SLOTS; i++) {
		JellyfinCacheSlot *slot = &stream->slots[i];
		if (slot->state == JELLYFIN_SLOT_FILLING &&
		    position >= slot->start &&
		    position < slot->start + JELLYFIN_CACHE_CHUNK)
			return 1;
	}
	return 0;
}

static int jellyfin_stream_read(void *opaque, void *output, size_t size) {
	JellyfinStream *stream = opaque;
	if (!stream || !output) return -1;
	if (stream->position >= stream->size) return 0;
	size_t total = 0;
	while (total < size && stream->position < stream->size) {
		if ((stream->cancel && *stream->cancel) || stream->worker_stop)
			return total ? (int)total : VT_NETWORK_ERROR;
		jellyfin_cache_lock(stream);
		JellyfinCacheSlot *slot =
		    jellyfin_ready_slot_locked(stream, stream->position);
		if (!slot) {
			int error = stream->worker_error;
			if (!error &&
			    !jellyfin_position_pending_locked(stream, stream->position))
				jellyfin_request_window_locked(stream, stream->position);
			jellyfin_cache_unlock(stream);
			if (error) return total ? (int)total : error;
			sceKernelDelayThread(500);
			continue;
		}
		size_t offset = (size_t)(stream->position - slot->start);
		size_t available = slot->size - offset;
		size_t wanted = size - total;
		if (wanted > available) wanted = available;
		memcpy((unsigned char *)output + total, slot->data + offset, wanted);
		stream->position += wanted;
		total += wanted;
		/* Retain the current and immediately preceding chunk for small backward
		 * demux seeks. Older slots are recycled into future read-ahead. */
		for (int i = 0; i < JELLYFIN_CACHE_SLOTS; i++) {
			JellyfinCacheSlot *candidate = &stream->slots[i];
			if (candidate->state == JELLYFIN_SLOT_READY &&
			    candidate->start + candidate->size + JELLYFIN_CACHE_CHUNK <=
			        stream->position) {
				candidate->state = JELLYFIN_SLOT_EMPTY;
				candidate->size = 0;
			}
		}
		jellyfin_publish_slots_locked(stream);
		jellyfin_cache_unlock(stream);
	}
	return (int)total;
}

static int64_t jellyfin_stream_seek(void *opaque, int64_t offset, int origin) {
	JellyfinStream *stream = opaque;
	if (!stream) return -1;
	int64_t base = origin == SEEK_SET ? 0
	             : origin == SEEK_CUR ? (int64_t)stream->position
	             : origin == SEEK_END ? (int64_t)stream->size : -1;
	if (base < 0 || offset < -base) return -1;
	int64_t next = base + offset;
	if (next < 0 || (uint64_t)next > stream->size) return -1;
	jellyfin_cache_lock(stream);
	stream->position = (uint64_t)next;
	int requested_window = 0;
	if (stream->position < stream->size &&
	    !jellyfin_ready_slot_locked(stream, stream->position) &&
	    !jellyfin_position_pending_locked(stream, stream->position)) {
		jellyfin_request_window_locked(stream, stream->position);
		requested_window = 1;
	}
	uint64_t window_start = stream->requested_start;
	jellyfin_cache_unlock(stream);
	if (requested_window)
		log_printf("jellyfin direct seek: byte=%llu range=%llu generation=%u\n",
		           (unsigned long long)stream->position,
		           (unsigned long long)window_start, stream->generation);
	return next;
}

static void jellyfin_stream_abort(void *opaque) {
	JellyfinStream *stream = opaque;
	if (!stream) return;
	if (stream->cancel) *stream->cancel = 1;
	stream->worker_cancel = 1;
	__sync_synchronize();
}

static void jellyfin_stream_close(void *opaque) {
	JellyfinStream *stream = opaque;
	if (!stream) return;
	stream->worker_stop = 1;
	stream->worker_cancel = 1;
	__sync_synchronize();
	if (stream->worker_thid >= 0) {
		sceKernelWaitThreadEnd(stream->worker_thid, NULL, NULL);
		sceKernelDeleteThread(stream->worker_thid);
	}
	vita_https_client_destroy(stream->client);
	if (stream->telemetry_owner)
		__sync_bool_compare_and_swap(stream->telemetry_owner,
		                             (uintptr_t)stream, (uintptr_t)0);
	secure_zero(stream->access_token, sizeof(stream->access_token));
	free(stream->cache);
	free(stream);
}

static int jellyfin_subtitle_read(void *opaque, void *output, size_t size) {
	JellyfinSubtitleStream *stream = opaque;
	if (!stream || !output) return -1;
	if (stream->position >= stream->size) return 0;
	size_t available = stream->size - stream->position;
	if (size > available) size = available;
	memcpy(output, stream->data + stream->position, size);
	stream->position += size;
	return (int)size;
}

static int64_t jellyfin_subtitle_seek(void *opaque, int64_t offset, int origin) {
	JellyfinSubtitleStream *stream = opaque;
	if (!stream) return -1;
	int64_t base = origin == SEEK_SET ? 0
	             : origin == SEEK_CUR ? (int64_t)stream->position
	             : origin == SEEK_END ? (int64_t)stream->size : -1;
	if (base < 0 || offset < -base) return -1;
	int64_t next = base + offset;
	if (next < 0 || (uint64_t)next > stream->size) return -1;
	stream->position = (size_t)next;
	return next;
}

static void jellyfin_subtitle_close(void *opaque) {
	JellyfinSubtitleStream *stream = opaque;
	if (!stream) return;
	free(stream->data);
	free(stream);
}

int vt_jellyfin_open_subtitle_stream(
	const VtNetworkSource *source, const VtNetworkCredential *credential,
	const char *path, const char *media_source_id, int subtitle_stream_index,
	VtDecoderStreamHandle *out, volatile int *cancel_flag) {
	if (!source || !credential || !path || !media_source_id ||
	    !media_source_id[0] || subtitle_stream_index < 0 || !out ||
	    !credential->access_token[0])
		return VT_NETWORK_AUTH_FAILED;
	char item_id[VT_NETWORK_USER_ID_MAX];
	if (item_id_from_path(path, item_id) < 0 || !item_id[0] ||
	    !safe_header_value(media_source_id)) return -1;
	char endpoint[320];
	int endpoint_size = snprintf(
	    endpoint, sizeof(endpoint), "Videos/%s/%s/Subtitles/%d/0/Stream.srt",
	    item_id, media_source_id, subtitle_stream_index);
	if (endpoint_size < 0 || (size_t)endpoint_size >= sizeof(endpoint)) return -1;
	char url[2048];
	if (jellyfin_url(source, endpoint, "copyTimestamps=true", url,
	                 sizeof(url)) < 0) return -1;
	char authorization[512];
	if (authorization_header(credential->access_token, authorization,
	                         sizeof(authorization)) < 0) return -1;
	VitaHttpsClientConfig config;
	jellyfin_client_config(source, &config);
	config.request_timeout_ms = 12000;
	VitaHttpsClient *client = vita_https_client_create(&config);
	if (!client) return -1;
	const char *headers[] = { authorization, "Accept: text/plain", NULL };
	JellyfinBuffer response = { .limit = JELLYFIN_SUBTITLE_MAX };
	VitaHttpsRequest request = {
		.method = "GET",
		.url = url,
		.headers = headers,
		.write = buffer_write,
		.write_opaque = &response,
		.cancel_flag = cancel_flag
	};
	VitaHttpsResponse transport = {0};
	int result = vita_https_perform(client, &request, &transport);
	vita_https_client_destroy(client);
	if (transport.status_code == 401 || transport.status_code == 403)
		result = VT_NETWORK_AUTH_FAILED;
	else if (result < 0) result = transport_result(result);
	else if (transport.status_code != 200 || response.size == 0) result = -1;
	log_printf("jellyfin subtitle: index=%d status=%ld bytes=%u ret=%d\n",
	           subtitle_stream_index, transport.status_code,
	           (unsigned int)response.size, result);
	if (result < 0) {
		free(response.data);
		return result;
	}
	JellyfinSubtitleStream *stream = calloc(1, sizeof(*stream));
	if (!stream) {
		free(response.data);
		return -1;
	}
	stream->data = response.data;
	stream->size = response.size;
	memset(out, 0, sizeof(*out));
	out->opaque = stream;
	out->read = jellyfin_subtitle_read;
	out->seek = jellyfin_subtitle_seek;
	out->close = jellyfin_subtitle_close;
	out->size = (int64_t)stream->size;
	return 0;
}

int vt_jellyfin_open_stream(const VtNetworkSource *source,
	                        const VtNetworkCredential *credential,
	                        const char *path, VtDecoderStreamHandle *out,
	                        volatile int *cancel_flag,
	                        VtNetworkBufferTelemetry *telemetry,
	                        volatile uintptr_t *telemetry_owner) {
	if (!source || !credential || !path || !out ||
	    !credential->access_token[0]) return VT_NETWORK_AUTH_FAILED;
	char item_id[VT_NETWORK_USER_ID_MAX];
	if (item_id_from_path(path, item_id) < 0 || !item_id[0]) return -1;
	char endpoint[128];
	int endpoint_size = snprintf(endpoint, sizeof(endpoint),
	                             "Videos/%s/stream", item_id);
	if (endpoint_size < 0 || (size_t)endpoint_size >= sizeof(endpoint)) return -1;
	JellyfinStream *stream = calloc(1, sizeof(*stream));
	if (!stream) return -1;
	stream->worker_thid = -1;
	stream->cancel = cancel_flag;
	if (telemetry && telemetry_owner &&
	    __sync_bool_compare_and_swap(telemetry_owner, (uintptr_t)0,
	                                 (uintptr_t)stream)) {
		stream->telemetry = telemetry;
		stream->telemetry_owner = telemetry_owner;
	}
	stream->cache = malloc(JELLYFIN_RANGE_CACHE);
	if (!stream->cache || jellyfin_url(source, endpoint, "static=true",
	                                  stream->url, sizeof(stream->url)) < 0) {
		jellyfin_stream_close(stream);
		return -1;
	}
	for (int i = 0; i < JELLYFIN_CACHE_SLOTS; i++)
		stream->slots[i].data = stream->cache + i * JELLYFIN_CACHE_CHUNK;
	snprintf(stream->access_token, sizeof(stream->access_token), "%s",
	         credential->access_token);
	VitaHttpsClientConfig config;
	jellyfin_client_config(source, &config);
	config.request_timeout_ms = 12000;
	stream->client = vita_https_client_create(&config);
	if (!stream->client) {
		jellyfin_stream_close(stream);
		return -1;
	}
	char authorization[512];
	if (authorization_header(stream->access_token, authorization,
	                         sizeof(authorization)) < 0) {
		jellyfin_stream_close(stream);
		return -1;
	}
	const char *headers[] = { authorization, NULL };
	VitaHttpsRequest request = {
		.method = "HEAD",
		.url = stream->url,
		.headers = headers,
		.cancel_flag = cancel_flag
	};
	VitaHttpsResponse response = { 0 };
	int result = vita_https_perform(stream->client, &request, &response);
	if (response.status_code == 401 || response.status_code == 403)
		result = VT_NETWORK_AUTH_FAILED;
	else if (result < 0) result = transport_result(result);
	else if (response.content_length <= 0) result = VT_NETWORK_RANGE_UNSUPPORTED;
	if (result < 0) {
		jellyfin_stream_close(stream);
		return result;
	}
	stream->size = (uint64_t)response.content_length;
	stream->generation = 1;
	stream->requested_start = 0;
	stream->reset_pending = 1;
	jellyfin_publish_cache(stream, 0, 0, 0);
	stream->worker_thid = sceKernelCreateThread(
	    "VitaMediaDeckJellyfinPrefetch", jellyfin_prefetch_worker,
	    JELLYFIN_PREFETCH_THREAD_CREATE_PRIORITY,
	    JELLYFIN_PREFETCH_THREAD_STACK, 0, 0, NULL);
	if (stream->worker_thid < 0) {
		jellyfin_stream_close(stream);
		return -1;
	}
	JellyfinStream *self = stream;
	result = sceKernelStartThread(stream->worker_thid, sizeof(self), &self);
	if (result < 0) {
		sceKernelDeleteThread(stream->worker_thid);
		stream->worker_thid = -1;
		jellyfin_stream_close(stream);
		return result;
	}
	memset(out, 0, sizeof(*out));
	out->opaque = stream;
	out->read = jellyfin_stream_read;
	out->seek = jellyfin_stream_seek;
	out->abort = jellyfin_stream_abort;
	out->close = jellyfin_stream_close;
	out->size = (int64_t)stream->size;
	return 0;
}
