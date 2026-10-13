#include "gesso.h"

#include <string.h>

/* History entries sit back to back in the log: a head, the before bytes,
   the after bytes, padding to 8, and the entry's whole length as a footer,
   so undo can walk back from top. A pixel entry holds a rectangle of one
   frame; an image entry holds the whole image (metadata and every frame),
   for the rare edits that change its shape. */
enum {
    ENTRY_PIXELS = 1,
    ENTRY_IMAGE = 2,
    FOOTER = 8,
};

#define META offsetof(gesso_image, pixels)
#define NONE ((size_t)-1)

typedef struct {
    uint32_t kind;
    uint32_t group;
    uint64_t serial;
    uint32_t frame;
    gesso_rect r;
    uint32_t before;
    uint32_t after;
} head;

typedef struct {
    size_t at;
    gesso_rect r;
    uint32_t frame;
} pixel_edit;

typedef struct {
    size_t at;
    size_t before;
} image_edit;

static const uint32_t pico8[16] = {
    0xff000000u, 0xff1d2b53u, 0xff7e2553u, 0xff008751u, 0xffab5236u, 0xff5f574fu,
    0xffc2c3c7u, 0xfffff1e8u, 0xffff004du, 0xffffa300u, 0xffffec27u, 0xff00e436u,
    0xff29adffu, 0xff83769cu, 0xffff77a8u, 0xffffccaau,
};

static const char *const messages[] = {
    [GESSO_OK] = "ok",
    [GESSO_ERR_NO_IMAGE] = "no image",
    [GESSO_ERR_SIZE] = "image size out of range",
    [GESSO_ERR_COORD] = "coordinate out of range",
    [GESSO_ERR_COLOR] = "color index out of range",
    [GESSO_ERR_FRAME] = "frame out of range",
    [GESSO_ERR_FRAMES_FULL] = "too many frames",
    [GESSO_ERR_LAST_FRAME] = "cannot delete the only frame",
    [GESSO_ERR_DURATION] = "duration out of range",
    [GESSO_ERR_PALETTE_FULL] = "palette full",
    [GESSO_ERR_TAG_NAME] = "bad tag name",
    [GESSO_ERR_TAG_RANGE] = "tag range out of frames",
    [GESSO_ERR_TAGS_FULL] = "too many tags",
    [GESSO_ERR_NO_TAG] = "no such tag",
    [GESSO_ERR_NO_SELECTION] = "nothing selected",
    [GESSO_ERR_CLIPBOARD] = "clipboard empty",
    [GESSO_ERR_TAG_DIRECTION] = "bad tag direction",
};

const char *gesso_message(gesso_status s) {
    if ((size_t)s >= sizeof messages / sizeof messages[0]) {
        return "unknown error";
    }
    return messages[s];
}

static size_t frame_size(const gesso_image *img) {
    return (size_t)img->width * (size_t)img->height;
}

// cppcheck-suppress constParameterPointer -- returns a writable pointer into img
uint8_t *gesso_frame_pixels(gesso_image *img, uint32_t frame) {
    return img->pixels + (size_t)frame * frame_size(img);
}

static const uint8_t *frame_at(const gesso_image *img, uint32_t frame) {
    return img->pixels + (size_t)frame * frame_size(img);
}

/* ---- history ---- */

static size_t entry_len(size_t before, size_t after) {
    return ((sizeof(head) + before + after + 7u) & ~(size_t)7u) + FOOTER;
}

static head read_head(const gesso_doc *d, size_t at) {
    head h;
    memcpy(&h, d->log + at, sizeof h);
    return h;
}

static size_t read_footer(const gesso_doc *d, size_t end) {
    uint64_t n;
    memcpy(&n, d->log + end - FOOTER, sizeof n);
    return (size_t)n;
}

static void commit(gesso_doc *d, size_t at, head h) {
    h.group = d->depth > 0 ? d->group : ++d->group_next;
    h.serial = ++d->serial;
    size_t len = entry_len(h.before, h.after);
    uint64_t n = len;
    memcpy(d->log + at, &h, sizeof h);
    memcpy(d->log + at + len - FOOTER, &n, sizeof n);
    d->top = at + len;
    d->end = d->top;
}

static uint64_t current(const gesso_doc *d) {
    if (d->top == 0) {
        return d->floor;
    }
    return read_head(d, d->top - read_footer(d, d->top)).serial;
}

static void drop_oldest(gesso_doc *d) {
    uint32_t group = read_head(d, 0).group;
    size_t at = 0;
    while (at < d->top) {
        head h = read_head(d, at);
        if (h.group != group) {
            break;
        }
        d->floor = h.serial;
        at += entry_len(h.before, h.after);
    }
    memmove(d->log, d->log + at, d->top - at);
    d->top -= at;
    d->end = d->top;
}

/* Room for an entry of need bytes at top, dropping redo and then the oldest
   steps. False when the entry cannot fit at all: the edit happens without
   history, and the history goes, since it no longer leads to this state. */
static int reserve(gesso_doc *d, size_t need) {
    d->end = d->top;
    if (need > d->cap) {
        d->top = 0;
        d->end = 0;
        d->floor = ++d->serial;
        return 0;
    }
    while (d->cap - d->top < need) {
        drop_oldest(d);
    }
    return 1;
}

static void copy_out(const gesso_image *img, uint32_t frame, gesso_rect r, uint8_t *dst) {
    const uint8_t *f = frame_at(img, frame);
    for (int32_t y = 0; y < r.h; y++) {
        memcpy(dst + (size_t)y * (size_t)r.w,
               f + (size_t)(r.y + y) * (size_t)img->width + (size_t)r.x, (size_t)r.w);
    }
}

static void copy_in(gesso_image *img, uint32_t frame, gesso_rect r, const uint8_t *src) {
    uint8_t *f = gesso_frame_pixels(img, frame);
    for (int32_t y = 0; y < r.h; y++) {
        memcpy(f + (size_t)(r.y + y) * (size_t)img->width + (size_t)r.x,
               src + (size_t)y * (size_t)r.w, (size_t)r.w);
    }
}

static pixel_edit pixel_begin(gesso_doc *d, gesso_rect r) {
    pixel_edit e = {NONE, r, d->frame};
    size_t n = (size_t)r.w * (size_t)r.h;
    if (!reserve(d, entry_len(n, n))) {
        return e;
    }
    e.at = d->top;
    copy_out(&d->img, e.frame, r, d->log + e.at + sizeof(head));
    return e;
}

