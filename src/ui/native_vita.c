/* Native PAF presentation adapter. The firmware owns widgets, common styles,
 * text rasterization and its drawing context. The app owns navigation and data.
 * No Sony implementation or extracted artwork is packaged with this adapter. */
#include "ui/native_vita.h"
#include "ui/native_paf_abi.h"
#include "ui/theme.h"
#include "i18n/i18n.h"
#include "common/text_log.h"
#include "app_paths.h"
#include <psp2/common_dialog.h>
#include <psp2/sysmodule.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <stdatomic.h>
#include <vita2d.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ui/native_vita_resource.inc"
#if defined(__vita__)
_Static_assert(sizeof(SceCommonDialogUpdateParam) == 96 &&
                   offsetof(SceCommonDialogUpdateParam, displaySyncObject) == 60 &&
                   offsetof(SceCommonDialogRenderTargetInfo, width) == 16,
               "CommonDialog input ARM ABI");
#endif

/* Fixed ARM layouts checked with GCC 15.2 against the pinned public ABI.
 * Opaque storage avoids bringing another SDK's C++ runtime into the app. */
#define FW_BYTES 124
#define FW_PARAMS_BYTES 180
#define PLUGIN_PARAMS_BYTES 148
#define PAGE_PARAMS_BYTES 44
#define PAF_HEAP_BYTES (8u * 1024u * 1024u)
enum { NATIVE_TEXT, NATIVE_BUTTON, NATIVE_PANEL, NATIVE_BUSY, NATIVE_KINDS };
#define NATIVE_COMMANDS 512
#define NATIVE_LABEL_BYTES 512
static const unsigned limits[NATIVE_KINDS] = {UI_NATIVE_TEXT_LIMIT, UI_NATIVE_BUTTON_LIMIT,
                                              UI_NATIVE_PANEL_LIMIT, UI_NATIVE_BUSY_LIMIT};
typedef struct {
    float x, y, w, h, alpha;
    float clip[4];
    unsigned color, size;
    int kind, selected, hidden, text_align;
    unsigned identity;
    char label[NATIVE_LABEL_BYTES];
} NativeCommand;
typedef struct {
    void *widget;
    NativeCommand prior;
    int shown;
} NativeSlot;
static void *framework, *plugin, *page;
static SceUID renderer_thread = -1;
/* Only the owner creates or mutates PAF objects and enters Run. The app
 * thread records commands and supplies the documented OneFrame Update only.
 * In particular it must NEVER unlock PAF's main-thread mutex on behalf of Run. */
static atomic_int renderer_exited, renderer_result, renderer_ready, renderer_abort;
static atomic_int renderer_error, renderer_visible, native_frame_window;
static atomic_uint requested_generation, applied_generation, owner_callbacks;
/* App-private mailbox, never PAF's mutex. Try-lock copies only: no waiting,
 * allocations, widget calls or disk work while holding it. Latest frame wins. */
static atomic_flag mailbox_lock = ATOMIC_FLAG_INIT;
#define NATIVE_TASK_TIMEOUT_US 2000000ULL
#define NATIVE_ACTIVE_GAP_US 250000ULL
static uint64_t progress_since_us, last_update_us;
static unsigned progress_generation, submitted_frames;
static int progress_started, handshake_primed;
static SceUID renderer_owner = -1;
static int native_language;
static int initialize_owner(void);
static void apply_commands(void *unused);
static unsigned renderer_running(void) {
    if (!atomic_load_explicit(&renderer_ready, memory_order_acquire))
        return 0;
    return __atomic_load_n(&((VtPafFrameworkPrefix *)framework)->run_loop_count, __ATOMIC_ACQUIRE);
}
static int renderer_main(SceSize args, void *argp) {
    (void)args;
    (void)argp;
    renderer_owner = sceKernelGetThreadId();
    int rc = initialize_owner();
    if (rc > 0 && !atomic_load(&renderer_abort)) {
        vmd_paf_main_task_register(apply_commands, NULL);
        atomic_store_explicit(&renderer_ready, 1, memory_order_release);
        rc = vmd_paf_fw_run(framework);
    }
    atomic_store(&renderer_result, rc);
    atomic_store(&renderer_exited, 1);
    return rc;
}
static int start_renderer(void) {
    native_language = vt_i18n_current_language() == VT_LANG_IT ? 5 : 1;
    renderer_thread = sceKernelCreateThread("VMDNativePaf", renderer_main, 0x10000100,
                                            0x40000, 0, 0, NULL);
    if (renderer_thread < 0)
        return renderer_thread;
    int rc = sceKernelStartThread(renderer_thread, 0, NULL);
    if (rc < 0) {
        sceKernelDeleteThread(renderer_thread);
        renderer_thread = -1;
        return rc;
    }
    /* Acquire publishes the complete owner-created graph, never half an init.
     * A late owner observes abort before entering Run or applying commands;
     * retained native storage is never freed underneath an in-flight init. */
    for (unsigned attempt = 0; attempt < 5000; attempt++) {
        if (atomic_load(&renderer_exited))
            break;
        if (renderer_running())
            return 0;
        sceKernelDelayThread(1000);
    }
    atomic_store(&renderer_abort, 1);
    rc = atomic_load(&renderer_result);
    return atomic_load(&renderer_exited) && rc < 0 ? rc : -0x5049;
}
static NativeSlot slots[NATIVE_KINDS][UI_NATIVE_TEXT_LIMIT];
static NativeCommand commands[NATIVE_COMMANDS]; /* app capture only */
static NativeCommand pending_commands[NATIVE_COMMANDS]; /* mailbox copy */
static NativeCommand owner_commands[NATIVE_COMMANDS]; /* PAF owner only */
static unsigned pending_count, owner_count;
static unsigned command_count, previous_order_count;
static unsigned capture_scope, scope_counts[3][NATIVE_KINDS];
int ui_native_vita_scope(int scope) {
    int previous = (int)capture_scope;
    if (scope >= UI_NATIVE_SCOPE_CONTENT && scope <= UI_NATIVE_SCOPE_ACTIONS)
        capture_scope = (unsigned)scope;
    return previous;
}
static void *previous_order[UI_NATIVE_TEXT_LIMIT + UI_NATIVE_BUTTON_LIMIT + UI_NATIVE_PANEL_LIMIT +
                            UI_NATIVE_BUSY_LIMIT];
