#ifndef VMD_ARTWORK_PIXELS_H
#define VMD_ARTWORK_PIXELS_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Complete artwork, contained inside a square. Video thumbnails deliberately
 * retain their separate 16:9 crop. Alpha composites onto the OLED black mat. */
static inline int vt_artwork_fit_rgb565(const unsigned char *src, unsigned stride,
    unsigned width, unsigned height, unsigned channels, uint16_t *out, unsigned edge) {
    if (!src || !out || !width || !height || !edge || (channels != 3 && channels != 4) ||
        (uint64_t)width * channels > stride) return -1;
    unsigned longest = width > height ? width : height;
    unsigned w = (uint64_t)width * edge / longest, h = (uint64_t)height * edge / longest;
    if (!w) w = 1;
    if (!h) h = 1;
    unsigned left = (edge - w) / 2, top = (edge - h) / 2;
    memset(out, 0, (size_t)edge * edge * sizeof(*out));
    for (unsigned y = 0; y < h; y++) for (unsigned x = 0; x < w; x++) {
        unsigned sx = ((uint64_t)x * 2 + 1) * width / (2 * w);
        unsigned sy = ((uint64_t)y * 2 + 1) * height / (2 * h);
        const unsigned char *p = src + (size_t)sy * stride + (size_t)sx * channels;
        unsigned a = channels == 4 ? p[3] : 255;
        unsigned r = p[0] * a / 255, g = p[1] * a / 255, b = p[2] * a / 255;
        out[(top+y)*edge+left+x] = ((r>>3)<<11) | ((g>>2)<<5) | (b>>3);
    }
    return 0;
}
#endif