static void pixel_end(gesso_doc *d, pixel_edit e) {
    if (e.at == NONE) {
        return;
    }
    size_t n = (size_t)e.r.w * (size_t)e.r.h;
    uint8_t *before = d->log + e.at + sizeof(head);
    copy_out(&d->img, e.frame, e.r, before + n);
    if (memcmp(before, before + n, n) == 0) {
        return;
    }
    commit(d, e.at,
           (head){
               .kind = ENTRY_PIXELS,
               .frame = e.frame,
               .r = e.r,
               .before = (uint32_t)n,
               .after = (uint32_t)n,
           });
}

static size_t snap_size(const gesso_image *img) {
    return META + (size_t)img->nframes * frame_size(img);
}

static void snap_write(uint8_t *dst, const gesso_image *img) {
    memcpy(dst, img, META);
    memcpy(dst + META, img->pixels, (size_t)img->nframes * frame_size(img));
}

static void snap_read(gesso_image *img, const uint8_t *src) {
    memcpy(img, src, META);
    memcpy(img->pixels, src + META, (size_t)img->nframes * frame_size(img));
}

static image_edit image_begin_sized(gesso_doc *d, size_t after_max) {
    const gesso_image *img = &d->img;
    image_edit e = {NONE, snap_size(img)};
    if (!reserve(d, entry_len(e.before, after_max))) {
        return e;
    }
    e.at = d->top;
    snap_write(d->log + e.at + sizeof(head), img);
    return e;
}

/* Room for one more frame of the same size, the most any edit but a resize
   adds. */
static image_edit image_begin(gesso_doc *d) {
    const gesso_image *img = &d->img;
    uint32_t frames = img->nframes < GESSO_FRAMES_MAX ? img->nframes + 1 : GESSO_FRAMES_MAX;
    return image_begin_sized(d, META + (size_t)frames * frame_size(img));
}

static void image_end(gesso_doc *d, image_edit e) {
    if (e.at == NONE) {
        return;
    }
    size_t after = snap_size(&d->img);
    uint8_t *before = d->log + e.at + sizeof(head);
    snap_write(before + e.before, &d->img);
    if (after == e.before && memcmp(before, before + e.before, after) == 0) {
        return;
    }
    commit(d, e.at,
           (head){.kind = ENTRY_IMAGE, .before = (uint32_t)e.before, .after = (uint32_t)after});
}

static void apply(gesso_doc *d, size_t at, int after) {
    head h = read_head(d, at);
    const uint8_t *p = d->log + at + sizeof(head) + (after ? h.before : 0);
    if (h.kind == ENTRY_PIXELS) {
        copy_in(&d->img, h.frame, h.r, p);
        return;
    }
    snap_read(&d->img, p);
}

static void settle(gesso_doc *d) {
    d->selected = 0;
    d->floating = 0;
    d->depth = 0;
    if (d->frame >= d->img.nframes) {
        d->frame = d->img.nframes - 1;
    }
}

void gesso_begin(gesso_doc *d) {
    if (d->depth++ == 0) {
        d->group = ++d->group_next;
    }
}

void gesso_end(gesso_doc *d) {
    if (d->depth > 0) {
        d->depth--;
    }
}

/* Takes back a group's entries: they leave the history instead of becoming
   redo. */
static void revert_group(gesso_doc *d, uint32_t group) {
    while (d->top > 0) {
        size_t at = d->top - read_footer(d, d->top);
        if (read_head(d, at).group != group) {
            break;
        }
        apply(d, at, 0);
        d->top = at;
    }
    d->end = d->top;
}

static void cancel_float(gesso_doc *d) {
    revert_group(d, d->float_group);
    settle(d);
}

void gesso_revert(gesso_doc *d) {
    if (d->depth > 0) {
        revert_group(d, d->group);
    }
}

int gesso_undo(gesso_doc *d) {
    if (d->floating) {
        cancel_float(d);
        return 1;
    }
    d->depth = 0;
    if (d->top == 0) {
        return 0;
    }
    uint32_t group = read_head(d, d->top - read_footer(d, d->top)).group;
    while (d->top > 0) {
        size_t at = d->top - read_footer(d, d->top);
        if (read_head(d, at).group != group) {
            break;
        }
        apply(d, at, 0);
        d->top = at;
    }
    settle(d);
    return 1;
}

uint32_t gesso_step(const gesso_doc *d) {
    if (d->floating) {
        return d->float_group;
    }
    if (d->top == 0) {
        return 0;
    }
    return read_head(d, d->top - read_footer(d, d->top)).group;
}

int gesso_redo(gesso_doc *d) {
    gesso_drop(d);
    d->depth = 0;
    if (d->top == d->end) {
        return 0;
    }
    uint32_t group = read_head(d, d->top).group;
    while (d->top < d->end) {
        head h = read_head(d, d->top);
        if (h.group != group) {
            break;
        }
        apply(d, d->top, 1);
        d->top += entry_len(h.before, h.after);
    }
    settle(d);
    return 1;
}

void gesso_mark_saved(gesso_doc *d) {
    d->saved = current(d);
}

int gesso_dirty(const gesso_doc *d) {
    return current(d) != d->saved;
}

/* ---- document ---- */

static void reset(gesso_doc *d) {
    d->frame = 0;
    d->selected = 0;
    d->floating = 0;
    d->transparent = 0;
    d->top = 0;
    d->end = 0;
    d->depth = 0;
    d->floor = ++d->serial;
    d->saved = d->floor;
}

/* Only the fields that matter are zeroed: the doc is megabytes of buffers
   whose contents are always written before they are read. */
void gesso_init(gesso_doc *d, uint8_t *log, size_t cap) {
    d->img.width = 0;
    d->img.height = 0;
    d->img.nframes = 0;
    d->lifted.w = 0;
    d->lifted.h = 0;
    d->clip.w = 0;
    d->clip.h = 0;
    d->log = log;
    d->cap = cap;
    d->brush = 1;
    d->brush_round = 0;
    d->group = 0;
    d->group_next = 0;
    d->float_group = 0;
    d->serial = 0;
    reset(d);
}

