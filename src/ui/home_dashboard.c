#include "ui/home_dashboard.h"
#include <psp2/ctrl.h>
#include <math.h>
UiDashboardLayout ui_dashboard_layout(int theme, int bottom) {
    UiDashboardLayout l = {0};
    int x = 18, y = 146, w = 924;
    l.title_y = 128;
    l.subtitle_y = 0;
    if (vt_ui_theme_legacy_layout(theme)) {
        y = 104;
        l.title_y = 86;
    }
    if (theme == VT_UI_THEME_GHOST_MESH) {
        x = 28;
        y = 158;
        w = 388;
        l.title_y = 118;
        l.subtitle_y = 144;
    } else if (theme == VT_UI_THEME_DECK_90) {
        x = 254;
        y = 160;
        w = 670;
        l.title_y = 114;
        l.subtitle_y = 142;
    } else if (theme == VT_UI_THEME_SIGNAL) {
        x = 28;
        y = 210;
        w = 904;
        l.title_y = 162;
        l.subtitle_y = 191;
    } else if (theme == VT_UI_THEME_APERTURE) {
        x = 46;
        y = 164;
        w = 868;
        l.title_y = 114;
        l.subtitle_y = 142;
    } else if (theme == VT_UI_THEME_RELAY) {
        x = 264;
        y = 160;
        w = 658;
        l.title_y = 114;
        l.subtitle_y = 142;
    } else if (theme == VT_UI_THEME_MOSAIC) {
        x = 24;
        y = 202;
        w = 912;
        l.title_y = 150;
        l.subtitle_y = 178;
    }
    int limit = theme == VT_UI_THEME_APERTURE ? 420 : bottom - 8;
    int h = limit - y, gap = 8, actions = 36;
    l.footer_y = theme == VT_UI_THEME_APERTURE ? 409 : bottom - 10;
    l.content = (UiThemeRect){x, y, w, h};
    if (theme == VT_UI_THEME_GHOST_MESH) {
        gap = 6;
        int middle = 72, recent = 46, hero = h - middle - recent - actions - 3 * gap;
        l.cards[0] = (UiThemeRect){x, y, w, hero};
        l.cards[1] = (UiThemeRect){x, y + hero + gap, (w - gap) / 2, middle};
        l.cards[2] = (UiThemeRect){x + (w + gap) / 2, y + hero + gap, (w - gap) / 2, middle};
        l.cards[3] = (UiThemeRect){x, y + hero + middle + 2 * gap, w, recent};
    } else if (theme == VT_UI_THEME_MOSAIC) {
        int left = (int)(w * .56f), right = w - left - gap, body = h - actions - gap;
        int recent = 48;
        l.cards[0] = (UiThemeRect){x, y, left, body - recent - gap};
        l.cards[3] = (UiThemeRect){x, y + body - recent, left, recent};
        l.cards[1] = (UiThemeRect){x + left + gap, y, right, (body - gap) / 2};
        l.cards[2] = (UiThemeRect){x + left + gap, y + (body + gap) / 2, right, (body - gap) / 2};
    } else {
        int left = (int)(w * (theme == VT_UI_THEME_SIGNAL  ? .56f
                              : theme == VT_UI_THEME_RELAY ? .64f
                                                           : .61f));
        int right = w - left - gap, body = h - actions - 2 * gap;
        int upper = (int)(body * .59f), lower = body - upper;
        int reverse = theme == VT_UI_THEME_APERTURE;
        int hero_x = reverse ? x + right + gap : x, side_x = reverse ? x : x + left + gap;
        l.cards[0] = (UiThemeRect){hero_x, y, left, upper};
        l.cards[1] = (UiThemeRect){side_x, y, right, upper};
        l.cards[2] = (UiThemeRect){hero_x, y + upper + gap, left, lower};
        l.cards[3] = (UiThemeRect){side_x, y + upper + gap, right, lower};
        if (theme == VT_UI_THEME_SIGNAL) {
            l.cards[2] = (UiThemeRect){x + right + gap, y + upper + gap, left, lower};
            l.cards[3] = (UiThemeRect){x, y + upper + gap, right, lower};
        }
    }
    if(theme==VT_UI_THEME_LEGACY_VITA) {
        /* Four distinct media components use the entire safe body; redundant
         * Sources/History shortcuts remain accessible from navigation. */
        int body=bottom-8-y, left=(int)(w*.62f), right=w-left-12;
        int upper=(int)((h-28)*.60f)+24, lower=body-upper-12;
        l.content.h=body; l.footer_y=0;
        l.cards[0]=(UiThemeRect){x,y,left,upper};
        l.cards[1]=(UiThemeRect){x+left+12,y,right,upper-48};
        l.cards[2]=(UiThemeRect){x,y+upper+12,left,lower};
        l.cards[3]=(UiThemeRect){x+left+12,y+upper-36,right,lower+48};
        return l;
    }
    l.cards[4] = (UiThemeRect){x, limit - actions, (w - gap) / 2, actions};
    l.cards[5] = (UiThemeRect){x + (w + gap) / 2, limit - actions, (w - gap) / 2, actions};
    return l;
}
/* Reclaim empty collections instead of presenting four empty category boxes.
 * Role identities remain stable for controller focus and accessibility. */
