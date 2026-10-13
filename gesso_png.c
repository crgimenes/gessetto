#include "gesso_png.h"

#include <string.h>

enum {
    ALPHA_OPAQUE = 128,
};

static int opaque(uint32_t argb) {
    return argb >> 24u >= ALPHA_OPAQUE;
}

/* The palette index of c, or 0 when it has none yet. */
static uint32_t find(const gesso_image *img, uint32_t c) {
    for (uint32_t k = 1; k < img->ncolors && k < GESSO_COLORS_MAX; k++) {
        if (img->palette[k] == c) {
            return k;
        }
    }
    return 0;
}

gesso_status gesso_from_png(gesso_image *img, const png_image *p) {
    if (p->width < 1 || p->height < 1 || p->width > GESSO_SIDE_MAX || p->height > GESSO_SIDE_MAX) {
        return GESSO_ERR_SIZE;
    }
    memset(img, 0, offsetof(gesso_image, pixels));
    img->width = p->width;
    img->height = p->height;
    img->ncolors = 1;
    img->nframes = 1;
    img->duration[0] = GESSO_DURATION_DEFAULT;
    uint8_t *px = img->pixels;
    size_t n = (size_t)p->width * (size_t)p->height;
    if (p->ncolors > 0) {
        uint8_t map[256] = {0};
        for (uint32_t i = 0; i < p->ncolors; i++) {
            if (!opaque(p->palette[i])) {
                continue;
            }
            if (img->ncolors == GESSO_COLORS_MAX) {
                return GESSO_ERR_PALETTE_FULL;
            }
            img->palette[img->ncolors] = p->palette[i] | 0xff000000u;
            map[i] = (uint8_t)img->ncolors++;
        }
        for (size_t i = 0; i < n; i++) {
            px[i] = map[p->index[i]];
        }
        return GESSO_OK;
    }
    for (size_t i = 0; i < n; i++) {
        uint32_t c = p->argb[i];
        if (!opaque(c)) {
            px[i] = 0;
            continue;
        }
        c |= 0xff000000u;
        uint32_t k = find(img, c);
        if (k == 0) {
            if (img->ncolors == GESSO_COLORS_MAX) {
                return GESSO_ERR_PALETTE_FULL;
            }
            k = img->ncolors++;
            img->palette[k] = c;
        }
        px[i] = (uint8_t)k;
    }
    return GESSO_OK;
}

gesso_status gesso_from_sheet(gesso_image *img, const uint32_t *argb, int32_t sw, int32_t sh,
                              int32_t w, int32_t h) {
    if (w < 1 || h < 1 || w > GESSO_SIDE_MAX || h > GESSO_SIDE_MAX || sw < w || sh < h) {
        return GESSO_ERR_SIZE;
    }
    int32_t cols = sw / w;
    int32_t rows = sh / h;
    if ((int64_t)cols * rows > GESSO_FRAMES_MAX) {
        return GESSO_ERR_FRAMES_FULL;
    }
    memset(img, 0, offsetof(gesso_image, pixels));
    img->width = w;
    img->height = h;
    img->ncolors = 1;
    img->nframes = (uint32_t)(cols * rows);
    for (uint32_t f = 0; f < img->nframes; f++) {
        img->duration[f] = GESSO_DURATION_DEFAULT;
        uint8_t *px = gesso_frame_pixels(img, f);
        int32_t ox = (int32_t)f % cols * w;
        int32_t oy = (int32_t)f / cols * h;
        for (int32_t y = 0; y < h; y++) {
            for (int32_t x = 0; x < w; x++) {
                uint32_t c = argb[(size_t)(oy + y) * (size_t)sw + (size_t)(ox + x)];
                uint8_t *dst = &px[(size_t)y * (size_t)w + (size_t)x];
                if (!opaque(c)) {
                    *dst = 0;
                    continue;
                }
                c |= 0xff000000u;
                uint32_t k = find(img, c);
                if (k == 0) {
                    if (img->ncolors == GESSO_COLORS_MAX) {
                        return GESSO_ERR_PALETTE_FULL;
                    }
                    k = img->ncolors++;
                    img->palette[k] = c;
                }
                *dst = (uint8_t)k;
            }
        }
    }
    return GESSO_OK;
}

uint8_t gesso_nearest(const gesso_image *img, uint32_t argb) {
    if (!opaque(argb)) {
        return 0;
    }
    uint32_t best = 0;
    uint32_t best_d = UINT32_MAX;
    for (uint32_t k = 1; k < img->ncolors && k < GESSO_COLORS_MAX; k++) {
        uint32_t d = 0;
        for (uint32_t shift = 0; shift < 24; shift += 8) {
            int32_t a = (int32_t)(argb >> shift & 0xffu);
            int32_t b = (int32_t)(img->palette[k] >> shift & 0xffu);
            d += (uint32_t)((a - b) * (a - b));
        }
        if (d < best_d) {
            best = k;
            best_d = d;
        }
    }
    return (uint8_t)best;
}
