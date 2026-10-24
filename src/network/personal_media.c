/* Read-only Navidrome/OpenSubsonic and Immich adapters. Paths are opaque
 * navigation tokens, never authenticated URLs; caches and logs contain no key. */
#include "network/personal_media.h"
#include <ctype.h>
#include <errno.h>
#include <jansson.h>
#include <mbedtls/md5.h>
#include <psp2/kernel/rng.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vita_https.h>
#define PAGE_SIZE 100
#define JSON_LIMIT (4U * 1024U * 1024U)
#define IMAGE_LIMIT (16U * 1024U * 1024U)
static void wipe(void *p, size_t n) {
    volatile unsigned char *s = p;
    while (n--)
        *s++ = 0;
}
static const char *str(json_t *j, const char *k) {
    const char *s = json_string_value(json_object_get(j, k));
    return s ? s : "";
}
static int copy(char *out, size_t n, const char *s) {
    int v = snprintf(out, n, "%s", s ? s : "");
    return v < 0 || (size_t)v >= n ? -1 : 0;
}
static int valid_id(const char *s) {
    if (!s || !*s || strlen(s) > 192)
        return 0;
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '-' && *s != '_')
            return 0;
    return 1;
}
static int encode(const char *s, char *out, size_t n) {
    const char *hex = "0123456789ABCDEF";
    size_t at = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        int safe = isalnum(*p) || strchr("-._~", *p);
        size_t add = safe ? 1 : 3;
        if (at + add >= n)
            return -1;
        if (safe)
            out[at++] = *p;
        else {
            out[at++] = '%';
            out[at++] = hex[*p >> 4];
            out[at++] = hex[*p & 15];
        }
    }
    out[at] = 0;
    return 0;
}
int vt_personal_base_url(const VtNetworkSource *s, char *out, size_t n) {
    if (!s || !out || !n)
        return -1;
    const char *h = s->host;
    int explicit_scheme = !strncmp(h, "https://", 8) ? 8 : !strncmp(h, "http://", 7) ? 7 : 0;
    if (!*h || (!explicit_scheme && strstr(h, "://")))
        return -1;
    const char *authority = h + explicit_scheme, *path = strchr(authority, '/');
    size_t alen = path ? (size_t)(path - authority) : strlen(authority);
    if (!alen || strpbrk(authority, "@?#\\") || strpbrk(s->root_path, "?#\\"))
        return -1;
    for (const unsigned char *p = (const unsigned char *)h; *p; p++)
        if (*p <= 32 || *p == 127)
            return -1;
    for (const unsigned char *p = (const unsigned char *)s->root_path; *p; p++)
        if (*p <= 32 || *p == 127)
            return -1;
    int port = memchr(authority, ':', alen) != NULL;
    if (*authority == '[') {
        const char *end = memchr(authority, ']', alen);
        if (!end)
            return -1;
        port = end + 1 < authority + alen && end[1] == ':';
    }
    int https = explicit_scheme != 7;
    unsigned pnum = s->port;
    char suffix[16] = "";
    if (!port && pnum && pnum != (https ? 443U : 80U))
        snprintf(suffix, sizeof(suffix), ":%u", pnum);
    int k = snprintf(out, n, "%s://%.*s%s%s%s%s", https ? "https" : "http", (int)alen, authority,
                     suffix, path ? path : "", s->root_path[0] && s->root_path[0] != '/' ? "/" : "",
                     s->root_path);
    if (k < 0 || (size_t)k >= n)
        return -1;
    size_t len = strlen(out);
    while (len && out[len - 1] == '/')
        out[--len] = 0;
    if (s->protocol == VT_NETWORK_IMMICH && (len < 4 || strcmp(out + len - 4, "/api"))) {
        if (len + 4 >= n)
            return -1;
        memcpy(out + len, "/api", 5);
    }
    return 0;
}
static int url(const VtNetworkSource *s, const char *endpoint, char *out, size_t n) {
    char base[1200];
    if (vt_personal_base_url(s, base, sizeof(base)) < 0)
        return -1;
    int k = snprintf(out, n, "%s/%s", base, endpoint);
    return k < 0 || (size_t)k >= n ? -1 : 0;
}
static int nav_url(const VtNetworkSource *s, const VtNetworkCredential *c, const char *endpoint,
                   const char *query, char *out, size_t n) {
    if (!s || !c || !s->username[0] || !c->password[0])
        return VT_NETWORK_AUTH_FAILED;
    unsigned char random[16], digest[16];
    char salt[33], token[33], user[VT_NETWORK_USER_MAX * 3], joined[VT_NETWORK_SECRET_MAX + 33];
    if (sceKernelGetRandomNumber(random, sizeof(random)) < 0)
        return -1;
    for (int i = 0; i < 16; i++)
        snprintf(salt + i * 2, 3, "%02x", random[i]);
    int k = snprintf(joined, sizeof(joined), "%s%s", c->password, salt);
    if (k < 0 || (size_t)k >= sizeof(joined) ||
        mbedtls_md5((unsigned char *)joined, (size_t)k, digest) < 0) {
        wipe(joined, sizeof(joined));
        return -1;
    }
    wipe(joined, sizeof(joined));
    for (int i = 0; i < 16; i++)
        snprintf(token + i * 2, 3, "%02x", digest[i]);
    wipe(digest, sizeof(digest));
    if (encode(s->username, user, sizeof(user)) < 0)
        return -1;
    char base[1200];
    if (vt_personal_base_url(s, base, sizeof(base)) < 0)
        return -1;
    k = snprintf(out, n, "%s/rest/%s.view?u=%s&t=%s&s=%s&v=1.16.1&c=VitaMediaDeck&f=json%s%s", base,
                 endpoint, user, token, salt, query && *query ? "&" : "", query ? query : "");
    wipe(token, sizeof(token));
    return k < 0 || (size_t)k >= n ? -1 : 0;
}
typedef struct {
    unsigned char *data;
    size_t size, cap, limit;
    long timeout_ms;
} Buffer;
static size_t write_buffer(const void *data, size_t size, void *ctx) {
    Buffer *b = ctx;
    if (size > b->limit - b->size)
        return 0;
    size_t need = b->size + size + 1;
    if (need > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 8192;
        while (cap < need)
            cap *= 2;
        if (cap > b->limit + 1)
            cap = b->limit + 1;
        void *next = realloc(b->data, cap);
        if (!next)
            return 0;
        b->data = next;
        b->cap = cap;
    }
    memcpy(b->data + b->size, data, size);
    b->size += size;
    b->data[b->size] = 0;
    return size;
}
static int request(const VtNetworkSource *s, const VtNetworkCredential *c, const char *endpoint,
                   const char *query, const char *body, Buffer *b, volatile int *cancel) {
    char target[3072], header[VT_NETWORK_SECRET_MAX + 20] = "Accept: application/json";
    int nav = s->protocol == VT_NETWORK_NAVIDROME;
    int ret = nav ? nav_url(s, c, endpoint, query, target, sizeof(target))
                  : url(s, endpoint, target, sizeof(target));
    if (ret < 0)
        return ret;
    if (!nav) {
        if (!c || !c->password[0])
            return VT_NETWORK_AUTH_FAILED;
        for (const unsigned char *p = (const unsigned char *)c->password; *p; p++)
            if (*p < 33 || *p > 126)
                return VT_NETWORK_AUTH_FAILED;
        snprintf(header, sizeof(header), "x-api-key: %s", c->password);
    }
    VitaHttpsClientConfig config = {
        .user_agent = "VitaMediaDeck/1.4.1",
        .connect_timeout_ms = 5000,
        .request_timeout_ms = b->timeout_ms ? b->timeout_ms : 12000,
        .low_speed_bytes_per_second = 256,
        .low_speed_seconds = 8,
        .allow_http = !strncmp(target, "http://", 7),
        .pinned_public_key = s->tls_public_key_sha256[0] ? s->tls_public_key_sha256 : NULL,
        .allow_untrusted_ca_with_pin = s->tls_public_key_sha256[0] != 0};
    VitaHttpsClient *client = vita_https_client_create(&config);
    if (!client)
        return -1;
    const char *headers[] = {header,
                             nav ? "Accept: application/json"
                                 : "Accept: image/jpeg, image/png, image/webp, application/json",
                             "Content-Type: application/json", NULL};
    VitaHttpsRequest req = {.url = target,
                            .method = body ? "POST" : "GET",
                            .headers = headers,
                            .body = body,
                            .body_size = body ? strlen(body) : 0,
                            .write = write_buffer,
                            .write_opaque = b,
                            .cancel_flag = cancel};
    VitaHttpsResponse resp = {0};
    ret = vita_https_perform_no_redirects(client, &req, &resp);
    vita_https_client_destroy(client);
    wipe(header, sizeof(header));
    wipe(target, sizeof(target));
    if (resp.status_code == 401 || resp.status_code == 403)
        return VT_NETWORK_AUTH_FAILED;
    if (ret == VITA_HTTPS_ERROR_UNTRUSTED_CERTIFICATE)
        return VT_NETWORK_TLS_TRUST_REQUIRED;
    if (ret == VITA_HTTPS_ERROR_PIN_MISMATCH)
        return VT_NETWORK_TLS_PIN_MISMATCH;
    return ret < 0 || resp.status_code < 200 || resp.status_code >= 300 ? -1 : 0;
}
static int api_cancel(const VtNetworkSource *s, const VtNetworkCredential *c, const char *endpoint,
                      const char *query, json_t *body, json_t **out, volatile int *cancel) {
    Buffer b = {.limit = JSON_LIMIT, .timeout_ms = 12000};
    char *payload = body ? json_dumps(body, JSON_COMPACT) : NULL;
    if (body && !payload)
        return -1;
    int ret = request(s, c, endpoint, query, payload, &b, cancel);
    free(payload);
    *out = NULL;
    if (ret == 0) {
        json_error_t error;
        json_t *j = json_loadb((char *)b.data, b.size, JSON_REJECT_DUPLICATES, &error);
        if (!j)
            ret = -1;
        else if (s->protocol == VT_NETWORK_NAVIDROME) {
            json_t *v = json_object_get(j, "subsonic-response");
            if (!json_is_object(v))
                ret = -1;
            else if (strcmp(str(v, "status"), "ok")) {
                int code =
                    (int)json_integer_value(json_object_get(json_object_get(v, "error"), "code"));
                ret = code == 40 || code == 41 || code == 42 ? VT_NETWORK_AUTH_FAILED : -1;
            } else
                *out = json_incref(v);
            json_decref(j);
        } else
            *out = j;
    }
    free(b.data);
    return ret;
}
int vt_personal_authenticate_cancel(const VtNetworkSource *s, VtNetworkCredential *c, char *detail,
                                    size_t n, volatile int *cancel) {
    if (!s || !c)
        return VT_NETWORK_ERROR;
    json_t *j = NULL;
    int ret = api_cancel(s, c, s->protocol == VT_NETWORK_NAVIDROME ? "ping" : "users/me", NULL,
                         NULL, &j, cancel);
    if (ret == 0 &&
        (!json_is_object(j) || (s->protocol == VT_NETWORK_IMMICH && !valid_id(str(j, "id")))))
        ret = -1;
    if (j)
        json_decref(j);
    if (detail && n)
        snprintf(detail, n, "%s",
                 ret == VT_NETWORK_AUTH_FAILED ? "Access denied / Accesso negato"
                 : ret < 0                     ? "Connection failed / Connessione fallita"
                                               : "");
    return ret;
}
int vt_personal_authenticate(const VtNetworkSource *s, VtNetworkCredential *c, char *detail,
                             size_t n) {
    return vt_personal_authenticate_cancel(s, c, detail, n, NULL);
}