static int media_text, rendered_once, drawing_scene;
static int state, error_code, visible, updating, capturing, clip_enabled, overflow_logged;
static int clip_box[4] = {0, 0, 960, 544};
static uint32_t fw_params[FW_PARAMS_BYTES / 4], plugin_params[PLUGIN_PARAMS_BYTES / 4];
static void put32(void *p, unsigned at, uint32_t value) {
    memcpy((char *)p + at, &value, 4);
}
static void putptr(void *p, unsigned at, const void *value) {
    memcpy((char *)p + at, &value, sizeof(value));
}
/* Initialization can abort inside firmware, before an error can be returned.
 * Persist only these rare boundaries, respecting the disk diagnostics setting;
 * no writes are added to frame, media or playback callbacks. */
static void checkpoint(const char *stage) {
    log_printf("native PAF: init %s", stage);
    log_save(VITAMEDIADECK_SESSION_LOG_PATH);
}
static const char *failure_reason(int code) {
    switch (code) {
    case -0x5041: return "foreign-framework";
    case -0x5042: return "missing-parameter-constructor";
    case -0x5043: return "native-allocation";
    case -0x5044: return "framework-constructor";
    case -0x5045: return "plugin-load";
    case -0x5046: return "page-open";
    case -0x5047: return "initial-widget";
    case -0x5048: return "widget-update";
    case -0x5049: return "renderer-unavailable";
    case -0x504a: return "wrong-widget-thread";
    case -0x504b: return "task-no-progress";
    case -0x504c: return "invalid-frame";
    default: return "platform-initialization";
    }
}
static void log_failure(int code) {
    error_code = code;
    state = -1;
    log_printf("native PAF: unavailable 0x%08X reason=%s", (unsigned)code, failure_reason(code));
    log_printf("native PAF: recovery run=%u exited=%d result=0x%08X requested=%u applied=%u "
               "callbacks=%u frames=%u primed=%d", renderer_running(),
               atomic_load(&renderer_exited), (unsigned)atomic_load(&renderer_result),
               atomic_load(&requested_generation), atomic_load(&applied_generation),
               atomic_load(&owner_callbacks), submitted_frames, handshake_primed);
    /* A failed native graph must never be submitted again. Restore the real
     * Legacy renderer and persist it so the next launch does not retry PAF. */
    capturing = updating = 0;
    atomic_store(&renderer_abort, 1);
    ui_theme_apply(VT_UI_THEME_LEGACY);
    int saved = vt_preferences_set_interface_theme(VT_UI_THEME_LEGACY);
    if (saved < 0)
        log_printf("native PAF: cannot persist Legacy recovery 0x%08X", (unsigned)saved);
    checkpoint("fallback to Legacy");
}
unsigned ui_native_vita_utf16(const char *text, uint16_t *out, unsigned capacity) {
    unsigned used = 0;
    if (!out || !capacity)
        return 0;
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    while (*p) {
        uint32_t c = *p++;
        int extra = 0;
        if (c >= 0xc2 && c <= 0xdf) {
            c &= 31;
            extra = 1;
        } else if (c >= 0xe0 && c <= 0xef) {
            c &= 15;
            extra = 2;
        } else if (c >= 0xf0 && c <= 0xf4) {
            c &= 7;
            extra = 3;
        } else if (c >= 0x80)
            c = 0xfffd;
        int valid = 1;
        for (int i = 0; i < extra; i++) {
            if ((*p & 0xc0) != 0x80) {
                valid = 0;
                break;
            }
            c = (c << 6) | (*p++ & 63);
        }
        if (!valid || (extra == 1 && c < 0x80) || (extra == 2 && c < 0x800) ||
            (extra == 3 && c < 0x10000) || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
            c = 0xfffd;
        unsigned need = c > 0xffff ? 2 : 1;
        if (used + need >= capacity)
            break;
        if (need == 2) {
            c -= 0x10000;
            out[used++] = (uint16_t)(0xd800 + (c >> 10));
            out[used++] = (uint16_t)(0xdc00 + (c & 1023));
        } else
            out[used++] = (uint16_t)c;
    }
    out[used] = 0;
    return used;
}
static VtPafId id(const char *name) {
    VtPafId value = {{name, (uint32_t)strlen(name), 0}, 0};
    value.hash = vmd_paf_id_hash(&value, &value.name);
    return value;
}
static void loaded(void *value) {
    plugin = value;
}
static void *create(const char *type, const char *name, const char *style) {
    VtPafId n = id(name), s = id(style);
    return vmd_paf_widget_create(plugin, page, type, &n, &s);
}
static void show(void *widget, int enabled) {
    /* Public RE headers declare int, but retail Show/Hide leave the stack
     * cookie in r0 at return. Its sign is not an error status. Preserve their
     * argument ABI and discard that register at EVERY visibility boundary. */
    if (enabled) vmd_paf_widget_show(widget, NULL, 0, 0, NULL);
    else vmd_paf_widget_hide(widget, NULL, 0, 0, NULL);
}
static int place(void *widget, float x, float y, float w, float h) {
    _Alignas(16) float size[4] = {w, h, 0, 0}, pos[4] = {x + w * .5f - 480, 272 - y - h * .5f, 0, 0};
    return vmd_paf_widget_size(widget, size, NULL, 0x10004, 0, 0, NULL, NULL) < 0 ||
                   vmd_paf_widget_pos(widget, pos, NULL, 0x10000, 0, 0, NULL, NULL) < 0
               ? -1
               : 0;
}
int ui_native_vita_prepare(void) {
    if (!state && ui_theme_id() == VT_UI_THEME_LEGACY_VITA) {
        if (drawing_scene)
            return 0; /* Defer first initialization to the next pre-scene boundary. */
        int rc = start_renderer();
        if (rc < 0)
            log_failure(rc);
        else {
            state = 1;
            checkpoint("ready; single PAF owner");
        }
    }
    if (state > 0 && (atomic_load(&renderer_error) || atomic_load(&renderer_exited) ||
                      !renderer_running()))
        log_failure(atomic_load(&renderer_error) ? atomic_load(&renderer_error) : -0x5049);
    if (state < 0 && ui_theme_id() == VT_UI_THEME_LEGACY_VITA)
        log_failure(error_code);
    return state > 0;
}
/* Called once by VMDNativePaf, before Run, on the same kernel thread. */
static int initialize_owner(void) {
    /* Never initialize AppUtil/CommonDialog, replace a foreign PAF framework,
     * alter the global SDK or load a copy of Sony's firmware from app0:. */
    if (sceSysmoduleIsLoadedInternal(SCE_SYSMODULE_INTERNAL_PAF) < 0) {
        struct {
            uint32_t heap, timeout, stack, dialog, heap_opt1, heap_opt2;
        } init = {PAF_HEAP_BYTES, 60000, 0x40000, 0, 0, 0};
        /* The module heap belongs in application memory. On retail firmware the
         * entire dialog partition is 9 MiB: an 8 MiB global heap there starves
         * Framework's first 1 MiB allocation (real 3.65 crash, 2026-10-25).
         * Framework Mode_CommonDialog still selects the shared framebuffer. */
        checkpoint("module; global heap in application memory");
        int result = -1;
        SceSysmoduleOpt opt = {0};
        opt.result = &result;
        int ret = sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, sizeof(init),
                                                        &init, &opt);
        if (ret < 0 || result < 0) {
            return ret < 0 ? ret : result;
        }
    }
    if (vmd_paf_fw_instance()) {
        return -0x5041;
    }
    vmd_paf_fw_params(fw_params);
    int initialized = 0;
    for (unsigned i = 0; i < FW_PARAMS_BYTES / 4; i++)
        initialized |= fw_params[i] != 0;
    if (!initialized) {
        return -0x5042;
    } /* An unresolved weak constructor is a no-op. */
    /* Bound the graphics heaps as well; do not inherit standalone-app defaults
     * while sharing vita2d's framebuffer and the small dialog partition. */
    put32(fw_params, 0, 256u * 1024u);       /* display list */
    put32(fw_params, 4, 1024u * 1024u);      /* parameter buffer */
    put32(fw_params, 8, 1);                  /* parameter buffer in LPDDR */
    put32(fw_params, 16, 1024u * 1024u);     /* graphics main heap */
    put32(fw_params, 20, 4u * 1024u * 1024u); /* graphics video heap */
    put32(fw_params, 12, 2u * 1024u * 1024u); /* surface pool */
    put32(fw_params, 24, 1024u * 1024u);      /* text surfaces */
    put32(fw_params, 104, 960);
    put32(fw_params, 108, 544);
    put32(fw_params, 112, 5); /* Mode_CommonDialog: caller-provided framebuffer */
    put32(fw_params, 116, 1); /* OneFrameMode: the app retains its event loop */
    put32(fw_params, 168, 1); /* CommonDialog sound mode; no common SE initialization */
    put32(fw_params, 120, native_language);
    ((unsigned char *)fw_params)[128] = 0; /* no PAF controller navigation */
    ((unsigned char *)fw_params)[130] = 1; /* external app control */
    framework = vmd_paf_allocate(FW_BYTES);
    if (!framework) {
        return -0x5043;
    }
    checkpoint("framework; bounded graphics heaps");
    vmd_paf_fw_create(framework, fw_params);
    if (vmd_paf_fw_instance() != framework) {
        return -0x5044;
    }
    checkpoint("common resources");
    vmd_paf_fw_common(framework, NULL);
    vmd_paf_plugin_params(plugin_params);
    /* All parameter storage lives until process exit. Strings are allocated with
     * the native allocator; PAF never receives stack-backed plugin parameters. */
    const char *names[] = {"vmd_legacy_vita", "__main__"};
    for (int i = 0; i < 2; i++) {
        size_t n = strlen(names[i]);
        void *s = vmd_paf_allocate(n + 1);
        if (!s) {
            return -0x5043;
        }
        memcpy(s, names[i], n + 1);
        putptr(plugin_params, i * 12, s);
        put32(plugin_params, i * 12 + 4, (uint32_t)n);
    }
    putptr(plugin_params, 56, vmd_native_vita_resource);
    put32(plugin_params, 60, sizeof(vmd_native_vita_resource));
    put32(plugin_params, 64, native_language);
    checkpoint("plugin");
    vmd_paf_plugin_load(plugin_params, loaded, 1);
    if (!plugin) {
        return -0x5045;
    }
    uint32_t page_params[PAGE_PARAMS_BYTES / 4];
    vmd_paf_page_params(page_params);
    VtPafId menu = id("menu");
    checkpoint("page");
    page = vmd_paf_page_open(plugin, &menu, page_params);
    if (!page) {
        return -0x5046;
    }
    for (unsigned n = 1; n <= UI_NATIVE_BUSY_LIMIT; n++) {
        char name[40];
        snprintf(name, sizeof(name), "vmd_native_3_%u", n);
        VtPafId busy = id(name);
        void *w = vmd_paf_widget_find(page, &busy, 0);
        if (!w) {
            log_printf("native PAF: initial widget missing kind=%d slot=%u operation=busy-lookup",
                       NATIVE_BUSY, n);
            return -0x5047;
        }
        vmd_paf_busy_stop(w);
        show(w, 0);
    }
    show(page, 0);
    checkpoint("owner graph ready; entering Run");
    return 1;
}
int ui_native_vita_error(void) {
    return error_code;
}
int ui_native_vita_active(void) {
    return state == 1 && capturing && ui_theme_id() == VT_UI_THEME_LEGACY_VITA;
}
static void occlude(float x, float y, float w, float h, unsigned alpha, int media) {
    if (!ui_native_vita_active() || !alpha || w <= 2 || h <= 2)
        return;
    if (clip_enabled) {
        float right = fminf(x + w, clip_box[2]), bottom = fminf(y + h, clip_box[3]);
        x = fmaxf(x, clip_box[0]);
        y = fmaxf(y, clip_box[1]);
        w = right - x;
        h = bottom - y;
    }
    if (w <= 0 || h <= 0)
        return;
    for (unsigned i = 0; i < command_count; i++) {
        NativeCommand *c = &commands[i];
        float *v = c->clip;
        if (c->hidden || x >= v[0] + v[2] || x + w <= v[0] || y >= v[1] + v[3] || y + h <= v[1])
            continue;
        /* Media cutouts retain the immediate panel base, preventing Sony
         * button decoration from painting over artwork/camera/video pixels. */
        if (media && c->kind != NATIVE_TEXT) {
            float bottom = v[1] + v[3], start = fmaxf(v[1], y + h);
            if (bottom - start >= 24) {
                v[3] = bottom - start;
                v[1] = start;
            } else
                c->hidden = 1;
            continue;
        }
        if (alpha < 255) {
            c->alpha *= 1 - alpha / 255.f;
            continue;
        }
        float right = v[0] + v[2], bottom = v[1] + v[3];
        float rects[4][4] = {{v[0], v[1], fmaxf(0, x - v[0]), v[3]},
                             {x + w, v[1], fmaxf(0, right - x - w), v[3]},
                             {v[0], v[1], v[2], fmaxf(0, y - v[1])},
                             {v[0], y + h, v[2], fmaxf(0, bottom - y - h)}};
        int best = 0;
        for (int n = 1; n < 4; n++)
            if (rects[n][2] * rects[n][3] > rects[best][2] * rects[best][3])
                best = n;
        memcpy(v, rects[best], sizeof(c->clip));
        if (v[2] <= 0 || v[3] <= 0)
            c->hidden = 1;
    }
}
void ui_native_vita_cover(float x, float y, float w, float h, unsigned color) {
    occlude(x, y, w, h, color >> 24, 0);
}
static int enqueue(int kind, float x, float y, float w, float h, unsigned color, unsigned size,
                   const char *label, int selected) {
    if (!ui_native_vita_active())
        return 0;
    unsigned identity = 1 + capture_scope * (NATIVE_COMMANDS + 1) +
                        scope_counts[capture_scope][kind]++;
    if (!isfinite(x) || !isfinite(y) || !isfinite(w) || !isfinite(h) || w <= 0 || h <= 0)
        return 1;
    float left = fmaxf(x, clip_enabled ? clip_box[0] : 0),
          top = fmaxf(y, clip_enabled ? clip_box[1] : 0);
    float right = fminf(x + w, clip_enabled ? clip_box[2] : 960),
          bottom = fminf(y + h, clip_enabled ? clip_box[3] : 544);
    if (left >= right || top >= bottom)
        return 1;
    /* Reclaim fully hidden commands before refusing a new native widget. */
    unsigned live = 0;
    for (unsigned i = 0; i < command_count; i++)
        if (!commands[i].hidden && commands[i].kind == kind)
            live++;
    if (command_count >= NATIVE_COMMANDS || live >= limits[kind]) {
        if (!overflow_logged) {
            log_printf("native PAF: bounded command pool exhausted; immediate overflow rendering");
            overflow_logged = 1;
        }
        return 0;
    }
    NativeCommand *c = &commands[command_count++];
    *c = (NativeCommand){.x = x,
                         .y = y,
                         .w = w,
                         .h = h,
                         .alpha = kind == NATIVE_BUTTON ? (color >> 24) / 255.f : 1,
                         .clip = {left, top, right - left, bottom - top},
                         .color = color,
                         .size = size,
                         .kind = kind,
                         .selected = selected,
                         .identity = identity};
    snprintf(c->label, sizeof(c->label), "%s", label ? label : "");
    return 1;
}
int ui_native_vita_text(float x, float baseline, float width, unsigned size, unsigned color,
                        const char *text) {
    if (media_text)
        return 0;
    if (!text || !text[0])
        return ui_native_vita_active();
    unsigned lines = 1;
    for (const char *p = text; *p; p++)
        if (*p == '\n' && lines < 16)
            lines++;
    return enqueue(NATIVE_TEXT, x, baseline - size, width + 2, (size + 4) * lines + 4, color, size,
                   text, 0);
}
int ui_native_vita_button(float x, float y, float w, float h, const char *label, int selected) {
    unsigned before = command_count;
    int result = enqueue(NATIVE_BUTTON, x, y, w, h, 0xffffffff, 20, "", selected);
    if (!result || command_count == before || !label || !label[0])
        return result;
    unsigned identity = 2166136261u;
    for (const unsigned char *p = (const unsigned char *)label; *p; p++)
        identity = (identity ^ *p) * 16777619u;
    identity = capture_scope ? 0x80000000u | (capture_scope << 29) | (identity & 0x1fffffffu)
                             : commands[before].identity;
    commands[before].identity = identity;
    /* A Sony button's default label has its own font/padding rules. Keep its
     * native nine-slice, but use our bounded native Text path for the caption. */
    float padding = w > 64 ? 12 : 6;
    unsigned size = (unsigned)fmaxf(10, fminf(20, h - 12));
    unsigned caption = command_count;
    if (!enqueue(NATIVE_TEXT, x + padding, y + 4,
                 fmaxf(1, w - padding * 2), fmaxf(1, h - 8), VT_THEME_TEXT, size, label, 0)) {
        command_count = before;
        return 0;
    }
    scope_counts[capture_scope][NATIVE_TEXT]--;
    if (command_count > caption) {
        commands[caption].text_align = 1;
        commands[caption].identity = identity | 0x40000000u;
    }
    return 1;
}
int ui_native_vita_button_fade(float x, float y, float w, float h, const char *label, int selected,
                               float alpha) {
    unsigned before = command_count;
    int result = ui_native_vita_button(x, y, w, h, label, selected);
    if (result)
        for (unsigned i = before; i < command_count; i++)
            commands[i].alpha = fmaxf(0, fminf(1, alpha));
    return result;
}
void ui_native_vita_media_text(int active) {
    media_text = active;
}
int ui_native_vita_panel(float x, float y, float w, float h, unsigned color, int selected) {
    return enqueue(NATIVE_PANEL, x, y, w, h, color, 20, "", selected);
}
int ui_native_vita_focus(float x, float y, float w, float h) {
    if (!ui_native_vita_active())
        return 0;
    for (unsigned n = command_count; n > 0; n--) {
        NativeCommand *c = &commands[n - 1];
        if (!c->hidden && (c->kind == NATIVE_BUTTON || c->kind == NATIVE_PANEL) && fabsf(c->x - x) < 12 &&
            fabsf(c->y - y) < 12 && fabsf(c->w - w) < 24 && fabsf(c->h - h) < 24) {
            c->selected = 1;
            return 1;
        }
    }
    return 0;
}
int ui_native_vita_busy(float x, float y, float size) {
    return enqueue(NATIVE_BUSY, x - size * .5f, y - size * .5f, size, size, 0xffffffff, 0, "", 0);
}
static int widget_failure(const char *operation, int kind, unsigned slot, int rc) {
    /* Never log media labels, source URLs or credentials. The operation and
     * typed slot identify the firmware boundary that failed. */
    log_printf("native PAF: widget failure kind=%d slot=%u operation=%s rc=0x%08X", kind,
               slot, operation, (unsigned)rc);
    return -1;
}
static int layout_bool(void *widget, int attr, int value, const char *operation,
                       unsigned slot) {
    int rc = vmd_paf_text_layout_bool(widget, attr, value);
    return rc < 0 ? widget_failure(operation, NATIVE_TEXT, slot, rc) : 0;
}
static int layout_vec(void *widget, int attr, float x, float y, const char *operation,
                      unsigned slot) {
    VtPafVec2 value = {{x, y, 0, 0}};
    int rc = vmd_paf_text_layout_vec(widget, attr, &value);
    return rc < 0 ? widget_failure(operation, NATIVE_TEXT, slot, rc) : 0;
}
static int present(void) {
    unsigned order_count = 0;
    unsigned char used[NATIVE_KINDS][UI_NATIVE_TEXT_LIMIT] = {{0}};
    int mapping[NATIVE_COMMANDS];
    /* Reserve identities before allocating: an overlay must never steal a
     * content slot that appears later in this same owner snapshot. */
    for (unsigned i = 0; i < owner_count; i++) {
        NativeCommand *c = &owner_commands[i];
        mapping[i] = -1;
        if (c->hidden) continue;
        for (unsigned n = 0; n < limits[c->kind]; n++)
            if (!used[c->kind][n] && slots[c->kind][n].widget &&
                slots[c->kind][n].prior.identity == c->identity) {
                mapping[i] = (int)n;
                used[c->kind][n] = 1;
                break;
            }
    }
    for (unsigned i = 0; i < owner_count; i++) {
        NativeCommand *c = &owner_commands[i];
        if (c->hidden || mapping[i] >= 0) continue;
        int best = -1, score = 4;
        for (unsigned n = 0; n < limits[c->kind]; n++) {
            NativeSlot *s = &slots[c->kind][n];
            int candidate = !s->widget ? 0 : !s->shown ? 1 : 2;
            if (!used[c->kind][n] && candidate < score) {
                best = (int)n;
                score = candidate;
            }
        }
        if (best < 0) return widget_failure("identity-pool", c->kind, 0, -1);
        mapping[i] = best;
        used[c->kind][best] = 1;
    }
    void *order[UI_NATIVE_TEXT_LIMIT + UI_NATIVE_BUTTON_LIMIT + UI_NATIVE_PANEL_LIMIT +
                UI_NATIVE_BUSY_LIMIT];
    int any = 0;
    /* CreateWidget consumes case-sensitive resource factory keys, not C++ class
     * names. The shipped firmware resource/sample uses these lowercase tags. */
    const char *types[] = {"text", "button", "button", "busyindicator"};
    const char *styles[] = {"_common_default_style_text", "_common_default_style_button",
                            "_common_default_style_button", "_common_style_busyindicator_8"};
    for (unsigned i = 0; i < owner_count; i++) {
        NativeCommand *c = &owner_commands[i];
        if (c->hidden)
            continue;
        unsigned number = (unsigned)mapping[i] + 1;
        NativeSlot *slot = &slots[c->kind][number - 1];
        int fresh = !slot->widget;
        if (fresh) {
            char name[48];
            snprintf(name, sizeof(name), "vmd_native_%d_%u", c->kind, number);
            if (c->kind == NATIVE_BUSY) {
                VtPafId busy = id(name);
                slot->widget = vmd_paf_widget_find(page, &busy, 0);
            } else
                slot->widget = create(types[c->kind], name, styles[c->kind]);
            if (!slot->widget)
                return widget_failure("create", c->kind, number, -1);
        }
        void *w = slot->widget;
        NativeCommand *old = &slot->prior;
        if (fresh || old->x != c->x || old->y != c->y || old->w != c->w || old->h != c->h ||
            memcmp(old->clip, c->clip, sizeof(c->clip))) {
            /* The native text widget occupies the physical visible window.
             * Logical size/visible position below preserve its original text
             * layout while scrolling clips through a header or a later modal. */
            const float *v = c->clip;
            int rc = place(w, v[0], v[1], v[2], v[3]);
            if (rc < 0)
                return widget_failure("geometry", c->kind, number, rc);
        }
        int text_changed = fresh || strcmp(old->label, c->label);
        if (c->kind == NATIVE_TEXT || c->kind == NATIVE_BUTTON || c->kind == NATIVE_PANEL) {
            uint16_t text[NATIVE_LABEL_BYTES];
            unsigned count = ui_native_vita_utf16(c->label, text, NATIVE_LABEL_BYTES);
            if (text_changed) {
                VtPafString value = {text, count, 0};
                int rc = c->kind != NATIVE_TEXT
                             ? vmd_paf_button_text(w, &value, VMD_PAF_BUTTON_LABEL_OBJECT)
                             : vmd_paf_widget_text(w, &value, VMD_PAF_TEXT_OBJECT);
                if (rc < 0)
                    return widget_failure("string", c->kind, number, rc);
            }
            if (c->kind == NATIVE_TEXT) {
                if (fresh || old->size != c->size || text_changed)
                    if (vmd_paf_text_size(w, 1, 0, count, (float)c->size) < 0)
                        return widget_failure("text-size", c->kind, number, -1);
                if (fresh || old->color != c->color || old->alpha != c->alpha || text_changed) {
                    _Alignas(16) float color[4] = {(c->color & 255) / 255.f, ((c->color >> 8) & 255) / 255.f,
                                      ((c->color >> 16) & 255) / 255.f,
                                      ((c->color >> 24) & 255) / 255.f * c->alpha};
                    if (vmd_paf_text_color(w, 2, 0, count, color) < 0)
                        return widget_failure("text-color", c->kind, number, -1);
                }
                if (fresh || old->text_align != c->text_align) {
                    for (int attr = VMD_PAF_LAYOUT_ALIGN_X; attr <= VMD_PAF_LAYOUT_TEXT_ALIGN;
                         attr++) {
                        int rc = vmd_paf_text_layout_int(w, attr,
                            attr == VMD_PAF_LAYOUT_ALIGN_Y || attr == VMD_PAF_LAYOUT_TEXT_ALIGN
                                ? c->text_align : 0);
                        if (rc < 0)
                            return widget_failure("text-layout-align", c->kind, number, rc);
                    }
                    if (layout_bool(w, VMD_PAF_LAYOUT_WORD_WRAP, 0, "text-layout-word-wrap",
                                    number) < 0 ||
                        layout_bool(w, VMD_PAF_LAYOUT_ELLIPSIS, 0, "text-layout-ellipsis",
                                    number) < 0 ||
                        layout_bool(w, VMD_PAF_LAYOUT_CONDENSE_X, 1, "text-layout-condense",
                                    number) < 0)
                        return -1;
                }
                if (fresh || old->w != c->w || old->h != c->h) {
                    if (layout_vec(w, VMD_PAF_LAYOUT_SIZE, c->w, c->h, "text-size-layout",
                                   number) < 0)
                        return -1;
                }
                if (fresh || memcmp(old->clip, c->clip, sizeof(c->clip)) || old->x != c->x ||
                    old->y != c->y) {
                    if (layout_vec(w, VMD_PAF_LAYOUT_VISIBLE_POS, c->clip[0] - c->x,
                                   c->clip[1] - c->y, "text-visible-position", number) < 0 ||
                        layout_vec(w, VMD_PAF_LAYOUT_VISIBLE_SIZE, c->clip[2], c->clip[3],
                                   "text-visible-size", number) < 0)
                        return -1;
                }
            } else if (fresh || old->selected != c->selected)
                vmd_paf_button_state(w, c->selected ? 2 : 0);
        }
        if (c->kind == NATIVE_PANEL && (fresh || old->color != c->color)) {
            _Alignas(16) float color[4] = {(c->color & 255) / 255.f, ((c->color >> 8) & 255) / 255.f,
                              ((c->color >> 16) & 255) / 255.f, ((c->color >> 24) & 255) / 255.f};
            if (vmd_paf_widget_color(w, color, NULL, 0x10001, 0, 0, NULL, NULL) < 0)
                return widget_failure("panel-color", c->kind, number, -1);
        }
        order[order_count++] = w;
        if (!slot->shown) {
            show(w, 1);
            if (c->kind == NATIVE_BUSY)
                vmd_paf_busy_start(w);
        }
        /* Show may reset the widget's show alpha: apply the requested fade
         * afterwards, including reused widgets becoming visible again. */
        if (c->kind != NATIVE_TEXT && (fresh || !slot->shown || old->alpha != c->alpha)) {
            int rc = vmd_paf_widget_alpha(w, c->alpha, NULL, 0x10002, 0, 0, NULL);
            if (rc < 0)
                return widget_failure("alpha", c->kind, number, rc);
        }
        slot->shown = 1;
        slot->prior = *c;
        any = 1;
    }
    for (int kind = 0; kind < NATIVE_KINDS; kind++)
        for (unsigned n = 0; n < limits[kind]; n++) {
            NativeSlot *s = &slots[kind][n];
            if (s->shown && !used[kind][n]) {
                show(s->widget, 0);
                if (kind == NATIVE_BUSY)
                    vmd_paf_busy_stop(s->widget);
                s->shown = 0;
            }
        }
    if (order_count != previous_order_count ||
        memcmp(order, previous_order, order_count * sizeof(void *))) {
        /* ReorderChild returns a bool, not an errno. False may mean no move. */
        for (unsigned n = 0; n < order_count; n++)
            vmd_paf_widget_order(page, order[n], NULL);
        memcpy(previous_order, order, order_count * sizeof(void *));
        previous_order_count = order_count;
    }
    if (any) {
        if (!visible) show(page, 1);
        visible = 1;
        return 1;
    }
    if (visible) {
        show(page, 0);
        visible = 0;
        return 1;
    }
    return 0;
}
/* A PAF main-thread task is scheduler work, not a per-Update completion
 * fence. Preserve independent app/mailbox/owner snapshots across delayed or
 * skipped visits. Neither side waits for the other's copy/PAF mutations. */
