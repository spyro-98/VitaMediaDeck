/* Real shared components/L1 with native dispatch enabled. This checks routing
 * and bounds, not firmware painting or Sony screenshots. */
#define VMD_ENABLE_PAF_NATIVE 1
#define VMD_CHROME_HELPERS_ONLY
#include "../ui_chrome/test.c"
#undef CHECK
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "Contract failed: %s (%s:%d)\n", #x, __FILE__, __LINE__); exit(1); } } while (0)
static int native_enabled, native_scope;
static float native_last_text_width;
static unsigned native_text_calls;
static char native_last_text[512];
static float native_brand_width;
void ui_native_vita_cover(float x, float y, float w, float h, unsigned color) {
    (void)x; (void)y; (void)w; (void)h; (void)color;
}
int ui_native_vita_scope(int scope) {
    int previous = native_scope;
    native_scope = scope;
    return previous;
}
int ui_native_vita_text(float x, float y, float w, unsigned size, unsigned color, const char *text) {
    (void)x; (void)y; (void)size; (void)color;
    native_text_calls++;
    native_last_text_width = w;
    if(!strcmp(text,"VitaMediaDeck"))native_brand_width=w;
    snprintf(native_last_text, sizeof(native_last_text), "%s", text);
    return 0; /* Host text renderer supplies reference pixels, not Sony text. */
}
static unsigned native_panels, native_rows, native_glows;
static UiThemeRect native_last_panel;
int ui_native_vita_active(void) {
    return native_enabled && ui_theme_id() == VT_UI_THEME_LEGACY_VITA;
}
int ui_native_vita_prepare(void) { return ui_native_vita_active(); }
int ui_native_vita_error(void) { return 0; }
int ui_native_vita_panel(float x, float y, float w, float h, unsigned color, int selected) {
    (void)x; (void)y; (void)color; (void)selected;
    if (!ui_native_vita_active()) return 0;
    CHECK(w > 0 && h > 0);
    native_last_panel=(UiThemeRect){x,y,w,h};
    native_panels++;
    return 1;
}
int ui_native_vita_button_fade(float x, float y, float w, float h, const char *label,
                               int selected, float alpha) {
    (void)selected;
    if (!ui_native_vita_active()) return 0;
    if (ui_input_prompt_contains(label)) return 0;
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
int ui_native_vita_decoration(int enabled) { (void)enabled;return 0; }
int ui_native_vita_focus(float x, float y, float w, float h) {
    CHECK(isfinite(x)&&isfinite(y)&&w>0&&h>0);
    if(ui_native_vita_active())native_glows++;
    return ui_native_vita_active();
}
int ui_native_vita_toggle(float x,float y,float w,float h,int checked) {
    (void)x;(void)y;(void)w;(void)h;(void)checked;return ui_native_vita_active();
}
int ui_native_vita_busy(float x, float y, float size) {
    (void)x; (void)y; (void)size;
    return ui_native_vita_active();
}
void ui_native_vita_media_text(int active) { (void)active; }
int main(int argc, char **argv) {
    CHECK(argc == 3); output = argv[2];
    CHECK(!FT_Init_FreeType(&ft)); CHECK(!FT_New_Face(ft, argv[1], 0, &face));
    for(int theme=0;theme<VT_UI_THEME_COUNT;theme++){
        ui_theme_apply(theme);native_brand_width=0;
        ui_breadcrumb_draw(86,696,NULL,NULL,NULL,1);
        CHECK(native_brand_width>=ui_font_text_width(ui_runtime_font(UI_FONT_SMALL),UI_FONT_SMALL,"VitaMediaDeck")+18);
    }
    ui_theme_apply(VT_UI_THEME_LEGACY_VITA);
    native_enabled = 1;
    host_reduced_motion = 1;
    frame = 0;
    vita2d_clear_screen();
    unsigned background_batches = gpu_draw_calls, background_vertices = gpu_vertex_count;
    ui_chrome_background(VT_THEME_BG, VT_THEME_COLD);
    CHECK(gpu_draw_calls == background_batches + 1 && gpu_vertex_count == background_vertices + 7830);
    memcpy(first_pixels,pixels,sizeof(pixels));
    unsigned textured = 0, highlights = 0;
    unsigned long green_total=0,blue_total=0;
    for (int y=0;y<544;y++) for (int x=0;x<960;x++) {
        if(pixels[y][x][0]>=70||pixels[y][x][1]>=190||pixels[y][x][2]>=210){fprintf(stderr,"Wave highlight exceeded dark-scene limit at %d,%d: %u,%u,%u\n",x,y,pixels[y][x][0],pixels[y][x][1],pixels[y][x][2]);}
        CHECK(pixels[y][x][0] < 70 && pixels[y][x][1] < 190 && pixels[y][x][2] < 210);
        green_total+=pixels[y][x][1];blue_total+=pixels[y][x][2];
        highlights+=pixels[y][x][1]>65 && pixels[y][x][2]>90;
        textured += pixels[y][x][2] != ((VT_THEME_BG >> 16)&255);
    }
    CHECK(textured > 1000);
    CHECK(highlights>1000 && highlights<15000); /* Thin luminous lines, not broad bright ribbons. */
    CHECK(green_total<24u*960u*544u && blue_total<40u*960u*544u);
    png("vmd-background-dark");
    frame = 900;
    vita2d_clear_screen();ui_chrome_background(VT_THEME_BG, VT_THEME_COLD);
    CHECK(!memcmp(first_pixels,pixels,sizeof(pixels)));
    host_reduced_motion = 0;
    frame = 0;
    vita2d_clear_screen();ui_chrome_background(VT_THEME_BG, VT_THEME_COLD);
    memcpy(first_pixels,pixels,sizeof(pixels));
    frame = 900;
    vita2d_clear_screen();ui_chrome_background(VT_THEME_BG, VT_THEME_COLD);
    CHECK(memcmp(first_pixels,pixels,sizeof(pixels)));
    host_reduced_motion = 1;
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
                CHECK(native_panels == panels && native_rows == rows);
                UiThemeRect target = sidebar_slot_rect(&side, slot);
                CHECK(target.y >= sidebar_view_top() && target.y + target.h <= sidebar_view_bottom());
            }
        }
    }
    /* A native panel owns its finish: an old Legacy border must not be drawn
     * before its deferred native widget. Its media-cutout base must remain. */
    unsigned fill = RGBA8(23, 44, 66, 255);
    clipped = 0; vita2d_clear_screen();
    unsigned batches = gpu_draw_calls, vertices = gpu_vertex_count;
    ui_panel(100, 100, 200, 40, fill, VT_THEME_FOCUS, 0);
    CHECK(gpu_draw_calls == batches + 1 && gpu_vertex_count == vertices + 84);
    ui_text_line(20, 70, 300, "Settings", VT_THEME_TEXT, UI_FONT_SMALL);
    CHECK(native_last_text_width == 300 && !strcmp(native_last_text, "Settings"));
    const unsigned char *p = pixels[100][100];
    CHECK(!p[0] && !p[1] && !p[2]); /* Rounded corner stays clear. */
    p = pixels[102][110];
    CHECK(p[0] == 23 && p[1] == 44 && p[2] == 66);
    CHECK(native_scope == UI_NATIVE_SCOPE_CONTENT);
    for(int bottom=472;bottom<=544;bottom+=72){
        unsigned before_panels=native_panels;
        ui_theme_home_footer("Local files",bottom);
        CHECK(native_panels==before_panels); /* Clean footer: no empty native tile. */
    }
    /* The real IPTV form must route both label and URL above the PAF panel.
     * The old exact/immediate-only helper fails this contract. */
    new_link();unsigned captions=native_text_calls;char shown[VT_IPTV_URL_MAX];
    draw_link_field("");CHECK(native_text_calls==captions+3);
    CHECK(!strcmp(native_last_text,TR(IPTV_LIST_URL_EMPTY))&&native_last_text_width==892);
    CHECK(!accept_link("https://example.org/channels.m3u?auth=secret-token"));display_link(shown);
    captions=native_text_calls;draw_link_field(shown);
    CHECK(native_text_calls==captions+3&&!strcmp(native_last_text,"https://example.org/channels.m3u")&&native_last_text_width==892);
    png("iptv-url-field-native-routing");
    /* Prompt sprites stay immediate; captions use a bounded native text box. */
    strcpy(native_last_text,"untouched");host_exact_width=0;
    ui_action_button(18,110,302,46,VT_THEME_SURFACE,UI_PAD_CROSS,"Riproduci canale",0);
    CHECK(!strcmp(native_last_text,"Riproduci canale")&&host_exact_width>0);
    CHECK(native_last_text_width<260);
    ui_menu_text(34,220,300,"Opzioni IPTV",UI_MENU_TEXT,UI_FONT_BODY);
    CHECK(!strcmp(native_last_text,"Opzioni IPTV"));
    CHECK(native_last_text_width==300);
    unsigned glows=native_glows;
    UiFocusMotion menu_focus={.x=20,.y=130.375f,.width=384,.height=42,.initialized=1};
    unsigned before_primitives=draw_primitives,before_batches=gpu_draw_calls;
    ui_menu_focus_draw(&menu_focus);CHECK(native_glows==glows+1);
    CHECK(draw_primitives==before_primitives&&gpu_draw_calls==before_batches); /* Firmware glow only; no extra wash over glyphs. */
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
