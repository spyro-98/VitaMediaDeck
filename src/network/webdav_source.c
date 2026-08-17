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
	int written;
	if (has_scheme)
		written = snprintf(out, out_size, "%s", host);
	else if (source->port && source->port != 443)
		written = snprintf(out, out_size, "https://%s:%u", host, source->port);
	else written = snprintf(out, out_size, "https://%s", host);
	if (written < 0 || (size_t)written >= out_size) return -1;
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

static int parse_propfind(const unsigned char *data, size_t size,
	                      const char *current_path, VtNetworkEntry *entries,
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
	int count = 0;
	for (xmlNode *response = root->children;
	     response && count < capacity; response = response->next) {
		if (response->type != XML_ELEMENT_NODE ||
		    xmlStrcmp(response->name, BAD_CAST "response")) continue;
		xmlNode *href_node = descendant_named(response, "href");
		if (!href_node) continue;
		xmlChar *href_value = xmlNodeGetContent(href_node);
		if (!href_value) continue;
		size_t decoded_length = 0;
		char *decoded = vita_https_unescape((const char *)href_value,
		                                    &decoded_length);
		xmlFree(href_value);
		if (!decoded || decoded_length == 0) { vita_https_free(decoded); continue; }
		while (decoded_length > 1 && decoded[decoded_length - 1] == '/')
			decoded[--decoded_length] = '\0';
		char *name = strrchr(decoded, '/');
		name = name ? name + 1 : decoded;
		if (!name[0]) { vita_https_free(decoded); continue; }
		xmlNode *resource_type = descendant_named(response, "resourcetype");
		int is_dir = child_named(resource_type, "collection") != NULL;
		int is_audio = 0;
		if (!is_dir && !vt_network_is_supported_media(name, &is_audio)) {
			vita_https_free(decoded);
			continue;
		}
		if (!is_dir && is_audio) {
			vita_https_free(decoded);
			continue;
		}
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
		entry->is_audio = !is_dir && is_audio;
		entry->is_video = !is_dir && !is_audio;
		vita_https_free(decoded);
	}
	xmlFreeDoc(document);
	return count;
}

int vt_webdav_list(const VtNetworkSource *source,
	               const VtNetworkCredential *credential,
	               const char *path, VtNetworkEntry *entries, int capacity,
	               char *detail, size_t detail_size) {
	char url[2048];
	if (webdav_url(source, path, url, sizeof(url)) < 0) return -1;
	VitaHttpsClientConfig config = {
		.user_agent = "VitaTube/1.1",
		.username = source->username,
		.password = credential ? credential->password : ""
	};
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
		ret = parse_propfind(response.data, response.size, path,
		                     entries, capacity);
	else if (detail && detail_size)
		snprintf(detail, detail_size, "WebDAV HTTP %ld: %s",
		         transport.status_code, vita_https_error_string(result));
	free(response.data);
	vita_https_client_destroy(client);
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
	                      const char *path, VtDecoderStreamHandle *out) {
	if (!source || !path || !out) return -1;
	WebDavStream *stream = calloc(1, sizeof(*stream));
	if (!stream) return -1;
	char url[2048];
	if (webdav_url(source, path, url, sizeof(url)) < 0) {
		webdav_stream_close(stream);
		return -1;
	}
	VitaHttpsClientConfig config = {
		.user_agent = "VitaTube/1.1",
		.username = source->username,
		.password = credential ? credential->password : ""
	};
	stream->client = vita_https_client_create(&config);
	if (!stream->client) {
		webdav_stream_close(stream);
		return -1;
	}
	int result = vita_https_open_range_stream(stream->client, url, NULL,
	                                          &stream->stream);
	if (result < 0) {
		webdav_stream_close(stream);
		return result == VITA_HTTPS_ERROR_RANGE_UNSUPPORTED
		     ? VT_NETWORK_RANGE_UNSUPPORTED : -1;
	}
	memset(out, 0, sizeof(*out));
	out->opaque = stream;
	out->read = webdav_stream_read;
	out->seek = webdav_stream_seek;
	out->close = webdav_stream_close;
	out->size = stream->stream.size;
	return 0;
}