static void apply_commands(void *unused) {
    (void)unused;
    atomic_fetch_add(&owner_callbacks, 1);
    if (atomic_load(&renderer_error) || atomic_load(&renderer_abort) ||
        !atomic_load_explicit(&native_frame_window, memory_order_acquire))
        return; /* Scheduler visits outside the app's post-scene flush are idle. */
    if (sceKernelGetThreadId() != renderer_owner) {
        atomic_store(&renderer_error, -0x504a);
        return;
    }
    if (atomic_flag_test_and_set_explicit(&mailbox_lock, memory_order_acquire))
        return;
    unsigned generation = atomic_load_explicit(&requested_generation, memory_order_relaxed);
    if (generation == atomic_load_explicit(&applied_generation, memory_order_relaxed)) {
        atomic_flag_clear_explicit(&mailbox_lock, memory_order_release);
        return;
    }
    owner_count = pending_count;
    memcpy(owner_commands, pending_commands, owner_count * sizeof(*owner_commands));
    atomic_flag_clear_explicit(&mailbox_lock, memory_order_release);
    int rc = present();
    if (rc < 0) {
        show(page, 0);
        visible = 0;
        atomic_store(&renderer_error, -0x5048);
    }
    atomic_store(&renderer_visible, visible);
    atomic_store_explicit(&applied_generation, generation, memory_order_release);
}
/* Copy publication only. The app's next capture never aliases owner memory.
 * A busy mailbox drops this obsolete UI snapshot, not a playback operation. */
