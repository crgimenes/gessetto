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

/* Median cut, for an image with more colors than a palette holds: the
   colors at 5 bits a channel are counted, the box of them with the most
   pixels is cut at its median across its longest side until there are 255
   boxes, and each pixel takes its box's mean. */
enum {
    BINS = 32768,
    BOXES = GESSO_COLORS_MAX - 1,
};

typedef struct {
    int32_t lo[3];
    int32_t hi[3];
    uint32_t count;
} box;

static uint32_t hist[BINS];
static uint8_t binmap[BINS];
static box boxes[BOXES];

static uint32_t bin(uint32_t c) {
    return (c >> 9u & 0x7c00u) | (c >> 6u & 0x3e0u) | (c >> 3u & 0x1fu);
}

static uint32_t cell(const int32_t v[3]) {
    return (uint32_t)v[0] << 10u | (uint32_t)v[1] << 5u | (uint32_t)v[2];
}

/* The box's counts, and its bounds pulled in to the cells that have any. */
static void shrink(box *b, uint32_t slices[32], int axis) {
    int32_t lo[3] = {31, 31, 31};
    int32_t hi[3] = {0, 0, 0};
    uint32_t count = 0;
    int32_t v[3];
    for (v[0] = b->lo[0]; v[0] <= b->hi[0]; v[0]++) {
        for (v[1] = b->lo[1]; v[1] <= b->hi[1]; v[1]++) {
            for (v[2] = b->lo[2]; v[2] <= b->hi[2]; v[2]++) {
                uint32_t n = hist[cell(v)];
                if (n == 0) {
                    continue;
                }
                count += n;
                if (slices != NULL) {
                    slices[v[axis]] += n;
                }
                for (int a = 0; a < 3; a++) {
                    lo[a] = v[a] < lo[a] ? v[a] : lo[a];
                    hi[a] = v[a] > hi[a] ? v[a] : hi[a];
                }
            }
        }
    }
    memcpy(b->lo, lo, sizeof lo);
    memcpy(b->hi, hi, sizeof hi);
    b->count = count;
}

static uint32_t expand(int32_t v) {
    return (uint32_t)v << 3u | (uint32_t)v >> 2u;
}

/* The palette from hist: 255 colors at most, binmap sending each bin to
   its index. */
static void cut(gesso_image *img) {
    uint32_t n = 1;
    boxes[0] = (box){{0, 0, 0}, {31, 31, 31}, 0};
    shrink(&boxes[0], NULL, 0);
    while (n < BOXES) {
        uint32_t pick = BOXES;
        for (uint32_t i = 0; i < n; i++) {
            const box *b = &boxes[i];
            int flat = b->lo[0] == b->hi[0] && b->lo[1] == b->hi[1] && b->lo[2] == b->hi[2];
            if (!flat && (pick == BOXES || b->count > boxes[pick].count)) {
                pick = i;
            }
        }
        if (pick == BOXES) {
            break;
        }
        box *b = &boxes[pick];
        int axis = 0;
        for (int a = 1; a < 3; a++) {
            if (b->hi[a] - b->lo[a] > b->hi[axis] - b->lo[axis]) {
                axis = a;
            }
        }
        uint32_t slices[32] = {0};
        shrink(b, slices, axis);
        int32_t at = b->lo[axis];
        uint32_t sum = slices[at];
        while (at + 1 < b->hi[axis] && sum * 2u < b->count) {
            sum += slices[++at];
        }
        boxes[n] = *b;
        boxes[n].lo[axis] = at + 1;
        b->hi[axis] = at;
        shrink(b, NULL, 0);
        shrink(&boxes[n], NULL, 0);
        n++;
    }
    img->ncolors = n + 1;
    for (uint32_t i = 0; i < n; i++) {
        const box *b = &boxes[i];
        uint64_t sum[3] = {0, 0, 0};
        int32_t v[3];
        for (v[0] = b->lo[0]; v[0] <= b->hi[0]; v[0]++) {
            for (v[1] = b->lo[1]; v[1] <= b->hi[1]; v[1]++) {
                for (v[2] = b->lo[2]; v[2] <= b->hi[2]; v[2]++) {
                    uint32_t k = cell(v);
                    binmap[k] = (uint8_t)(i + 1);
                    for (int a = 0; a < 3; a++) {
                        sum[a] += (uint64_t)hist[k] * expand(v[a]);
                    }
                }
            }
        }
        uint32_t rgb = 0xff000000u;
        for (int a = 0; a < 3; a++) {
            rgb |= (uint32_t)((sum[a] + b->count / 2) / b->count) << (16u - 8u * (uint32_t)a);
        }
        img->palette[i + 1] = rgb;
    }
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
                memset(hist, 0, sizeof hist);
                for (size_t j = 0; j < n; j++) {
                    uint32_t c = p->palette[p->index[j]];
                    hist[bin(c)] += opaque(c) ? 1u : 0u;
                }
                cut(img);
                for (size_t j = 0; j < n; j++) {
                    uint32_t c = p->palette[p->index[j]];
                    px[j] = opaque(c) ? binmap[bin(c)] : 0;
                }
                return GESSO_OK;
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
                memset(hist, 0, sizeof hist);
                for (size_t j = 0; j < n; j++) {
                    hist[bin(p->argb[j])] += opaque(p->argb[j]) ? 1u : 0u;
                }
                cut(img);
                for (size_t j = 0; j < n; j++) {
                    px[j] = opaque(p->argb[j]) ? binmap[bin(p->argb[j])] : 0;
                }
                return GESSO_OK;
            }
            k = img->ncolors++;
            img->palette[k] = c;
        }
        px[i] = (uint8_t)k;
    }
    return GESSO_OK;
}

/* The sheet cut into img's frames: 0 when it has more colors than a
   palette holds, unless quantized, when binmap gives each index. */
static int tiles(gesso_image *img, const uint32_t *argb, int32_t sw, int32_t cols, int quantized) {
    int32_t w = img->width;
    int32_t h = img->height;
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
                if (quantized) {
                    *dst = binmap[bin(c)];
                    continue;
                }
                c |= 0xff000000u;
                uint32_t k = find(img, c);
                if (k == 0) {
                    if (img->ncolors == GESSO_COLORS_MAX) {
                        return 0;
                    }
                    k = img->ncolors++;
                    img->palette[k] = c;
                }
                *dst = (uint8_t)k;
            }
        }
    }
    return 1;
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
    if (tiles(img, argb, sw, cols, 0)) {
        return GESSO_OK;
    }
    memset(hist, 0, sizeof hist);
    for (int32_t y = 0; y < rows * h; y++) {
        for (int32_t x = 0; x < cols * w; x++) {
            uint32_t c = argb[(size_t)y * (size_t)sw + (size_t)x];
            hist[bin(c)] += opaque(c) ? 1u : 0u;
        }
    }
    cut(img);
    (void)tiles(img, argb, sw, cols, 1);
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
