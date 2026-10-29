#include "ui/input_prompts.h"
#include <vita2d.h>
#include <stdio.h>
#include "common/text_log.h"

static vita2d_texture *textures[UI_PAD_COUNT];
static const char *const filenames[UI_PAD_COUNT] = {
    "playstation_button_color_cross_outline.png",
    "playstation_button_color_circle_outline.png",
    "playstation_button_color_square_outline.png",
    "playstation_button_color_triangle_outline.png",
    "playstation_trigger_l1_alternative.png",
    "playstation_trigger_r1_alternative.png"
};

int ui_input_prompts_load(void) {
    int loaded = 0;
    for (int i = 0; i < UI_PAD_COUNT; i++) {
        if (!textures[i]) {
            char path[128];
            snprintf(path, sizeof(path), "app0:input-prompts/%s", filenames[i]);
            textures[i] = vita2d_load_PNG_file(path);
            if (!textures[i]) log_printf("input prompt unavailable: %s", filenames[i]);
        }
        loaded += textures[i] != NULL;
    }
    return loaded == UI_PAD_COUNT ? 0 : -1;
}

void ui_input_prompts_free(void) {
    for (int i = 0; i < UI_PAD_COUNT; i++) {
        if (textures[i]) vita2d_free_texture(textures[i]);
        textures[i] = NULL;
    }
}

int ui_input_prompt_draw(int id, int x, int baseline, unsigned size, unsigned alpha) {
    if (id < 0 || id >= UI_PAD_COUNT || !textures[id] || !size) return 0;
    if (alpha > 255) alpha = 255;
    /* Crop only the upstream transparent gutter. The visible face buttons
     * occupy 48x48, shoulder caps 48x40; keep their original proportions. */
    float width = ui_input_prompt_width(id, size) - UI_PAD_GUTTER;
    float height = id >= 4 ? width * 40 / 48 : width;
    /* Shoulder lettering needs the line's descender space to stay legible. */
    if (height > size + 4) { width *= (size + 4) / height; height = size + 4; }
    float left = x + (ui_input_prompt_width(id, size) - width) * .5f;
    float top = (float)baseline - size + 2 + (size - height) * .5f;
    /* A dark backing keeps the original colours visible on light themes and
     * the cut-out L1/R1 lettering readable, while staying inside the slot. */
    if (id < 4)
        vita2d_draw_fill_circle(left + width * .5f, top + height * .5f,
                                width * .5f, RGBA8(9, 14, 25, alpha));
    else
        vita2d_draw_rectangle(left, top, width, height, RGBA8(9, 14, 25, alpha));
    vita2d_draw_texture_tint_part_scale(textures[id], left, top, 8, id >= 4 ? 12 : 8,
        48, id >= 4 ? 40 : 48, width / 48, height / (id >= 4 ? 40 : 48),
        RGBA8(255, 255, 255, alpha));
    return 1;
}