gesso_status gesso_new(gesso_doc *d, int32_t width, int32_t height) {
    if (width < 1 || height < 1 || width > GESSO_SIDE_MAX || height > GESSO_SIDE_MAX) {
        return GESSO_ERR_SIZE;
    }
    gesso_image *img = &d->img;
    memset(img, 0, META);
    img->width = width;
    img->height = height;
    img->ncolors = 1 + sizeof pico8 / sizeof pico8[0];
    memcpy(img->palette + 1, pico8, sizeof pico8);
    img->nframes = 1;
    img->duration[0] = GESSO_DURATION_DEFAULT;
    memset(img->pixels, 0, frame_size(img));
    reset(d);
    return GESSO_OK;
}

void gesso_forget(gesso_doc *d) {
    reset(d);
}

void gesso_adopt(gesso_doc *d, const gesso_image *img) {
    memcpy(&d->img, img, META);
    memcpy(d->img.pixels, img->pixels, (size_t)img->nframes * frame_size(img));
    reset(d);
}

static gesso_status ready(const gesso_doc *d) {
    return d->img.nframes == 0 ? GESSO_ERR_NO_IMAGE : GESSO_OK;
}

static int coord_ok(int32_t v) {
    return v >= -GESSO_COORD_MAX && v <= GESSO_COORD_MAX;
}

static gesso_status check(const gesso_doc *d, uint8_t c, int32_t x0, int32_t y0, int32_t x1,
                          int32_t y1) {
    gesso_status st = ready(d);
    if (st != GESSO_OK) {
        return st;
    }
    if (c >= d->img.ncolors) {
        return GESSO_ERR_COLOR;
    }
    if (!coord_ok(x0) || !coord_ok(y0) || !coord_ok(x1) || !coord_ok(y1)) {
        return GESSO_ERR_COORD;
    }
    return GESSO_OK;
}

static gesso_rect corners(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    gesso_rect r = {x0 < x1 ? x0 : x1, y0 < y1 ? y0 : y1, 0, 0};
    r.w = (x0 < x1 ? x1 - x0 : x0 - x1) + 1;
    r.h = (y0 < y1 ? y1 - y0 : y0 - y1) + 1;
    return r;
}

static gesso_rect clip(const gesso_doc *d, gesso_rect r) {
    int32_t x0 = r.x < 0 ? 0 : r.x;
    int32_t y0 = r.y < 0 ? 0 : r.y;
    int32_t x1 = r.x + r.w > d->img.width ? d->img.width : r.x + r.w;
    int32_t y1 = r.y + r.h > d->img.height ? d->img.height : r.y + r.h;
    gesso_rect c = {x0, y0, x1 - x0, y1 - y0};
    if (c.w <= 0 || c.h <= 0) {
        c.w = 0;
        c.h = 0;
    }
    return c;
}

static void plot(gesso_doc *d, int32_t x, int32_t y, uint8_t c) {
    if (x < 0 || y < 0 || x >= d->img.width || y >= d->img.height) {
        return;
    }
    gesso_frame_pixels(&d->img, d->frame)[(size_t)y * (size_t)d->img.width + (size_t)x] = c;
}

void gesso_brush(gesso_doc *d, uint32_t size, int round) {
    d->brush = size < 1 ? 1 : size;
    if (d->brush > GESSO_BRUSH_MAX) {
        d->brush = GESSO_BRUSH_MAX;
    }
    d->brush_round = round != 0;
}

/* The pen at (x, y). Round keeps the cells whose centers lie within the
   circle of diameter size, in doubled units to stay in integers. */
static void dab(gesso_doc *d, int32_t x, int32_t y, uint8_t c) {
    int32_t n = (int32_t)d->brush;
    int32_t lo = (n - 1) / 2;
    for (int32_t j = 0; j < n; j++) {
        for (int32_t i = 0; i < n; i++) {
            int32_t u = 2 * i - (n - 1);
            int32_t v = 2 * j - (n - 1);
            if (d->brush_round && u * u + v * v > n * n) {
                continue;
            }
            plot(d, x - lo + i, y - lo + j, c);
        }
    }
}

/* r grown by the pen's reach, for what a stroke may touch. */
static gesso_rect reach(const gesso_doc *d, gesso_rect r) {
    int32_t lo = ((int32_t)d->brush - 1) / 2;
    int32_t hi = (int32_t)d->brush / 2;
    return (gesso_rect){r.x - lo, r.y - lo, r.w + lo + hi, r.h + lo + hi};
}

/* A filled row, or an outline's two ends stamped with the pen. */
static void span(gesso_doc *d, int32_t xa, int32_t xb, int32_t y, uint8_t c, int filled) {
    if (!filled) {
        dab(d, xa, y, c);
        dab(d, xb, y, c);
        return;
    }
    for (int32_t x = xa; x <= xb; x++) {
        plot(d, x, y, c);
    }
}

uint8_t gesso_view(const gesso_doc *d, int32_t x, int32_t y) {
    const gesso_image *img = &d->img;
    if (img->nframes == 0 || x < 0 || y < 0 || x >= img->width || y >= img->height) {
        return 0;
    }
    if (d->floating && x >= d->sel.x && y >= d->sel.y && x < d->sel.x + d->sel.w &&
        y < d->sel.y + d->sel.h) {
        uint8_t v =
            d->lifted.pixels[(size_t)(y - d->sel.y) * (size_t)d->lifted.w + (size_t)(x - d->sel.x)];
        if (v != 0 || !d->transparent) {
            return v;
        }
    }
    return frame_at(img, d->frame)[(size_t)y * (size_t)img->width + (size_t)x];
}

/* ---- pixel edits ---- */

gesso_status gesso_pixel(gesso_doc *d, int32_t x, int32_t y, uint8_t c) {
    return gesso_line(d, x, y, x, y, c);
}

