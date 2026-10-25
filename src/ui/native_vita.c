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
#include <vita2d.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ui/native_vita_resource.inc"

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
    int kind, selected, hidden;
    char label[NATIVE_LABEL_BYTES];
} NativeCommand;
typedef struct {
    void *widget;
    NativeCommand prior;
    int shown;
} NativeSlot;
static void *framework, *plugin, *page;
static NativeSlot slots[NATIVE_KINDS][UI_NATIVE_TEXT_LIMIT];
static NativeCommand commands[NATIVE_COMMANDS];
static unsigned command_count, previous_order_count;
static void *previous_order[UI_NATIVE_TEXT_LIMIT + UI_NATIVE_BUTTON_LIMIT + UI_NATIVE_PANEL_LIMIT +
                            UI_NATIVE_BUSY_LIMIT];
static int media_text;
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
static void log_failure(int code) {
    error_code = code;
    state = -1;
    log_printf("native PAF: unavailable 0x%08X", (unsigned)code);
    checkpoint("fallback");
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
    if (enabled)
        vmd_paf_widget_show(widget, NULL, 0, 0, NULL);
    else
        vmd_paf_widget_hide(widget, NULL, 0, 0, NULL);
}
static int place(void *widget, float x, float y, float w, float h) {
    float size[4] = {w, h, 0, 0}, pos[4] = {x + w * .5f - 480, 272 - y - h * .5f, 0, 0};
    return vmd_paf_widget_size(widget, size, NULL, 0x10004, 0, 0, NULL, NULL) < 0 ||
                   vmd_paf_widget_pos(widget, pos, NULL, 0x10000, 0, 0, NULL, NULL) < 0
               ? -1
               : 0;
}
int ui_native_vita_prepare(void) {
    if (state)
        return state > 0;
    if (ui_theme_id() != VT_UI_THEME_LEGACY_VITA)
        return 0;
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
            log_failure(ret < 0 ? ret : result);
            return 0;
        }
    }
    if (vmd_paf_fw_instance()) {
        log_failure(-0x5041);
        return 0;
    }
    vmd_paf_fw_params(fw_params);
    int initialized = 0;
    for (unsigned i = 0; i < FW_PARAMS_BYTES / 4; i++)
        initialized |= fw_params[i] != 0;
    if (!initialized) {
        log_failure(-0x5042);
        return 0;
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
    put32(fw_params, 120, vt_i18n_current_language() == VT_LANG_IT ? 5 : 1);
    ((unsigned char *)fw_params)[128] = 0; /* no PAF controller navigation */
    ((unsigned char *)fw_params)[130] = 1; /* external app control */
    framework = vmd_paf_allocate(FW_BYTES);
    if (!framework) {
        log_failure(-0x5043);
        return 0;
    }
    checkpoint("framework; bounded graphics heaps");
    vmd_paf_fw_create(framework, fw_params);
    if (vmd_paf_fw_instance() != framework) {
        log_failure(-0x5044);
        return 0;
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
            log_failure(-0x5043);
            return 0;
        }
        memcpy(s, names[i], n + 1);
        putptr(plugin_params, i * 12, s);
        put32(plugin_params, i * 12 + 4, (uint32_t)n);
    }
    putptr(plugin_params, 56, vmd_native_vita_resource);
    put32(plugin_params, 60, sizeof(vmd_native_vita_resource));
    put32(plugin_params, 64, vt_i18n_current_language() == VT_LANG_IT ? 5 : 1);
    checkpoint("plugin");
    vmd_paf_plugin_load(plugin_params, loaded, 1);
    if (!plugin) {
        log_failure(-0x5045);
        return 0;
    }
    uint32_t page_params[PAGE_PARAMS_BYTES / 4];
    vmd_paf_page_params(page_params);
    VtPafId menu = id("menu");
    checkpoint("page");
    page = vmd_paf_page_open(plugin, &menu, page_params);
    if (!page) {
        log_failure(-0x5046);
        return 0;
    }
    for (unsigned n = 1; n <= UI_NATIVE_BUSY_LIMIT; n++) {
        char name[40];
        snprintf(name, sizeof(name), "vmd_native_3_%u", n);
        VtPafId busy = id(name);
        void *w = vmd_paf_widget_find(page, &busy, 0);
        if (!w) {
            log_failure(-0x5047);
            return 0;
        }
        vmd_paf_busy_stop(w);
        show(w, 0);
    }
    show(page, 0);
    state = 1;
    checkpoint("ready");
    log_printf("native PAF: ready; Sony widgets; global heap=%u surface=%u text=%u", PAF_HEAP_BYTES,
               fw_params[3], fw_params[6]);
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
            c->hidden = 1;
            continue;
        }
        if (alpha < 255) {
            c->alpha *= 1 - alpha / 255.f;
            continue;
        }
        if (c->kind != NATIVE_TEXT) {
            c->hidden = 1;
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
static int enqueue(int kind, float x, float y, float w, float h, unsigned color, unsigned size,
                   const char *label, int selected) {
    if (!ui_native_vita_active())
        return 0;
    if (!isfinite(x) || !isfinite(y) || !isfinite(w) || !isfinite(h) || w <= 0 || h <= 0)
        return 1;
    float left = fmaxf(x, clip_enabled ? clip_box[0] : 0),
          top = fmaxf(y, clip_enabled ? clip_box[1] : 0);
    float right = fminf(x + w, clip_enabled ? clip_box[2] : 960),
          bottom = fminf(y + h, clip_enabled ? clip_box[3] : 544);
    if (left >= right || top >= bottom)
        return 1;
    if (kind != NATIVE_TEXT && (left > x || top > y || right < x + w || bottom < y + h))
        return 0;
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
                         .selected = selected};
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
    return enqueue(NATIVE_BUTTON, x, y, w, h, 0xffffffff, 20, label, selected);
}
int ui_native_vita_button_fade(float x, float y, float w, float h, const char *label, int selected,
                               float alpha) {
    unsigned before = command_count;
    int result = ui_native_vita_button(x, y, w, h, label, selected);
    if (result && command_count > before)
        commands[command_count - 1].alpha = fmaxf(0, fminf(1, alpha));
    return result;
}
void ui_native_vita_media_text(int active) {
    media_text = active;
}
int ui_native_vita_panel(float x, float y, float w, float h, unsigned color, int selected) {
    return enqueue(h <= 96 && w >= 150 ? NATIVE_BUTTON : NATIVE_PANEL, x, y, w, h, color, 20, "",
                   selected);
}
int ui_native_vita_focus(float x, float y, float w, float h) {
    if (!ui_native_vita_active())
        return 0;
    for (unsigned n = command_count; n > 0; n--) {
        NativeCommand *c = &commands[n - 1];
        if (!c->hidden && c->kind == NATIVE_BUTTON && fabsf(c->x - x) < 12 &&
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
static int present(void) {
    unsigned at[NATIVE_KINDS] = {0}, order_count = 0;
    void *order[UI_NATIVE_TEXT_LIMIT + UI_NATIVE_BUTTON_LIMIT + UI_NATIVE_PANEL_LIMIT +
                UI_NATIVE_BUSY_LIMIT];
    int any = 0;
    const char *types[] = {"Text", "Button", "Plane", "BusyIndicator"};
    const char *styles[] = {"_common_default_style_text", "_common_default_style_button",
                            "vmd_white", "_common_style_busyindicator_8"};
    for (unsigned i = 0; i < command_count; i++) {
        NativeCommand *c = &commands[i];
        if (c->hidden)
            continue;
        NativeSlot *slot = &slots[c->kind][at[c->kind]++];
        int fresh = !slot->widget;
        if (fresh) {
            char name[48];
            snprintf(name, sizeof(name), "vmd_native_%d_%u", c->kind, at[c->kind]);
            if (c->kind == NATIVE_BUSY) {
                VtPafId busy = id(name);
                slot->widget = vmd_paf_widget_find(page, &busy, 0);
            } else
                slot->widget = create(types[c->kind], name, styles[c->kind]);
            if (!slot->widget)
                return -1;
        }
        void *w = slot->widget;
        NativeCommand *old = &slot->prior;
        if (fresh || old->x != c->x || old->y != c->y || old->w != c->w || old->h != c->h ||
            (c->kind == NATIVE_TEXT && memcmp(old->clip, c->clip, sizeof(c->clip)))) {
            /* The native text widget occupies the physical visible window.
             * Logical size/visible position below preserve its original text
             * layout while scrolling clips through a header or a later modal. */
            const float *v = c->clip;
            if ((c->kind == NATIVE_TEXT ? place(w, v[0], v[1], v[2], v[3])
                                        : place(w, c->x, c->y, c->w, c->h)) < 0)
                return -1;
        }
        int text_changed = fresh || strcmp(old->label, c->label);
        if (c->kind == NATIVE_TEXT || c->kind == NATIVE_BUTTON) {
            uint16_t text[NATIVE_LABEL_BYTES];
            unsigned count = ui_native_vita_utf16(c->label, text, NATIVE_LABEL_BYTES);
            if (text_changed) {
                VtPafString value = {text, count, 0};
                int rc = c->kind == NATIVE_BUTTON ? vmd_paf_button_text(w, &value, 0)
                                                  : vmd_paf_widget_text(w, &value, 0);
                if (rc < 0)
                    return -1;
            }
            if (c->kind == NATIVE_TEXT) {
                if (fresh || old->size != c->size || text_changed)
                    if (vmd_paf_text_size(w, 1, 0, count, (float)c->size) < 0)
                        return -1;
                if (fresh || old->color != c->color || old->alpha != c->alpha || text_changed) {
                    float color[4] = {(c->color & 255) / 255.f, ((c->color >> 8) & 255) / 255.f,
                                      ((c->color >> 16) & 255) / 255.f,
                                      ((c->color >> 24) & 255) / 255.f * c->alpha};
                    if (vmd_paf_text_color(w, 2, 0, count, color) < 0)
                        return -1;
                }
                if (fresh) {
                    if (vmd_paf_text_layout_int(w, 0, 0) < 0 ||
                        vmd_paf_text_layout_int(w, 1, 0) < 0 ||
                        vmd_paf_text_layout_int(w, 2, 0) < 0 ||
                        vmd_paf_text_layout_bool(w, 12, 0) < 0 ||
                        vmd_paf_text_layout_bool(w, 14, 1) < 0 ||
                        vmd_paf_text_layout_bool(w, 10, 1) < 0)
                        return -1;
                }
                if (fresh || old->w != c->w || old->h != c->h) {
                    float logical[2] = {c->w, c->h};
                    if (vmd_paf_text_layout_vec(w, 5, logical) < 0)
                        return -1;
                }
                if (fresh || memcmp(old->clip, c->clip, sizeof(c->clip)) || old->x != c->x ||
                    old->y != c->y) {
                    float origin[2] = {c->clip[0] - c->x, c->clip[1] - c->y},
                          extent[2] = {c->clip[2], c->clip[3]};
                    if (vmd_paf_text_layout_vec(w, 6, origin) < 0 ||
                        vmd_paf_text_layout_vec(w, 7, extent) < 0)
                        return -1;
                }
            } else if (fresh || old->selected != c->selected)
                vmd_paf_button_state(w, c->selected ? 2 : 0);
        }
        if (c->kind == NATIVE_PANEL && (fresh || old->color != c->color)) {
            float color[4] = {(c->color & 255) / 255.f, ((c->color >> 8) & 255) / 255.f,
                              ((c->color >> 16) & 255) / 255.f, ((c->color >> 24) & 255) / 255.f};
            if (vmd_paf_widget_color(w, color, NULL, 0x10001, 0, 0, NULL, NULL) < 0)
                return -1;
        }
        order[order_count++] = w;
        if (!slot->shown) {
            show(w, 1);
            if (c->kind == NATIVE_BUSY)
                vmd_paf_busy_start(w);
        }
        /* Show may reset the widget's show alpha: apply the requested fade
         * afterwards, including reused widgets becoming visible again. */
        if (c->kind != NATIVE_TEXT && (fresh || !slot->shown || old->alpha != c->alpha))
            vmd_paf_widget_alpha(w, c->alpha, NULL, 0x10002, 0, 0, NULL);
        slot->shown = 1;
        slot->prior = *c;
        any = 1;
    }
    for (int kind = 0; kind < NATIVE_KINDS; kind++)
        for (unsigned n = at[kind]; n < limits[kind]; n++) {
            NativeSlot *s = &slots[kind][n];
            if (s->shown) {
                show(s->widget, 0);
                if (kind == NATIVE_BUSY)
                    vmd_paf_busy_stop(s->widget);
                s->shown = 0;
            }
        }
    if (order_count != previous_order_count ||
        memcmp(order, previous_order, order_count * sizeof(void *))) {
        for (unsigned n = 0; n < order_count; n++)
            vmd_paf_widget_order(page, order[n], NULL);
        memcpy(previous_order, order, order_count * sizeof(void *));
        previous_order_count = order_count;
    }
    if (any) {
        if (!visible)
            show(page, 1);
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
/* libvita2d supplies its real framebuffer stride and sync object. The wrapper
 * is reached only during this explicit flush, after sceGxmEndScene. It never
 * substitutes a guessed display buffer or drives PAF from the draw callback. */
int __real_sceCommonDialogUpdate(const SceCommonDialogUpdateParam *params);
int __wrap_sceCommonDialogUpdate(const SceCommonDialogUpdateParam *params) {
    if (!updating)
        return __real_sceCommonDialogUpdate(params);
    if (!params || !params->renderTarget.colorSurfaceData || !params->displaySyncObject)
        return -1;
    vmd_paf_fw_update(framework, (void *)params);
    return 0;
}
void __real_vita2d_start_drawing(void);
void __wrap_vita2d_start_drawing(void) {
    capturing = 0;
    if (ui_theme_id() == VT_UI_THEME_LEGACY_VITA)
        ui_native_vita_prepare();
    command_count = 0;
    media_text = 0;
    clip_enabled = 0;
    clip_box[0] = clip_box[1] = 0;
    clip_box[2] = 960;
    clip_box[3] = 544;
    capturing = state == 1 && ui_theme_id() == VT_UI_THEME_LEGACY_VITA;
    __real_vita2d_start_drawing();
}
void __real_vita2d_end_drawing(void);
void __wrap_vita2d_end_drawing(void) {
    __real_vita2d_end_drawing();
    capturing = 0;
    if (state != 1)
        return;
    int changed = present();
    if (changed < 0) {
        show(page, 0);
        log_failure(-0x5048);
        changed = 1;
    }
    if (changed) {
        updating = 1;
        int rc = vita2d_common_dialog_update();
        updating = 0;
        if (rc < 0)
            log_failure(rc);
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
