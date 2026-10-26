/* Actual production PAF adapter, with firmware boundaries supplied by mocks.
 * This verifies ownership/protocol/lifecycle; it does not render Sony widgets. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include "../../src/ui/native_vita.c"
/* Contract failures exit normally with a diagnostic, never generate macOS
 * crash reports. In particular the foreign-unlock negative control is data. */
#undef assert
#define assert(condition) do { if (!(condition)) { \
    fprintf(stderr, "Contract failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__); \
    exit(EXIT_FAILURE); } } while (0)
static atomic_uint checks;
static uint64_t host_clock_us;
uint64_t sceKernelGetProcessTimeWide(void) { return host_clock_us; }
static pthread_t host_runner;
static pthread_t host_paf_owner;
static int host_owner_valid, host_protocol_probe, host_page_shown, host_failed_graph_draws;
static int host_callback_wrong_thread;
static pthread_mutex_t host_paf_mutex;
static int host_paf_mutex_live, host_paf_locked;
static void (*host_paf_task)(void *);
static void host_owner_check(void) {
    assert(host_protocol_probe || (host_owner_valid && pthread_equal(pthread_self(), host_paf_owner)));
}
int sceKernelGetThreadId(void) {
    return host_callback_wrong_thread ? 7 :
        host_owner_valid && pthread_equal(pthread_self(), host_paf_owner) ? 123 : 7;
}
void vmd_paf_main_task_register(void (*callback)(void *), void *userdata) {
    host_owner_check();
    assert(callback && !userdata && !host_paf_task);
    host_paf_task = callback;
}

static pthread_mutex_t host_runner_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t host_runner_cond = PTHREAD_COND_INITIALIZER;
static SceKernelThreadEntry host_runner_entry;
static int host_thread_live, host_thread_creates, host_thread_create_error, host_thread_start_error;
static int host_run_behavior, host_frame_requested, host_frames_drawn, host_idle_visit;
static atomic_int host_runner_stop, host_delays;
static void *host_thread_main(void *arg) {
    (void)arg;
    host_paf_owner = pthread_self();
    host_owner_valid = 1;
    host_runner_entry(0, NULL);
    if (host_paf_locked) {
        assert(!pthread_mutex_unlock(&host_paf_mutex));
        host_paf_locked = 0;
    }
    return NULL;
}
SceUID sceKernelCreateThread(const char *name, SceKernelThreadEntry entry, int priority,
                             unsigned stack, unsigned attributes, int cpu, const void *options) {
    assert(name && entry && priority == 0x10000100 && stack == 0x40000 && !attributes && !cpu && !options);
    if (host_thread_create_error) return -88;
    host_runner_entry = entry;
    host_thread_creates++;
    return 123;
}
int sceKernelStartThread(SceUID uid, SceSize args, void *argp) {
    assert(uid == 123 && !args && !argp);
    if (host_thread_start_error) return -89;
    assert(!pthread_create(&host_runner, NULL, host_thread_main, NULL));
    host_thread_live = 1;
    return 0;
}
int sceKernelDeleteThread(SceUID uid) {
    assert(uid == 123 && !host_thread_live);
    return 0;
}
int sceKernelDelayThread(unsigned microseconds) {
    assert(microseconds == 1000);
    atomic_fetch_add(&host_delays, 1);
    struct timespec delay = {0, 1000000};
    nanosleep(&delay, NULL);
    pthread_mutex_lock(&host_runner_mutex);
    pthread_cond_broadcast(&host_runner_cond);
    pthread_mutex_unlock(&host_runner_mutex);
    return 0;
}
int vmd_paf_fw_run(void *self) {
    host_owner_check();
    assert(self && self == framework && host_paf_locked);
    if (host_run_behavior == 1) return 0; /* unresolved weak Run / unexpected exit */
    pthread_mutex_lock(&host_runner_mutex);
    while (!atomic_load(&host_runner_stop) &&
           (host_run_behavior == 2 || (host_run_behavior == 3 && atomic_load(&host_delays) < 5)))
        pthread_cond_wait(&host_runner_cond, &host_runner_mutex);
    if (atomic_load(&host_runner_stop)) {
        pthread_mutex_unlock(&host_runner_mutex);
        return -9;
    }
    __atomic_store_n(&((VtPafFrameworkPrefix *)self)->run_loop_count, 1, __ATOMIC_RELEASE);
    /* The scheduler can also visit tasks before its initial wait. No app
     * commands may be read before publication of a validated frame. */
    host_paf_task(NULL);
    /* OneFrameMode Run stays alive and waits. Update alone does not paint. */
    while (!atomic_load(&host_runner_stop)) {
        while (host_frame_requested == host_frames_drawn && !host_idle_visit && !atomic_load(&host_runner_stop))
            pthread_cond_wait(&host_runner_cond, &host_runner_mutex);
        if (atomic_load(&host_runner_stop)) break;
        if (host_idle_visit) {
            host_paf_task(NULL);
            host_idle_visit = 0;
            pthread_cond_broadcast(&host_runner_cond);
            continue;
        }
        /* The public main-thread task is called by Run, never by Update.
         * Like retail's startup signal, behavior 4 first returns without paint. */
        if (!(host_run_behavior == 4 && !host_frames_drawn)) {
            if (host_run_behavior != 5 &&
                (host_run_behavior != 6 || host_frames_drawn % 3 == 2)) host_paf_task(NULL);
            /* Reproduce retail Run's owner-only release/reacquire. An init on
             * the app thread would return EPERM here, as the console did. */
            assert(!pthread_mutex_unlock(&host_paf_mutex));
            host_paf_locked = 0;
            if (atomic_load(&renderer_error) && host_page_shown) host_failed_graph_draws++;
            assert(!pthread_mutex_lock(&host_paf_mutex));
            host_paf_locked = 1;
        }
        host_frames_drawn++;
        pthread_cond_broadcast(&host_runner_cond);
    }
    __atomic_store_n(&((VtPafFrameworkPrefix *)self)->run_loop_count, 0, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&host_runner_mutex);
    return 0;
}
static void *host_foreign_unlock(void *unused) {
    (void)unused;
    return (void *)(intptr_t)pthread_mutex_unlock(&host_paf_mutex);
}
static void host_stop_runner(void) {
    if (!host_thread_live) return;
    pthread_mutex_lock(&host_runner_mutex);
    atomic_store(&host_runner_stop, 1);
    pthread_cond_broadcast(&host_runner_cond);
    pthread_mutex_unlock(&host_runner_mutex);
    assert(!pthread_join(host_runner, NULL));
    host_thread_live = 0;
}
static int host_frame_variant;
static SceCommonDialogUpdateParam host_expected_frame;
static const void *host_source_frame;
static int host_fail_layout, host_fail_show, host_fail_hide, host_fail_alpha;
static unsigned host_failure_code;
static char host_failure_operation[64];
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        assert(x);                                                                                 \
    } while (0)