gesso_status gesso_line(gesso_doc *d, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t c) {
    gesso_status st = check(d, c, x0, y0, x1, y1);
    if (st != GESSO_OK) {
        return st;
    }
    gesso_drop(d);
    gesso_rect r = clip(d, reach(d, corners(x0, y0, x1, y1)));
    if (r.w == 0) {
        return GESSO_OK;
    }
    pixel_edit e = pixel_begin(d, r);
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy;
    for (;;) {
        dab(d, x0, y0, c);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int32_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
    pixel_end(d, e);
    return GESSO_OK;
}

gesso_status gesso_rect_draw(gesso_doc *d, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                             uint8_t c, int filled) {
    gesso_status st = check(d, c, x0, y0, x1, y1);
    if (st != GESSO_OK) {
        return st;
    }
    gesso_drop(d);
    gesso_rect box = corners(x0, y0, x1, y1);
    gesso_rect r = clip(d, filled ? box : reach(d, box));
    if (r.w == 0) {
        return GESSO_OK;
    }
    pixel_edit e = pixel_begin(d, r);
    int32_t right = box.x + box.w - 1;
    int32_t bottom = box.y + box.h - 1;
    for (int32_t y = box.y; y <= bottom; y++) {
        if (filled || (y != box.y && y != bottom)) {
            span(d, box.x, right, y, c, filled);
            continue;
        }
        for (int32_t x = box.x; x <= right; x++) {
            dab(d, x, y, c);
        }
    }
    pixel_end(d, e);
    return GESSO_OK;
}

/* Alois Zingl's plotEllipseRect, in 64-bit integers: the rules the corpus
   README fixes, the same as the Go engine's. */
gesso_status gesso_ellipse(gesso_doc *d, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t c,
                           int filled) {
    gesso_status st = check(d, c, x0, y0, x1, y1);
    if (st != GESSO_OK) {
        return st;
    }
    gesso_drop(d);
    gesso_rect box = corners(x0, y0, x1, y1);
    gesso_rect r = clip(d, filled ? box : reach(d, box));
    if (r.w == 0) {
        return GESSO_OK;
    }
    pixel_edit e = pixel_begin(d, r);
    int32_t xl = box.x;
    int32_t xr = box.x + box.w - 1;
    int64_t a = box.w - 1;
    int64_t b = box.h - 1;
    int64_t b1 = b % 2;
    int64_t dx = 4 * (1 - a) * b * b;
    int64_t dy = 4 * (b1 + 1) * a * a;
    int64_t err = dx + dy + b1 * a * a;
    int32_t yb = box.y + (int32_t)((b + 1) / 2);
    int32_t yt = yb - (int32_t)b1;
    int64_t a8 = 8 * a * a;
    int64_t b8 = 8 * b * b;
    while (xl <= xr) {
        span(d, xl, xr, yb, c, filled);
        span(d, xl, xr, yt, c, filled);
        int64_t e2 = 2 * err;
        if (e2 <= dy) {
            yb++;
            yt--;
            dy += a8;
            err += dy;
        }
        if (e2 >= dx || 2 * err > dy) {
            xl++;
            xr--;
            dx += b8;
            err += dx;
        }
    }
    /* A thin ellipse (a <= 1) leaves the loop before its tips. */
    while ((int64_t)(yb - yt) <= b) {
        span(d, xl - 1, xr + 1, yb, c, filled);
        span(d, xl - 1, xr + 1, yt, c, filled);
        yb++;
        yt--;
    }
    pixel_end(d, e);
    return GESSO_OK;
}

/* 4-connected, exact index. A pixel is painted when it is pushed, so each is
   pushed once and the stack never holds more than the frame. */
gesso_status gesso_fill(gesso_doc *d, int32_t x, int32_t y, uint8_t c) {
    gesso_status st = check(d, c, x, y, x, y);
    if (st != GESSO_OK) {
        return st;
    }
    gesso_drop(d);
    gesso_image *img = &d->img;
    if (x < 0 || y < 0 || x >= img->width || y >= img->height) {
        return GESSO_OK;
    }
    uint8_t *f = gesso_frame_pixels(img, d->frame);
    uint32_t w = (uint32_t)img->width;
    uint32_t h = (uint32_t)img->height;
    uint32_t start = (uint32_t)y * w + (uint32_t)x;
    uint8_t target = f[start];
    if (target == c) {
        return GESSO_OK;
    }
    pixel_edit e = pixel_begin(d, (gesso_rect){0, 0, img->width, img->height});
    uint32_t n = 0;
    f[start] = c;
    d->stack[n++] = start;
    while (n > 0) {
        uint32_t i = d->stack[--n];
        uint32_t px = i % w;
        uint32_t py = i / w;
        uint32_t next[4];
        uint32_t k = 0;
        if (px > 0) {
            next[k++] = i - 1;
        }
        if (px + 1 < w) {
            next[k++] = i + 1;
        }
        if (py > 0) {
            next[k++] = i - w;
        }
        if (py + 1 < h) {
            next[k++] = i + w;
        }
        for (uint32_t j = 0; j < k; j++) {
            if (f[next[j]] == target) {
                f[next[j]] = c;
                d->stack[n++] = next[j];
            }
        }
    }
    pixel_end(d, e);
    return GESSO_OK;
}

/* ---- selection ---- */

gesso_status gesso_select(gesso_doc *d, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    gesso_status st = check(d, 0, x0, y0, x1, y1);
    if (st != GESSO_OK) {
        return st;
    }
    gesso_drop(d);
    gesso_rect r = clip(d, corners(x0, y0, x1, y1));
    if (r.w == 0) {
        return GESSO_OK;
    }
    d->sel = r;
    d->selected = 1;
    return GESSO_OK;
}

static void clear_rect(gesso_doc *d, gesso_rect r) {
    uint8_t *f = gesso_frame_pixels(&d->img, d->frame);
    for (int32_t y = 0; y < r.h; y++) {
        memset(f + (size_t)(r.y + y) * (size_t)d->img.width + (size_t)r.x, 0, (size_t)r.w);
    }
}

static void lift(gesso_doc *d, int duplicate) {
    copy_out(&d->img, d->frame, d->sel, d->lifted.pixels);
    d->lifted.w = d->sel.w;
    d->lifted.h = d->sel.h;
    d->floating = 1;
    gesso_begin(d);
    d->float_group = d->group;
    if (duplicate) {
        return;
    }
    pixel_edit e = pixel_begin(d, d->sel);
    clear_rect(d, d->sel);
    pixel_end(d, e);
}

static gesso_status move(gesso_doc *d, int32_t dx, int32_t dy, int duplicate) {
    if (!d->selected) {
        return GESSO_OK;
    }
    int64_t x = (int64_t)d->sel.x + dx;
    int64_t y = (int64_t)d->sel.y + dy;
    if (x < -GESSO_COORD_MAX || y < -GESSO_COORD_MAX || x + d->sel.w > GESSO_COORD_MAX ||
        y + d->sel.h > GESSO_COORD_MAX) {
        return GESSO_ERR_COORD;
    }
    if (!d->floating) {
        lift(d, duplicate);
    }
    d->sel.x = (int32_t)x;
    d->sel.y = (int32_t)y;
    return GESSO_OK;
}

gesso_status gesso_move_selection(gesso_doc *d, int32_t dx, int32_t dy) {
    return move(d, dx, dy, 0);
}

gesso_status gesso_duplicate_selection(gesso_doc *d, int32_t dx, int32_t dy) {
    return move(d, dx, dy, 1);
}

void gesso_selection_transparent(gesso_doc *d, int on) {
    d->transparent = on != 0;
}

/* Flip and rotate work on the floating pixels, lifting them first; with
   nothing selected, on the whole frame. */
static gesso_status float_all(gesso_doc *d) {
    gesso_status st = ready(d);
    if (st != GESSO_OK) {
        return st;
    }
    if (!d->selected) {
        d->sel = (gesso_rect){0, 0, d->img.width, d->img.height};
        d->selected = 1;
    }
    if (!d->floating) {
        lift(d, 0);
    }
    return GESSO_OK;
}

gesso_status gesso_flip(gesso_doc *d, int vertical) {
    gesso_status st = float_all(d);
    if (st != GESSO_OK) {
        return st;
    }
    gesso_patch *p = &d->lifted;
    if (vertical) {
        for (int32_t y = 0; y < p->h / 2; y++) {
            uint8_t *a = p->pixels + (size_t)y * (size_t)p->w;
            uint8_t *b = p->pixels + (size_t)(p->h - 1 - y) * (size_t)p->w;
            for (int32_t x = 0; x < p->w; x++) {
                uint8_t t = a[x];
                a[x] = b[x];
                b[x] = t;
            }
        }
        return GESSO_OK;
    }
    for (int32_t y = 0; y < p->h; y++) {
        uint8_t *row = p->pixels + (size_t)y * (size_t)p->w;
        for (int32_t x = 0; x < p->w / 2; x++) {
            uint8_t t = row[x];
            row[x] = row[p->w - 1 - x];
            row[p->w - 1 - x] = t;
        }
    }
    return GESSO_OK;
}

/* Clockwise, about the center: the rectangle keeps its middle, rounded the
   way C division rounds. */
gesso_status gesso_rotate(gesso_doc *d) {
    gesso_status st = float_all(d);
    if (st != GESSO_OK) {
        return st;
    }
    gesso_patch *p = &d->lifted;
    gesso_patch *t = &d->scratch;
    t->w = p->h;
    t->h = p->w;
    for (int32_t ny = 0; ny < t->h; ny++) {
        for (int32_t nx = 0; nx < t->w; nx++) {
            t->pixels[(size_t)ny * (size_t)t->w + (size_t)nx] =
                p->pixels[(size_t)(p->h - 1 - nx) * (size_t)p->w + (size_t)ny];
        }
    }
    d->sel.x += (p->w - p->h) / 2;
    d->sel.y += (p->h - p->w) / 2;
    memcpy(p->pixels, t->pixels, (size_t)t->w * (size_t)t->h);
    p->w = t->w;
    p->h = t->h;
    d->sel.w = p->w;
    d->sel.h = p->h;
    return GESSO_OK;
}

void gesso_drop(gesso_doc *d) {
    if (!d->selected) {
        return;
    }
    d->selected = 0;
    if (!d->floating) {
        return;
    }
    d->floating = 0;
    gesso_rect r = clip(d, d->sel);
    if (r.w > 0) {
        pixel_edit e = pixel_begin(d, r);
        uint8_t *f = gesso_frame_pixels(&d->img, d->frame);
        for (int32_t y = r.y; y < r.y + r.h; y++) {
            for (int32_t x = r.x; x < r.x + r.w; x++) {
                uint8_t v = d->lifted.pixels[(size_t)(y - d->sel.y) * (size_t)d->lifted.w +
                                             (size_t)(x - d->sel.x)];
                if (v == 0 && d->transparent) {
                    continue;
                }
                f[(size_t)y * (size_t)d->img.width + (size_t)x] = v;
            }
        }
        pixel_end(d, e);
    }
    gesso_end(d);
}

gesso_status gesso_delete_selection(gesso_doc *d) {
    if (!d->selected) {
        return GESSO_OK;
    }
    d->selected = 0;
    if (d->floating) {
        d->floating = 0;
        gesso_end(d);
        return GESSO_OK;
    }
    pixel_edit e = pixel_begin(d, d->sel);
    clear_rect(d, d->sel);
    pixel_end(d, e);
    return GESSO_OK;
}

gesso_status gesso_copy(gesso_doc *d) {
    if (!d->selected) {
        return GESSO_ERR_NO_SELECTION;
    }
    if (d->floating) {
        memcpy(d->clip.pixels, d->lifted.pixels, (size_t)d->lifted.w * (size_t)d->lifted.h);
        d->clip.w = d->lifted.w;
        d->clip.h = d->lifted.h;
        return GESSO_OK;
    }
    copy_out(&d->img, d->frame, d->sel, d->clip.pixels);
    d->clip.w = d->sel.w;
    d->clip.h = d->sel.h;
    return GESSO_OK;
}

gesso_status gesso_cut(gesso_doc *d) {
    gesso_status st = gesso_copy(d);
    if (st != GESSO_OK) {
        return st;
    }
    return gesso_delete_selection(d);
}

gesso_status gesso_paste(gesso_doc *d, int32_t x, int32_t y) {
    gesso_status st = check(d, 0, x, y, x, y);
    if (st != GESSO_OK) {
        return st;
    }
    if (d->clip.w == 0) {
        return GESSO_ERR_CLIPBOARD;
    }
    if ((int64_t)x + d->clip.w > GESSO_COORD_MAX || (int64_t)y + d->clip.h > GESSO_COORD_MAX) {
        return GESSO_ERR_COORD;
    }
    gesso_drop(d);
    memcpy(d->lifted.pixels, d->clip.pixels, (size_t)d->clip.w * (size_t)d->clip.h);
    d->lifted.w = d->clip.w;
    d->lifted.h = d->clip.h;
    d->sel = (gesso_rect){x, y, d->clip.w, d->clip.h};
    d->selected = 1;
    d->floating = 1;
    gesso_begin(d);
    d->float_group = d->group;
    return GESSO_OK;
}

gesso_status gesso_clip_set(gesso_doc *d, const uint8_t *px, int32_t w, int32_t h) {
    if (w < 1 || h < 1 || w > GESSO_SIDE_MAX || h > GESSO_SIDE_MAX) {
        return GESSO_ERR_SIZE;
    }
    memcpy(d->clip.pixels, px, (size_t)w * (size_t)h);
    d->clip.w = w;
    d->clip.h = h;
    return GESSO_OK;
}

gesso_status gesso_replace(gesso_doc *d, const gesso_image *img) {
    gesso_status st = ready(d);
    if (st != GESSO_OK) {
        return st;
    }
    gesso_drop(d);
    d->selected = 0;
    image_edit e = image_begin_sized(d, snap_size(img));
    memcpy(&d->img, img, META);
    memcpy(d->img.pixels, img->pixels, (size_t)img->nframes * frame_size(img));
    d->frame = 0;
    image_end(d, e);
    return GESSO_OK;
}

/* Frame by frame through scratch, in the order that never writes over a
   frame not yet moved: forward when frames shrink, backward when they
   grow. */
gesso_status gesso_resize(gesso_doc *d, int32_t width, int32_t height) {
    gesso_status st = ready(d);
    if (st != GESSO_OK) {
        return st;
    }
    if (width < 1 || height < 1 || width > GESSO_SIDE_MAX || height > GESSO_SIDE_MAX) {
        return GESSO_ERR_SIZE;
    }
    gesso_image *img = &d->img;
    if (width == img->width && height == img->height) {
        return GESSO_OK;
    }
    gesso_drop(d);
    d->selected = 0;
    size_t old = frame_size(img);
    size_t size = (size_t)width * (size_t)height;
    image_edit e = image_begin_sized(d, META + (size_t)img->nframes * size);
    int32_t cw = width < img->width ? width : img->width;
    int32_t ch = height < img->height ? height : img->height;
    uint8_t *tmp = d->scratch.pixels;
    for (uint32_t k = 0; k < img->nframes; k++) {
        uint32_t f = size <= old ? k : img->nframes - 1 - k;
        memcpy(tmp, img->pixels + (size_t)f * old, old);
        uint8_t *dst = img->pixels + (size_t)f * size;
        memset(dst, 0, size);
        for (int32_t y = 0; y < ch; y++) {
            memcpy(dst + (size_t)y * (size_t)width, tmp + (size_t)y * (size_t)img->width,
                   (size_t)cw);
        }
    }
    img->width = width;
    img->height = height;
    image_end(d, e);
    return GESSO_OK;
}

/* ---- frames, tags, palette ---- */

gesso_status gesso_frame_select(gesso_doc *d, uint32_t frame) {
    if (frame >= d->img.nframes) {
        return GESSO_ERR_FRAME;
    }
    gesso_drop(d);
    d->frame = frame;
    return GESSO_OK;
}

gesso_status gesso_frame_add(gesso_doc *d) {
    gesso_status st = ready(d);
    if (st != GESSO_OK) {
        return st;
    }
    gesso_image *img = &d->img;
    if (img->nframes == GESSO_FRAMES_MAX) {
        return GESSO_ERR_FRAMES_FULL;
    }
    gesso_drop(d);
    image_edit e = image_begin(d);
    size_t n = frame_size(img);
    uint32_t at = d->frame + 1;
    memmove(img->pixels + (size_t)(at + 1) * n, img->pixels + (size_t)at * n,
            (size_t)(img->nframes - at) * n);
    memmove(img->duration + at + 1, img->duration + at,
            (size_t)(img->nframes - at) * sizeof img->duration[0]);
    memset(img->pixels + (size_t)at * n, 0, n);
    img->duration[at] = img->duration[d->frame];
    img->nframes++;
    d->frame = at;
    image_end(d, e);
    return GESSO_OK;
}

static void remove_tag(gesso_image *img, uint32_t i) {
    memmove(img->tags + i, img->tags + i + 1, (img->ntags - i - 1) * sizeof img->tags[0]);
    img->ntags--;
    memset(&img->tags[img->ntags], 0, sizeof img->tags[0]);
}

static gesso_status range(const gesso_doc *d, uint32_t first, uint32_t last) {
    gesso_status st = ready(d);
    if (st != GESSO_OK) {
        return st;
    }
    if (first > last || last >= d->img.nframes) {
        return GESSO_ERR_FRAME;
    }
    return GESSO_OK;
}

gesso_status gesso_frames_duplicate(gesso_doc *d, uint32_t first, uint32_t last) {
    gesso_status st = range(d, first, last);
    if (st != GESSO_OK) {
        return st;
    }
    gesso_image *img = &d->img;
    uint32_t k = last - first + 1;
    if (img->nframes + k > GESSO_FRAMES_MAX) {
        return GESSO_ERR_FRAMES_FULL;
    }
    gesso_drop(d);
    size_t n = frame_size(img);
    image_edit e = image_begin_sized(d, META + (size_t)(img->nframes + k) * n);
    uint32_t at = last + 1;
    memmove(img->pixels + (size_t)(at + k) * n, img->pixels + (size_t)at * n,
            (size_t)(img->nframes - at) * n);
    memmove(img->duration + at + k, img->duration + at,
            (size_t)(img->nframes - at) * sizeof img->duration[0]);
    memcpy(img->pixels + (size_t)at * n, img->pixels + (size_t)first * n, (size_t)k * n);
    memcpy(img->duration + at, img->duration + first, k * sizeof img->duration[0]);
    img->nframes += k;
    d->frame = at;
    image_end(d, e);
    return GESSO_OK;
}

gesso_status gesso_frames_insert(gesso_doc *d, const uint8_t *px, const uint32_t *duration,
                                 uint32_t count) {
    gesso_status st = ready(d);
    if (st != GESSO_OK) {
        return st;
    }
    gesso_image *img = &d->img;
    if (count == 0 || count > GESSO_FRAMES_MAX - img->nframes) {
        return GESSO_ERR_FRAMES_FULL;
    }
    size_t n = frame_size(img);
    for (uint32_t f = 0; f < count; f++) {
        if (duration[f] < 1 || duration[f] > GESSO_DURATION_MAX) {
            return GESSO_ERR_DURATION;
        }
    }
    for (size_t i = 0; i < (size_t)count * n; i++) {
        if (px[i] >= img->ncolors) {
            return GESSO_ERR_COLOR;
        }
    }
    gesso_drop(d);
    image_edit e = image_begin_sized(d, META + (size_t)(img->nframes + count) * n);
    uint32_t at = d->frame + 1;
    memmove(img->pixels + (size_t)(at + count) * n, img->pixels + (size_t)at * n,
            (size_t)(img->nframes - at) * n);
    memmove(img->duration + at + count, img->duration + at,
            (size_t)(img->nframes - at) * sizeof img->duration[0]);
    memcpy(img->pixels + (size_t)at * n, px, (size_t)count * n);
    memcpy(img->duration + at, duration, count * sizeof img->duration[0]);
    img->nframes += count;
    d->frame = at;
    image_end(d, e);
    return GESSO_OK;
}

gesso_status gesso_frame_duplicate(gesso_doc *d) {
    return gesso_frames_duplicate(d, d->frame, d->frame);
}

/* A tag wholly inside the deleted frames goes; the others close the gap. */
gesso_status gesso_frames_delete(gesso_doc *d, uint32_t first, uint32_t last) {
    gesso_status st = range(d, first, last);
    if (st != GESSO_OK) {
        return st;
    }
    gesso_image *img = &d->img;
    uint32_t k = last - first + 1;
    if (k == img->nframes) {
        return GESSO_ERR_LAST_FRAME;
    }
    gesso_drop(d);
    image_edit e = image_begin(d);
    size_t n = frame_size(img);
    memmove(img->pixels + (size_t)first * n, img->pixels + (size_t)(last + 1) * n,
            (size_t)(img->nframes - last - 1) * n);
    memmove(img->duration + first, img->duration + last + 1,
            (size_t)(img->nframes - last - 1) * sizeof img->duration[0]);
    img->nframes -= k;
    memset(img->duration + img->nframes, 0, k * sizeof img->duration[0]);
    for (uint32_t i = img->ntags; i-- > 0;) {
        gesso_tag *t = &img->tags[i];
        if (t->first >= first && t->last <= last) {
            remove_tag(img, i);
            continue;
        }
        if (t->first > last) {
            t->first -= k;
        } else if (t->first >= first) {
            t->first = first;
        }
        if (t->last > last) {
            t->last -= k;
        } else if (t->last >= first) {
            t->last = first - 1;
        }
    }
    if (d->frame >= first) {
        d->frame = d->frame > last ? d->frame - k : first;
    }
    if (d->frame >= img->nframes) {
        d->frame = img->nframes - 1;
    }
    image_end(d, e);
    return GESSO_OK;
}

gesso_status gesso_frame_delete(gesso_doc *d) {
    return gesso_frames_delete(d, d->frame, d->frame);
}

/* Frames a..b (both included) in reverse order, one frame of scratch. */
static void reverse_frames(gesso_doc *d, uint32_t a, uint32_t b) {
    gesso_image *img = &d->img;
    size_t n = frame_size(img);
    while (a < b) {
        uint8_t *x = img->pixels + (size_t)a * n;
        uint8_t *y = img->pixels + (size_t)b * n;
        memcpy(d->scratch.pixels, x, n);
        memcpy(x, y, n);
        memcpy(y, d->scratch.pixels, n);
        uint32_t t = img->duration[a];
        img->duration[a] = img->duration[b];
        img->duration[b] = t;
        a++;
        b--;
    }
}

/* Frames a..c with a..b moved after the rest: three reversals. */
static void rotate_frames(gesso_doc *d, uint32_t a, uint32_t b, uint32_t c) {
    reverse_frames(d, a, b);
    reverse_frames(d, b + 1, c);
    reverse_frames(d, a, c);
}

/* The frames first..last go so the first of them lands at to; tags stay on
   their frame numbers. */
gesso_status gesso_frames_move(gesso_doc *d, uint32_t first, uint32_t last, uint32_t to) {
    gesso_status st = range(d, first, last);
    if (st != GESSO_OK) {
        return st;
    }
    uint32_t k = last - first + 1;
    if (to > d->img.nframes - k) {
        return GESSO_ERR_FRAME;
    }
    if (to == first) {
        return GESSO_OK;
    }
    gesso_drop(d);
    image_edit e = image_begin(d);
    if (to > first) {
        rotate_frames(d, first, last, to + k - 1);
    } else {
        rotate_frames(d, to, first - 1, last);
    }
    d->frame = to;
    image_end(d, e);
    return GESSO_OK;
}

gesso_status gesso_frame_move(gesso_doc *d, uint32_t from, uint32_t to) {
    if (from >= d->img.nframes || to >= d->img.nframes) {
        return GESSO_ERR_FRAME;
    }
    return gesso_frames_move(d, from, from, to);
}

gesso_status gesso_frames_duration(gesso_doc *d, uint32_t first, uint32_t last, uint32_t ms) {
    if (first > last || last >= d->img.nframes) {
        return GESSO_ERR_FRAME;
    }
    if (ms < 1 || ms > GESSO_DURATION_MAX) {
        return GESSO_ERR_DURATION;
    }
    gesso_drop(d);
    image_edit e = image_begin(d);
    for (uint32_t f = first; f <= last; f++) {
        d->img.duration[f] = ms;
    }
    image_end(d, e);
    return GESSO_OK;
}

gesso_status gesso_frame_duration(gesso_doc *d, uint32_t frame, uint32_t ms) {
    return gesso_frames_duration(d, frame, frame, ms);
}

int gesso_tag_name_ok(const uint8_t *name, size_t len) {
    if (len < 1 || len >= GESSO_TAG_NAME_MAX) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        if (name[i] < 0x20 || name[i] > 0x7e || name[i] == '"' || name[i] == '\\') {
            return 0;
        }
    }
    return 1;
}