static void publish_commands(void) {
    if (atomic_flag_test_and_set_explicit(&mailbox_lock, memory_order_acquire))
        return;
    pending_count = command_count;
    memcpy(pending_commands, commands, pending_count * sizeof(*pending_commands));
    atomic_fetch_add_explicit(&requested_generation, 1, memory_order_release);
    atomic_flag_clear_explicit(&mailbox_lock, memory_order_release);
}
static int task_progress(void) {
    unsigned applied = atomic_load_explicit(&applied_generation, memory_order_acquire);
    unsigned requested = atomic_load(&requested_generation);
    uint64_t now = sceKernelGetProcessTimeWide();
    int inactive = !progress_started || now < last_update_us ||
                   now - last_update_us > NATIVE_ACTIVE_GAP_US;
    last_update_us = now;
    if (inactive || applied != progress_generation || applied == requested) {
        progress_started = 1;
        progress_generation = applied;
        progress_since_us = now;
        return 0;
    }
    /* Time without progress during active updates, not frame-count equality.
     * A suspend/idle gap starts a fresh window. No sleeps, extra firmware
     * frames or disk writes for a normal delayed task. */
    return now >= progress_since_us && now - progress_since_us >= NATIVE_TASK_TIMEOUT_US
               ? -0x504b : 0;
}
/* libvita2d supplies its real framebuffer stride and sync object. Translate
 * its CommonDialog format into PAF's smaller OnUpdateData format. The wrapper
 * runs during the explicit flush after sceGxmEndScene, before the display swap. */