static int host_theme, host_scene, host_loaded, host_load_error, host_ctor_missing, host_foreign;
static int host_checkpoints, host_saved_theme, host_persist_error, host_fail_geometry;
static int host_loads, host_updates, host_creates, host_strings, host_fail_create, host_fail_text;
static int host_plugin_missing, host_alloc_fail, host_real_common, host_bad_target;
static void *host_instance, *host_allocated[32];
static unsigned host_alloc_count;
static unsigned char host_nodes[1024];
static int host_node_kinds[1024], host_bad_text_targets;
static float host_left[1024],host_width[1024];
static int host_nodes_used;
static char host_last_text[512];
int ui_theme_id(void) {
    assert(!host_owner_valid || !pthread_equal(pthread_self(), host_paf_owner));
    return host_theme;
}
void ui_theme_apply(int theme) {
    assert(!host_owner_valid || !pthread_equal(pthread_self(), host_paf_owner));
    host_theme = theme;
}
int vt_preferences_set_interface_theme(int theme) {
    assert(!host_owner_valid || !pthread_equal(pthread_self(), host_paf_owner));
    CHECK(theme == VT_UI_THEME_LEGACY);
    host_saved_theme = theme;
    return host_persist_error;
}
int vt_i18n_current_language(void) {
    assert(!host_owner_valid || !pthread_equal(pthread_self(), host_paf_owner));
    return VT_LANG_IT;
}
void log_printf(const char *format, ...) {
    CHECK(format && !strstr(format, "password"));
    if (strstr(format, "widget failure")) {
        va_list ap;
        va_start(ap, format);
        (void)va_arg(ap, int);
        (void)va_arg(ap, unsigned);
        snprintf(host_failure_operation, sizeof(host_failure_operation), "%s",
                 va_arg(ap, const char *));
        host_failure_code = va_arg(ap, unsigned);
        va_end(ap);
    }
}
int log_save(const char *path) {
    CHECK(path && strstr(path, "ux0:data/VitaMediaDeck/") && !host_scene);
    host_checkpoints++;
    return 0;
}
int sceSysmoduleIsLoadedInternal(unsigned which) {
    CHECK(which == SCE_SYSMODULE_INTERNAL_PAF);
    return host_loaded ? 0 : -1;
}
int sceSysmoduleLoadModuleInternalWithArg(unsigned which, unsigned bytes, void *args,
                                          const SceSysmoduleOpt *opt) {
    CHECK(which == SCE_SYSMODULE_INTERNAL_PAF && bytes == 24 && !host_scene);
    uint32_t *v = args;
    CHECK(v[0] == 8 * 1024 * 1024 && v[3] == 0);
    host_loads++;
    *opt->result = host_load_error;
    host_loaded = !host_load_error;
    return host_load_error;
}
void *vmd_paf_allocate(size_t n) {
    host_owner_check();
    if (host_alloc_fail)
        return NULL;
    CHECK(host_alloc_count < 32);
    void *p = calloc(1, n);
    CHECK(p);
    host_allocated[host_alloc_count++] = p;
    return p;
}
void vmd_paf_fw_params(void *p) {
    host_owner_check();
    CHECK(!host_scene);
    memset(p, 0, 180);
    if (!host_ctor_missing)
        ((uint32_t *)p)[0] = 0x40000;
}
void *vmd_paf_fw_instance(void) {
    host_owner_check();
    return host_foreign ? (void *)host_nodes : host_instance;
}
void vmd_paf_fw_create(void *self, const void *p) {
    host_owner_check();
    CHECK(!host_scene);
    const uint32_t *v = p;
    /* The actual retail crash exhausted a 9 MiB dialog partition. The global
     * heap must not use it; graphics heaps must be explicit and bounded. */
    CHECK(v[0] == 256 * 1024 && v[1] == 1024 * 1024 && v[2] == 1);
    CHECK(v[4] == 1024 * 1024 && v[5] == 4 * 1024 * 1024);
    CHECK(v[3] == 2 * 1024 * 1024 && v[6] == 1024 * 1024);
    CHECK(v[28] == 5 && v[29] == 1 && v[30] == 5 && v[42] == 1);
    const unsigned char *b = p;
    CHECK(!b[128] && b[130]);
    host_instance = self;
    assert(!pthread_mutex_lock(&host_paf_mutex));
    host_paf_locked = 1;
}
void vmd_paf_fw_common(void *self, void (*done)(void)) {
    host_owner_check();
    CHECK(self == host_instance && !host_scene && !done);
}
void vmd_paf_plugin_params(void *p) {
    host_owner_check();
    memset(p, 0, 148);
}
void vmd_paf_plugin_load(const void *p, void (*done)(void *), int options) {
    host_owner_check();
    CHECK(p && options == 1 && !host_scene);
    done(host_plugin_missing ? NULL : host_nodes);
}
void vmd_paf_page_params(void *p) {
    host_owner_check();
    memset(p, 0, 44);
}
void *vmd_paf_page_open(void *self, const VtPafId *which, const void *p) {
    host_owner_check();
    CHECK(self == host_nodes && which->hash && p);
    return host_nodes + 1;
}
uint32_t vmd_paf_id_hash(VtPafId *self, const VtPafString *name) {
    host_owner_check();
    CHECK(self && name->data && name->count);
    return name->count;
}
void *vmd_paf_widget_create(void *self, void *parent, const char *type, const VtPafId *name,
                            const VtPafId *style) {
    host_owner_check();
    CHECK(self == host_nodes && parent == host_nodes + 1 && type && name->hash && style->hash);
    /* Reproduce the real case-sensitive factory, rather than accepting any
     * arbitrary C++ type name as the old mock did. */
    if (strcmp(type, "text") && strcmp(type, "button") && strcmp(type, "plane"))
        return NULL;
    host_creates++;
    if (host_fail_create)
        return NULL;
    CHECK(host_nodes_used < 1000);
    unsigned node = 2 + host_nodes_used++;
    host_node_kinds[node] = !strcmp(type, "button") ? NATIVE_BUTTON
                              : !strcmp(type, "text") ? NATIVE_TEXT : NATIVE_PANEL;
    return host_nodes + node;
}
int vmd_paf_widget_pos(void *self, const float v[4], void *timer, int id, int opt, float delay,
                       void *cb, void *data) {
    host_owner_check();
    CHECK(self && !host_scene && !timer && !opt && !delay && !cb && !data && id == 0x10000);
    CHECK(isfinite(v[0]) && isfinite(v[1]) && !((uintptr_t)v & 15));
    unsigned node=(unsigned)((unsigned char *)self-host_nodes);
    host_left[node]=v[0]+480-host_width[node]*.5f;
    return 0;
}
int vmd_paf_widget_size(void *self, const float v[4], void *timer, int id, int opt, float delay,
                        void *cb, void *data) {
    host_owner_check();
    CHECK(self && !host_scene && !timer && !opt && !delay && !cb && !data && id == 0x10004);
    CHECK(v[0] > 0 && v[1] > 0 && !((uintptr_t)v & 15));
    host_width[(unsigned char *)self-host_nodes]=v[0];
    return host_fail_geometry ? -1 : 0;
}
int vmd_paf_widget_show(void *self, void *timer, int opt, float delay, void *cb) {
    host_owner_check();
    if (self == page && !host_fail_show) host_page_shown = 1;
    CHECK(self && !timer && opt == 0 && !delay && !cb && !host_scene);
    return host_fail_show ? -12346 : 0;
}
int vmd_paf_widget_hide(void *self, void *timer, int opt, float delay, void *cb) {
    host_owner_check();
    if (self == page && !host_fail_hide) host_page_shown = 0;
    CHECK(self && !timer && opt == 0 && !delay && !cb && !host_scene);
    return host_fail_hide ? -12347 : 0;
}
int vmd_paf_widget_alpha(void *self, float alpha, void *timer, int id, int opt, float delay,
                         void *cb) {
    host_owner_check();
    CHECK(self && alpha >= 0 && alpha <= 1 && !timer && id == 0x10002 && !opt && !delay && !cb &&
          !host_scene);
    return host_fail_alpha ? -12348 : 0;
}
static int set_text(void *self, const VtPafString *text) {
    CHECK(self && text && !host_scene);
    host_strings++;
    const uint16_t *v = text->data;
    for (unsigned i = 0; i < text->count && i < sizeof(host_last_text) - 1; i++)
        host_last_text[i] = v[i] < 128 ? (char)v[i] : '?';
    host_last_text[text->count] = 0;
    return host_fail_text ? -1 : 0;
}
int vmd_paf_button_text(void *self, const VtPafString *text, int obj) {
    host_owner_check();
    CHECK(self && host_node_kinds[(unsigned char *)self - host_nodes] == NATIVE_BUTTON);
    /* Actual Button OBJ_BG=0 is a PlaneObj; SetString requires OBJ_LABEL=1.
     * Return the console's exact error instead of accepting every object. */
    if (obj != 1) {
        host_bad_text_targets++;
        return (int)0x80af4101u;
    }
    return set_text(self, text);
}
int vmd_paf_widget_text(void *self, const VtPafString *text, int obj) {
    host_owner_check();
    CHECK(self && host_node_kinds[(unsigned char *)self - host_nodes] == NATIVE_TEXT);
    if (obj != 0) {
        host_bad_text_targets++;
        return (int)0x80af4101u;
    }
    return set_text(self, text);
}
uint32_t vmd_paf_button_state(void *self, int s) {
    host_owner_check();
    CHECK(self && (s == 0 || s == 2) && !host_scene);
    return 0;
}
void vmd_paf_fw_update(void *self, const VtPafFrameUpdate *v) {
    const SceCommonDialogRenderTargetInfo *t = &host_expected_frame.renderTarget;
    CHECK(self == host_instance && !host_scene && v && (const void *)v != host_source_frame);
    CHECK(v->pitch_pixels == t->strideInPixels && v->width == t->width &&
          v->height == t->height);
    CHECK(v->color_format == t->colorFormat && v->surface_type == t->surfaceType);
    CHECK(v->color_data == t->colorSurfaceData && v->depth_data == t->depthSurfaceData &&
          v->display_sync == host_expected_frame.displaySyncObject);
    host_updates++;
    host_clock_us += 16000;
    pthread_mutex_lock(&host_runner_mutex);
    if (__atomic_load_n(&((VtPafFrameworkPrefix *)self)->run_loop_count, __ATOMIC_ACQUIRE)) {
        host_frame_requested++;
        pthread_cond_broadcast(&host_runner_cond);
        while (host_frames_drawn < host_frame_requested && !atomic_load(&host_runner_stop))
            pthread_cond_wait(&host_runner_cond, &host_runner_mutex);
    }
    pthread_mutex_unlock(&host_runner_mutex);
}
int __real_sceCommonDialogUpdate(const SceCommonDialogUpdateParam *p) {
    CHECK(p);
    host_real_common++;
    return 77;
}
int vita2d_common_dialog_update(void) {
    SceCommonDialogUpdateParam v = {0};
    /* Distinct markers expose field swaps and accidental reserved-area reads. */
    memset(v.renderTarget.reserved, 0xa5, sizeof(v.renderTarget.reserved));
    memset(v.reserved, 0x5a, sizeof(v.reserved));
    v.renderTarget.colorSurfaceData = host_nodes + 64 * host_frame_variant;
    v.renderTarget.depthSurfaceData = host_frame_variant ? NULL : host_nodes + 16;
    v.displaySyncObject = host_nodes + 32 + 64 * host_frame_variant;
    v.renderTarget.width = 960;
    v.renderTarget.height = 544;
    v.renderTarget.strideInPixels = host_frame_variant ? 960 : 1024;
    v.renderTarget.colorFormat = host_frame_variant ? 0x1000 : 0;
    v.renderTarget.surfaceType = host_frame_variant ? 1 : 0;
    switch (host_bad_target) {
    case 1: v.renderTarget.colorSurfaceData = NULL; break;
    case 2: v.displaySyncObject = NULL; break;
    case 3: v.renderTarget.width = 0; break;
    case 4: v.renderTarget.height = 0; break;
    case 5: v.renderTarget.strideInPixels = 959; break;
    case 6: v.renderTarget.strideInPixels = 4097; break;
    case 7: v.renderTarget.width = 1920; break;
    case 8: v.renderTarget.height = 1088; break;
    }
    host_expected_frame = v;
    host_source_frame = &v;
    int rc = __wrap_sceCommonDialogUpdate(&v);
    host_source_frame = NULL;
    return rc;
}
void __real_vita2d_start_drawing(void) {
    CHECK(!host_scene);
    host_scene = 1;
}
void __real_vita2d_end_drawing(void) {
    CHECK(host_scene);
    host_scene = 0;
}