static size_t name_len(const char *s) {
    size_t n = 0;
    while (n < GESSO_TAG_NAME_MAX && s[n] != '\0') {
        n++;
    }
    return n;
}

static int find_tag(const gesso_image *img, const char *name, size_t len) {
    for (uint32_t i = 0; i < img->ntags; i++) {
        if (name_len(img->tags[i].name) == len && memcmp(img->tags[i].name, name, len) == 0) {
            return (int)i;
        }
    }
    return -1;
}

gesso_status gesso_tag_set(gesso_doc *d, const char *name, uint32_t first, uint32_t last,
                           uint32_t direction) {
    gesso_image *img = &d->img;
    size_t len = name_len(name);
    if (!gesso_tag_name_ok((const uint8_t *)name, len)) {
        return GESSO_ERR_TAG_NAME;
    }
    if (first > last || last >= img->nframes) {
        return GESSO_ERR_TAG_RANGE;
    }
    if (direction >= GESSO_DIRECTIONS) {
        return GESSO_ERR_TAG_DIRECTION;
    }
    int i = find_tag(img, name, len);
    if (i < 0 && img->ntags == GESSO_TAGS_MAX) {
        return GESSO_ERR_TAGS_FULL;
    }
    gesso_drop(d);
    image_edit e = image_begin(d);
    if (i < 0) {
        i = (int)img->ntags++;
        memset(&img->tags[i], 0, sizeof img->tags[i]);
        memcpy(img->tags[i].name, name, len);
    }
    img->tags[i].first = first;
    img->tags[i].last = last;
    img->tags[i].direction = direction;
    image_end(d, e);
    return GESSO_OK;
}

