#include "settings/preferences.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "ui/page_motion.h"
static unsigned checks;
#define CHECK(x) do {checks++;assert(x);} while(0)
int main(void){
    for (int theme = 0; theme < VT_UI_THEME_COUNT; theme++)
        for (int cols = 1; cols <= 5; cols++)
            for (int rows = 1; rows <= 8; rows++) {
                UiPageMotion m = {0};
                uint64_t now = 1;
                int slots = cols * rows, count = 700;
                UiPageView v =
                    ui_page_update(&m, now, theme, 0, "files", "/root", 0, count, cols, slots, 54);
                CHECK(v.first == 0 && v.end == slots && v.dy == 0);
                for (int tick = 0; tick < 360; tick++) {
                    now += 16667;
                    int first = (tick / 3 % 2 ? rows + 12 : rows + 3) * cols;
                    if (tick > 230)
                        first = 17 * cols;
                    v = ui_page_update(&m, now, theme, 0, "files", "/root", first, count, cols,
                                       slots, 54);
                    CHECK(isfinite(v.dy) && isfinite(v.dx) && isfinite(m.velocity));
                    CHECK(v.first >= 0 && v.end <= count && v.end >= v.first);
                    CHECK(v.end - v.first <= (rows + 1) * cols);
                    CHECK(fabsf(v.dy) <= (rows + 1) * 54);
                    if (tick > 280)
                        CHECK(fabsf(v.dy) < .01f);
                }
                float before = m.row;
                now += 16667;
                ui_page_update(&m, now, theme, 0, "files", "/root", 17 * cols, count + 10, cols,
                               slots, 54);
                CHECK(fabsf(m.row - before) < .01f); /* Cache append does not replay an entrance. */
                float camera = m.camera;
                v = ui_page_update(&m, now, theme, 0, "files", "/root/child", 0, count, cols, slots,
                                   54);
                CHECK(m.row == 0 && m.camera == camera &&
                      v.first == 0); /* Context changes are position-continuous. */
                for (int tick = 0; tick < 25; tick++) {
                    now += 16667;
                    camera = m.camera;
                    v = ui_page_update(&m, now, theme, 0, "files",
                                       tick % 2 ? "/root" : "/root/child", 0, count, cols, slots,
                                       54);
                    CHECK(fabsf(m.camera - camera) < 6.7f && fabsf(m.camera) <= 12);
                }
                for (int tick = 0; tick < 200; tick++) {
                    now += 16667;
                    ui_page_update(&m, now, theme, 0, "files", tick % 2 ? "A01" : "A02", 0, count,
                                   cols, slots, 54);
                    CHECK(fabsf(m.camera) <=
                          12); /* Repeated equal-length menu identities cannot drift. */
                }
                now += 16667;
                v = ui_page_update(&m, now, theme, 1, "files", "/root", 19 * cols, count, cols,
                                   slots, 54);
                CHECK(v.dy == 0 && v.dx == 0 && v.energy == 0 && m.row == 19);
                now += 500000;
                v = ui_page_update(&m, now, theme, 0, "files", "/root", 5 * cols, count, cols,
                                   slots, 54);
                CHECK(v.dy == 0);
                now = 0;
                v = ui_page_update(&m, now, theme, 0, "files", "/root", 0, count, cols, slots, 54);
                CHECK(v.dy == 0);
                now += 16667;
                v = ui_page_update(&m, now, theme, 0, "files", "/root", 0, 0, cols, slots, 54);
                CHECK(v.first == 0 && v.end == 0);
            }
 puts("Continuous pages: rapid reversals, bounded overscan, finite settle, cache refresh, context continuity, reduced motion, suspend and empty data passed.");
 printf("%u checks\n",checks);
}
