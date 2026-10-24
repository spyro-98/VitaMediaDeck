/* Bounded metadata cache plus append-only playback checkpoints. No filesystem
 * work runs in put/started/progress/query or on the audio/render thread. */
#include "library/library.h"
#include "app_paths.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/power.h>
#define DB VITAMEDIADECK_DATA_DIR "/library.bin"
#define TMP VITAMEDIADECK_DATA_DIR "/library.tmp"
#define BAK VITAMEDIADECK_DATA_DIR "/library.bak"
#define INTERVAL 15000000ULL
typedef struct {
    char id[16];
    uint64_t position, duration, order;
    int started;
} State;
typedef struct {
    VtLibraryItem item;
    State state;
    uint64_t cached_order;
    unsigned metadata_revision, state_revision;
} Slot;
typedef struct {
    uint32_t magic, version, type, size, crc;
} Header;
_Static_assert(sizeof(State) == 48, "Stable checkpoint record");
_Static_assert(sizeof(Header) == 20, "Stable journal header");
static Slot slots[VT_LIBRARY_MAX];
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static int count, stop, urgent, repair;
static unsigned revision;
static uint64_t next_order = 1, cache_order = 1;
static SceUID worker = -1;
static VtLibrarySample sampler;
static unsigned journal_bytes;
static const char *providers[] = {"Local", "Jellyfin", "Navidrome", "Immich",
                                  "IPTV",  "Radio",    "Podcast",   "Network"};