int __real_sceCommonDialogUpdate(const SceCommonDialogUpdateParam *params);
int __wrap_sceCommonDialogUpdate(const SceCommonDialogUpdateParam *params) {
    if (!updating)
        return __real_sceCommonDialogUpdate(params);
    if (!params || !params->renderTarget.colorSurfaceData || !params->displaySyncObject)
        return -0x504c;
    const SceCommonDialogRenderTargetInfo *target = &params->renderTarget;
    /* The app and its PAF environment use a fixed 960x544 output. Reject a
     * malformed target before firmware can assert inside render-target setup. */
    if (target->width != 960 || target->height != 544 ||
        target->strideInPixels < target->width || target->strideInPixels > 4096) {
        log_printf("native PAF: rejected frame width=%u height=%u pitch=%u", target->width,
                   target->height, target->strideInPixels);
        return -0x504c;
    }
    VtPafFrameUpdate frame = {.pitch_pixels = target->strideInPixels,
                              .width = target->width,
                              .height = target->height,
                              .color_format = target->colorFormat,
                              .surface_type = target->surfaceType,
                              .color_data = target->colorSurfaceData,
                              .depth_data = target->depthSurfaceData,
                              .display_sync = params->displaySyncObject};
    if (!handshake_primed) {
        log_printf("native PAF: frame width=%u height=%u pitch=%u format=0x%08X type=0x%08X depth=%d",
                   frame.width, frame.height, frame.pitch_pixels, frame.color_format,
                   frame.surface_type, frame.depth_data != NULL);
        log_save(VITAMEDIADECK_SESSION_LOG_PATH);
    }
    if (atomic_load(&renderer_exited) || !renderer_running())
        return -0x5049;
    /* A rejected target cannot publish commands even during early Run visits. */
    atomic_store_explicit(&native_frame_window, 1, memory_order_release);
    publish_commands();
    submitted_frames++;
    vmd_paf_fw_update(framework, &frame);
    /* Prime Run's initial condition signal once, separately from task delivery. */
    if (!handshake_primed && !atomic_load(&renderer_exited) && !atomic_load(&renderer_error))
        vmd_paf_fw_update(framework, &frame);
    atomic_store_explicit(&native_frame_window, 0, memory_order_release);
    handshake_primed = 1;
    if (atomic_load(&renderer_error))
        return atomic_load(&renderer_error);
    if (atomic_load(&renderer_exited) || !renderer_running())
        return -0x5049;
    return task_progress();
}
void __real_vita2d_start_drawing(void);
void __wrap_vita2d_start_drawing(void) {
    capturing = 0;
    if (ui_theme_id() == VT_UI_THEME_LEGACY_VITA)
        ui_native_vita_prepare();
    command_count = 0;
    capture_scope = UI_NATIVE_SCOPE_CONTENT;
    memset(scope_counts, 0, sizeof(scope_counts));
    media_text = 0;
    clip_enabled = 0;
    clip_box[0] = clip_box[1] = 0;
    clip_box[2] = 960;
    clip_box[3] = 544;
    capturing = state == 1 && ui_theme_id() == VT_UI_THEME_LEGACY_VITA;
    __real_vita2d_start_drawing();
    drawing_scene = 1;
}
void __real_vita2d_end_drawing(void);
void __wrap_vita2d_end_drawing(void) {
    __real_vita2d_end_drawing();
    drawing_scene = 0;
    capturing = 0;
    if (state != 1)
        return;
    if (!command_count && !atomic_load(&renderer_visible) &&
        atomic_load(&requested_generation) == atomic_load(&applied_generation))
        return;
    if (atomic_load(&renderer_error) || atomic_load(&renderer_exited)) {
        log_failure(atomic_load(&renderer_error) ? atomic_load(&renderer_error) : -0x5049);
        return;
    }
    if (!handshake_primed)
        checkpoint("first-frame render; owner mailbox publication");
    updating = 1;
    int rc = vita2d_common_dialog_update();
    updating = 0;
    if (rc < 0)
        log_failure(rc);
    else if (!rendered_once && atomic_load_explicit(&applied_generation, memory_order_acquire)) {
        rendered_once = 1;
        checkpoint("first-frame complete; owner snapshot acknowledged");
    }
}

