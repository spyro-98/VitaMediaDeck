#include "startup.h"
#include "app_paths.h"
#include "common/text_log.h"
#include "history/playback_history.h"
#include "library/library.h"
#include "library/search.h"
#include "media/background_playback.h"
#include "network/artwork_cache.h"
#include "network/network_source.h"
#include <pthread.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <vita_https.h>

static pthread_t network_worker, artwork_worker;
static int network_started, artwork_started, network_ready;
static uint64_t startup_started_us;

static void milestone(const char *name, uint64_t began, int result) {
    uint64_t now = sceKernelGetProcessTimeWide();
    log_printf("startup stage: %s=%llu ms result=%d total=%llu ms", name,
               (unsigned long long)((now - began) / 1000ULL), result,
               (unsigned long long)((now - startup_started_us) / 1000ULL));
    log_save(VITAMEDIADECK_SESSION_LOG_PATH);
}

static void *network_start(void *unused) {
    (void)unused;
    sceKernelChangeThreadPriority(sceKernelGetThreadId(), 191);
    uint64_t began = sceKernelGetProcessTimeWide();
    /* Initialize the transport, never wait for association, DNS or a server. */
    int result = vita_https_init();
    if (result >= 0) result = vt_network_init();
    __atomic_store_n(&network_ready, result >= 0, __ATOMIC_RELEASE);
    milestone("network-background", began, result);
    return NULL;
}

static void *artwork_start(void *unused) {
    (void)unused;
    sceKernelChangeThreadPriority(sceKernelGetThreadId(), 191);
    uint64_t began = sceKernelGetProcessTimeWide();
    vt_artwork_cache_init();
    milestone("artwork-discovery-background", began, 0);
    return NULL;
}

void vt_startup_background_begin(uint64_t started_us) {
    startup_started_us = started_us;
    pthread_attr_t attributes;
    int result = pthread_attr_init(&attributes);
    if (result) { log_printf("startup: background attributes failed %d", result); return; }
    result = pthread_attr_setstacksize(&attributes, 0x40000);
    if (!result) {
        result = pthread_create(&network_worker, &attributes, network_start, NULL);
        network_started = result == 0;
    }
    pthread_attr_destroy(&attributes);
    if (result) log_printf("startup: network worker unavailable %d; local UI remains available", result);
}

int vt_startup_load_local(void *unused) {
    (void)unused;
    uint64_t began = sceKernelGetProcessTimeWide();
    int result = vt_playback_history_init();
    milestone("history-cache", began, result);
    /* Do not reset sampled playback state after its checkpoint worker starts. */
    vt_background_playback_init();
    began = sceKernelGetProcessTimeWide();
    result = vt_library_init(vt_background_playback_library_sample);
    milestone("library-cache", began, result);
    began = sceKernelGetProcessTimeWide();
    result = vt_library_search_init();
    milestone("search-workers", began, result);
    /* Scanning thousands of cached files must never gate Home or its covers. */
    pthread_attr_t attributes;
    result = pthread_attr_init(&attributes);
    if (!result) {
        result = pthread_attr_setstacksize(&attributes, 0x40000);
        if (!result) {
            result = pthread_create(&artwork_worker, &attributes, artwork_start, NULL);
            artwork_started = result == 0;
        }
        pthread_attr_destroy(&attributes);
    }
    if (result) log_printf("startup: artwork discovery unavailable %d; keyed cache reads remain available", result);
    log_printf("startup: local cache ready; opening Home without waiting for network/artwork");
    return 0;
}

int vt_startup_network_ready(void) {
    return __atomic_load_n(&network_ready, __ATOMIC_ACQUIRE);
}

void vt_startup_background_join(void) {
    /* Shutdown only: never tear down TLS/NetCtl while initialization is live. */
    if (network_started) { pthread_join(network_worker, NULL); network_started = 0; }
    if (artwork_started) { pthread_join(artwork_worker, NULL); artwork_started = 0; }
}