gesso_status gesso_tag_delete(gesso_doc *d, const char *name) {
    int i = find_tag(&d->img, name, name_len(name));
    if (i < 0) {
        return GESSO_ERR_NO_TAG;
    }
    gesso_drop(d);
    image_edit e = image_begin(d);
    remove_tag(&d->img, (uint32_t)i);
    image_end(d, e);
    return GESSO_OK;
}

/* Every pixel index, in every frame and in the clipboard, through map. */
static void remap(gesso_doc *d, const uint8_t *map) {
    gesso_image *img = &d->img;
    size_t total = (size_t)img->nframes * frame_size(img);
    for (size_t i = 0; i < total; i++) {
        img->pixels[i] = map[img->pixels[i]];
    }
    size_t held = (size_t)d->clip.w * (size_t)d->clip.h;
    for (size_t i = 0; i < held; i++) {
        d->clip.pixels[i] = map[d->clip.pixels[i]];
    }
}

static uint32_t distance(uint32_t a, uint32_t b) {
    uint32_t sum = 0;
    for (uint32_t shift = 0; shift < 24; shift += 8) {
        int32_t x = (int32_t)(a >> shift & 0xffu);
        int32_t y = (int32_t)(b >> shift & 0xffu);
        sum += (uint32_t)((x - y) * (x - y));
    }
    return sum;
}

