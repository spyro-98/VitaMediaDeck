/* Hold network and artwork work indefinitely: production local startup must
 * still return with the cached registry/search available to the Home loop. */
#include "startup.h"
#include "library/library.h"
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static pthread_t render_thread;
static pthread_mutex_t gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_changed = PTHREAD_COND_INITIALIZER;
static int network_entered, artwork_entered, release_workers, network_fails;
static int history_ready, playback_ready, registry_ready, search_ready, transport_ready;

static void held_work(int *entered) {
    assert(!pthread_equal(render_thread, pthread_self()));
    pthread_mutex_lock(&gate_mutex);
    *entered = 1;
    pthread_cond_broadcast(&gate_changed);
    while (!release_workers) pthread_cond_wait(&gate_changed, &gate_mutex);
    pthread_mutex_unlock(&gate_mutex);
}
static void wait_for(int *entered) {
    pthread_mutex_lock(&gate_mutex);
    while (!*entered) pthread_cond_wait(&gate_changed, &gate_mutex);
    pthread_mutex_unlock(&gate_mutex);
}
uint64_t sceKernelGetProcessTimeWide(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000ULL + t.tv_nsec / 1000;
}
int sceKernelGetThreadId(void) { return 42; }
int sceKernelChangeThreadPriority(int id, int priority) { assert(id == 42 && priority == 191); return 0; }
void log_printf(const char *format, ...) { (void)format; }
int log_save(const char *path) { assert(path); return 0; }
int vita_https_init(void) { held_work(&network_entered); return network_fails ? -1 : 0; }
int vt_network_init(void) { transport_ready = 1; return 0; }
void vt_artwork_cache_init(void) { assert(search_ready); held_work(&artwork_entered); }
int vt_playback_history_init(void) { history_ready = 1; return 0; }
void vt_background_playback_init(void) { assert(history_ready); playback_ready = 1; }
int vt_background_playback_library_sample(char id[64], uint64_t *p, uint64_t *d) {
    (void)id; (void)p; (void)d; return 0;
}
int vt_library_init(VtLibrarySample sample) {
    assert(playback_ready && sample == vt_background_playback_library_sample);
    registry_ready = 1; return 0;
}
int vt_library_search_init(void) { assert(registry_ready); search_ready = 1; return 0; }

int main(int argc, char **argv) {
    alarm(10);
    render_thread = pthread_self();
    network_fails = argc > 1 && !strcmp(argv[1], "--offline");
    vt_startup_background_begin(sceKernelGetProcessTimeWide());
    wait_for(&network_entered);
    assert(!vt_startup_network_ready());
    assert(vt_startup_load_local(NULL) == 0);
    wait_for(&artwork_entered);
    assert(history_ready && playback_ready && registry_ready && search_ready);
    assert(!vt_startup_network_ready());
    /* Neither blocked task has completed, yet all Home prerequisites have. */
    pthread_mutex_lock(&gate_mutex); release_workers = 1;
    pthread_cond_broadcast(&gate_changed); pthread_mutex_unlock(&gate_mutex);
    vt_startup_background_join();
    assert(vt_startup_network_ready() == !network_fails);
    assert(transport_ready == !network_fails);
    vt_startup_background_join(); /* No double join on a failed/finished worker. */
    puts("Startup services: cached local data/search ready while network and artwork stay blocked; independent completion/offline failure and joined shutdown passed.");
}
