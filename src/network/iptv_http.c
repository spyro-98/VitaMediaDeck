#include "network/iptv_source.h"
#include <stdlib.h>
#include <string.h>
#include <vita_https.h>

typedef struct { unsigned char *data; size_t size, capacity, limit; int error; } Response;
static size_t receive(const void *bytes, size_t size, void *ctx) {
    Response *r = ctx;
    if (size > r->limit - r->size) { r->error = VT_IPTV_LIMIT; return 0; }
    if (r->size + size > r->capacity) {
        size_t capacity = r->capacity ? r->capacity : 65536;
        while (capacity < r->size + size) capacity *= 2;
        if (capacity > r->limit) capacity = r->limit;
        void *next = realloc(r->data, capacity);
        if (!next) { r->error = VT_IPTV_MEMORY; return 0; }
        r->data = next; r->capacity = capacity;
    }
    if (size) memcpy(r->data + r->size, bytes, size);
    r->size += size; return size;
}
int vt_iptv_http_fetch(void *ctx, const char *url, size_t limit, unsigned char **data,
    size_t *size, char *effective, size_t capacity, volatile int *cancel) {
    (void)ctx;
    if (!data || !size || !effective || !capacity || !limit || limit > 32u * 1024u * 1024u) return VT_IPTV_INVALID;
    *data = NULL; *size = 0; effective[0] = 0;
    char checked[VT_IPTV_URL_MAX];
    if (vt_iptv_resolve_url(url, url, checked, sizeof(checked))) return VT_IPTV_INVALID;
    if (cancel && *cancel) return VT_IPTV_CANCELLED;
    VitaHttpsClientConfig config = {.user_agent = "VitaMediaDeck/1.3.0", .allow_http = 1,
        .connect_timeout_ms = 10000, .request_timeout_ms = 20000};
    VitaHttpsClient *client = vita_https_client_create(&config);
    if (!client) return VT_IPTV_MEMORY;
    Response response = {.limit = limit};
    VitaHttpsRequest request = {.method = "GET", .url = url, .write = receive,
        .write_opaque = &response, .cancel_flag = cancel};
    VitaHttpsResponse metadata = {0};
    int result = vita_https_perform(client, &request, &metadata);
    vita_https_client_destroy(client);
    *data = response.data; *size = response.size;
    if (cancel && *cancel) return VT_IPTV_CANCELLED;
    if (response.error) return response.error;
    if (result < 0 || metadata.status_code != 200) return VT_IPTV_IO;
    return vt_iptv_resolve_url(url, metadata.effective_url, effective, capacity);
}