static int entry(VtNetworkEntry *e, int n, int cap, const char *name, const char *path, int kind) {
    if (n >= cap || strlen(path) >= sizeof(e[n].path))
        return n;
    memset(&e[n], 0, sizeof(e[n]));
    size_t title_length = strlen(name);
    if (title_length >= sizeof(e[n].name)) {
        title_length = sizeof(e[n].name) - 4;
        while (title_length && ((unsigned char)name[title_length] & 0xc0) == 0x80)
            title_length--;
        memcpy(e[n].name, name, title_length);
        memcpy(e[n].name + title_length, "...", 4);
    } else
        copy(e[n].name, sizeof(e[n].name), name);
    copy(e[n].path, sizeof(e[n].path), path);
    e[n].is_directory = kind == 1 || kind == 2 ? kind : 0;
    e[n].is_audio = kind == 3;
    e[n].is_image = kind == 4;
    return n + 1;
}
static int object_entries(json_t *a, VtNetworkEntry *e, int cap, int kind, const char *prefix,
                          int offset, int limit) {
    if (!json_is_array(a))
        return -1;
    int n = 0;
    size_t i;
    json_t *v;
    json_array_foreach(a, i, v) {
        if ((int)i < offset)
            continue;
        if (limit > 0 && (int)i >= offset + limit)
            break;
        const char *id = str(v, "id");
        if (!valid_id(id))
            continue;
        if (kind == 4 &&
            (strcmp(str(v, "type"), "IMAGE") || json_is_true(json_object_get(v, "isTrashed"))))
            continue;
        char path[VT_NETWORK_PATH_MAX];
        snprintf(path, sizeof(path), "%s%s", prefix, id);
        const char *name = str(v, kind == 4 ? "originalFileName" : kind == 3 ? "title" : "name");
        if (!*name)
            name = str(v, "title");
        if (!*name)
            name = str(v, "albumName");
        if (!*name)
            name = id;
        int before = n;
        n = entry(e, n, cap, name, path, kind);
        if (n > before) {
            json_int_t duration = json_integer_value(json_object_get(v, "duration")),
                       bytes = json_integer_value(json_object_get(v, "size"));
            e[n - 1].runtime_ms =
                duration > 0 && duration < INT64_MAX / 1000 ? (uint64_t)duration * 1000ULL : 0;
            e[n - 1].size = bytes > 0 ? (uint64_t)bytes : 0;
            if (kind == 1)
                copy(e[n - 1].artist, sizeof(e[n - 1].artist), str(v, "artist"));
            if (kind == 3) {
                copy(e[n - 1].artist, sizeof(e[n - 1].artist), str(v, "artist"));
                copy(e[n - 1].album, sizeof(e[n - 1].album), str(v, "album"));
                copy(e[n - 1].audio_format, sizeof(e[n - 1].audio_format), str(v, "suffix"));
                json_int_t rate = json_integer_value(json_object_get(v, "bitRate"));
                e[n - 1].audio_bitrate_kbps = rate > 0 && rate < 100000 ? (uint32_t)rate : 0;
            }
        }
    }
    return n;
}
static int next_entry(VtNetworkEntry *e, int n, int cap, const char *path) {
    return entry(e, n, cap, "Next page / Pagina successiva", path, 2);
}
/* Strict numeric offsets and bounded tokens prevent a saved path from becoming
 * an arbitrary endpoint, query, or credential-bearing URL. */