/* Media frames remain in their existing GPU path; only geometry is observed. */
void __real_vita2d_set_clip_rectangle(int, int, int, int);
void __wrap_vita2d_set_clip_rectangle(int a, int b, int c, int d) {
    clip_box[0] = a;
    clip_box[1] = b;
    clip_box[2] = c;
    clip_box[3] = d;
    __real_vita2d_set_clip_rectangle(a, b, c, d);
}
void __real_vita2d_enable_clipping(void);
void __wrap_vita2d_enable_clipping(void) {
    clip_enabled = 1;
    __real_vita2d_enable_clipping();
}
void __real_vita2d_disable_clipping(void);
void __wrap_vita2d_disable_clipping(void) {
    clip_enabled = 0;
    __real_vita2d_disable_clipping();
}
void __real_vita2d_draw_rectangle(float, float, float, float, unsigned);
void __wrap_vita2d_draw_rectangle(float x, float y, float w, float h, unsigned color) {
    occlude(x, y, w, h, color >> 24, 0);
    __real_vita2d_draw_rectangle(x, y, w, h, color);
}
void __real_vita2d_draw_texture_scale(const vita2d_texture *, float, float, float, float);
void __wrap_vita2d_draw_texture_scale(const vita2d_texture *t, float x, float y, float sx,
                                      float sy) {
    occlude(x, y, vita2d_texture_get_width(t) * sx, vita2d_texture_get_height(t) * sy, 255, 1);
    __real_vita2d_draw_texture_scale(t, x, y, sx, sy);
}
void __real_vita2d_draw_texture_part_scale(const vita2d_texture *, float, float, float, float,
                                           float, float, float, float);