UiDashboardLayout ui_dashboard_compose(int theme, int bottom, const int counts[4]) {
    UiDashboardLayout l = ui_dashboard_layout(theme, bottom);
    if (!counts)
        return l;
    int gap = theme == VT_UI_THEME_GHOST_MESH ? 6 : 8;
    if (!counts[1] && !counts[2] && !counts[3]) {
        l.cards[0] = (UiThemeRect){l.content.x, l.content.y, l.content.w,
                                 (theme==VT_UI_THEME_LEGACY_VITA ? l.content.h-4
                                  : l.cards[4].y-gap-l.content.y)};
        for (int i = 1; i < 4; i++)
            l.cards[i].h = 0;
        return l;
    }
    if (theme == VT_UI_THEME_GHOST_MESH) {
        if (!counts[1] && !counts[2]) {
            l.cards[0].h = l.cards[1].y + l.cards[1].h - l.cards[0].y;
            l.cards[1].h = l.cards[2].h = 0;
        } else {
            if (!counts[1]) {
                l.cards[2].x = l.content.x;
                l.cards[2].w = l.content.w;
                l.cards[1].h = 0;
            }
            if (!counts[2]) {
                l.cards[1].w = l.content.w;
                l.cards[2].h = 0;
            }
            if (!counts[3])
                for (int i = 1; i <= 2; i++)
                    if (l.cards[i].h)
                        l.cards[i].h = l.cards[3].y + l.cards[3].h - l.cards[i].y;
        }
        if (!counts[3]) l.cards[3].h = 0;
        return l;
    }
    /* Signal's full composition deliberately staggers its lower columns.
     * Align column edges before vertically merging an absent collection. */
    if (theme == VT_UI_THEME_SIGNAL && (!counts[1] || !counts[2] || !counts[3])) {
        l.cards[3].w = l.cards[0].w;
        l.cards[2].x = l.cards[1].x;
        l.cards[2].w = l.cards[1].w;
    }
    int left_lower = theme == VT_UI_THEME_SIGNAL || theme == VT_UI_THEME_MOSAIC ? 3 : 2;
    int right_lower = left_lower == 3 ? 2 : 3;
    if (!counts[left_lower]) {
        l.cards[0].h = l.cards[left_lower].y + l.cards[left_lower].h - l.cards[0].y;
        l.cards[left_lower].h = 0;
    }
    if (!counts[1] && counts[right_lower]) {
        l.cards[right_lower].h += l.cards[right_lower].y - l.cards[1].y;
        l.cards[right_lower].y = l.cards[1].y;
        l.cards[1].h = 0;
    } else if (!counts[right_lower]) {
        l.cards[1].h = l.cards[right_lower].y + l.cards[right_lower].h - l.cards[1].y;
        l.cards[right_lower].h = 0;
    }
    if (!counts[1] && !counts[right_lower]) {
        l.cards[0].x = l.content.x;
        l.cards[0].w = l.content.w;
        if (l.cards[left_lower].h) {
            l.cards[left_lower].x = l.content.x;
            l.cards[left_lower].w = l.content.w;
        }
        l.cards[1].h = 0;
    }
    return l;
}
int ui_dashboard_move(const UiDashboardLayout *l, int selected, unsigned nav) {
    if (!l || selected < 0 || selected >= UI_DASH_COUNT)
        return 0;
    UiThemeRect from = l->cards[selected];
    float x = from.x + from.w * .5f, y = from.y + from.h * .5f, best = 1e9f;
    int result = selected;
    for (int i = 0; i < UI_DASH_COUNT; i++)
        if (i != selected && l->cards[i].h > 0) {
            UiThemeRect to = l->cards[i];
            float dx = to.x + to.w * .5f - x, dy = to.y + to.h * .5f - y;
            int vertical = !!(nav & (SCE_CTRL_UP | SCE_CTRL_DOWN));
            float along = vertical ? dy : dx, across = vertical ? dx : dy;
            int sign = nav & (SCE_CTRL_UP | SCE_CTRL_LEFT) ? -1 : 1;
            if (along * sign <= 8)
                continue;
            float score = fabsf(along) + fabsf(across) * 2.4f;
            if (score < best) {
                best = score;
                result = i;
            }
        }
    return result;
}
