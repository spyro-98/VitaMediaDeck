#include "network/network_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <vita_https.h>

typedef struct {
	unsigned char *data;
	size_t size;
	size_t capacity;
} MemoryBuffer;

typedef struct {
	VitaHttpsClient *client;
	VitaHttpsStream stream;
	volatile int *cancel;
} WebDavStream;

static size_t memory_write(const void *contents, size_t bytes, void *opaque) {
	MemoryBuffer *buffer = opaque;
	if (!bytes) return 0;
	if (buffer->size + bytes + 1 > buffer->capacity) {
		size_t capacity = buffer->capacity ? buffer->capacity * 2 : 8192;
		while (capacity < buffer->size + bytes + 1) capacity *= 2;
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

static int append_encoded_path(char *out, size_t out_size, const char *path) {
	size_t used = strlen(out);
	const char *cursor = path ? path : "";
	while (*cursor == '/') cursor++;
	while (*cursor) {
		const char *slash = strchr(cursor, '/');
		size_t length = slash ? (size_t)(slash - cursor) : strlen(cursor);
		char *escaped = vita_https_escape(cursor, length);
		if (!escaped) return -1;
		int written = snprintf(out + used, out_size - used, "/%s", escaped);
		vita_https_free(escaped);
		if (written < 0 || (size_t)written >= out_size - used) return -1;
		used += (size_t)written;
		if (!slash) break;
		cursor = slash + 1;
	}
	return 0;
}

static int webdav_url(const VtNetworkSource *source, const char *path,
	                  char *out, size_t out_size) {
	if (!source || !source->username[0]) return -1;
	const char *host = source->host;
	/* Credentials must never be sent over clear-text HTTP. A host without a
	 * scheme is deliberately upgraded to HTTPS; an explicit HTTP URL is
	 * rejected rather than silently rewritten to a potentially different
	 * endpoint. */
	if (!strncmp(host, "http://", 7)) return -1;
	int has_scheme = !strncmp(host, "https://", 8);
	const char *authority = has_scheme ? host + 8 : host;
	if (!authority[0] || strchr(authority, '?') || strchr(authority, '#')) return -1;
	const char *base_path = strchr(authority, '/');
	size_t authority_length = base_path
	                        ? (size_t)(base_path - authority) : strlen(authority);
	if (!authority_length) return -1;
	/* The editor keeps the port in its own field. When a user pastes
	 * https://host instead of a bare host, keep honoring that port rather than
	 * silently falling back to 443. An explicit port in the pasted authority
	 * remains authoritative. */
	int explicit_port = 0;
	if (authority[0] == '[') {
		const char *closing = memchr(authority, ']', authority_length);
		explicit_port = closing && closing + 1 < authority + authority_length &&
		                closing[1] == ':';
	} else explicit_port = memchr(authority, ':', authority_length) != NULL;
	int written = snprintf(out, out_size, "https://%.*s",
	                       (int)authority_length, authority);
	if (written < 0 || (size_t)written >= out_size) return -1;
	if (!explicit_port && source->port && source->port != 443) {
		size_t used = strlen(out);
		written = snprintf(out + used, out_size - used, ":%u", source->port);
		if (written < 0 || (size_t)written >= out_size - used) return -1;
	}
	if (base_path && base_path[0]) {
		size_t used = strlen(out);
		written = snprintf(out + used, out_size - used, "%s", base_path);
		if (written < 0 || (size_t)written >= out_size - used) return -1;
	}
	size_t length = strlen(out);
	while (length > 0 && out[length - 1] == '/') out[--length] = '\0';
	if (append_encoded_path(out, out_size, source->root_path) < 0) return -1;
	if (append_encoded_path(out, out_size, path) < 0) return -1;
	return 0;
}

static xmlNode *child_named(xmlNode *parent, const char *name) {
	for (xmlNode *node = parent ? parent->children : NULL; node; node = node->next)
		if (node->type == XML_ELEMENT_NODE && !xmlStrcmp(node->name, BAD_CAST name))
			return node;
	return NULL;
}

static xmlNode *descendant_named(xmlNode *parent, const char *name) {
	if (!parent) return NULL;
	for (xmlNode *node = parent->children; node; node = node->next) {
		if (node->type == XML_ELEMENT_NODE && !xmlStrcmp(node->name, BAD_CAST name))
			return node;
		xmlNode *found = descendant_named(node, name);
		if (found) return found;
	}
	return NULL;
}

static char *decoded_url_path(const char *value) {
	if (!value) return NULL;
	const char *path = value;
	const char *scheme = strstr(value, "://");
	if (scheme) {
		path = strchr(scheme + 3, '/');
		if (!path) path = "/";
	}
	size_t encoded_length = strcspn(path, "?#");
	char encoded[2048];
	if (encoded_length >= sizeof(encoded)) return NULL;
	memcpy(encoded, path, encoded_length);
	encoded[encoded_length] = '\0';
	size_t decoded_length = 0;
	char *decoded = vita_https_unescape(encoded, &decoded_length);
	if (!decoded) return NULL;
	while (decoded_length > 1 && decoded[decoded_length - 1] == '/')
		decoded[--decoded_length] = '\0';
	return decoded;
}

static int parse_propfind(const unsigned char *data, size_t size,
	                      const char *request_url, const char *current_path,
	                      VtNetworkEntry *entries,
	                      int capacity) {
	xmlDoc *document = xmlReadMemory((const char *)data, (int)size,
	                                 "webdav.xml", NULL,
	                                 XML_PARSE_NONET | XML_PARSE_NOERROR |
	                                 XML_PARSE_NOWARNING);
	if (!document) return -1;
	xmlNode *root = xmlDocGetRootElement(document);
	if (!root) {
		xmlFreeDoc(document);
		return -1;
	}
	char *requested_path = decoded_url_path(request_url);
	int count = 0;
	for (xmlNode *response = root->children;
	     response && count < capacity; response = response->next) {
		if (response->type != XML_ELEMENT_NODE ||
		    xmlStrcmp(response->name, BAD_CAST "response")) continue;
		xmlNode *href_node = descendant_named(response, "href");
		if (!href_node) continue;
		xmlChar *href_value = xmlNodeGetContent(href_node);
		if (!href_value) continue;
		char *decoded = decoded_url_path((const char *)href_value);
		xmlFree(href_value);
		if (!decoded || !decoded[0]) { vita_https_free(decoded); continue; }
		/* A Depth: 1 response normally starts with the requested collection.
		 * Treating that self response as a child created a fake recursive folder
		 * such as movies/movies on standards-compliant WebDAV servers. */
		if (requested_path && strcmp(decoded, requested_path) == 0) {
			vita_https_free(decoded);
			continue;
		}
		char *name = strrchr(decoded, '/');
		name = name ? name + 1 : decoded;
		if (!name[0]) { vita_https_free(decoded); continue; }
		xmlNode *resource_type = descendant_named(response, "resourcetype");
		int is_dir = child_named(resource_type, "collection") != NULL;
		int is_audio = 0;
		int supported = !is_dir && vt_network_is_supported_media(name, &is_audio);
		VtNetworkEntry *entry = &entries[count++];
		memset(entry, 0, sizeof(*entry));
		snprintf(entry->name, sizeof(entry->name), "%s", name);
		if (current_path && current_path[0])
			snprintf(entry->path, sizeof(entry->path), "%s/%s", current_path, name);
		else snprintf(entry->path, sizeof(entry->path), "%s", name);
		xmlNode *length_node = descendant_named(response, "getcontentlength");
		if (length_node) {
			xmlChar *value = xmlNodeGetContent(length_node);
			if (value) { entry->size = strtoull((const char *)value, NULL, 10); xmlFree(value); }
		}
		entry->is_directory = is_dir;
		entry->is_audio = supported && is_audio;
		entry->is_video = supported && !is_audio;
		vita_https_free(decoded);
	}
	vita_https_free(requested_path);
	xmlFreeDoc(document);
	return count;
}

static void webdav_client_config(const VtNetworkSource *source,
	                             const VtNetworkCredential *credential,
	                             VitaHttpsClientConfig *config) {
	memset(config, 0, sizeof(*config));
	config->user_agent = "VitaMediaDeck/1.1";
	config->username = source->username;
	config->password = credential ? credential->password : "";
	if (source->tls_public_key_sha256[0]) {
		config->pinned_public_key = source->tls_public_key_sha256;
		config->allow_untrusted_ca_with_pin = 1;
	}
}

static int webdav_transport_result(int result) {
	if (result == VITA_HTTPS_ERROR_UNTRUSTED_CERTIFICATE)
		return VT_NETWORK_TLS_TRUST_REQUIRED;
	if (result == VITA_HTTPS_ERROR_PIN_MISMATCH)
		return VT_NETWORK_TLS_PIN_MISMATCH;
	return -1;
}

int vt_webdav_probe_public_key(const VtNetworkSource *source,
	                           const VtNetworkCredential *credential,
	                           char *pin, size_t pin_size,
	                           char *detail, size_t detail_size) {
	if (!source || !pin || !pin_size) return -1;
	char url[2048];
	if (webdav_url(source, "", url, sizeof(url)) < 0) return -1;
	VitaHttpsClientConfig config;
	webdav_client_config(source, credential, &config);
	/* The probe must observe the current certificate rather than an old pin. */
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

int vt_webdav_list(const VtNetworkSource *source,
	               const VtNetworkCredential *credential,
	               const char *path, VtNetworkEntry *entries, int capacity,
	               char *detail, size_t detail_size) {
	char url[2048];
	if (webdav_url(source, path, url, sizeof(url)) < 0) return -1;
	VitaHttpsClientConfig config;
	webdav_client_config(source, credential, &config);
	VitaHttpsClient *client = vita_https_client_create(&config);
	if (!client) return -1;
	MemoryBuffer response = { 0 };
	const char *headers[] = {
		"Depth: 1",
		"Content-Type: application/xml; charset=utf-8",
		NULL
	};
	static const char body[] =
		"<?xml version=\"1.0\"?><d:propfind xmlns:d=\"DAV:\"><d:prop>"
		"<d:resourcetype/><d:getcontentlength/><d:displayname/>"
		"</d:prop></d:propfind>";
	VitaHttpsRequest request = {
		.method = "PROPFIND",
		.url = url,
		.headers = headers,
		.body = body,
		.body_size = strlen(body),
		.write = memory_write,
		.write_opaque = &response
	};
	VitaHttpsResponse transport = {0};
	int result = vita_https_perform(client, &request, &transport);
	int ret = -1;
	if (result == 0 && transport.status_code == 207)
		ret = parse_propfind(response.data, response.size, url, path,
		                     entries, capacity);
	else if (detail && detail_size) {
		if (transport.status_code == 401 || transport.status_code == 403)
			snprintf(detail, detail_size, "WebDAV HTTP %ld: authentication failed",
			         transport.status_code);
		else
			snprintf(detail, detail_size, "WebDAV HTTP %ld: %s",
			         transport.status_code, vita_https_error_string(result));
	}
	free(response.data);
	vita_https_client_destroy(client);
	if (transport.status_code == 401 || transport.status_code == 403)
		return VT_NETWORK_AUTH_FAILED;
	if (result < 0) return webdav_transport_result(result);
	return ret;
}

static int webdav_stream_read(void *opaque, void *output, size_t size) {
	WebDavStream *stream = opaque;
	return stream && stream->stream.read
	     ? stream->stream.read(stream->stream.opaque, output, size) : -1;
}

static int64_t webdav_stream_seek(void *opaque, int64_t offset, int whence) {
	WebDavStream *stream = opaque;
	return stream && stream->stream.seek
	     ? stream->stream.seek(stream->stream.opaque, offset, whence) : -1;
}

static void webdav_stream_abort(void *opaque) {
	WebDavStream *stream = opaque;
	if (!stream || !stream->cancel) return;
	*stream->cancel = 1;
	__sync_synchronize();
}

static void webdav_stream_close(void *opaque) {
	WebDavStream *stream = opaque;
	if (!stream) return;
	if (stream->stream.close) stream->stream.close(stream->stream.opaque);
	vita_https_client_destroy(stream->client);
	memset(stream, 0, sizeof(*stream));
	free(stream);
}

int vt_webdav_open_stream(const VtNetworkSource *source,
	                      const VtNetworkCredential *credential,
	                      const char *path, VtDecoderStreamHandle *out,
	                      volatile int *cancel_flag) {
	if (!source || !path || !out) return -1;
	WebDavStream *stream = calloc(1, sizeof(*stream));
	if (!stream) return -1;
	char url[2048];
	if (webdav_url(source, path, url, sizeof(url)) < 0) {
		webdav_stream_close(stream);
		return -1;
	}
	VitaHttpsClientConfig config;
	webdav_client_config(source, credential, &config);
	/* Media cursors must fail responsively; directory listings retain the
	 * library defaults because a large PROPFIND body can legitimately take
	 * longer than a small HEAD/range request. */
	config.connect_timeout_ms = 5000;
	config.request_timeout_ms = 8000;
	config.low_speed_bytes_per_second = 1024;
	config.low_speed_seconds = 5;
	stream->client = vita_https_client_create(&config);
	if (!stream->client) {
		webdav_stream_close(stream);
		return -1;
	}
	stream->cancel = cancel_flag;
	int result = vita_https_open_range_stream(stream->client, url, cancel_flag,
	                                          &stream->stream);
	if (result < 0) {
		webdav_stream_close(stream);
		return result == VITA_HTTPS_ERROR_RANGE_UNSUPPORTED
		     ? VT_NETWORK_RANGE_UNSUPPORTED : webdav_transport_result(result);
	}
	memset(out, 0, sizeof(*out));
	out->opaque = stream;
	out->read = webdav_stream_read;
	out->seek = webdav_stream_seek;
	out->abort = webdav_stream_abort;
	out->close = webdav_stream_close;
	out->size = stream->stream.size;
	return 0;
}
