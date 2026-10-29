#include "ui/breadcrumbs.h"
#include "ui/components.h"
#include <stdio.h>
#include <string.h>
#include "ui/runtime.h"
#include "ui/theme.h"

static void label_copy(char *out, size_t capacity, const char *source) {
    if (!source) source = "";
    /* Defensive fallback for legacy callers: do not expose URL credentials,
     * query parameters or fragments as navigation labels. */
    const char *scheme = strstr(source, "://");
    size_t length = strlen(source);
    if (scheme) {
        const char *end = source + strcspn(source, "?#");
        const char *path = strchr(scheme + 3, '/');
        if (!path || path >= end) { out[0] = 0; return; }
        source = path + 1;
        for (const char *p = source; p < end; ++p)
            if (*p == '/') source = p + 1;
        length = (size_t)(end - source);
    }
    if (length >= capacity) {
        length = capacity - 1;
        while (length && ((unsigned char)source[length] & 0xc0) == 0x80) --length;
    }
    memcpy(out, source, length);
    for (size_t i = 0; i < length; ++i)
        if ((unsigned char)out[i] < 32 || out[i] == 127) out[i] = ' ';
    out[length] = 0;
}

static unsigned tint(unsigned color, float opacity) {
    return (color & 0xffffffu) | ((unsigned)(opacity * 255.0f) << 24);
}

void ui_breadcrumb_draw(int x, int right, const char *section,
                        const char *parent, const char *leaf, float opacity) {
    if (opacity <= 0.0f || right <= x) return;
    if (opacity > 1.0f) opacity = 1.0f;
    vita2d_font *font = ui_runtime_font(UI_FONT_SMALL);
    if (!font) return;
    char root[64], a[256], b[512], c[512], prefix[800], fitted[800];
    label_copy(a, sizeof(a), section);
    label_copy(b, sizeof(b), parent);
    label_copy(c, sizeof(c), leaf);
    if (!strcmp(a, b) || !strcmp(b, c)) b[0] = 0;
    if (!c[0]) { label_copy(c, sizeof(c), b); b[0] = 0; }
    if (!c[0]) { label_copy(c, sizeof(c), a); a[0] = 0; }
    int width = ui_font_fit_text(font, UI_FONT_SMALL, "VitaMediaDeck", root,
                                 sizeof(root), right - x);
    /* PAF's firmware face has different advances/ink bounds from the packaged
     * measuring font. Give the fixed brand a real slot, including its last k. */
    int root_slot=width+18;if(root_slot>right-x)root_slot=right-x;
    ui_menu_text(x,33,root_slot,root,tint(VT_THEME_TEXT,opacity),UI_FONT_SMALL);
    x += root_slot;
    if (!c[0] || right - x < 40) return;
    ui_menu_text(x+9,33,12,"/",tint(VT_THEME_SIGNAL,opacity),UI_FONT_SMALL);
    x += 26;
    int available = right - x;
    snprintf(prefix, sizeof(prefix), "%s%s%s%s", a, a[0] ? " / " : "",
             b, b[0] ? " / " : "");
    int leaf_width = ui_font_text_width(font, UI_FONT_SMALL, c);
    width = ui_font_text_width(font, UI_FONT_SMALL, prefix);
    if (width + leaf_width > available) {
        /* Browser paths may contain several display-only folder labels. Keep
         * the final folder, not a truncated prefix of a deep directory. */
        char *last = strrchr(c, '/');
        if (last && last[1]) {
            ++last;
            while (*last == ' ') ++last;
            memmove(c, last, strlen(last) + 1);
            leaf_width = ui_font_text_width(font, UI_FONT_SMALL, c);
            snprintf(prefix, sizeof(prefix), "%s%s... / ", a, a[0] ? " / " : "");
            width = ui_font_text_width(font, UI_FONT_SMALL, prefix);
        }
    }
    if (width + leaf_width > available && b[0]) {
        snprintf(prefix, sizeof(prefix), "%s%s... / ", a, a[0] ? " / " : "");
        width = ui_font_text_width(font, UI_FONT_SMALL, prefix);
    }
    /* Keep at least two thirds of a narrow trail for the current item. The
     * root and status icons never shift when names or loading state change. */
    if (width + leaf_width > available && width > available / 3) {
        ui_font_fit_text(font, UI_FONT_SMALL, a, fitted, sizeof(fitted),
                         available / 3 - 20);
        snprintf(prefix, sizeof(prefix), "%.255s%s", fitted, fitted[0] ? " / " : "");
        width = ui_font_text_width(font, UI_FONT_SMALL, prefix);
    }
    if(prefix[0])ui_menu_text(x,33,width,prefix,tint(VT_THEME_TEXT_MUTED,opacity),UI_FONT_SMALL);
    ui_font_fit_text(font, UI_FONT_SMALL, c, fitted, sizeof(fitted), available - width);
    ui_menu_text(x+width,33,available-width,fitted,tint(VT_THEME_TEXT,opacity),UI_FONT_SMALL);
}
