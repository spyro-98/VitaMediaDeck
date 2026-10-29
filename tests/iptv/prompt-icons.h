/* Real downloaded PNGs in host layout fixtures; firmware/GXM are substituted. */
#define link host_posix_link
#include <unistd.h>
#undef link
static char host_prompt_root[1024];
__attribute__((constructor)) static void host_prompt_paths(void) {
    if (!getcwd(host_prompt_root, sizeof(host_prompt_root))) abort();
}
static void host_prompt_draw(int id, int x, int baseline, unsigned size, unsigned color) {
    static struct { int loaded; unsigned char rgba[64 * 64 * 4]; } images[6];
    static const char *const names[] = {
        "playstation_button_color_cross_outline", "playstation_button_color_circle_outline",
        "playstation_button_color_square_outline", "playstation_button_color_triangle_outline",
        "playstation_trigger_l1_alternative", "playstation_trigger_r1_alternative"
    };
    if (!images[id].loaded) {
        char path[1280];
        snprintf(path, sizeof(path), "%s/assets/input-prompts/%s.png", host_prompt_root, names[id]);
        png_image image = {.version = PNG_IMAGE_VERSION};
        CHECK(png_image_begin_read_from_file(&image, path));
        image.format = PNG_FORMAT_RGBA;
        CHECK(image.width == 64 && image.height == 64);
        CHECK(png_image_finish_read(&image, NULL, images[id].rgba, 0, NULL));
        png_image_free(&image);
        images[id].loaded = 1;
    }
    int slot = ui_input_prompt_width(id, size);
    int w = slot - UI_PAD_GUTTER, h = id >= 4 ? w * 40 / 48 : w;
    if(h>(int)size+4){w=w*((int)size+4)/h;h=(int)size+4;}
    int left = x + (slot - w) / 2, top = baseline - (int)size + 2 + ((int)size-h)/2;
    for (int y = 0; y < h; y++) for (int xx = 0; xx < w; xx++) {
        int sx = 8 + xx * 48 / w, sy = (id >= 4 ? 12 : 8) + y * (id >= 4 ? 40 : 48) / h;
        const unsigned char *p = images[id].rgba + (sy * 64 + sx) * 4;
        pixel(left + xx, top + y, RGBA8(9,14,25,color >> 24), 255);
        pixel(left + xx, top + y, RGBA8(p[0],p[1],p[2],p[3]), color >> 24);
    }
}