const char *vt_library_provider_name(int p) {
    return p >= 0 && p < VT_LIBRARY_PROVIDER_COUNT ? providers[p] : "?";
}
static uint32_t crc(const void *data, size_t n) {
    uint32_t v = ~0U;
    const unsigned char *p = data;
    while (n--) {
        v ^= *p++;
        for (int i = 0; i < 8; i++)
            v = (v >> 1) ^ (0xedb88320U & (uint32_t)-(int32_t)(v & 1));
    }
    return ~v;
}
static uint64_t hash_text(uint64_t h, const char *s) {
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ULL;
    }
    return (h ^ 255) * 1099511628211ULL;
}
void vt_library_identity(VtLibraryItem *item) {
    uint64_t h = hash_text(14695981039346656037ULL, vt_library_provider_name(item->provider));
    h = hash_text(h, item->server);
    const char *locator = item->locator;
    if (item->provider == VT_LIBRARY_JELLYFIN) {
        const char *slash = strrchr(locator, '/');
        if (slash)
            locator = slash + 1;
    }
    if (item->provider == VT_LIBRARY_PODCAST && item->server[0])
        locator = item->server;
    h = hash_text(h, locator);
    snprintf(item->id, sizeof(item->id), "u%014llx", (unsigned long long)(h & 0xffffffffffffffULL));
}
static int find(const char *id) {
    for (int i = 0; i < count; i++)
        if (!strcmp(slots[i].item.id, id))
            return i;
    return -1;
}
static int valid(const VtLibraryItem *v) {
    return v->provider >= 0 && v->provider < VT_LIBRARY_PROVIDER_COUNT &&
           v->kind >= VT_LIBRARY_VIDEO && v->kind <= VT_LIBRARY_FEED &&
           memchr(v->id, 0, sizeof(v->id)) && v->id[0] &&
           memchr(v->history_id, 0, sizeof(v->history_id)) &&
           memchr(v->server_name, 0, sizeof(v->server_name)) &&
           memchr(v->server, 0, sizeof(v->server)) && memchr(v->locator, 0, sizeof(v->locator)) &&
           v->locator[0] && memchr(v->artwork_url, 0, sizeof(v->artwork_url)) &&
           memchr(v->user_agent, 0, sizeof(v->user_agent)) &&
           memchr(v->context, 0, sizeof(v->context)) && memchr(v->title, 0, sizeof(v->title)) &&
           memchr(v->artist, 0, sizeof(v->artist)) && memchr(v->album, 0, sizeof(v->album)) &&
           memchr(v->artwork, 0, sizeof(v->artwork));
}
static int insert(const VtLibraryItem *item) {
    int i = find(item->id);
    if (i < 0) {
        if (count < VT_LIBRARY_MAX)
            i = count++;
        else { /* Evict unplayed cache first; retain the most recent activity. */
            i = 0;
            for (int j = 1; j < count; j++)
                if (slots[j].state.order < slots[i].state.order ||
                    (slots[j].state.order == slots[i].state.order &&
                     slots[j].cached_order < slots[i].cached_order))
                    i = j;
        }
        memset(&slots[i], 0, sizeof(slots[i]));
        memcpy(slots[i].state.id, item->id, 16);
    }
    VtLibraryItem merged = *item;
#define RETAIN_EMPTY(field)                                                                        \
    if (!merged.field[0])                                                                          \
    memcpy(merged.field, slots[i].item.field, sizeof(merged.field))
    RETAIN_EMPTY(artwork_url);
    RETAIN_EMPTY(context);
    RETAIN_EMPTY(artwork);
    RETAIN_EMPTY(artist);
    RETAIN_EMPTY(album);
    RETAIN_EMPTY(server_name);
#undef RETAIN_EMPTY
    if (!merged.size)
        merged.size = slots[i].item.size;
    if (!merged.duration_ms)
        merged.duration_ms = slots[i].item.duration_ms;
    if (slots[i].state.started && slots[i].item.history_id[0])
        memcpy(merged.history_id, slots[i].item.history_id, sizeof(merged.history_id));
    slots[i].cached_order = cache_order++;
    if (memcmp(&slots[i].item, &merged, sizeof(merged))) {
        slots[i].item = merged;
        slots[i].metadata_revision = ++revision;
    }
    return i;
}
void vt_library_put(const VtLibraryItem *item) {
    if (!valid(item))
        return;
    pthread_mutex_lock(&mutex);
    insert(item);
    pthread_mutex_unlock(&mutex);
}
void vt_library_started(const VtLibraryItem *item) {
    if (!valid(item))
        return;
    pthread_mutex_lock(&mutex);
    int i = insert(item);
    if (strcmp(slots[i].item.history_id, item->history_id)) {
        memcpy(slots[i].item.history_id, item->history_id, sizeof(item->history_id));
        slots[i].metadata_revision = ++revision;
    }
    slots[i].state.started = 1;
    slots[i].state.order = next_order++;
    slots[i].state_revision = ++revision;
    urgent = 1;
    pthread_mutex_unlock(&mutex);
}
void vt_library_progress(const char *id, uint64_t p, uint64_t d) {
    if (!id || !*id)
        return;
    pthread_mutex_lock(&mutex);
    for (int i = 0; i < count; i++)
        if (!strcmp(slots[i].item.history_id, id) || !strcmp(slots[i].item.id, id)) {
            State *s = &slots[i].state;
            if (!s->started)
                continue;
            if (d && p > d)
                p = d;
            if (s->position != p || (d && s->duration != d)) {
                s->position = p;
                if (d)
                    s->duration = d;
                slots[i].state_revision = ++revision;
            }
        }
    pthread_mutex_unlock(&mutex);
}
static uint64_t resumable(const Slot *s) {
    uint64_t p = s->state.position, d = s->state.duration ? s->state.duration : s->item.duration_ms;
    if (s->item.kind == VT_LIBRARY_LIVE || s->item.kind == VT_LIBRARY_IMAGE ||
        s->item.kind >= VT_LIBRARY_FOLDER)
        return 0;
    if (d && p >= d)
        return 0;
    return p >= 10000 && (!d || d <= 15000 || p < d - 15000) ? p : 0;
}
uint64_t vt_library_resume(const VtLibraryItem *item) {
    pthread_mutex_lock(&mutex);
    int i = find(item->id);
    uint64_t p = i >= 0 ? resumable(&slots[i]) : 0;
    pthread_mutex_unlock(&mutex);
    return p;
}
/* All query terms may match different metadata fields. ASCII folds preserve
 * complete UTF-8 byte sequences; non-Latin text remains searchable exactly. */