/* Its pixels take the nearest color left (index 0 when none is); the
   indices above it come down by one. */
gesso_status gesso_color_delete(gesso_doc *d, uint32_t index) {
    gesso_status st = ready(d);
    if (st != GESSO_OK) {
        return st;
    }
    gesso_image *img = &d->img;
    if (index == 0 || index >= img->ncolors) {
        return GESSO_ERR_COLOR;
    }
    gesso_drop(d);
    image_edit e = image_begin(d);
    uint32_t best = 0;
    uint32_t best_d = UINT32_MAX;
    for (uint32_t k = 1; k < img->ncolors; k++) {
        uint32_t dk = distance(img->palette[k], img->palette[index]);
        if (k != index && dk < best_d) {
            best = k;
            best_d = dk;
        }
    }
    uint8_t map[GESSO_COLORS_MAX];
    for (uint32_t k = 0; k < GESSO_COLORS_MAX; k++) {
        uint32_t to = k == index ? best : k;
        map[k] = (uint8_t)(to > index ? to - 1 : to);
    }
    memmove(img->palette + index, img->palette + index + 1,
            (img->ncolors - index - 1) * sizeof img->palette[0]);
    img->ncolors--;
    img->palette[img->ncolors] = 0;
    remap(d, map);
    image_end(d, e);
    return GESSO_OK;
}

