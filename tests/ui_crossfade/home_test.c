/* Production carousel/components with host rasterization. Not a Vita screenshot. */
#define ui_home_tab_set ui_home_tab_set_stub
#define main iptv_ui_main
#include "../iptv/ui_test.c"
#undef main
#undef ui_home_tab_set
#include "../../src/ui/home_tabs.c"
#include "ui/media_grid_layout.h"
int main(int argc,char **argv) {
 CHECK(argc==3);output=argv[2];CHECK(!FT_Init_FreeType(&ft));CHECK(!FT_New_Face(ft,argv[1],0,&face));
 CHECK(UI_HOME_TABS_BOTTOM<=54&&UI_HOME_CONTENT_Y>=62); /* Tabs stay in chrome; content begins below it. */
 for (int legacy = 0; legacy < 2; legacy++)
     for (language = 0; language < 2; language++)
         for (int tab = 0; tab < HOME_TAB_COUNT; tab++)
             for (int mini = 0; mini < 2; mini++) {
                 ui_theme_apply(legacy ? VT_UI_THEME_LEGACY_VITA : VT_UI_THEME_LEGACY);
                 int bottom = mini ? 472 : UI_CONTENT_BOTTOM;
                 UiMediaGridLayout grid = ui_media_grid_layout(UI_HOME_CONTENT_Y, bottom);
                 loaded = 1;
                 ui_home_tab_set(tab);
                 vita2d_clear_screen();
                 /* Header/status are substituted, matching the Home rail's reserved space. */
                 vita2d_draw_rectangle(0, 0, 960, 54, VT_THEME_SURFACE);
                 vita2d_draw_rectangle(18, 9, 36, 36, VT_THEME_SIGNAL_DIM);
                 vita2d_draw_rectangle(710, 9, 232, 36, VT_THEME_BORDER_DIM);
                 ui_home_tabs_draw(1);
                 for (int i = 0; i < 6; i++) {
                     float x = UI_CONTENT_X + (i % 3) * (UI_GRID_CARD_W + 9);
                     float y = UI_HOME_CONTENT_Y + (i / 3) * grid.step;
                     ui_panel(x, y, UI_GRID_CARD_W, grid.card_h, VT_THEME_SURFACE,
                              VT_THEME_BORDER_DIM, 0);
                     ui_folder_icon(x + (UI_GRID_CARD_W - 136) * .5f,
                                    y + 6 + (grid.thumb_h - 108) * .5f);
                     ui_text_line(x + 13, y + grid.title_y, UI_GRID_CARD_W - 26,
                                  i % 3 == 0   ? "Memory card / Movies"
                                  : i % 3 == 1 ? "Memory card / Music"
                                               : "Memory card / Pictures",
                                  VT_THEME_TEXT, UI_FONT_SMALL);
                     ui_text_line(x + 13, y + grid.detail_y, UI_GRID_CARD_W - 26, "Layout fixture",
                                  VT_THEME_TEXT_MUTED, UI_FONT_SMALL);
                     CHECK(y + grid.card_h + UI_MEDIA_GRID_FOCUS_OUTSET < bottom);
                 }
                 ui_focus_glow_draw(UI_CONTENT_X, UI_HOME_CONTENT_Y, UI_GRID_CARD_W, grid.card_h, 0,
                                    UI_HOME_CONTENT_Y, bottom);
                 CHECK(pixels[UI_HOME_CONTENT_Y - 3][UI_CONTENT_X + 20][0] ==
                       (VT_THEME_FOCUS & 255));
                 CHECK(pixels[UI_HOME_CONTENT_Y - 3][UI_CONTENT_X + 20][1] ==
                       ((VT_THEME_FOCUS >> 8) & 255));
                 CHECK(pixels[UI_HOME_CONTENT_Y - 3][UI_CONTENT_X + 20][2] ==
                       ((VT_THEME_FOCUS >> 16) & 255));
                 if (mini)
                     vita2d_draw_rectangle(0, bottom, 960, 544 - bottom, VT_THEME_SURFACE_RAISED);
                 char name[80];
                 snprintf(name, sizeof(name), "home-carousel-%d-%d-%s%s", legacy, tab,
                          language ? "it" : "en", mini ? "-mini" : "");
                 png(name);
             }
 FT_Face inter=face,mono;CHECK(!FT_New_Face(ft,"assets/fonts/IBMPlexMono-Regular.ttf",0,&mono));
 for (int theme = 1; theme < VT_UI_THEME_LEGACY_VITA; theme++)
     for (language = 0; language < 2; language++)
         for (int mini = 0; mini < 2; mini++)
             for (int owner = 0; owner < 2; owner++) {
                 ui_theme_apply(theme);
                 face = theme == VT_UI_THEME_DECK_90 ? mono : inter;
                 int bottom = mini ? 472 : UI_CONTENT_BOTTOM;
                 UiThemeHomeLayout l = ui_theme_home_layout(theme, bottom);
                 CHECK(l.x >= 18 && l.x + l.w <= 942 && l.y >= 62);
                 CHECK(l.rows > 0);
                 UiThemeRect last = ui_theme_home_item_rect(theme, l.rows - 1, bottom);
                 CHECK(last.y + last.h + 3 < bottom - 30);
                 for (int i = 0; i < 6; i++) {
                     UiThemeRect r = ui_theme_home_tab_rect(theme, i, 1);
                     CHECK(r.x >= 18 && r.y >= 62 && r.x + r.w <= 942 && r.y + r.h < bottom - 3);
                     CHECK(r.x + r.w <= l.x || r.x >= l.x + l.w || r.y + r.h <= l.y ||
                           r.y >= l.y + l.rows * l.step);
                 }
                 UiFocusMotion motion = {0};
                 ui_home_tabs_set_expanded(1);
                 loaded = 1;
                 ui_home_tab_set(HOME_VIDEO);
                 vita2d_clear_screen();
                 ui_chrome_background(VT_THEME_BG, VT_THEME_SIGNAL_BRIGHT);
                 vita2d_draw_rectangle(0, 0, 960, 54, VT_THEME_SURFACE);
                 ui_text_line(18, 32, 650, "VitaMediaDeck", VT_THEME_TEXT, UI_FONT_BODY);
                 ui_home_tabs_draw_with_focus(owner, &motion);
                 ui_theme_home_heading(ui_home_tab_label(), "ux0:movies", bottom);
                 ui_theme_home_preview(NULL, language ? "Cartella selezionata" : "Selected folder",
                                       bottom, 0);
                 for (int slot = 0; slot < l.rows; slot++)
                     ui_theme_home_row(slot, slot,
                                       slot % 2 ? "Memory card / Movies" : "USB / Video",
                                       language ? "3 file - 3.72 GB" : "3 files - 3.72 GB",
                                       slot == 0, !owner, bottom);
                 ui_theme_home_footer("ux0:movies/Film e serie / A long folder name", bottom);
                 if (mini)
                     vita2d_draw_rectangle(0, bottom, 960, 544 - bottom, VT_THEME_SURFACE_RAISED);
                 char name[96];
                 snprintf(name, sizeof(name), "theme-home-%d-%s-%s%s", theme,
                          owner ? "sections" : "files", language ? "it" : "en",
                          mini ? "-mini" : "");
                 png(name);
                 /* Touch each visible menu entry using the exact production geometry. */
                 for (int i = 0; i < 6; i++) {
                     UiThemeRect r = ui_theme_home_tab_rect(theme, i, 1);
                     UiTouchEvent t = {.down_x = r.x + r.w / 2,
                                       .down_y = r.y + r.h / 2,
                                       .x = r.x + r.w / 2,
                                       .y = r.y + r.h / 2};
                     CHECK(ui_home_tabs_touch_hit(&t));
                     ui_home_tabs_input(0, UI_TOUCH_EVENT_TAP, &t, 0);
                     CHECK(active == order[i]);
                 }
                 if (theme == VT_UI_THEME_DECK_90) {
                     UiThemeHomeNav move = ui_theme_home_navigation(theme, 1, 6, SCE_CTRL_DOWN, 0);
                     CHECK(move.focused && move.tabs == SCE_CTRL_RIGHT && !move.content);
                     ui_home_tab_set(order[0]);
                     ui_home_tabs_input(move.tabs, 0, NULL, move.focused);
                     CHECK(active == order[1]);
                     move = ui_theme_home_navigation(theme, 1, 6, SCE_CTRL_RIGHT, SCE_CTRL_RIGHT);
                     CHECK(!move.focused && !move.tabs && !move.content);
                     move = ui_theme_home_navigation(theme, 0, 6, SCE_CTRL_LEFT, SCE_CTRL_LEFT);
                     CHECK(move.focused && !move.tabs && !move.content);
                     move = ui_theme_home_navigation(theme, 1, 0, SCE_CTRL_RIGHT, SCE_CTRL_RIGHT);
                     CHECK(move.focused && !move.tabs && !move.content);
                 }
                 /* Signal must restore its light palette after dark media overlays, also nested. */
                 const unsigned *before = ui_theme_colors, *saved = ui_theme_media_begin();
                 const unsigned *nested = ui_theme_media_begin();
                 ui_theme_media_end(nested);
                 ui_theme_media_end(saved);
                 CHECK(ui_theme_colors == before && ui_theme_id() == theme);
             }
 /* Independent sticks survive ownership and local/online transitions. */
 ui_theme_apply(VT_UI_THEME_GHOST_MESH);ui_home_controls_reset();loaded=1;
 ui_home_tab_set(HOME_LIBRARY);
 UiHomeControlSample pad={.lx=128,.ly=128,.rx=128,.ry=128,.count=8,.bottom=536,.focus_sections=1};
 frame=0;ui_home_controls_update(&pad);
 pad.rx=240;pad.ry=64;frame++;UiHomeControlResult routed=ui_home_controls_update(&pad);
 CHECK(routed.sections_changed&&active==HOME_VIDEO&&!routed.content_nav);
 pad.focus_sections=0;pad.ly=255;pad.pressed=SCE_CTRL_DOWN;pad.held=SCE_CTRL_DOWN;frame++;
 routed=ui_home_controls_update(&pad);CHECK(!routed.sections_changed&&active==HOME_VIDEO);
 CHECK((routed.content_nav&SCE_CTRL_DOWN)&&!routed.focus_sections);
 pad.ly=128;pad.pressed=pad.held=0;pad.rx=128;pad.ry=255;frame++;
 routed=ui_home_controls_update(&pad);CHECK(active==HOME_RADIO&&routed.sections_changed);
 /* The same held stick in the next screen cannot choose another section. */
 frame++;routed=ui_home_controls_update(&pad);CHECK(active==HOME_RADIO&&!routed.sections_changed);
 pad.ly=255;pad.pressed=pad.held=SCE_CTRL_DOWN;frame++;
 routed=ui_home_controls_update(&pad);CHECK(active==HOME_RADIO&&(routed.content_nav&SCE_CTRL_DOWN));
 pad.ly=128;pad.pressed=pad.held=0;pad.rx=18;pad.ry=192;frame++;
 routed=ui_home_controls_update(&pad);CHECK(active==HOME_PODCAST&&routed.sections_changed);
 pad.rx=18;pad.ry=64;pad.pressed=SCE_CTRL_CROSS;frame++;
 routed=ui_home_controls_update(&pad);CHECK(active==HOME_IMAGES&&routed.sections_changed&&!(routed.pressed&SCE_CTRL_CROSS));
 /* Modal layers require right-stick neutral before accepting another sector. */
 pad.blocked=1;frame++;routed=ui_home_controls_update(&pad);CHECK(!routed.pressed&&!routed.content_nav);
 pad.blocked=0;pad.pressed=0;pad.rx=240;frame++;routed=ui_home_controls_update(&pad);CHECK(active==HOME_IMAGES&&!routed.sections_changed);
 pad.rx=pad.ry=128;frame++;ui_home_controls_update(&pad);
 pad.rx=240;pad.ry=64;frame++;routed=ui_home_controls_update(&pad);CHECK(active==HOME_VIDEO&&routed.sections_changed);
 /* Up on the first content item never steals the right-hand wheel's axes. */
 pad.rx=pad.ry=128;pad.selected=0;pad.ly=0;pad.pressed=pad.held=SCE_CTRL_UP;frame++;
 routed=ui_home_controls_update(&pad);CHECK(!routed.focus_sections&&(routed.content_nav&SCE_CTRL_UP));
 /* All new themes keep full menus regardless of the previous screen's mode. */
 for (int theme = 1; theme < VT_UI_THEME_LEGACY_VITA; theme++) {
     ui_theme_apply(theme);
     ui_home_tabs_set_expanded(0);
     for (int i = 0; i < 6; i++) {
         UiThemeRect a = ui_theme_home_tab_rect(theme, i, 0),
                     b = ui_theme_home_tab_rect(theme, i, 1);
         CHECK(a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h && a.y >= 62);
     }
     for (int bottom = 472; bottom <= 536; bottom++)
         for (int index = 0; index < 40; index++) {
             int top = ui_theme_home_top(theme, index, 0, bottom);
             UiThemeHomeLayout layout = ui_theme_home_layout(theme, bottom);
             CHECK(index >= top && index < top + layout.rows && top % layout.columns == 0);
             UiThemeRect item = ui_theme_home_item_rect(theme, index - top, bottom);
             CHECK(item.x >= 18 && item.x + item.w <= 942 && item.y >= 62 &&
                   item.y + item.h < bottom - 30);
         }
 }

 face=inter;FT_Done_Face(mono);ui_home_tabs_set_expanded(0);ui_theme_apply(VT_UI_THEME_LEGACY);
 FT_Done_Face(face);FT_Done_FreeType(ft);printf("Home carousel native layout: %u checks passed\n",checks);
}