static int contains(const char *s, const char *term, size_t n) {
    for (; *s; s++) {
        size_t j = 0;
        for (; j < n && s[j]; j++) {
            unsigned a = (unsigned char)s[j], b = (unsigned char)term[j];
            if (a >= 'A' && a <= 'Z')
                a += 32;
            if (b >= 'A' && b <= 'Z')
                b += 32;
            if (a != b)
                break;
        }
        if (j == n)
            return 1;
    }
    return 0;
}
int vt_library_matches(const VtLibraryItem *v, const char *q) {
    if (!q)
        return 1;
    while (*q) {
        while (*q == ' ')
            q++;
        const char *end = strchr(q, ' ');
        size_t n = end ? (size_t)(end - q) : strlen(q);
        if (n && !contains(v->title, q, n) && !contains(v->artist, q, n) &&
            !contains(v->album, q, n))
            return 0;
        q += n;
    }
    return 1;
}
int vt_library_query(const char *q, int view, VtLibraryResult *out, int cap) {
    int indices[VT_LIBRARY_MAX], n = 0;
    if (!out || cap <= 0)
        return 0;
    pthread_mutex_lock(&mutex);
    for (int i = 0; i < count; i++)
        if (vt_library_matches(&slots[i].item, q) &&
            (view != VT_LIBRARY_CONTINUE || resumable(&slots[i])) &&
            (view != VT_LIBRARY_RECENT || slots[i].state.started))
            indices[n++] = i;
    for (int i = 1; i < n; i++) {
        int idx = indices[i], j = i;
        while (j > 0) {
            Slot *a = &slots[indices[j - 1]], *b = &slots[idx];
            int move = view == VT_LIBRARY_ALL ? (a->item.provider > b->item.provider ||
                                                 (a->item.provider == b->item.provider &&
                                                  strcmp(a->item.title, b->item.title) > 0))
                                              : a->state.order < b->state.order;
            if (!move)
                break;
            indices[j] = indices[j - 1];
            j--;
        }
        indices[j] = idx;
    }
    if (n > cap)
        n = cap;
    for (int i = 0; i < n; i++) {
        Slot *s = &slots[indices[i]];
        out[i] = (VtLibraryResult){.item = s->item,
                                   .position_ms = s->state.position,
                                   .duration_ms =
                                       s->state.duration ? s->state.duration : s->item.duration_ms,
                                   .order = s->state.order,
                                   .started = s->state.started,
                                   .cached = 1};
    }
    pthread_mutex_unlock(&mutex);
    return n;
}
void vt_library_checkpoint(void) {
    pthread_mutex_lock(&mutex);
    urgent = 1;
    pthread_mutex_unlock(&mutex);
}
unsigned vt_library_revision(void) {
    pthread_mutex_lock(&mutex);
    unsigned r = revision;
    pthread_mutex_unlock(&mutex);
    return r;
}
static int write_all(int fd, const void *data, size_t size) {
    const char *p = data;
    while (size) {
        int n = sceIoWrite(fd, p, size);
        if (n <= 0)
            return -1;
        p += n;
        size -= (size_t)n;
    }
    return 0;
}
static int frame(int fd, int type, const void *p, size_t size) {
    Header h = {0x564d444c, 1, (unsigned)type, (unsigned)size, crc(p, size)};
    return write_all(fd, &h, sizeof(h)) || write_all(fd, p, size) ? -1 : 0;
}
static int load(void) {
    SceIoStat stat;
    if (sceIoGetstat(DB, &stat) < 0)
        sceIoRename(BAK, DB);
    int fd = sceIoOpen(DB, SCE_O_RDONLY, 0);
    if (fd < 0)
        return 0;
    if (sceIoGetstat(DB, &stat) < 0 || stat.st_size > 32 * 1024 * 1024) {
        sceIoClose(fd);
        return -1;
    }
    int result = 0;
    Header h = {0};
    for (;;) {
        int n = sceIoRead(fd, &h, sizeof(h));
        if (!n)
            break;
        union {
            VtLibraryItem item;
            State state;
        } payload;
        size_t size = h.type == 1 ? sizeof(VtLibraryItem) : sizeof(State);
        if (n != sizeof(h) || h.magic != 0x564d444c || h.version != 1 ||
            (h.type != 1 && h.type != 2) || h.size != size ||
            sceIoRead(fd, &payload, size) != (int)size || crc(&payload, size) != h.crc) {
            result = -1;
            break;
        }
        journal_bytes += (unsigned)(sizeof(h) + size);
        if (h.type == 1) {
            if (!valid(&payload.item)) {
                result = -1;
                break;
            }
            insert(&payload.item);
        } else if (memchr(payload.state.id, 0, 16)) {
            int i = find(payload.state.id);
            if (i >= 0) {
                slots[i].state = payload.state;
                if (next_order <= payload.state.order)
                    next_order = payload.state.order + 1;
            }
        }
    }
    sceIoClose(fd);
    for (int i = 0; i < count; i++)
        slots[i].metadata_revision = slots[i].state_revision = 0;
    return result;
}
/* Storage copies one slot under the mutex, then releases it before any I/O.
 * Revisions acknowledge only that exact copy, retaining edits made mid-write. */