static int number(const char *s, int *n) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno || !*s || *end || v < 0 || v > 100000000)
        return -1;
    *n = (int)v;
    return 0;
}
int vt_personal_list_cancel(const VtNetworkSource *s, const VtNetworkCredential *c,
                            const char *path, VtNetworkEntry *e, int cap, char *detail, size_t ds,
                            volatile int *cancel) {
    if (!s || !c || !e || cap < 4)
        return -1;
    if (!path)
        path = "";
    if (detail && ds)
        *detail = 0;
    const int page_size = cap - 2 < PAGE_SIZE ? cap - 2 : PAGE_SIZE;
    int nav = s->protocol == VT_NETWORK_NAVIDROME;
    if (!*path || !strcmp(path, "/")) {
        int n = 0;
        if (nav) {
            n = entry(e, n, cap, "Albums / Album", "albums:0", 1);
            n = entry(e, n, cap, "Artists / Artisti", "artists:0", 1);
            n = entry(e, n, cap, "Playlists / Playlist", "playlists:0", 1);
        } else {
            n = entry(e, n, cap, "All photos / Tutte le foto", "photos:1", 1);
            n = entry(e, n, cap, "Albums / Album", "collections:0", 1);
            n = entry(e, n, cap, "Favorites / Preferiti", "favorites:1", 1);
        }
        return n;
    }
    const char *colon = strchr(path, ':');
    if (!colon)
        return -1;
    char type[24], arg[700];
    size_t len = (size_t)(colon - path);
    if (len >= sizeof(type) || copy(arg, sizeof(arg), colon + 1) < 0)
        return -1;
    memcpy(type, path, len);
    type[len] = 0;
    json_t *j = NULL, *items = NULL, *payload = NULL;
    char ep[256] = "", query[512] = "", next[768] = "";
    int kind = 1, offset = 0, more = 0, ret = -1;
    const char *prefix = "";
    if (!strcmp(type, "search")) {
        if (!*arg || strlen(arg) > 128)
            return -1;
        if (nav) {
            char encoded[385];
            if (encode(arg, encoded, sizeof(encoded)) < 0)
                return -1;
            snprintf(query, sizeof(query), "query=%s&artistCount=8&albumCount=8&songCount=24",
                     encoded);
            ret = api_cancel(s, c, "search3", query, NULL, &j, cancel);
            if (ret == 0) {
                json_t *r = json_object_get(j, "searchResult3");
                int n = 0;
                const char *keys[] = {"artist", "album", "song"},
                           *prefixes[] = {"artist:", "album:", "song:"};
                for (int k = 0; k < 3 && n < cap; k++) {
                    json_t *a = json_object_get(r, keys[k]);
                    if (!a)
                        continue;
                    int added =
                        object_entries(a, e + n, cap - n, k == 2 ? 3 : 1, prefixes[k], 0, 0);
                    if (added < 0) {
                        ret = -1;
                        break;
                    }
                    n += added;
                }
                if (ret == 0)
                    ret = n;
            }
        } else {
            payload = json_pack("{s:s,s:s,s:i,s:i}", "type", "IMAGE", "originalFileName", arg,
                                "page", 1, "size", 100);
            if (!payload)
                return -1;
            ret = api_cancel(s, c, "search/metadata", NULL, payload, &j, cancel);
            json_decref(payload);
            if (ret == 0)
                ret = object_entries(json_object_get(json_object_get(j, "assets"), "items"), e, cap,
                                     4, "photo:", 0, 0);
        }
        if (j)
            json_decref(j);
        return ret;
    }
    if (nav) {
        if (!strcmp(type, "albums")) {
            if (number(arg, &offset) < 0)
                return -1;
            copy(ep, sizeof(ep), "getAlbumList2");
            snprintf(query, sizeof(query), "type=alphabeticalByName&size=%d&offset=%d", page_size,
                     offset);
            ret = api_cancel(s, c, ep, query, NULL, &j, cancel);
            items = json_object_get(json_object_get(j, "albumList2"), "album");
            prefix = "album:";
            more = json_array_size(items) == (size_t)page_size;
            snprintf(next, sizeof(next), "albums:%d", offset + page_size);
        } else if (!strcmp(type, "artists")) {
            if (number(arg, &offset) < 0)
                return -1; /* Paged artist query is built below. */
        } else if (!strcmp(type, "playlists")) {
            if (number(arg, &offset) < 0)
                return -1;
            ret = api_cancel(s, c, "getPlaylists", NULL, NULL, &j, cancel);
            items = json_object_get(json_object_get(j, "playlists"), "playlist");
            prefix = "playlist:";
            more = json_array_size(items) > (size_t)(offset + page_size);
            snprintf(next, sizeof(next), "playlists:%d", offset + page_size);
        } else if (!strcmp(type, "album") || !strcmp(type, "artist") || !strcmp(type, "playlist")) {
            char *slice = strchr(arg, '@');
            if (slice) {
                *slice++ = 0;
                if (number(slice, &offset) < 0)
                    return -1;
            }
            if (!valid_id(arg))
                return -1;
            snprintf(query, sizeof(query), "id=%s", arg);
            int artist = !strcmp(type, "artist"), playlist = !strcmp(type, "playlist");
            ret = api_cancel(s, c,
                             artist     ? "getArtist"
                             : playlist ? "getPlaylist"
                                        : "getAlbum",
                             query, NULL, &j, cancel);
            items = json_object_get(json_object_get(j, artist     ? "artist"
                                                       : playlist ? "playlist"
                                                                  : "album"),
                                    artist     ? "album"
                                    : playlist ? "entry"
                                               : "song");
            prefix = artist ? "album:" : "song:";
            kind = artist ? 1 : 3;
            more = json_array_size(items) > (size_t)(offset + page_size);
            snprintf(next, sizeof(next), "%s:%s@%d", type, arg, offset + page_size);
        }
    } else {
        if (!strcmp(type, "photos") || !strcmp(type, "favorites")) {
            int page = 0;
            char *cursor = strchr(arg, '@');
            if (cursor)
                *cursor++ = 0;
            if (number(arg, &page) < 0 || page < 1)
                return -1;
            payload = json_pack("{s:s,s:i,s:i,s:s}", "type", "IMAGE", "page", page, "size",
                                page_size, "order", "desc");
            if (cursor && *cursor)
                json_object_set_new(payload, "cursor", json_string(cursor));
            if (!strcmp(type, "favorites"))
                json_object_set_new(payload, "isFavorite", json_true());
            ret = api_cancel(s, c, "search/metadata", NULL, payload, &j, cancel);
            json_decref(payload);
            json_t *assets = json_object_get(j, "assets");
            items = json_object_get(assets, "items");
            const char *nc = str(assets, "nextCursor"), *np = str(assets, "nextPage");
            int v = 0;
            if (*nc && strlen(nc) < 600 && (!cursor || strcmp(cursor, nc))) {
                more = 1;
                snprintf(next, sizeof(next), "%s:%d@%s", type, page + 1, nc);
            } else if (*np && number(np, &v) == 0 && v > page) {
                more = 1;
                snprintf(next, sizeof(next), "%s:%d", type, v);
            }
            kind = 4;
            prefix = "photo:";
        } else if (!strcmp(type, "collections")) {
            if (number(arg, &offset) < 0)
                return -1;
            ret = api_cancel(s, c, "albums", NULL, NULL, &j, cancel);
            items = j;
            prefix = "collection:";
            more = json_array_size(items) > (size_t)(offset + page_size);
            snprintf(next, sizeof(next), "collections:%d", offset + page_size);
        } else if (!strcmp(type, "collection")) {
            char *split = strchr(arg, '@');
            if (split) {
                *split++ = 0;
                if (number(split, &offset) < 0)
                    return -1;
            }
            if (!valid_id(arg))
                return -1;
            snprintf(ep, sizeof(ep), "albums/%s", arg);
            ret = api_cancel(s, c, ep, NULL, NULL, &j, cancel);
            items = json_object_get(j, "assets");
            kind = 4;
            prefix = "photo:";
            more = json_array_size(items) > (size_t)(offset + page_size);
            snprintf(next, sizeof(next), "collection:%s@%d", arg, offset + page_size);
        }
    }
    if (nav && !strcmp(type, "artists")) {
        if (j) {
            json_decref(j);
            j = NULL;
        }
        snprintf(query, sizeof(query),
                 "query=&artistCount=%d&artistOffset=%d&albumCount=0&songCount=0", page_size,
                 offset);
        ret = api_cancel(s, c, "search3", query, NULL, &j, cancel);
        items = json_object_get(json_object_get(j, "searchResult3"), "artist");
        prefix = "artist:";
        more = json_array_size(items) == (size_t)page_size;
        snprintf(next, sizeof(next), "artists:%d", offset + page_size);
    }
    if (ret == 0) {
        if (nav && !items)
            ret = 0;
        else {
            int local_offset = !strcmp(type, "playlists") || !strcmp(type, "collections") ||
                                       !strcmp(type, "collection") || !strcmp(type, "album") ||
                                       !strcmp(type, "artist") || !strcmp(type, "playlist")
                                   ? offset
                                   : 0;
            ret = object_entries(items, e, cap - 1, kind, prefix, local_offset, page_size);
            if (ret >= 0 && more)
                ret = next_entry(e, ret, cap, next);
        }
    }
    if (j)
        json_decref(j);
    if (ret < 0 && detail && ds)
        snprintf(detail, ds, "%s",
                 ret == VT_NETWORK_AUTH_FAILED ? "Access denied / Accesso negato"
                                               : "Invalid response or connection failed / Risposta "
                                                 "non valida o connessione fallita");
    return ret;
}
int vt_personal_list(const VtNetworkSource *s, const VtNetworkCredential *c, const char *path,
                     VtNetworkEntry *e, int cap, char *detail, size_t ds) {
    return vt_personal_list_cancel(s, c, path, e, cap, detail, ds, NULL);
}

