#include "library/search.h"
#include "library/catalog.h"
#include "history/playback_history.h"
#include "settings/preferences.h"
#include "ui/personal_media_screen.h"
#include "network/personal_media.h"
#include "network/network_internal.h"
#include <vita_https.h>
#include "network/hls_source.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#define WORKERS 3
#define JOB_MAX (VT_NETWORK_MAX_SOURCES + 5)
typedef struct {
    unsigned generation;
    int type, provider;
    char query[129];
    VtLibraryItem folder;
    VtNetworkSource source;
    VtNetworkCredential credential;
    volatile int *cancel;
    int local_matches;
} Job;
static pthread_mutex_t search_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t search_changed = PTHREAD_COND_INITIALIZER;
static struct {
    char key[16];
    VtNetworkCredential credential;
} sessions[VT_NETWORK_MAX_SOURCES];
int vt_library_search_get_session(const VtNetworkSource *s, VtNetworkCredential *c) {
    char key[16];
    vt_network_media_history_id(s, "", key);
    int found = 0;
    pthread_mutex_lock(&search_mutex);
    for (int i = 0; i < VT_NETWORK_MAX_SOURCES; i++)
        if (!strcmp(key, sessions[i].key)) {
            if (!c->password[0] || !strcmp(c->password, sessions[i].credential.password))
                *c = sessions[i].credential;
            found = 1;
            break;
        }
    pthread_mutex_unlock(&search_mutex);
    return found;
}
void vt_library_search_set_session(const VtNetworkSource *s, const VtNetworkCredential *c) {
    char key[16];
    vt_network_media_history_id(s, "", key);
    pthread_mutex_lock(&search_mutex);
    int chosen = -1;
    for (int i = 0; i < VT_NETWORK_MAX_SOURCES; i++)
        if (!strcmp(key, sessions[i].key)) {
            chosen = i;
            break;
        } else if (chosen < 0 && !sessions[i].key[0])
            chosen = i;
    if (chosen >= 0) {
        snprintf(sessions[chosen].key, 16, "%s", key);
        sessions[chosen].credential = *c;
    }
    pthread_mutex_unlock(&search_mutex);
}
static Job jobs[JOB_MAX];
static int job_count, next_job, stopping, available_workers;
static unsigned generation, publication_revision;
static char cover_ids[24][16];
static int cover_count;
static volatile int cancellations[WORKERS];
static int worker_indices[WORKERS];
static VtLibrarySearchStatus status;
static VtLibraryItem live[VT_LIBRARY_RESULTS];
static int live_count, scoped;
static char text[129];
static VtLibraryItem scope;
/* Workers assemble snapshots in reusable storage. The UI only copies the published
 * page; it never scans the cache, allocates a merge buffer or sorts results. */