static int flush(int compact) {
    int fd = sceIoOpen(compact ? TMP : DB,
                       SCE_O_WRONLY | SCE_O_CREAT | (compact ? SCE_O_TRUNC : SCE_O_APPEND), 0777);
    if (fd < 0)
        return -1;
    int ret = 0;
    unsigned bytes = compact ? 0 : journal_bytes;
    for (int i = 0; i < VT_LIBRARY_MAX; i++) {
        pthread_mutex_lock(&mutex);
        if (i >= count) {
            pthread_mutex_unlock(&mutex);
            break;
        }
        Slot s = slots[i];
        pthread_mutex_unlock(&mutex);
        int meta = compact || s.metadata_revision, state = compact || s.state_revision;
        if (meta) {
            ret = frame(fd, 1, &s.item, sizeof(s.item));
            bytes += sizeof(Header) + sizeof(s.item);
        }
        if (!ret && state) {
            ret = frame(fd, 2, &s.state, sizeof(s.state));
            bytes += sizeof(Header) + sizeof(s.state);
        }
        if (ret)
            break;
        /* Acknowledge after sync below, never before a durable append. */
    }
    if (!ret)
        ret = sceIoSyncByFd(fd, 0);
    int closed = sceIoClose(fd);
    if (!ret && closed < 0)
        ret = closed;
    if (!ret && compact) {
        sceIoRemove(BAK);
        int previous = sceIoRename(DB, BAK) >= 0;
        ret = sceIoRename(TMP, DB);
        if (ret < 0 && previous)
            sceIoRename(BAK, DB);
        if (!ret)
            sceIoRemove(BAK);
    }
    if (ret < 0)
        return -1;
    journal_bytes = bytes;
    return 0;
}
static int power_event(int a, int flags, int c, void *ctx) {
    (void)a;
    (void)flags;
    (void)c;
    (void)ctx;
    vt_library_checkpoint();
    return 0;
}
static int saver(SceSize args, void *arg) {
    (void)args;
    (void)arg;
    int callback = sceKernelCreateCallback("LibrarySuspend", 0, power_event, NULL), power = -1;
    if (callback >= 0)
        power = scePowerRegisterCallback(callback);
    uint64_t last = 0;
    unsigned revisions[VT_LIBRARY_MAX * 2] = {0};
    for (;;) {
        char id[64] = {0};
        uint64_t p = 0, d = 0;
        if (sampler && sampler(id, &p, &d))
            vt_library_progress(id, p, d);
        uint64_t now = sceKernelGetProcessTimeWide();
        pthread_mutex_lock(&mutex);
        int done = stop, force = urgent;
        urgent = 0;
        int dirty = 0;
        for (int i = 0; i < count; i++) {
            revisions[i * 2] = slots[i].metadata_revision;
            revisions[i * 2 + 1] = slots[i].state_revision;
            dirty |= slots[i].metadata_revision || slots[i].state_revision;
        }
        pthread_mutex_unlock(&mutex);
        if (dirty && (done || force || now - last >= INTERVAL)) {
            int compact = repair || journal_bytes > 4U * VT_LIBRARY_MAX *
                                                        (sizeof(Header) * 2 +
                                                         sizeof(VtLibraryItem) + sizeof(State));
            if (!flush(compact)) {
                pthread_mutex_lock(&mutex);
                for (int i = 0; i < count; i++) {
                    if (slots[i].metadata_revision == revisions[i * 2])
                        slots[i].metadata_revision = 0;
                    if (slots[i].state_revision == revisions[i * 2 + 1])
                        slots[i].state_revision = 0;
                }
                pthread_mutex_unlock(&mutex);
                repair = 0;
            } else
                repair = 1;
            last = now;
        }
        if (done)
            break;
        sceKernelDelayThreadCB(1000000);
    }
    if (power >= 0)
        scePowerUnregisterCallback(callback);
    if (callback >= 0)
        sceKernelDeleteCallback(callback);
    return 0;
}
int vt_library_init(VtLibrarySample sample) {
    memset(slots, 0, sizeof(slots));
    count = 0;
    next_order = cache_order = 1;
    revision = 0;
    stop = urgent = 0;
    sampler = sample;
    journal_bytes = 0;
    sceIoMkdir(VITAMEDIADECK_DATA_DIR, 0777);
    repair = load() < 0;
    worker = sceKernelCreateThread("LibraryCheckpoint", saver, 0x10000140, 0x10000, 0, 0, NULL);
    if (worker < 0)
        return -1;
    if (sceKernelStartThread(worker, 0, NULL) < 0) {
        sceKernelDeleteThread(worker);
        worker = -1;
        return -1;
    }
    return 0;
}
void vt_library_shutdown(void) {
    if (worker < 0)
        return;
    pthread_mutex_lock(&mutex);
    stop = 1;
    pthread_mutex_unlock(&mutex);
    sceKernelWaitThreadEnd(worker, NULL, NULL);
    sceKernelDeleteThread(worker);
    worker = -1;
}