void __wrap_vita2d_draw_texture_part_scale(const vita2d_texture *t, float x, float y, float tx,
                                           float ty, float tw, float th, float sx, float sy) {
    occlude(x, y, tw * sx, th * sy, 255, 1);
    __real_vita2d_draw_texture_part_scale(t, x, y, tx, ty, tw, th, sx, sy);
}
void __real_vita2d_draw_texture_scale_rotate_hotspot(const vita2d_texture *, float, float, float,
                                                     float, float, float, float);
void __wrap_vita2d_draw_texture_scale_rotate_hotspot(const vita2d_texture *t, float x, float y,
                                                     float sx, float sy, float rad, float cx,
                                                     float cy) {
    float w = vita2d_texture_get_width(t) * sx, h = vita2d_texture_get_height(t) * sy;
    float cr = cosf(rad), sr = sinf(rad), minx = 960, maxx = 0, miny = 544, maxy = 0;
    for (int corner = 0; corner < 4; corner++) {
        float px = ((corner & 1) ? w : 0) - cx * sx, py = ((corner & 2) ? h : 0) - cy * sy;
        float dx = x + px * cr - py * sr, dy = y + px * sr + py * cr;
        minx = fminf(minx, dx);
        maxx = fmaxf(maxx, dx);
        miny = fminf(miny, dy);
        maxy = fmaxf(maxy, dy);
    }
    occlude(minx, miny, maxx - minx, maxy - miny, 255, 1);
    __real_vita2d_draw_texture_scale_rotate_hotspot(t, x, y, sx, sy, rad, cx, cy);
}