static pthread_mutex_t snapshot_mutex = PTHREAD_MUTEX_INITIALIZER;
static VtLibraryResult *snapshot_stage, *snapshot_results;
static int snapshot_count, cache_refresh_queued;
static unsigned snapshot_library_revision;
static void rebuild_snapshot(unsigned gen);
static SceUID threads[WORKERS] = {-1, -1, -1};
static int current(unsigned gen) {
    pthread_mutex_lock(&search_mutex);
    int ok = !stopping && gen == generation;
    pthread_mutex_unlock(&search_mutex);
    return ok;
}
static void publish(Job *j, const VtLibraryItem *item) {
    pthread_mutex_lock(&search_mutex);
    if (j->generation == generation && !stopping) {
        vt_library_cache(item);
        int exists = 0;
        for (int i = 0; i < live_count; i++)
            if (!strcmp(live[i].id, item->id)) {
                live[i] = *item;
                exists = 1;
                break;
            }
        int in_provider = 0;
        for (int i = 0; i < live_count; i++)
            if (live[i].provider == item->provider)
                in_provider++;
        if (!exists && live_count < VT_LIBRARY_RESULTS && (scoped || in_provider < 24))
            live[live_count++] = *item;
    }
    pthread_mutex_unlock(&search_mutex);
}
static int local_item(const VtLocalMediaItem *item, void *ctx) {
    Job *job = ctx;
    if (!current(job->generation))
        return 0;
    VtLibraryItem v;
    vt_library_local(item, &v);
    uint64_t p = 0, d = 0, order = vt_playback_history_order(v.history_id);
    if (order) {
        vt_playback_history_progress(v.history_id, &p, &d);
        vt_library_import(&v, p, d, order);
    } else {
        char podcast_id[16];
        snprintf(podcast_id, sizeof(podcast_id), "%s", v.history_id);
        memcpy(podcast_id, "podcs", 5);
        order = vt_playback_history_order(podcast_id);
        if (order) {
            vt_playback_history_progress(podcast_id, &p, &d);
            vt_library_import(&v, p, d, order);
        }
    }
    if (job->query[0] || order)
        publish(job, &v);
    if (job->query[0] && ++job->local_matches >= 24)
        return 0;
    return 1;
}
static int process(Job *j) {
    if (j->type == 6)
        return 0;
    if (j->type >= 2 && j->type != 5 && !vita_https_is_connected())
        return -1;
    if (j->type == 5) {
        VtOnlineItem *cover = calloc(1, sizeof(*cover));
        if (!cover)
            return -1;
        snprintf(cover->artwork_url, sizeof(cover->artwork_url), "%s", j->folder.artwork_url);
        int ret = vt_online_fetch_cover(cover, j->cancel);
        if (!ret && current(j->generation)) {
            snprintf(j->folder.artwork, sizeof(j->folder.artwork), "%s", cover->artwork_path);
            publish(j, &j->folder);
        }
        free(cover);
        return ret;
    }
    if (j->type == 0) {
        return ui_local_media_search(j->query, local_item, j, j->cancel);
    }
    if (j->type == 1) {
        VtOnlineItem *saved = calloc(VT_ONLINE_MAX, sizeof(*saved));
        VtIptvCatalog *tv = calloc(1, sizeof(*tv));
        if (!saved || !tv) {
            free(saved);
            free(tv);
            return -1;
        }
        int n = vt_online_load_saved(saved, VT_ONLINE_MAX);
        for (int i = 0; i < n && current(j->generation); i++) {
            VtLibraryItem v;
            vt_library_online(&saved[i], NULL, &v);
            if (vt_library_matches(&v, j->query))
                publish(j, &v);
        }
        int ret = vt_iptv_saved_load(tv, 0);
        if (!ret)
            for (unsigned i = 0; i < tv->count && current(j->generation); i++) {
                VtLibraryItem v;
                vt_library_iptv(&tv->items[i], &v);
                if (vt_library_matches(&v, j->query))
                    publish(j, &v);
            }
        free(saved);
        free(tv);
        return ret;
    }
    if (j->type == 2) {
        VtOnlineQuery *q = calloc(1, sizeof(*q));
        if (!q)
            return -1;
        q->cancel_parent = j->cancel;
        q->kind = j->folder.kind == VT_LIBRARY_FEED ? VT_ONLINE_EPISODE
                  : j->provider == VT_LIBRARY_RADIO ? VT_ONLINE_RADIO
                                                    : VT_ONLINE_FEED;
        snprintf(q->query, sizeof(q->query), "%s",
                 j->folder.kind == VT_LIBRARY_FEED ? j->folder.locator : j->query);
        int ret = vt_online_fetch_query(q);
        if (!ret)
            for (int i = 0; i < q->count && current(j->generation); i++) {
                if (q->items[i].kind != VT_ONLINE_RADIO && q->items[i].kind != VT_ONLINE_FEED &&
                    q->items[i].kind != VT_ONLINE_EPISODE)
                    continue;
                VtLibraryItem v;
                vt_library_online(&q->items[i], q->kind == VT_ONLINE_EPISODE ? q->query : NULL, &v);
                publish(j, &v);
            }
        free(q);
        return ret;
    }
    if (j->type == 3) {
        if (!j->credential.password[0])
            return VT_NETWORK_AUTH_FAILED;
        char detail[192];
        int ret = j->source.protocol == VT_NETWORK_JELLYFIN
                      ? vt_jellyfin_authenticate_cancel(&j->source, &j->credential, detail,
                                                        sizeof(detail), j->cancel)
                      : vt_personal_authenticate_cancel(&j->source, &j->credential, detail,
                                                        sizeof(detail), j->cancel);
        if (ret >= 0 && current(j->generation))
            vt_library_search_set_session(&j->source, &j->credential);
        VtNetworkEntry *entries = ret < 0 ? NULL : calloc(VT_NETWORK_MAX_ENTRIES, sizeof(*entries));
        if (ret >= 0 && !entries)
            return -1;
        char path[768];
        snprintf(path, sizeof(path), "%s", j->folder.locator);
        if (!path[0])
            snprintf(path, sizeof(path), "search:%s", j->query);
        if (ret >= 0)
            ret = j->source.protocol == VT_NETWORK_JELLYFIN
                      ? vt_jellyfin_list_cancel(&j->source, &j->credential, path, entries,
                                                VT_NETWORK_MAX_ENTRIES, detail, sizeof(detail),
                                                j->cancel)
                      : vt_personal_list_cancel(&j->source, &j->credential, path, entries,
                                                VT_NETWORK_MAX_ENTRIES, detail, sizeof(detail),
                                                j->cancel);
        for (int i = 0; i < ret && current(j->generation); i++) {
            VtLibraryItem v;
            vt_library_remote(&j->source, &entries[i], j->folder.locator, &v);
            publish(j, &v);
        }
        free(entries);
        return ret < 0 ? ret : 0;
    }
    if (j->type == 4) {
        unsigned char *data = NULL;
        size_t size = 0;
        char effective[VT_IPTV_URL_MAX];
        VtIptvHttpOptions options = {0};
        snprintf(options.user_agent, sizeof(options.user_agent), "%s", j->folder.user_agent);
        int ret = vt_iptv_http_fetch(&options, j->folder.locator, VT_IPTV_PLAYLIST_LIMIT, &data,
                                     &size, effective, sizeof(effective), j->cancel);
        VtIptvCatalog *tv = ret ? NULL : calloc(1, sizeof(*tv));
        if (!ret && !tv)
            ret = -1;
        if (!ret)
            ret = vt_iptv_catalog_parse(data, size, effective, tv);
        free(data);
        if (!ret)
            for (unsigned i = 0; i < tv->count && current(j->generation); i++) {
                VtLibraryItem v;
                vt_library_iptv(&tv->items[i], &v);
                snprintf(v.context, sizeof(v.context), "%s", j->folder.locator);
                publish(j, &v);
            }
        free(tv);
        return ret;
    }
    return -1;
}
static int worker(SceSize n, void *arg) {
    (void)n;
    int index = *(int *)arg;
    for (;;) {
        pthread_mutex_lock(&search_mutex);
        while (!stopping && next_job >= job_count)
            pthread_cond_wait(&search_changed, &search_mutex);
        int done = stopping;
        Job *j = NULL;
        if (!done && next_job < job_count) {
            j = malloc(sizeof(*j));
            if (j) {
                *j = jobs[next_job++];
                __atomic_store_n(&cancellations[index], 0, __ATOMIC_RELEASE);
                j->cancel = &cancellations[index];
            }
        }
        pthread_mutex_unlock(&search_mutex);
        if (done)
            break;
        if (!j) {
            sceKernelDelayThread(50000);
            continue;
        }
        int result = process(j);
        rebuild_snapshot(j->generation);
        pthread_mutex_lock(&search_mutex);
        if (j->generation == generation) {
            if (j->type == 6)
                cache_refresh_queued = 0;
            if (j->type != 5 && status.pending > 0)
                status.pending--;
            if (j->provider >= 0) {
                if (status.updating[j->provider] > 0)
                    status.updating[j->provider]--;
                int *s = &status.state[j->provider];
                if (result == VT_NETWORK_AUTH_FAILED)
                    *s = VT_SEARCH_LOGIN;
                else if (result < 0)
                    *s = VT_SEARCH_ERROR;
                else if (*s != VT_SEARCH_ERROR && *s != VT_SEARCH_LOGIN)
                    *s = status.updating[j->provider] ? VT_SEARCH_LOADING : VT_SEARCH_READY;
            }
        }
        pthread_mutex_unlock(&search_mutex);
        memset(j, 0, sizeof(*j));
        free(j);
    }
    return 0;
}
static void add_job(int type, int provider, const VtNetworkSource *s, const VtNetworkCredential *c,
                    const VtLibraryItem *folder) {
    if (job_count == JOB_MAX && next_job) {
        int pending = job_count - next_job;
        memmove(jobs, jobs + next_job, (size_t)pending * sizeof(*jobs));
        memset(jobs + pending, 0, (JOB_MAX - pending) * sizeof(*jobs));
        job_count = pending;
        next_job = 0;
    }
    if (job_count >= JOB_MAX)
        return;
    Job *j = &jobs[job_count++];
    memset(j, 0, sizeof(*j));
    j->generation = generation;
    j->type = type;
    j->provider = provider;
    snprintf(j->query, sizeof(j->query), "%s", text);
    if (s)
        j->source = *s;
    if (c)
        j->credential = *c;
    if (folder)
        j->folder = *folder;
    if (!available_workers) {
        if (provider >= 0)
            status.state[provider] = VT_SEARCH_ERROR;
        return;
    }
    status.pending++;
    if (provider >= 0) {
        status.updating[provider]++;
        status.state[provider] = VT_SEARCH_LOADING;
    }
}
void vt_library_search_begin(const char *q, const VtLibraryItem *folder) {
    VtNetworkSource *sources = calloc(VT_NETWORK_MAX_SOURCES, sizeof(*sources));
    VtNetworkCredential *credentials = calloc(VT_NETWORK_MAX_SOURCES, sizeof(*credentials));
    int n = sources && credentials ? vt_network_sources_load(sources, VT_NETWORK_MAX_SOURCES) : 0;
    if (n > 0 && vt_preferences_remember_network_passwords())
        vt_network_credentials_load(credentials, VT_NETWORK_MAX_SOURCES);
    for (int i = 0; i < n; i++)
        ui_network_personal_resume_credential(&sources[i], &credentials[i]);
    pthread_mutex_lock(&search_mutex);
    for (int i = 0; i < n; i++) {
        char key[16];
        vt_network_media_history_id(&sources[i], "", key);
        for (int k = 0; k < VT_NETWORK_MAX_SOURCES; k++)
            if (!strcmp(key, sessions[k].key) && !credentials[i].password[0])
                credentials[i] = sessions[k].credential;
    }
    for (int k = 0; k < WORKERS; k++)
        __atomic_store_n(&cancellations[k], 1, __ATOMIC_RELEASE);
    int reuse = !folder && !scoped && !strcmp(text, q ? q : "");
    if (!reuse)
        snapshot_count = 0;
    else
        for (int i = 0; i < snapshot_count; i++)
            snapshot_results[i].cached = 1;
    generation++;
    __atomic_add_fetch(&publication_revision, 1, __ATOMIC_RELEASE);
    memset(&status, 0, sizeof(status));
    status.generation = generation;
    memset(jobs, 0, sizeof(jobs));
    job_count = next_job = live_count = cover_count = 0;
    memset(cover_ids, 0, sizeof(cover_ids));
    snprintf(text, sizeof(text), "%s", q ? q : "");
    scoped = folder != NULL;
    scope = folder ? *folder : (VtLibraryItem){0};
    cache_refresh_queued = available_workers != 0;
    add_job(6, -1, NULL, NULL, NULL);
    if (folder) {
        if (folder->provider == VT_LIBRARY_PODCAST)
            add_job(2, VT_LIBRARY_PODCAST, NULL, NULL, folder);
        else if (folder->provider == VT_LIBRARY_IPTV)
            add_job(4, VT_LIBRARY_IPTV, NULL, NULL, folder);
        else
            for (int i = 0; i < n; i++) {
                char key[16];
                vt_network_media_history_id(&sources[i], "", key);
                if (!strcmp(key, folder->server))
                    add_job(3, folder->provider, &sources[i], &credentials[i], folder);
            }
    } else {
        add_job(0, VT_LIBRARY_LOCAL, NULL, NULL, NULL);
        add_job(1, -1, NULL, NULL, NULL);
        if (text[0]) {
            add_job(2, VT_LIBRARY_RADIO, NULL, NULL, NULL);
            add_job(2, VT_LIBRARY_PODCAST, NULL, NULL, NULL);
            for (int i = 0; i < n; i++) {
                int p = sources[i].protocol == VT_NETWORK_JELLYFIN    ? VT_LIBRARY_JELLYFIN
                        : sources[i].protocol == VT_NETWORK_NAVIDROME ? VT_LIBRARY_NAVIDROME
                        : sources[i].protocol == VT_NETWORK_IMMICH    ? VT_LIBRARY_IMMICH
                                                                      : -1;
                if (p >= 0)
                    add_job(3, p, &sources[i], &credentials[i], NULL);
            }
        }
    }
    pthread_cond_broadcast(&search_changed);
    pthread_mutex_unlock(&search_mutex);
    if (credentials) {
        memset(credentials, 0, VT_NETWORK_MAX_SOURCES * sizeof(*credentials));
        free(credentials);
    }
    free(sources);
}
static int compare_results(const void *a, const void *b) {
    const VtLibraryResult *x = a, *y = b;
    int order = x->item.provider - y->item.provider;
    return order ? order : strcmp(x->item.title, y->item.title);
}
static void rebuild_snapshot(unsigned gen) {
    pthread_mutex_lock(&snapshot_mutex);
    VtLibraryResult *out = snapshot_stage;
    const int capacity = VT_LIBRARY_RESULTS;
    if (!out || !current(gen)) {
        pthread_mutex_unlock(&snapshot_mutex);
        return;
    }
    char q[129];
    pthread_mutex_lock(&search_mutex);
    snprintf(q, sizeof(q), "%s", text);
    int scope_active = scoped;
    VtLibraryItem parent = scope;
    pthread_mutex_unlock(&search_mutex);
    unsigned library_revision = vt_library_revision();
    int count = vt_library_query_search(scope_active ? "" : q, scope_active ? &parent : NULL, out,
                                        capacity);
    pthread_mutex_lock(&search_mutex);
    if (gen != generation || stopping) {
        pthread_mutex_unlock(&search_mutex);
        pthread_mutex_unlock(&snapshot_mutex);
        return;
    }
    for (int i = 0; i < live_count; i++) {
        int found = -1;
        for (int k = 0; k < count; k++)
            if (!strcmp(out[k].item.id, live[i].id)) {
                found = k;
                break;
            }
        if (found < 0) {
            int in_provider = 0;
            for (int k = 0; k < count; k++)
                if (out[k].item.provider == live[i].provider)
                    in_provider++;
            if (count < capacity && (scope_active || in_provider < 24))
                out[count++] =
                    (VtLibraryResult){.item = live[i], .duration_ms = live[i].duration_ms};
            else {
                int replace = -1;
                for (int k = count - 1; k >= 0; k--)
                    if (out[k].cached && out[k].item.provider == live[i].provider) {
                        replace = k;
                        break;
                    }
                if (replace >= 0)
                    out[replace] =
                        (VtLibraryResult){.item = live[i], .duration_ms = live[i].duration_ms};
            }
        } else {
            out[found].item = live[i];
            out[found].cached = 0;
        }
    }
    pthread_mutex_unlock(&search_mutex);

    /* Stable provider grouping. UI restores focus by identity on every refresh. */
    qsort(out, (size_t)count, sizeof(*out), compare_results);
    pthread_mutex_lock(&search_mutex);
    if (gen == generation && !stopping) {
        VtLibraryResult *prior = snapshot_results;
        snapshot_results = snapshot_stage;
        snapshot_stage = prior;
        snapshot_count = count;
        snapshot_library_revision = library_revision;
        __atomic_add_fetch(&publication_revision, 1, __ATOMIC_RELEASE);
    }
    pthread_mutex_unlock(&search_mutex);
    pthread_mutex_unlock(&snapshot_mutex);
}
int vt_library_search_snapshot(VtLibraryResult *out, int capacity, VtLibrarySearchStatus *s) {
    if (!out || capacity <= 0 || !s)
        return 0;
    unsigned revision = vt_library_revision();
    pthread_mutex_lock(&search_mutex);
    if (revision != snapshot_library_revision && !cache_refresh_queued && available_workers &&
        !stopping && (job_count < JOB_MAX || next_job)) {
        cache_refresh_queued = 1;
        add_job(6, -1, NULL, NULL, NULL);
        pthread_cond_broadcast(&search_changed);
    }
    *s = status;
    s->revision = __atomic_load_n(&publication_revision, __ATOMIC_ACQUIRE);
    int n = snapshot_count < capacity ? snapshot_count : capacity;
    if (n)
        memcpy(out, snapshot_results, (size_t)n * sizeof(*out));
    pthread_mutex_unlock(&search_mutex);
    return n;
}
unsigned vt_library_search_revision(void) {
    return __atomic_load_n(&publication_revision, __ATOMIC_ACQUIRE);
}
void vt_library_search_cover(const VtLibraryItem *v) {
    if (!v || !v->artwork_url[0] || v->artwork[0])
        return;
    pthread_mutex_lock(&search_mutex);
    int requested = 0;
    for (int i = 0; i < cover_count; i++)
        if (!strcmp(cover_ids[i], v->id))
            requested = 1;
    if (!requested && cover_count < 24 && available_workers && !stopping) {
        if (job_count == JOB_MAX && next_job) {
            int pending = job_count - next_job;
            memmove(jobs, jobs + next_job, (size_t)pending * sizeof(*jobs));
            memset(jobs + pending, 0, (JOB_MAX - pending) * sizeof(*jobs));
            job_count = pending;
            next_job = 0;
        }
        if (job_count < JOB_MAX) {
            Job *j = &jobs[job_count++];
            memset(j, 0, sizeof(*j));
            j->generation = generation;
            j->type = 5;
            j->provider = -1;
            j->folder = *v;
            snprintf(cover_ids[cover_count++], 16, "%s", v->id);
        }
    }
    pthread_cond_broadcast(&search_changed);
    pthread_mutex_unlock(&search_mutex);
}
void vt_library_search_local_refresh(void) {
    pthread_mutex_lock(&search_mutex);
    if (!scoped && !stopping) {
        int pending = job_count - next_job;
        memmove(jobs, jobs + next_job, (size_t)pending * sizeof(*jobs));
        memset(jobs + pending, 0, (JOB_MAX - pending) * sizeof(*jobs));
        next_job = 0;
        job_count = pending;
        add_job(0, VT_LIBRARY_LOCAL, NULL, NULL, NULL);
    }
    pthread_cond_broadcast(&search_changed);
    pthread_mutex_unlock(&search_mutex);
}
void vt_library_search_cancel(void) {
    pthread_mutex_lock(&search_mutex);
    generation++;
    for (int i = 0; i < WORKERS; i++)
        __atomic_store_n(&cancellations[i], 1, __ATOMIC_RELEASE);
    job_count = next_job = 0;
    memset(jobs, 0, sizeof(jobs));
    pthread_mutex_unlock(&search_mutex);
}
int vt_library_search_init(void) {
    snapshot_stage = calloc(VT_LIBRARY_RESULTS, sizeof(*snapshot_stage));
    snapshot_results = calloc(VT_LIBRARY_RESULTS, sizeof(*snapshot_results));
    if (!snapshot_stage || !snapshot_results) {
        free(snapshot_stage);
        free(snapshot_results);
        snapshot_stage = snapshot_results = NULL;
        return -1;
    }
    snapshot_count = cache_refresh_queued = 0;
    stopping = 0;
    int started = 0;
    for (int i = 0; i < WORKERS; i++) {
        worker_indices[i] = i;
        threads[i] =
            sceKernelCreateThread("LibrarySearch", worker, 0x10000140, 0x40000, 0, 0, NULL);
        if (threads[i] >= 0) {
            if (sceKernelStartThread(threads[i], sizeof(worker_indices[i]), &worker_indices[i]) <
                0) {
                sceKernelDeleteThread(threads[i]);
                threads[i] = -1;
            } else
                started++;
        }
    }
    available_workers = started;
    if (!started) {
        free(snapshot_stage);
        free(snapshot_results);
        snapshot_stage = snapshot_results = NULL;
    }
    return started ? 0 : -1;
}
void vt_library_search_shutdown(void) {
    pthread_mutex_lock(&search_mutex);
    stopping = 1;
    generation++;
    for (int i = 0; i < WORKERS; i++)
        __atomic_store_n(&cancellations[i], 1, __ATOMIC_RELEASE);
    memset(sessions, 0, sizeof(sessions));
    memset(jobs, 0, sizeof(jobs));
    pthread_cond_broadcast(&search_changed);
    pthread_mutex_unlock(&search_mutex);
    for (int i = 0; i < WORKERS; i++)
        if (threads[i] >= 0) {
            sceKernelWaitThreadEnd(threads[i], NULL, NULL);
            sceKernelDeleteThread(threads[i]);
            threads[i] = -1;
        }
    free(snapshot_stage);
    free(snapshot_results);
    snapshot_stage = snapshot_results = NULL;
    snapshot_count = available_workers = 0;
}