void vt_library_import(const VtLibraryItem *v, uint64_t p, uint64_t d, uint64_t order) {
    if (!order || !valid(v))
        return;
    pthread_mutex_lock(&mutex);
    int i = insert(v);
    if (!slots[i].state.started) {
        slots[i].state = (State){.position = p, .duration = d, .order = order, .started = 1};
        memcpy(slots[i].state.id, v->id, 16);
        slots[i].state_revision = ++revision;
        if (next_order <= order)
            next_order = order + 1;
    }
    pthread_mutex_unlock(&mutex);
}
uint64_t vt_library_resume_fallback(const VtLibraryItem *v, uint64_t legacy) {
    pthread_mutex_lock(&mutex);
    int i = find(v->id);
    uint64_t p = i >= 0 && slots[i].state.started ? resumable(&slots[i]) : legacy;
    pthread_mutex_unlock(&mutex);
    return p;
}
void vt_library_start_id(const char *id) {
    if (!id)
        return;
    pthread_mutex_lock(&mutex);
    int i = -1;
    for (int k = 0; k < count; k++)
        if (!strcmp(slots[k].item.history_id, id) || !strcmp(slots[k].item.id, id)) {
            i = k;
            break;
        }
    if (i >= 0) {
        slots[i].state.started = 1;
        slots[i].state.order = next_order++;
        slots[i].state_revision = ++revision;
        urgent = 1;
    }
    pthread_mutex_unlock(&mutex);
}

void vt_library_bind(const VtLibraryItem *v) {
    if (!valid(v))
        return;
    pthread_mutex_lock(&mutex);
    int i = insert(v);
    if (strcmp(slots[i].item.history_id, v->history_id)) {
        memcpy(slots[i].item.history_id, v->history_id, sizeof(v->history_id));
        slots[i].metadata_revision = ++revision;
    }
    pthread_mutex_unlock(&mutex);
}