int vt_personal_stream_url(const VtNetworkSource *s, const VtNetworkCredential *c, const char *path,
                           char *out, size_t n) {
    if (!path || strncmp(path, "song:", 5) || !valid_id(path + 5))
        return -1;
    char q[256];
    snprintf(q, sizeof(q), "id=%s&format=raw", path + 5);
    return nav_url(s, c, "stream", q, out, n);
}
static int image(const VtNetworkSource *s, const VtNetworkCredential *c, const char *path,
                 unsigned char **data, size_t *size, volatile int *cancel, int full) {
    if (!s || !c || !data || !size)
        return VT_NETWORK_ERROR;
    *data = NULL;
    *size = 0;
    if (!path)
        return -1;
    const char *id = strchr(path, ':');
    if (!id || !valid_id(++id))
        return -1;
    char ep[300], q[256] = "";
    if (s->protocol == VT_NETWORK_NAVIDROME) {
        char cover[193];
        copy(cover, sizeof(cover), id);
        if (!strncmp(path, "song:", 5)) {
            json_t *j = NULL;
            snprintf(q, sizeof(q), "id=%s", id);
            int result = api_cancel(s, c, "getSong", q, NULL, &j, cancel);
            if (result < 0)
                return result;
            const char *art = str(json_object_get(j, "song"), "coverArt");
            if (valid_id(art))
                copy(cover, sizeof(cover), art);
            json_decref(j);
        }
        snprintf(ep, sizeof(ep), "getCoverArt");
        snprintf(q, sizeof(q), "id=%s&size=256", cover);
    } else {
        if (strncmp(path, "photo:", 6))
            return -1;
        snprintf(ep, sizeof(ep), "assets/%s/thumbnail?size=%s", id, "preview");
    }
    Buffer b = {.limit = full ? IMAGE_LIMIT : 4U * 1024U * 1024U,
                .timeout_ms = full ? 20000 : 5000};
    int ret = request(s, c, ep, q, NULL, &b, cancel);
    if (ret == 0 && b.size >= 3 && b.data[0] == 0xff && b.data[1] == 0xd8 && b.data[2] == 0xff) {
        *data = b.data;
        *size = b.size;
        return 0;
    }
    if (ret == 0 && b.size >= 8 && !memcmp(b.data, "\x89PNG\r\n\x1a\n", 8)) {
        *data = b.data;
        *size = b.size;
        return 0;
    }
    if (ret == 0 && b.size >= 12 && !memcmp(b.data, "RIFF", 4) && !memcmp(b.data + 8, "WEBP", 4)) {
        *data = b.data;
        *size = b.size;
        return 0;
    }
    free(b.data);
    return ret < 0 ? ret : -1;
}
int vt_personal_artwork(const VtNetworkSource *s, const VtNetworkCredential *c, const char *path,
                        unsigned char **data, size_t *size, volatile int *cancel) {
    return image(s, c, path, data, size, cancel, 0);
}
int vt_personal_preview(const VtNetworkSource *s, const VtNetworkCredential *c, const char *path,
                        unsigned char **data, size_t *size, volatile int *cancel) {
    return image(s, c, path, data, size, cancel, 1);
}
