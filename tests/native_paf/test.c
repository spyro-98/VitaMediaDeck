/* Actual production PAF adapter, with firmware boundaries supplied by mocks.
 * This verifies ownership/protocol/lifecycle; it does not render Sony widgets. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../../src/ui/native_vita.c"
static unsigned checks;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        assert(x);                                                                                 \
    } while (0)
static int host_theme, host_scene, host_loaded, host_load_error, host_ctor_missing, host_foreign;
static int host_checkpoints;
static int host_loads, host_updates, host_creates, host_strings, host_fail_create, host_fail_text;
static int host_plugin_missing, host_alloc_fail, host_real_common, host_bad_target;
static void *host_instance, *host_allocated[32];
static unsigned host_alloc_count;
static unsigned char host_nodes[1024];
static float host_left[1024],host_width[1024];
static int host_nodes_used;
static char host_last_text[512];
int ui_theme_id(void) {
    return host_theme;
}
int vt_i18n_current_language(void) {
    return VT_LANG_IT;
}
void log_printf(const char *format, ...) {
    CHECK(format && !strstr(format, "password"));
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
    if (host_alloc_fail)
        return NULL;
    CHECK(host_alloc_count < 32);
    void *p = calloc(1, n);
    CHECK(p);
    host_allocated[host_alloc_count++] = p;
    return p;
}
void vmd_paf_fw_params(void *p) {
    CHECK(!host_scene);
    memset(p, 0, 180);
    if (!host_ctor_missing)
        ((uint32_t *)p)[0] = 0x40000;
}
void *vmd_paf_fw_instance(void) {
    return host_foreign ? (void *)host_nodes : host_instance;
}
void vmd_paf_fw_create(void *self, const void *p) {
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
}
void vmd_paf_fw_common(void *self, void (*done)(void)) {
    CHECK(self == host_instance && !host_scene && !done);
}
void vmd_paf_plugin_params(void *p) {
    memset(p, 0, 148);
}
void vmd_paf_plugin_load(const void *p, void (*done)(void *), int options) {
    CHECK(p && options == 1 && !host_scene);
    done(host_plugin_missing ? NULL : host_nodes);
}
void vmd_paf_page_params(void *p) {
    memset(p, 0, 44);
}
void *vmd_paf_page_open(void *self, const VtPafId *which, const void *p) {
    CHECK(self == host_nodes && which->hash && p);
    return host_nodes + 1;
}
uint32_t vmd_paf_id_hash(VtPafId *self, const VtPafString *name) {
    CHECK(self && name->data && name->count);
    return name->count;
}
void *vmd_paf_widget_create(void *self, void *parent, const char *type, const VtPafId *name,
                            const VtPafId *style) {
    CHECK(self == host_nodes && parent == host_nodes + 1 && type && name->hash && style->hash);
    host_creates++;
    if (host_fail_create)
        return NULL;
    CHECK(host_nodes_used < 1000);
    return host_nodes + 2 + host_nodes_used++;
}
int vmd_paf_widget_pos(void *self, const float v[4], void *timer, int id, int opt, float delay,
                       void *cb, void *data) {
    CHECK(self && !host_scene && !timer && !opt && !delay && !cb && !data && id == 0x10000);
    CHECK(isfinite(v[0]) && isfinite(v[1]));
    unsigned node=(unsigned)((unsigned char *)self-host_nodes);
    host_left[node]=v[0]+480-host_width[node]*.5f;
    return 0;
}
int vmd_paf_widget_size(void *self, const float v[4], void *timer, int id, int opt, float delay,
                        void *cb, void *data) {
    CHECK(self && !host_scene && !timer && !opt && !delay && !cb && !data && id == 0x10004);
    CHECK(v[0] > 0 && v[1] > 0);
    host_width[(unsigned char *)self-host_nodes]=v[0];
    return 0;
}
int vmd_paf_widget_show(void *self, void *timer, int opt, float delay, void *cb) {
    CHECK(self && !timer && opt == 0 && !delay && !cb && !host_scene);
    return 0;
}
int vmd_paf_widget_hide(void *self, void *timer, int opt, float delay, void *cb) {
    CHECK(self && !timer && opt == 0 && !delay && !cb && !host_scene);
    return 0;
}
int vmd_paf_widget_alpha(void *self, float alpha, void *timer, int id, int opt, float delay,
                         void *cb) {
    CHECK(self && alpha >= 0 && alpha <= 1 && !timer && id == 0x10002 && !opt && !delay && !cb &&
          !host_scene);
    return 0;
}
static int set_text(void *self, const VtPafString *text, int obj) {
    CHECK(self && text && obj == 0 && !host_scene);
    host_strings++;
    const uint16_t *v = text->data;
    for (unsigned i = 0; i < text->count && i < sizeof(host_last_text) - 1; i++)
        host_last_text[i] = v[i] < 128 ? (char)v[i] : '?';
    host_last_text[text->count] = 0;
    return host_fail_text ? -1 : 0;
}
int vmd_paf_button_text(void *self, const VtPafString *text, int obj) {
    return set_text(self, text, obj);
}
int vmd_paf_widget_text(void *self, const VtPafString *text, int obj) {
    return set_text(self, text, obj);
}
uint32_t vmd_paf_button_state(void *self, int s) {
    CHECK(self && (s == 0 || s == 2) && !host_scene);
    return 0;
}
void vmd_paf_fw_update(void *self, void *p) {
    const SceCommonDialogUpdateParam *v = p;
    CHECK(self == host_instance && !host_scene);
    CHECK(v->renderTarget.colorSurfaceData == host_nodes &&
          v->displaySyncObject == host_nodes + 1 && v->renderTarget.strideInPixels == 1024);
    host_updates++;
}
int __real_sceCommonDialogUpdate(const SceCommonDialogUpdateParam *p) {
    CHECK(p);
    host_real_common++;
    return 77;
}
int vita2d_common_dialog_update(void) {
    SceCommonDialogUpdateParam v = {0};
    v.renderTarget.colorSurfaceData = host_bad_target ? NULL : host_nodes;
    v.displaySyncObject = host_nodes + 1;
    v.renderTarget.width = 960;
    v.renderTarget.height = 544;
    v.renderTarget.strideInPixels = 1024;
    return __wrap_sceCommonDialogUpdate(&v);
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
    CHECK(w && !host_scene && !timer && id == 0x10001 && !opt && !delay && !cb && !data &&
          c[3] >= 0 && c[3] <= 1);
    return 0;
}
int vmd_paf_widget_order(void *parent, void *w, void *sibling) {
    CHECK(parent == page && w && !sibling && !host_scene);
    return 1;
}
int vmd_paf_text_size(void *w, int attr, unsigned start, unsigned count, float value) {
    CHECK(w && !host_scene && attr == 1 && !start && count < 512 && value > 0);
    return 0;
}
int vmd_paf_text_color(void *w, int attr, unsigned start, unsigned count, const float c[4]) {
    CHECK(w && !host_scene && attr == 2 && !start && count < 512 && c[3] >= 0 && c[3] <= 1);
    return 0;
}
int vmd_paf_text_layout_int(void *w, int attr, int value) {
    CHECK(w && !host_scene && attr >= 0 && attr <= 2 && !value);
    return 0;
}
int vmd_paf_text_layout_bool(void *w, int attr, int value) {
    CHECK(w && !host_scene && (attr == 10 || attr == 12 || attr == 14) &&
          (value == 0 || value == 1));
    return 0;
}
int vmd_paf_text_layout_vec(void *w, int attr, const float v[2]) {
    CHECK(w && !host_scene && (attr == 5 || attr == 6 || attr == 7) && v[0] >= 0 && v[1] >= 0);
    return 0;
}
void vmd_paf_busy_start(void *w) {
    CHECK(w && !host_scene);
}
void vmd_paf_busy_stop(void *w) {
    CHECK(w && !host_scene);
}
void *vmd_paf_widget_find(void *parent, const VtPafId *name, int options) {
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
    for (unsigned i = 0; i < host_alloc_count; i++)
        free(host_allocated[i]);
    host_alloc_count = 0;
    host_checkpoints = 0;
    host_theme = VT_UI_THEME_LEGACY_VITA;
    host_scene = host_loaded = host_load_error = host_ctor_missing = host_foreign = 0;
    host_loads = host_updates = host_creates = host_strings = host_fail_create = host_fail_text = 0;
    host_plugin_missing = host_alloc_fail = host_real_common = host_bad_target = 0;
    host_instance = NULL;
    host_nodes_used = 0;
    framework = plugin = page = NULL;
    memset(slots, 0, sizeof(slots));
    memset(commands, 0, sizeof(commands));
    memset(fw_params, 0, sizeof(fw_params));
    state = error_code = visible = updating = capturing = clip_enabled = overflow_logged = 0;
    command_count = previous_order_count = 0;
    media_text = 0;
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
    CHECK(host_updates == 1 && host_creates == 3 && host_strings == 2 && visible);
    int creates = host_creates, strings = host_strings;
    CHECK(host_checkpoints == 6);
    int checkpoints = host_checkpoints;
    for (int tick = 0; tick < 30; tick++) {
        __wrap_vita2d_start_drawing();
        fixture();
        __wrap_vita2d_end_drawing();
    }
    CHECK(host_creates == creates && host_strings == strings && host_checkpoints == checkpoints);
    __wrap_vita2d_start_drawing();
    __wrap_vita2d_end_drawing();
    CHECK(!visible && host_updates == 32);
    __wrap_vita2d_start_drawing();
    __wrap_vita2d_end_drawing();
    CHECK(host_updates == 32);
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
    CHECK(state == -1 && ui_native_vita_error());
    reset();
    host_bad_target = 1;
    __wrap_vita2d_start_drawing();
    fixture();
    __wrap_vita2d_end_drawing();
    CHECK(state == -1 && !host_updates);
    uint16_t text[8];
    CHECK(ui_native_vita_utf16("citt\xc3\xa0", text, 8) == 5 && text[4] == 0xe0);
    CHECK(ui_native_vita_utf16("\xf0\x9f\x8e\xb5", text, 8) == 2 && text[0] == 0xd83c &&
          text[1] == 0xdfb5);
    CHECK(ui_native_vita_utf16("\xf0\x9f\x8e\xb5", text, 2) == 0 && text[0] == 0);
    CHECK(ui_native_vita_utf16("\xc0\x80", text, 8) == 2 && text[0] == 0xfffd && text[1] == 0xfffd);
    CHECK(!ui_native_vita_utf16("x", text, 1) && !text[0]);
    CHECK(!ui_native_vita_utf16("x", NULL, 0));
    reset();
    printf("Native PAF canvas: %u checks; lazy loading, fixed typed pools, text/style reuse, "
           "clipping, modal/media composition, scene/sync ownership, idle/theme switch, failures "
           "and UTF-16 passed. Sony firmware rendering remains unverified.\n",
           checks);
}
