#include "network/network_internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jansson.h>
#include <vita_https.h>

#define JELLYFIN_RESPONSE_MAX (2U * 1024U * 1024U)
#define JELLYFIN_RANGE_CACHE (1024U * 1024U)
#define JELLYFIN_CLIENT_VERSION "1.1"
#define JELLYFIN_DEVICE_ID "VMDK00001"

typedef struct {
	unsigned char *data;
	size_t size;
	size_t capacity;
	size_t limit;
} JellyfinBuffer;

typedef struct {
	VitaHttpsClient *client;
	char url[2048];
	char access_token[VT_NETWORK_TOKEN_MAX];
	volatile int *cancel;
	uint64_t size;
	uint64_t position;
	uint64_t cache_start;
	size_t cache_size;
	unsigned char *cache;
	VtNetworkBufferTelemetry *telemetry;
} JellyfinStream;

static void jellyfin_publish_cache(JellyfinStream *stream, uint64_t start,
	                               size_t resident) {
	if (!stream || !stream->telemetry) return;
	VtNetworkBufferTelemetry *telemetry = stream->telemetry;
	while (__sync_lock_test_and_set(&telemetry->writer_lock, 1U)) { }
	__sync_add_and_fetch(&telemetry->sequence, 1U);
	__sync_synchronize();
	telemetry->source_size = stream->size;
	telemetry->range_start = start;
	telemetry->range_end = start + resident;
	telemetry->resident_bytes = (uint32_t)resident;
	telemetry->capacity_bytes = JELLYFIN_RANGE_CACHE;
	__sync_synchronize();
	__sync_add_and_fetch(&telemetry->sequence, 1U);
	__sync_lock_release(&telemetry->writer_lock);
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

static int jellyfin_stream_fetch(JellyfinStream *stream, uint64_t start) {
	if (!stream || start >= stream->size) return 0;
	uint64_t remaining = stream->size - start;
	uint64_t wanted = remaining < JELLYFIN_RANGE_CACHE
	                ? remaining : JELLYFIN_RANGE_CACHE;
	uint64_t end = start + wanted - 1;
	char authorization[512];
	char range[96];
	if (authorization_header(stream->access_token, authorization,
	                         sizeof(authorization)) < 0) return -1;
	snprintf(range, sizeof(range), "Range: bytes=%llu-%llu",
	         (unsigned long long)start, (unsigned long long)end);
	const char *headers[] = { authorization, range, NULL };
	JellyfinBuffer output = {
		.data = stream->cache,
		.capacity = JELLYFIN_RANGE_CACHE,
		.limit = JELLYFIN_RANGE_CACHE
	};
	VitaHttpsRequest request = {
		.method = "GET",
		.url = stream->url,
		.headers = headers,
		.write = fixed_write,
		.write_opaque = &output,
		.cancel_flag = stream->cancel
	};
	VitaHttpsResponse response = { 0 };
	stream->cache_size = 0;
	jellyfin_publish_cache(stream, start, 0);
	int result = vita_https_perform(stream->client, &request, &response);
	if (response.status_code == 401 || response.status_code == 403)
		return VT_NETWORK_AUTH_FAILED;
	if (result < 0 || response.status_code != 206 ||
	    output.size != (size_t)wanted ||
	    response.content_length != (int64_t)wanted)
		return result < 0 ? transport_result(result)
		                  : VT_NETWORK_RANGE_UNSUPPORTED;
	stream->cache_start = start;
	stream->cache_size = output.size;
	jellyfin_publish_cache(stream, stream->cache_start, stream->cache_size);
	return 0;
}

static int jellyfin_stream_read(void *opaque, void *output, size_t size) {
	JellyfinStream *stream = opaque;
	if (!stream || !output) return -1;
	if (stream->position >= stream->size) return 0;
	size_t total = 0;
	while (total < size && stream->position < stream->size) {
		if (stream->position < stream->cache_start ||
		    stream->position >= stream->cache_start + stream->cache_size) {
			int ret = jellyfin_stream_fetch(stream, stream->position);
			if (ret < 0) return total ? (int)total : ret;
		}
		size_t offset = (size_t)(stream->position - stream->cache_start);
		size_t available = stream->cache_size - offset;
		size_t wanted = size - total;
		if (wanted > available) wanted = available;
		memcpy((unsigned char *)output + total, stream->cache + offset, wanted);
		stream->position += wanted;
		total += wanted;
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
	stream->position = (uint64_t)next;
	if (stream->position < stream->cache_start ||
	    stream->position >= stream->cache_start + stream->cache_size)
		jellyfin_publish_cache(stream, stream->position, 0);
	return next;
}

static void jellyfin_stream_abort(void *opaque) {
	JellyfinStream *stream = opaque;
	if (!stream || !stream->cancel) return;
	*stream->cancel = 1;
	__sync_synchronize();
}

static void jellyfin_stream_close(void *opaque) {
	JellyfinStream *stream = opaque;
	if (!stream) return;
	vita_https_client_destroy(stream->client);
	secure_zero(stream->access_token, sizeof(stream->access_token));
	free(stream->cache);
	free(stream);
}

int vt_jellyfin_open_stream(const VtNetworkSource *source,
	                        const VtNetworkCredential *credential,
	                        const char *path, VtDecoderStreamHandle *out,
	                        volatile int *cancel_flag,
	                        VtNetworkBufferTelemetry *telemetry) {
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
	stream->cancel = cancel_flag;
	stream->telemetry = telemetry;
	stream->cache = malloc(JELLYFIN_RANGE_CACHE);
	if (!stream->cache || jellyfin_url(source, endpoint, "static=true",
	                                  stream->url, sizeof(stream->url)) < 0) {
		jellyfin_stream_close(stream);
		return -1;
	}
	snprintf(stream->access_token, sizeof(stream->access_token), "%s",
	         credential->access_token);
	VitaHttpsClientConfig config;
	jellyfin_client_config(source, &config);
	config.request_timeout_ms = 8000;
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
	result = jellyfin_stream_fetch(stream, 0);
	if (result < 0) {
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