int vmd_paf_widget_color(void *w, const float c[4], void *timer, int id, int opt, float delay,
                         void *cb, void *data) {
    host_owner_check();
    CHECK(w && !host_scene && !timer && id == 0x10001 && !opt && !delay && !cb && !data &&
          c[3] >= 0 && c[3] <= 1 && !((uintptr_t)c & 15));
    return 0;
}
bool vmd_paf_widget_order(void *parent, void *w, void *sibling) {
    host_owner_check();
    CHECK(parent == page && w && !sibling && !host_scene);
    return false; /* A no-op reorder is not a firmware error. */
}
int vmd_paf_text_size(void *w, int attr, unsigned start, unsigned count, float value) {
    host_owner_check();
    CHECK(w && !host_scene && attr == 1 && !start && count < 512 && value > 0);
    return 0;
}
int vmd_paf_text_color(void *w, int attr, unsigned start, unsigned count, const float c[4]) {
    host_owner_check();
    CHECK(w && !host_scene && attr == 2 && !start && count < 512 && c[3] >= 0 && c[3] <= 1 &&
          !((uintptr_t)c & 15));
    return 0;
}
int vmd_paf_text_layout_int(void *w, int attr, int value) {
    host_owner_check();
    CHECK(w && !host_scene && attr >= 0 && attr <= 2 && !value);
    return 0;
}
int vmd_paf_text_layout_bool(void *w, int attr, bool value) {
    host_owner_check();
    CHECK(w && !host_scene && (attr == 11 || attr == 13 || attr == 15) &&
          (value == 0 || value == 1));
    return attr == host_fail_layout ? -12345 : 0;
}
int vmd_paf_text_layout_vec(void *w, int attr, const VtPafVec2 *v) {
    host_owner_check();
    /* Typed contract from the firmware checks, independent of the adapter's
     * named constants: 5 is not a vector attribute on retail PAF. */
    CHECK(w && !host_scene && (attr == 6 || attr == 7 || attr == 8));
    CHECK(v && !((uintptr_t)v & 15) && v->lane[0] >= 0 && v->lane[1] >= 0 &&
          v->lane[2] == 0 && v->lane[3] == 0);
    return attr == host_fail_layout ? -12345 : 0;
}
void vmd_paf_busy_start(void *w) {
    host_owner_check();
    CHECK(w && !host_scene);
}
void vmd_paf_busy_stop(void *w) {
    host_owner_check();
    CHECK(w && !host_scene);
}
void *vmd_paf_widget_find(void *parent, const VtPafId *name, int options) {
    host_owner_check();
    CHECK(parent == page && name->hash && !options && !host_scene);
    return host_nodes + 900;
}
void __real_vita2d_set_clip_rectangle(int a, int b, int c, int d) {
    (void)a;
    (void)b;
    (void)c;
    (void)d;
}
void __real_vita2d_enable_clipping(void) {}
void __real_vita2d_disable_clipping(void) {}
void __real_vita2d_draw_rectangle(float x, float y, float w, float h, unsigned color) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
    CHECK(host_scene);
}
unsigned int vita2d_texture_get_width(const vita2d_texture *t) {
    CHECK(t);
    return 100;
}
unsigned int vita2d_texture_get_height(const vita2d_texture *t) {
    CHECK(t);
    return 100;
}
void __real_vita2d_draw_texture_scale(const vita2d_texture *t, float x, float y, float sx,
                                      float sy) {
    (void)t;
    (void)x;
    (void)y;
    (void)sx;
    (void)sy;
    CHECK(host_scene);
}
void __real_vita2d_draw_texture_part_scale(const vita2d_texture *t, float x, float y, float tx,
                                           float ty, float tw, float th, float sx, float sy) {
    (void)t;
    (void)x;
    (void)y;
    (void)tx;
    (void)ty;
    (void)tw;
    (void)th;
    (void)sx;
    (void)sy;
    CHECK(host_scene);
}
void __real_vita2d_draw_texture_scale_rotate_hotspot(const vita2d_texture *t, float x, float y,
                                                     float sx, float sy, float rad, float cx,
                                                     float cy) {
    (void)t;
    (void)x;
    (void)y;
    (void)sx;
    (void)sy;
    (void)rad;
    (void)cx;
    (void)cy;
    CHECK(host_scene);
}
static void reset(void) {
    host_stop_runner();
    if (host_paf_mutex_live) assert(!pthread_mutex_destroy(&host_paf_mutex));
    pthread_mutexattr_t attr;
    assert(!pthread_mutexattr_init(&attr));
    assert(!pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK));
    assert(!pthread_mutex_init(&host_paf_mutex, &attr));
    assert(!pthread_mutexattr_destroy(&attr));
    host_paf_mutex_live = 1;
    host_paf_locked = host_owner_valid = host_protocol_probe = 0;
    host_page_shown = host_failed_graph_draws = host_callback_wrong_thread = 0;
    host_fail_show = host_fail_hide = host_fail_alpha = 0;
    host_clock_us = 0;
    atomic_flag_clear(&mailbox_lock);
    atomic_store(&owner_callbacks, 0);
    atomic_store(&native_frame_window, 0);
    drawing_scene = host_idle_visit = 0;
    progress_since_us = last_update_us = 0;
    progress_generation = submitted_frames = 0;
    progress_started = handshake_primed = 0;
    pending_count = owner_count = 0;
    memset(pending_commands, 0, sizeof(pending_commands));
    memset(owner_commands, 0, sizeof(owner_commands));
    host_paf_task = NULL;
    atomic_store(&renderer_ready, 0);
    atomic_store(&renderer_abort, 0);
    atomic_store(&renderer_error, 0);
    atomic_store(&renderer_visible, 0);
    atomic_store(&requested_generation, 0);
    atomic_store(&applied_generation, 0);
    renderer_owner = -1;
    renderer_thread = -1;
    atomic_store(&renderer_exited, 0);
    atomic_store(&renderer_result, 0);
    atomic_store(&host_runner_stop, 0);
    atomic_store(&host_delays, 0);
    host_thread_creates = host_thread_create_error = host_thread_start_error = host_run_behavior = 0;
    host_frame_requested = host_frames_drawn = 0;
    for (unsigned i = 0; i < host_alloc_count; i++)
        free(host_allocated[i]);
    host_alloc_count = 0;
    host_checkpoints = host_persist_error = host_fail_geometry = host_fail_layout = 0;
    host_frame_variant = 0;
    host_source_frame = NULL;
    memset(&host_expected_frame, 0, sizeof(host_expected_frame));
    host_failure_code = 0;
    host_failure_operation[0] = 0;
    host_saved_theme = VT_UI_THEME_LEGACY_VITA;
    host_theme = VT_UI_THEME_LEGACY_VITA;
    host_scene = host_loaded = host_load_error = host_ctor_missing = host_foreign = 0;
    host_loads = host_updates = host_creates = host_strings = host_fail_create = host_fail_text = 0;
    host_plugin_missing = host_alloc_fail = host_real_common = host_bad_target = 0;
    host_instance = NULL;
    host_nodes_used = host_bad_text_targets = 0;
    memset(host_node_kinds, 0, sizeof(host_node_kinds));
    framework = plugin = page = NULL;
    memset(slots, 0, sizeof(slots));
    memset(commands, 0, sizeof(commands));
    memset(fw_params, 0, sizeof(fw_params));
    state = error_code = visible = updating = capturing = clip_enabled = overflow_logged = 0;
    command_count = previous_order_count = 0;
    media_text = rendered_once = 0;
}
static void fixture(void) {
    CHECK(ui_native_vita_panel(12, 60, 420, 360, 0xff502010, 0));
    CHECK(ui_native_vita_button(24, 112, 384, 42, "Jellyfin", 1));
    CHECK(ui_native_vita_text(28, 94, 340, 24, 0xffffffff, "Native title"));
}
int main(void) {
    reset();
    host_theme = VT_UI_THEME_LEGACY;
    __wrap_vita2d_start_drawing();
    CHECK(!ui_native_vita_active() && !host_loads);
    CHECK(!ui_native_vita_text(0, 30, 200, 20, 0xffffffff, "Legacy"));
    __wrap_vita2d_end_drawing();
    reset();
    host_load_error = -99;
    CHECK(!ui_native_vita_prepare() && ui_native_vita_error() == -99);
    CHECK(!ui_native_vita_prepare() && host_loads == 1);
    reset();
    host_loaded = host_foreign = 1;
    CHECK(!ui_native_vita_prepare() && !host_loads && !host_instance);
    reset();
    host_ctor_missing = 1;
    CHECK(!ui_native_vita_prepare() && !host_instance);
    reset();
    host_alloc_fail = 1;
    CHECK(!ui_native_vita_prepare() && !host_instance);
    reset();
    host_plugin_missing = 1;
    CHECK(!ui_native_vita_prepare() && ui_native_vita_error());
    reset();
    __wrap_vita2d_start_drawing();
    CHECK(ui_native_vita_active() && host_loads == 1);
    fixture();
    __wrap_vita2d_end_drawing();
    CHECK(host_updates == 2 && host_creates == 3 && host_strings == 2 && visible);
    int creates = host_creates, strings = host_strings;
    CHECK(host_checkpoints == 10);
    CHECK(host_thread_creates == 1 && renderer_running() == 1 && host_frames_drawn == 2);
    int checkpoints = host_checkpoints;
    for (int tick = 0; tick < 30; tick++) {
        __wrap_vita2d_start_drawing();
        fixture();
        __wrap_vita2d_end_drawing();
    }
    CHECK(host_creates == creates && host_strings == strings && host_checkpoints == checkpoints);
    CHECK(host_thread_creates == 1 && host_frames_drawn == host_updates);
    __wrap_vita2d_start_drawing();
    __wrap_vita2d_end_drawing();
    CHECK(!visible && host_updates == 33);
    __wrap_vita2d_start_drawing();
    __wrap_vita2d_end_drawing();
    CHECK(host_updates == 33);
    SceCommonDialogUpdateParam q = {0};
    CHECK(__wrap_sceCommonDialogUpdate(&q) == 77 && host_real_common == 1);
    __wrap_vita2d_start_drawing();
    ui_native_vita_media_text(1);
    CHECK(!ui_native_vita_text(0, 40, 200, 20, 0xffffffff, "Subtitle"));
    ui_native_vita_media_text(0);
    CHECK(ui_native_vita_button_fade(20, 80, 200, 42, "Faded", 0, .4f) &&
          fabsf(commands[0].alpha - .4f) < .001f);
    CHECK(ui_native_vita_focus(20, 80, 200, 42) && commands[0].selected);
    __wrap_vita2d_end_drawing();
    /* Source labels are copied; later modal paint clips labels behind it. */
    __wrap_vita2d_start_drawing();
    char name[] = "Immutable";
    CHECK(ui_native_vita_text(0, 40, 200, 20, 0xffffffff, name));
    strcpy(name, "Changed");
    CHECK(!strcmp(commands[0].label, "Immutable"));
    __wrap_vita2d_draw_rectangle(100, 0, 860, 544, 0xff000000);
    CHECK(commands[0].clip[2] == 100);
    CHECK(ui_native_vita_text(120, 40, 200, 20, 0xffffffff, "Modal"));
    __wrap_vita2d_end_drawing();
    CHECK(!strcmp(slots[NATIVE_TEXT][0].prior.label, "Immutable") &&
          !strcmp(slots[NATIVE_TEXT][1].prior.label, "Modal"));
    /* Clip state tracks each frame and never leaks to another page/theme. */
    __wrap_vita2d_start_drawing();
    __wrap_vita2d_set_clip_rectangle(50, 50, 350, 300);
    __wrap_vita2d_enable_clipping();
    CHECK(ui_native_vita_text(20, 80, 200, 20, 0xffffffff, "Clipped"));
    CHECK(commands[0].clip[0] == 50 && commands[0].clip[2] == 172);
    CHECK(!ui_native_vita_button(20, 50, 200, 42, "Partial target", 0));
    __wrap_vita2d_disable_clipping();
    CHECK(ui_native_vita_panel(400, 60, 200, 80, 0xffffffff, 0));
    __wrap_vita2d_draw_texture_scale((const vita2d_texture *)host_nodes, 440, 70, 1, 1);
    CHECK(commands[1].hidden);
    __wrap_vita2d_end_drawing();
    unsigned clipped_node=(unsigned)((unsigned char *)slots[NATIVE_TEXT][0].widget-host_nodes);
    CHECK(host_left[clipped_node]==50&&host_width[clipped_node]==172);
    __wrap_vita2d_start_drawing();
    CHECK(!clip_enabled);
    CHECK(ui_native_vita_text(0, 40, 200, 20, 0xffffffff, "Dimmed"));
    __wrap_vita2d_draw_rectangle(0, 0, 960, 544, 0x80000000);
    CHECK(commands[0].alpha > .49f && commands[0].alpha < .51f);
    __wrap_vita2d_end_drawing();
    /* The same typed slots survive screen changes and spinner lifetime changes. */
    for (int screen = 0; screen < 24; screen++) {
        __wrap_vita2d_start_drawing();
        for (int n = 0; n < 20; n++) {
            char label[64];
            snprintf(label, sizeof(label), "Screen %d row %d", screen, n);
            CHECK(ui_native_vita_text(40, 80 + n * 18, 400, 16, 0xffffffff, label));
        }
        CHECK(ui_native_vita_busy(800, 200, 32));
        __wrap_vita2d_end_drawing();
        CHECK(state == 1);
    }
    CHECK(host_creates <= 24);
    host_theme = VT_UI_THEME_LEGACY;
    int updates = host_updates;
    __wrap_vita2d_start_drawing();
    __wrap_vita2d_end_drawing();
    CHECK(host_updates == updates + 1 && !visible);
    __wrap_vita2d_start_drawing();
    __wrap_vita2d_end_drawing();
    CHECK(host_updates == updates + 1);
    /* Bounded queue overflow and malformed inputs cannot mutate accepted rows. */
    reset();
    __wrap_vita2d_start_drawing();
    for (unsigned i = 0; i < UI_NATIVE_TEXT_LIMIT; i++)
        CHECK(ui_native_vita_text(10, 30, 100, 20, 0xffffffff, "row"));
    CHECK(!ui_native_vita_text(10, 30, 100, 20, 0xffffffff, "overflow"));
    CHECK(command_count == UI_NATIVE_TEXT_LIMIT);
    CHECK(ui_native_vita_text(NAN, 20, 100, 20, 0xffffffff, "invalid") &&
          command_count == UI_NATIVE_TEXT_LIMIT);
    __wrap_vita2d_end_drawing();
    CHECK(state == 1);
    reset();
    host_fail_create = 1;
    __wrap_vita2d_start_drawing();
    fixture();
    __wrap_vita2d_end_drawing();
    CHECK(state == -1 && ui_native_vita_error() && host_updates == 1 && !host_failed_graph_draws);
    CHECK(host_theme == VT_UI_THEME_LEGACY && host_saved_theme == VT_UI_THEME_LEGACY);
    int failed_creates = host_creates;
    for (int i = 0; i < 3; i++) {
        __wrap_vita2d_start_drawing();
        __wrap_vita2d_end_drawing();
    }
    CHECK(host_updates == 1 && host_creates == failed_creates && !host_failed_graph_draws);
    host_theme = VT_UI_THEME_LEGACY_VITA;
    CHECK(!ui_native_vita_prepare() && host_theme == VT_UI_THEME_LEGACY);
    CHECK(host_updates == 1 && host_creates == failed_creates && !host_failed_graph_draws);
    reset();
    host_fail_text = 1;
    __wrap_vita2d_start_drawing();
    fixture();
    __wrap_vita2d_end_drawing();
    CHECK(state == -1 && host_updates == 1 && !host_failed_graph_draws && host_theme == VT_UI_THEME_LEGACY);
    reset();
    host_fail_geometry = 1;
    __wrap_vita2d_start_drawing();
    fixture();
    __wrap_vita2d_end_drawing();
    CHECK(state == -1 && host_updates == 1 && !host_failed_graph_draws && host_theme == VT_UI_THEME_LEGACY);
    reset();
    host_fail_create = 1;
    host_persist_error = -1;
    __wrap_vita2d_start_drawing();
    fixture();
    __wrap_vita2d_end_drawing();
    CHECK(state == -1 && host_updates == 1 && !host_failed_graph_draws && host_theme == VT_UI_THEME_LEGACY);
    reset();
    host_bad_target = 1;
    __wrap_vita2d_start_drawing();
    fixture();
    __wrap_vita2d_end_drawing();
    CHECK(state == -1 && !host_updates);
    /* Firmware layout failures must preserve the actual code and stop before
     * Framework::Update, for each typed stage rather than just string errors. */
    const int layout_attrs[] = {6, 7, 8, 11, 13, 15};
    const char *layout_ops[] = {"text-size-layout", "text-visible-position", "text-visible-size",
                                "text-layout-condense", "text-layout-word-wrap",
                                "text-layout-ellipsis"};
    for (unsigned n = 0; n < sizeof(layout_attrs) / sizeof(layout_attrs[0]); n++) {
        reset();
        host_fail_layout = layout_attrs[n];
        __wrap_vita2d_start_drawing();
        fixture();
        __wrap_vita2d_end_drawing();
        CHECK(state == -1 && host_updates == 1 && !host_failed_graph_draws && host_theme == VT_UI_THEME_LEGACY);
        CHECK(host_saved_theme == VT_UI_THEME_LEGACY);
        CHECK(host_failure_code == (unsigned)-12345);
        CHECK(!strcmp(host_failure_operation, layout_ops[n]));
    }
    /* A button's text object differs from Text's, including empty labels on
     * reused panel/button slots. Replay the precise console error explicitly. */
    reset();
    __wrap_vita2d_start_drawing();
    CHECK(ui_native_vita_button(24, 112, 384, 42, "First label", 1));
    CHECK(ui_native_vita_text(28, 94, 340, 24, 0xffffffff, "Plain text"));
    __wrap_vita2d_end_drawing();
    CHECK(state == 1 && !host_bad_text_targets && host_updates == 2);
    void *button_widget = slots[NATIVE_BUTTON][0].widget;
    void *text_widget = slots[NATIVE_TEXT][0].widget;
    VtPafString empty = {(const uint16_t[]){0}, 0, 0};
    host_protocol_probe = 1; /* Direct mock-only typed negative control, not production. */
    CHECK(vmd_paf_button_text(button_widget, &empty, 0) == (int)0x80af4101u);
    CHECK(vmd_paf_widget_text(text_widget, &empty, 1) == (int)0x80af4101u);
    CHECK(host_bad_text_targets == 2);
    host_protocol_probe = 0;
    int stable_creates = host_creates;
    for (int n = 0; n < 3; n++) {
        const char *labels[] = {"Updated", "", "Restored"};
        __wrap_vita2d_start_drawing();
        CHECK(ui_native_vita_button(24, 112, 384, 42, labels[n], n != 1));
        __wrap_vita2d_end_drawing();
        CHECK(state == 1 && host_updates == n + 3 && host_creates == stable_creates);
        CHECK(host_bad_text_targets == 2 && slots[NATIVE_BUTTON][0].widget == button_widget);
        CHECK(!strcmp(host_last_text, labels[n]));
    }
    /* Exercise the actual flush with both buffer descriptions, including a
     * missing depth surface, alternating sync objects and distinct format/type. */
    reset();
    for (int n = 0; n < 8; n++) {
        host_frame_variant = n & 1;
        __wrap_vita2d_start_drawing();
        fixture();
        __wrap_vita2d_end_drawing();
        CHECK(state == 1 && host_updates == n + 2 && rendered_once);
    }
    /* Invalid targets must not enter firmware; recovery retains its guard. */
    for (int n = 1; n <= 8; n++) {
        reset();
        host_bad_target = n;
        __wrap_vita2d_start_drawing();
        fixture();
        __wrap_vita2d_end_drawing();
        CHECK(state == -1 && !host_updates && !rendered_once && !updating);
        CHECK(host_theme == VT_UI_THEME_LEGACY && host_saved_theme == VT_UI_THEME_LEGACY);
    }
    reset();
    CHECK(ui_native_vita_prepare());
    updating = 1;
    CHECK(__wrap_sceCommonDialogUpdate(NULL) < 0 && !host_updates && !host_real_common);
    updating = 0;
    uint16_t text[8];
    CHECK(ui_native_vita_utf16("citt\xc3\xa0", text, 8) == 5 && text[4] == 0xe0);
    CHECK(ui_native_vita_utf16("\xf0\x9f\x8e\xb5", text, 8) == 2 && text[0] == 0xd83c &&
          text[1] == 0xdfb5);
    CHECK(ui_native_vita_utf16("\xf0\x9f\x8e\xb5", text, 2) == 0 && text[0] == 0);
    CHECK(ui_native_vita_utf16("\xc0\x80", text, 8) == 2 && text[0] == 0xfffd && text[1] == 0xfffd);
    CHECK(!ui_native_vita_utf16("x", text, 1) && !text[0]);
    CHECK(!ui_native_vita_utf16("x", NULL, 0));
    /* Reproduce the old lifecycle explicitly: Update can accept a valid
     * descriptor without Run, yet no paint occurs. The old mock conflated
     * these counters and therefore could not detect the blank native UI. */
    reset();
    host_protocol_probe = 1;
    framework = host_instance = vmd_paf_allocate(FW_BYTES);
    host_expected_frame.renderTarget = (SceCommonDialogRenderTargetInfo){
        .width = 960, .height = 544, .strideInPixels = 960,
        .colorSurfaceData = host_nodes, .depthSurfaceData = host_nodes + 16};
    host_expected_frame.displaySyncObject = host_nodes + 32;
    VtPafFrameUpdate target_only = {.pitch_pixels = 960, .width = 960, .height = 544,
        .color_data = host_nodes, .depth_data = host_nodes + 16, .display_sync = host_nodes + 32};
    vmd_paf_fw_update(framework, &target_only);
    CHECK(host_updates == 1 && host_frames_drawn == 0 && !renderer_running());
    /* Runner startup must never accept a target-only Update as a drawn frame. */
    for (int failure = 0; failure < 4; failure++) {
        reset();
        host_thread_create_error = failure == 0;
        host_thread_start_error = failure == 1;
        host_run_behavior = failure == 2 ? 1 : failure == 3 ? 2 : 0;
        CHECK(!ui_native_vita_prepare() && state == -1 && !rendered_once);
        CHECK(host_theme == VT_UI_THEME_LEGACY && !host_updates && !host_frames_drawn);
        CHECK(ui_native_vita_error() == (failure == 0 ? -88 : failure == 1 ? -89 : -0x5049));
    }
    reset();
    host_run_behavior = 3;
    CHECK(ui_native_vita_prepare() && atomic_load(&host_delays) >= 5);
    __wrap_vita2d_start_drawing();
    fixture();
    __wrap_vita2d_end_drawing();
    CHECK(host_frames_drawn == 2 && rendered_once && !atomic_load(&renderer_exited));
    host_stop_runner();
    __wrap_vita2d_start_drawing();
    CHECK(!ui_native_vita_active() && state == -1 && host_theme == VT_UI_THEME_LEGACY);
    __wrap_vita2d_end_drawing();
    CHECK(host_frames_drawn == 2 && host_updates == 2);
    /* Cold startup may signal the app before the first actual frame. The
     * bounded extra handshake must still consume the immutable generation. */
    reset();
    host_run_behavior = 4;
    __wrap_vita2d_start_drawing(); fixture(); __wrap_vita2d_end_drawing();
    CHECK(state == 1 && rendered_once && host_updates == 2 &&
          atomic_load(&applied_generation) == atomic_load(&requested_generation));
    /* A missing weak callback or wrong callback thread cannot report success. */
    for (int wrong_thread = 0; wrong_thread < 2; wrong_thread++) {
        reset();
        host_run_behavior = wrong_thread ? 0 : 5;
        __wrap_vita2d_start_drawing(); fixture();
        host_callback_wrong_thread = wrong_thread;
        __wrap_vita2d_end_drawing();
        if (!wrong_thread) {
            CHECK(state == 1 && !rendered_once && host_updates == 2 && handshake_primed);
            int checkpoints = host_checkpoints;
            for (int n = 0; n < 130 && state == 1; n++) {
                __wrap_vita2d_start_drawing(); fixture(); __wrap_vita2d_end_drawing();
            }
            CHECK(host_checkpoints == checkpoints + 1); /* fallback only, not per frame */
        }
        CHECK(state == -1 && !rendered_once && host_theme == VT_UI_THEME_LEGACY);
        CHECK(ui_native_vita_error() == (wrong_thread ? -0x504a : -0x504b));
        CHECK(!host_creates && !host_failed_graph_draws);
    }
    /* Scheduler visits are asynchronous. Skip two frames repeatedly and
     * coalesce obsolete UI states for >2 seconds without false Legacy recovery. */
    reset();
    host_run_behavior = 6;
    for (int n = 0; n < 150; n++) {
        __wrap_vita2d_start_drawing();
        char label[32]; snprintf(label, sizeof(label), "Snapshot %d", n);
        CHECK(ui_native_vita_text(20, 40, 200, 20, 0xffffffff, label));
        __wrap_vita2d_end_drawing();
        CHECK(state == 1 && host_theme == VT_UI_THEME_LEGACY_VITA);
        if (n == 0) CHECK(!rendered_once && host_checkpoints == 9);
    }
    CHECK(rendered_once && host_checkpoints == 10 && host_clock_us > NATIVE_TASK_TIMEOUT_US);
    /* Never read the capture array while native delivery is pending. */
    reset(); host_run_behavior = 5;
    __wrap_vita2d_start_drawing(); fixture(); __wrap_vita2d_end_drawing();
    CHECK(state == 1 && pending_count == 3 && owner_count == 0);
    strcpy(commands[1].label, "Reused app capture");
    CHECK(!strcmp(pending_commands[1].label, "Jellyfin"));
    /* Theme changes replace pending native data with an empty snapshot;
     * no obsolete native screen may appear later over Legacy. */
    host_theme = VT_UI_THEME_LEGACY; host_run_behavior = 0;
    __wrap_vita2d_start_drawing(); __wrap_vita2d_end_drawing();
    CHECK(!visible && !owner_count && !host_creates &&
          atomic_load(&applied_generation) == atomic_load(&requested_generation));
    int idle_updates = host_updates;
    __wrap_vita2d_start_drawing(); __wrap_vita2d_end_drawing();
    CHECK(host_updates == idle_updates);
    /* A suspended/idle process must not inherit a stale task deadline. */
    reset(); host_run_behavior = 5;
    __wrap_vita2d_start_drawing(); fixture(); __wrap_vita2d_end_drawing();
    host_clock_us += 3600000000ULL;
    __wrap_vita2d_start_drawing(); fixture(); __wrap_vita2d_end_drawing();
    CHECK(state == 1 && !rendered_once && !ui_native_vita_error());
    host_run_behavior = 0;
    __wrap_vita2d_start_drawing(); fixture(); __wrap_vita2d_end_drawing();
    CHECK(state == 1 && rendered_once);
    /* A scheduler visit during an app-owned GXM scene cannot mutate PAF. */
    reset(); host_run_behavior = 5;
    __wrap_vita2d_start_drawing(); fixture(); __wrap_vita2d_end_drawing();
    CHECK(!atomic_load(&applied_generation) && atomic_load(&requested_generation) == 1);
    __wrap_vita2d_start_drawing();
    pthread_mutex_lock(&host_runner_mutex);
    host_idle_visit = 1;
    pthread_cond_broadcast(&host_runner_cond);
    while (host_idle_visit) pthread_cond_wait(&host_runner_cond, &host_runner_mutex);
    pthread_mutex_unlock(&host_runner_mutex);
    CHECK(!host_creates && !atomic_load(&applied_generation) && host_scene);
    fixture(); host_run_behavior = 0; __wrap_vita2d_end_drawing();
    CHECK(state == 1 && rendered_once && host_creates == 3);
    /* A request to initialize inside an already-open scene is deferred. */
    reset(); host_theme = VT_UI_THEME_LEGACY;
    __wrap_vita2d_start_drawing(); host_theme = VT_UI_THEME_LEGACY_VITA;
    CHECK(!ui_native_vita_prepare() && !host_thread_creates);
    __wrap_vita2d_end_drawing();
    __wrap_vita2d_start_drawing(); CHECK(ui_native_vita_active());
    fixture(); __wrap_vita2d_end_drawing(); CHECK(rendered_once);
    /* Try-lock contention drops a stale snapshot; neither side spins. */
    reset(); __wrap_vita2d_start_drawing(); fixture();
    CHECK(!atomic_flag_test_and_set(&mailbox_lock));
    __wrap_vita2d_end_drawing();
    CHECK(state == 1 && !rendered_once && !host_creates);
    atomic_flag_clear(&mailbox_lock);
    __wrap_vita2d_start_drawing(); fixture(); __wrap_vita2d_end_drawing();
    CHECK(state == 1 && rendered_once && host_creates == 3);
    /* Native visibility/alpha failures must reach owner-side recovery, too. */
    for (int kind = 0; kind < 3; kind++) {
        reset(); CHECK(ui_native_vita_prepare());
        host_fail_show = kind == 0;
        host_fail_alpha = kind == 1;
        __wrap_vita2d_start_drawing(); fixture(); __wrap_vita2d_end_drawing();
        if (kind == 2) {
            CHECK(state == 1 && visible);
            host_fail_hide = 1;
            __wrap_vita2d_start_drawing(); __wrap_vita2d_end_drawing();
        }
        CHECK(state == -1 && host_theme == VT_UI_THEME_LEGACY &&
              ui_native_vita_error() == -0x5048);
    }
    /* Error-check mutex models the exact error class without crashing macOS. */
    reset();
    assert(!pthread_mutex_lock(&host_paf_mutex));
    host_protocol_probe = 1;
    pthread_t foreign;
    void *unlock_status;
    CHECK(!pthread_create(&foreign, NULL, host_foreign_unlock, NULL));
    CHECK(!pthread_join(foreign, &unlock_status) && (intptr_t)unlock_status == EPERM);
    CHECK(!pthread_mutex_unlock(&host_paf_mutex));
    reset();
    printf("Native PAF canvas: %u checks; lazy loading, fixed typed pools, text/style reuse, "
           "clipping, modal/media composition, scene/sync ownership, idle/theme switch, failures, "
           "UTF-16, single-owner PAF APIs, bounded asynchronous snapshots, timed progress, native setter failures and mutex ownership passed. Sony pixels remain unverified.\n",
           checks);
}