/* The color at from goes to to, the ones between shifting over; pixels keep
   their colors. */
gesso_status gesso_color_move(gesso_doc *d, uint32_t from, uint32_t to) {
    gesso_status st = ready(d);
    if (st != GESSO_OK) {
        return st;
    }
    gesso_image *img = &d->img;
    if (from == 0 || to == 0 || from >= img->ncolors || to >= img->ncolors) {
        return GESSO_ERR_COLOR;
    }
    if (from == to) {
        return GESSO_OK;
    }
    gesso_drop(d);
    image_edit e = image_begin(d);
    uint8_t map[GESSO_COLORS_MAX];
    uint32_t moved[GESSO_COLORS_MAX] = {0};
    for (uint32_t k = 0; k < GESSO_COLORS_MAX; k++) {
        uint32_t m = k;
        if (k == from) {
            m = to;
        } else if (from < to && k > from && k <= to) {
            m = k - 1;
        } else if (from > to && k >= to && k < from) {
            m = k + 1;
        }
        map[k] = (uint8_t)m;
    }
    for (uint32_t k = 0; k < img->ncolors; k++) {
        moved[map[k]] = img->palette[k];
    }
    memcpy(img->palette, moved, img->ncolors * sizeof img->palette[0]);
    remap(d, map);
    image_end(d, e);
    return GESSO_OK;
}

gesso_status gesso_palette_load(gesso_doc *d, const uint32_t *colors, uint32_t count, int match) {
    gesso_status st = ready(d);
    if (st != GESSO_OK) {
        return st;
    }
    if (count < 1 || count >= GESSO_COLORS_MAX) {
        return GESSO_ERR_PALETTE_FULL;
    }
    gesso_image *img = &d->img;
    gesso_drop(d);
    image_edit e = image_begin(d);
    uint8_t map[GESSO_COLORS_MAX] = {0};
    for (uint32_t k = 1; k < img->ncolors; k++) {
        if (!match && k <= count) {
            map[k] = (uint8_t)k;
            continue;
        }
        uint32_t best = 1;
        uint32_t best_d = UINT32_MAX;
        for (uint32_t c = 0; c < count; c++) {
            uint32_t dc = distance(colors[c], img->palette[k]);
            if (dc < best_d) {
                best = c + 1;
                best_d = dc;
            }
        }
        map[k] = (uint8_t)best;
    }
    memset(img->palette, 0, sizeof img->palette);
    for (uint32_t c = 0; c < count; c++) {
        img->palette[c + 1] = 0xff000000u | (colors[c] & 0xffffffu);
    }
    img->ncolors = count + 1;
    remap(d, map);
    image_end(d, e);
    return GESSO_OK;
}

gesso_status gesso_color_set(gesso_doc *d, uint32_t index, uint32_t rgb) {
    gesso_status st = ready(d);
    if (st != GESSO_OK) {
        return st;
    }
    gesso_image *img = &d->img;
    if (index == 0 || index > img->ncolors) {
        return GESSO_ERR_COLOR;
    }
    if (index == GESSO_COLORS_MAX) {
        return GESSO_ERR_PALETTE_FULL;
    }
    gesso_drop(d);
    image_edit e = image_begin(d);
    img->palette[index] = 0xff000000u | (rgb & 0xffffffu);
    if (index == img->ncolors) {
        img->ncolors++;
    }
    image_end(d, e);
    return GESSO_OK;
}
