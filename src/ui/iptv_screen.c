#include "ui/iptv_screen.h"
#include <stdio.h>
#include <string.h>
#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>
#include "common/text_log.h"
#include "app_paths.h"
#include "i18n/i18n.h"
#include "media/background_playback.h"
#include "media/hw_player_screen.h"
#include "media/video_thumbnail.h"
#include "network/iptv_session.h"
#include "ui/brand.h"
#include "ui/components.h"
#include "ui/loading_screen.h"
#include "ui/mini_player.h"
#include "ui/runtime.h"
#include "ui/sections_sidebar.h"
#include "ui/text_input.h"
#include "ui/theme.h"
#include "ui/touch.h"

/* Keep token-bearing links only in RAM. Nothing is fetched until Play. */
static char link[VT_IPTV_URL_MAX];
static const char sample[] = "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8";
#define TR(id) vt_i18n_str(VT_STR_##id)

static const char *failure_detail(int event) {
    switch (event) {
    case VT_IPTV_DISCONTINUITY: case VT_IPTV_LAGGED: return TR(IPTV_CHANGED);
    case VT_IPTV_LIMIT: case VT_IPTV_MEMORY: return TR(IPTV_TOO_LARGE);
    case VT_IPTV_UNSUPPORTED: case VT_IPTV_INVALID: return TR(IPTV_UNSUPPORTED);
    case VT_IPTV_IO: return TR(IPTV_NETWORK_ERROR);
    default: return TR(IPTV_DECODE_ERROR);
    }
}
static void display_link(char out[VT_IPTV_URL_MAX]) {
    snprintf(out, VT_IPTV_URL_MAX, "%s", link);
    char *query = strchr(out, '?');
    if (query) *query = 0; /* Never expose an access token in the page/HUD. */
}
static int play_link(const char **message) {
    char checked[VT_IPTV_URL_MAX];
    if (vt_iptv_resolve_url(link, link, checked, sizeof(checked))) {
        *message = TR(IPTV_INVALID); return UI_SECTION_NONE;
    }
    vt_video_thumbnail_prepare_playback();
    vt_background_playback_set_fullscreen_resume(NULL, NULL);
    /* Release the previous audio port and fence mini-player video before
     * opening a second decoder. request_stop alone is asynchronous. */
    vt_background_playback_stop();
    for (;;) {
        VtIptvSession *session = vt_iptv_session_create(link, NULL);
        if (!session) { *message = TR(IPTV_TOO_LARGE); return UI_SECTION_NONE; }
        VtHwPlayerScreenSource source = {0};
        vt_iptv_session_factory(session, &source.stream);
        source.title = !strcmp(link, sample) ? TR(IPTV_SAMPLE_TITLE) : TR(IPTV_TITLE);
        source.location = TR(IPTV_PREVIEW);
        source.authenticated_remote = 1;
        int result = vt_hw_player_screen_run_live(&source, session);
        int event = vt_iptv_session_event(session);
        VtIptvSourceStatus status;
        vt_iptv_session_status(session, &status);
        /* The player has closed both cursors before producer/session teardown. */
        vt_iptv_session_destroy(session);
        log_printf("IPTV playback: result=%d source=%d generation=%llu segments=%u bytes=%u\n",
            result, event, (unsigned long long)status.generation,
            status.buffered_segments, (unsigned)status.buffered_bytes);
        if (result < 0) {
            *message = failure_detail(event);
            log_save(VITAMEDIADECK_SESSION_LOG_PATH);
        } else if (status.terminal == VT_IPTV_END) *message = TR(IPTV_FINISHED);
        else *message = TR(IPTV_EXIT_NOTE);
        if (result == VT_HW_PLAYER_ACTION_GO_LIVE) continue;
        if (result >= VT_HW_PLAYER_ACTION_SECTION_BASE &&
            result < VT_HW_PLAYER_ACTION_SECTION_BASE + UI_SECTION_ACTION_COUNT)
            return result - VT_HW_PLAYER_ACTION_SECTION_BASE;
        return UI_SECTION_NONE;
    }
}
static void text_line(int x, int y, int width, const char *text, unsigned color, int size) {
    vita2d_font *font = ui_runtime_font(size);
    if (!font) return;
    char fitted[512];
    ui_font_fit_text(font, size, text, fitted, sizeof(fitted), width);
    ui_font_draw_text(font, x, y, color, size, fitted);
}
int ui_iptv_screen(void) {
    const char *message = TR(IPTV_EMPTY);
    int focus = 0;
    UiSectionsSidebar sidebar;
    ui_sections_sidebar_init(&sidebar, UI_SECTION_IPTV);
    UiNavRepeat repeat;
    ui_nav_repeat_reset(&repeat);
    SceCtrlData controls = {0}, previous = {0};
    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceCtrlPeekBufferPositive(0, &previous, 1);
    ui_touch_reset();
    for (;;) {
        ui_mini_player_pump();
        sceCtrlPeekBufferPositive(0, &controls, 1);
        unsigned pressed = controls.buttons & ~previous.buttons;
        previous = controls;
        ui_mini_player_handle_buttons(&pressed);
        UiTouchEvent touch;
        unsigned touches = ui_touch_poll(&touch);
        if (ui_mini_player_input_locked()) {
            pressed = touches = 0; controls.buttons &= SCE_CTRL_SELECT;
            controls.lx = controls.ly = 128;
        }
        if (ui_brand_sections_hit(touches, touch.x, touch.y)) {
            pressed |= SCE_CTRL_LTRIGGER; touches = 0;
        }
        int was_open = sidebar.open;
        int section = ui_sections_sidebar_handle_buttons(&sidebar, &pressed,
                                                         controls.buttons, controls.ly);
        if (was_open || sidebar.open) {
            int selected = ui_sections_sidebar_handle_touch(&sidebar, touches, touch.x, touch.y);
            if (selected != UI_SECTION_NONE) section = selected;
            touches = 0;
        }
        if (section != UI_SECTION_NONE) return section;
        ui_sections_sidebar_tick(&sidebar);
        int page_input = !was_open && !sidebar.open && sidebar.animation <= 0.01f;
        if (page_input && ui_mini_player_handle_touch(touches, &touch)) touches = 0;
        if (page_input) {
            unsigned nav = ui_nav_repeat_update(&repeat, pressed, controls.buttons,
                controls.lx, controls.ly, SCE_CTRL_LEFT | SCE_CTRL_RIGHT);
            if (nav & SCE_CTRL_LEFT) focus = focus > 0 ? focus - 1 : 0;
            if (nav & SCE_CTRL_RIGHT) focus = focus < 2 ? focus + 1 : 2;
            if (pressed & SCE_CTRL_CIRCLE) return UI_SECTION_LOCAL_MEDIA;
            int action = (pressed & SCE_CTRL_CROSS) ? focus : -1;
            if (pressed & SCE_CTRL_START) action = 0;
            if (pressed & SCE_CTRL_SQUARE) action = 2;
            if (touches & UI_TOUCH_EVENT_TAP)
                for (int i = 0; i < 3; i++)
                    if (ui_touch_hit_rect(touch.x, touch.y, 48 + i * 292, 290, 272, 52))
                        action = focus = i;
            if (action == 0) {
                char input[VT_IPTV_URL_MAX];
                if (ui_text_input(TR(IPTV_LINK), link, input, sizeof(input)) > 0) {
                    char checked[VT_IPTV_URL_MAX];
                    if (!vt_iptv_resolve_url(input, input, checked, sizeof(checked))) {
                        snprintf(link, sizeof(link), "%s", checked);
                        focus = 1; message = TR(IPTV_EXIT_NOTE);
                    } else message = TR(IPTV_INVALID);
                }
            } else if (action == 1) {
                section = play_link(&message);
                if (section != UI_SECTION_NONE && section != UI_SECTION_IPTV) return section;
            } else if (action == 2) {
                snprintf(link, sizeof(link), "%s", sample);
                focus = 1; message = TR(IPTV_SAMPLE_TITLE);
            }
            if (action >= 0) {
                sceCtrlPeekBufferPositive(0, &previous, 1);
                ui_touch_reset(); ui_nav_repeat_reset(&repeat);
            }
        } else ui_nav_repeat_reset(&repeat);
        char shown[VT_IPTV_URL_MAX]; display_link(shown);
        vita2d_start_drawing(); vita2d_clear_screen();
        ui_chrome_background(VT_THEME_BG, VT_THEME_SIGNAL_BRIGHT);
        ui_brand_draw_header(TR(IPTV_TITLE));
        ui_scene_identity(48, 78, 864, "TV", TR(IPTV_PREVIEW), TR(IPTV_FORMATS));
        ui_panel(48, 176, 864, 84, VT_THEME_SURFACE, VT_THEME_BORDER, 0);
        text_line(66, 201, 824, TR(IPTV_LINK), VT_THEME_TEXT_MUTED, UI_FONT_SMALL);
        text_line(66, 234, 824, link[0] ? shown : TR(IPTV_EMPTY), VT_THEME_TEXT, UI_FONT_BODY);
        const char *labels[] = {TR(IPTV_EDIT), TR(IPTV_PLAY), TR(IPTV_SAMPLE)};
        const char *keys[] = {"Start", "X", "Square"};
        for (int i = 0; i < 3; i++) {
            ui_panel(44 + i * 292, 286, 280, 60, VT_THEME_BG,
                     page_input && focus == i ? VT_THEME_SIGNAL_LIGHT : VT_THEME_BORDER_DIM,
                     page_input && focus == i);
            ui_action_button(48 + i * 292, 290, 272, 52, VT_THEME_SURFACE_FOCUS,
                             keys[i], labels[i], page_input && focus == i);
        }
        text_line(48, 384, 864, message, VT_THEME_SIGNAL_LIGHT, UI_FONT_SMALL);
        text_line(48, 427, 864, TR(IPTV_LIMITS), VT_THEME_TEXT_MUTED, UI_FONT_SMALL);
        text_line(48, 455, 864, TR(IPTV_EXIT_NOTE), VT_THEME_TEXT_MUTED, UI_FONT_SMALL);
        ui_mini_player_draw();
        if (sidebar.animation > 0.01f) ui_sections_sidebar_draw(&sidebar);
        vita2d_end_drawing(); vita2d_wait_rendering_done(); vita2d_swap_buffers();
        sceKernelDelayThread(1000);
    }
}
