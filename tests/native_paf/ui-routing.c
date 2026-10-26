/* Real shared components/L1 with native dispatch enabled. This checks routing
 * and bounds, not firmware painting or Sony screenshots. */
#define VMD_ENABLE_PAF_NATIVE 1
#define VMD_CHROME_HELPERS_ONLY
#include "../ui_chrome/test.c"
static int native_enabled;
static unsigned native_panels, native_rows;
int ui_native_vita_active(void) {
    return native_enabled && ui_theme_id() == VT_UI_THEME_LEGACY_VITA;
}
int ui_native_vita_prepare(void) { return ui_native_vita_active(); }
int ui_native_vita_error(void) { return 0; }
int ui_native_vita_panel(float x, float y, float w, float h, unsigned color, int selected) {
    (void)x; (void)y; (void)color; (void)selected;
    if (!ui_native_vita_active()) return 0;
    CHECK(w > 0 && h > 0);
    native_panels++;
    return 1;
}
int ui_native_vita_button_fade(float x, float y, float w, float h, const char *label,
                               int selected, float alpha) {
    (void)selected;
    if (!ui_native_vita_active()) return 0;
    CHECK(isfinite(x) && isfinite(y) && w > 0 && h > 0 && label && label[0]);
    CHECK(alpha >= 0 && alpha <= 1);
    if (x < 0 || x + w > 960 || y < sidebar_view_top() || y + h > sidebar_view_bottom())
        return 0;
    native_rows++;
    return 1;
}
int ui_native_vita_button(float x, float y, float w, float h, const char *s, int selected) {
    return ui_native_vita_button_fade(x, y, w, h, s, selected, 1);
}
int ui_native_vita_focus(float x, float y, float w, float h) {
    (void)x; (void)y; (void)w; (void)h;
    return 0;
}
int ui_native_vita_busy(float x, float y, float size) {
    (void)x; (void)y; (void)size;
    return ui_native_vita_active();
}
void ui_native_vita_media_text(int active) { (void)active; }
int main(int argc, char **argv) {
    CHECK(argc == 3); output = argv[2];
    CHECK(!FT_Init_FreeType(&ft)); CHECK(!FT_New_Face(ft, argv[1], 0, &face));
    ui_theme_apply(VT_UI_THEME_LEGACY_VITA);
    native_enabled = 1;
    for (language = 0; language < 2; language++) {
        for (int live = 0; live < 2; live++) {
            live_slots = download_slots = live;
            UiSectionsSidebar side;
            ui_sections_sidebar_init(&side, UI_SECTION_ONLINE_AUDIO);
            side.open = 1; side.animation = 1;
            int total = UI_SECTION_COUNT + side.player_available + side.download_available;
            for (int slot = 0; slot < total; slot++) {
                side.cursor = slot; side.focus_cursor = slot;
                sidebar_scroll_to_cursor(&side);
                side.scroll = side.scroll_target;
                unsigned panels = native_panels, rows = native_rows;
                vita2d_clear_screen();
                ui_sections_sidebar_draw(&side);
                CHECK(native_panels > panels && native_rows > rows);
                UiThemeRect target = sidebar_slot_rect(&side, slot);
                CHECK(target.y >= sidebar_view_top() && target.y + target.h <= sidebar_view_bottom());
            }
        }
    }
    /* A native panel owns its finish: an old Legacy border must not be drawn
     * before its deferred native widget. Its media-cutout base must remain. */
    unsigned fill = RGBA8(23, 44, 66, 255);
    clipped = 0; vita2d_clear_screen();
    ui_panel(100, 100, 200, 40, fill, VT_THEME_FOCUS, 0);
    const unsigned char *p = pixels[100][100];
    CHECK(p[0] == 23 && p[1] == 44 && p[2] == 66);
    ui_theme_apply(VT_UI_THEME_LEGACY);
    unsigned rows = native_rows, panels = native_panels;
    ui_panel(100, 100, 200, 40, fill, VT_THEME_FOCUS, 0);
    p = pixels[100][100]; unsigned border = VT_THEME_BORDER_DIM;
    CHECK(p[0] == (border & 255) && p[1] == ((border >> 8) & 255));
    CHECK(native_rows == rows && native_panels == panels);
    FT_Done_Face(face); FT_Done_FreeType(ft);
    printf("Native UI dispatch: %u checks; IT/EN, live slots, every L1 selection, native rows, "
           "panel finish ownership and unchanged Legacy borders passed. Sony pixels unverified.\n", checks);
    return 0;
}
