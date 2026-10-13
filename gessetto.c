#include "gessetto.h"

#include "editor.h"
#include "gesso.h"
#include "gesso_c.h"
#include "gesso_filo.h"
#include "gesso_png.h"

#include <string.h>

enum {
    NEW_SIDE = 32,
    LOG_CAP = 64 * 1024 * 1024,
    SCRIPT_CAP = 64 * 1024 * 1024,
    TEXT_CAP = 48 * 1024 * 1024,
    PNG_FILE_MAX = 16 * 1024 * 1024,
    ONION_PREVIOUS = 96,
    ONION_NEXT = 56,
};

#define CHECK_LIGHT 0xffe0e0e0u
#define CHECK_DARK 0xffb0b0b0u

/* The cast keeps -Wenum-compare quiet: two enums that must agree. */
// NOLINTNEXTLINE(readability-redundant-casting)
_Static_assert((int)GST_SELECT == TOOL_SELECT, "gst_tool mirrors tool_id");
// NOLINTNEXTLINE(readability-redundant-casting)
_Static_assert((int)GST_TOOL_COUNT == TOOL_COUNT, "gst_tool mirrors tool_id");
_Static_assert(GST_SIDE_MAX == GESSO_SIDE_MAX, "the limits are gesso.h's");
_Static_assert(GST_COLORS_MAX == GESSO_COLORS_MAX, "the limits are gesso.h's");
_Static_assert(GST_FRAMES_MAX == GESSO_FRAMES_MAX, "the limits are gesso.h's");
_Static_assert(GST_TAG_NAME_MAX == GESSO_TAG_NAME_MAX, "the limits are gesso.h's");
_Static_assert(GST_DURATION_MAX == GESSO_DURATION_MAX, "the limits are gesso.h's");
_Static_assert(GST_ERROR_MAX >= FILO_ERROR_MAX + 64, "room for a Filo error and its place");

struct gst {
    gesso_doc doc;
    editor ed;
    uint32_t seen; /* the newest step handed to the undo manager */
    int live;      /* a color edit's group is open */
    uint32_t live_index;
    uint8_t clip_index[GESSO_FRAME_PIXELS];
    uint8_t log[LOG_CAP];
};

/* Shared by every document; the caller serializes their use. */
static gesso_image scratch;
static gesso_script script;
static uint8_t script_mem[SCRIPT_CAP];
static uint8_t text[TEXT_CAP];
static uint8_t png_work[PNG_FILE_MAX + PNG_WORK_RAW];
static uint32_t png_argb[PNG_SIDE_MAX * PNG_SIDE_MAX];
static uint8_t png_index[PNG_SIDE_MAX * PNG_SIDE_MAX];

static void say(char *dst, const char *a, const char *b) {
    size_t n = 0;
    for (const char *s = a; *s != '\0' && n + 1 < GST_ERROR_MAX; s++) {
        dst[n++] = *s;
    }
    for (const char *s = b; s != NULL && *s != '\0' && n + 1 < GST_ERROR_MAX; s++) {
        dst[n++] = *s;
    }
    dst[n] = '\0';
}

static void say_line(char *dst, uint32_t line, const char *msg) {
    char num[12];
    size_t n = sizeof num;
    num[--n] = '\0';
    do {
        num[--n] = (char)('0' + line % 10u);
        line /= 10u;
    } while (line > 0 && n > 0);
    char head[24] = "line ";
    size_t h = 5;
    for (const char *s = num + n; *s != '\0'; s++) {
        head[h++] = *s;
    }
    head[h++] = ':';
    head[h++] = ' ';
    head[h] = '\0';
    say(dst, head, msg);
}

/* A color edit left open ends before anything else happens. */
static void settle(gst *g) {
    if (g->live) {
        gesso_end(&g->doc);
        g->live = 0;
    }
}

/* Undo can take colors away from under the tools. */
static void fix_colors(gst *g) {
    if (g->ed.fg >= g->doc.img.ncolors) {
        g->ed.fg = 0;
    }
    if (g->ed.bg >= g->doc.img.ncolors) {
        g->ed.bg = 0;
    }
}

static void adopt(gst *g) {
    settle(g);
    editor_tool(&g->ed, g->ed.tool);
    gesso_adopt(&g->doc, &scratch);
    fix_colors(g);
}

size_t gst_size(void) {
    return sizeof(gst);
}

void gst_init(gst *g) {
    gesso_init(&g->doc, g->log, sizeof g->log);
    (void)gesso_new(&g->doc, NEW_SIDE, NEW_SIDE);
    editor_init(&g->ed, &g->doc);
}

int gst_resize(gst *g, int32_t width, int32_t height, char err[GST_ERROR_MAX]) {
    settle(g);
    editor_tool(&g->ed, g->ed.tool);
    if (gesso_resize(&g->doc, width, height) != GESSO_OK) {
        say(err, "The size must be 1 to 256 pixels on each side.", NULL);
        return 0;
    }
    return 1;
}

int gst_read(gst *g, const uint8_t *src, size_t len, char err[GST_ERROR_MAX]) {
    if (gesso_script_file(&script, &scratch, script_mem, sizeof script_mem) != FILO_OK ||
        gesso_script_run(&script, src, len) != FILO_OK) {
        uint32_t line = 0;
        uint32_t col = 0;
        if (filo_error_at(&script.ctx, &line, &col)) {
            say_line(err, line, filo_error(&script.ctx));
        } else {
            say(err, filo_error(&script.ctx), NULL);
        }
        return 0;
    }
    adopt(g);
    return 1;
}

int gst_import_png(gst *g, const uint8_t *src, size_t len, char err[GST_ERROR_MAX]) {
    if (len > PNG_FILE_MAX) {
        say(err, "The file is larger than 16 MB.", NULL);
        return 0;
    }
    png_image p = {.argb = png_argb, .index = png_index};
    png_status ps = png_decode(src, len, png_work, sizeof png_work, &p);
    if (ps != PNG_OK) {
        say(err, "Not a PNG gessetto can read: ", png_message(ps));
        return 0;
    }
    gesso_status gs = gesso_from_png(&scratch, &p);
    if (gs == GESSO_ERR_PALETTE_FULL) {
        say(err, "It has more than 255 colors.", NULL);
        return 0;
    }
    if (gs != GESSO_OK) {
        say(err, gesso_message(gs), NULL);
        return 0;
    }
    settle(g);
    editor_tool(&g->ed, g->ed.tool);
    (void)gesso_replace(&g->doc, &scratch);
    fix_colors(g);
    return 1;
}

int gst_import_sheet(gst *g, const uint32_t *argb, int32_t sw, int32_t sh, int32_t w, int32_t h,
                     char err[GST_ERROR_MAX]) {
    gesso_status st = gesso_from_sheet(&scratch, argb, sw, sh, w, h);
    if (st == GESSO_ERR_PALETTE_FULL) {
        say(err, "It has more than 255 colors.", NULL);
        return 0;
    }
    if (st == GESSO_ERR_FRAMES_FULL) {
        say(err, "That cuts it into more than 256 frames.", NULL);
        return 0;
    }
    if (st != GESSO_OK) {
        say(err, "Frames must be 1 to 256 pixels on a side and fit in the sheet.", NULL);
        return 0;
    }
    settle(g);
    editor_tool(&g->ed, g->ed.tool);
    (void)gesso_replace(&g->doc, &scratch);
    fix_colors(g);
    return 1;
}

/* The floating selection goes into the copy written, not into the
   document: an autosave in the middle of a move must not drop it. */
static void snapshot(const gst *g) {
    const gesso_doc *d = &g->doc;
    const gesso_image *img = &d->img;
    size_t fs = (size_t)img->width * (size_t)img->height;
    memcpy(&scratch, img, offsetof(gesso_image, pixels));
    memcpy(scratch.pixels, img->pixels, (size_t)img->nframes * fs);
    if (d->floating) {
        uint8_t *f = gesso_frame_pixels(&scratch, d->frame);
        for (int32_t y = 0; y < img->height; y++) {
            for (int32_t x = 0; x < img->width; x++) {
                f[(size_t)y * (size_t)img->width + (size_t)x] = gesso_view(d, x, y);
            }
        }
    }
}

const uint8_t *gst_write(const gst *g, size_t *len) {
    snapshot(g);
    size_t n = gesso_write(&scratch, text, sizeof text);
    if (n > sizeof text) {
        return NULL;
    }
    *len = n;
    return text;
}

const uint8_t *gst_header(const gst *g, const char *name, size_t *len) {
    char id[GESSO_C_NAME_MAX];
    gesso_c_name(name, id);
    snapshot(g);
    size_t n = gesso_c_header(&scratch, id, name, text, sizeof text);
    if (n > sizeof text) {
        return NULL;
    }
    *len = n;
    return text;
}

int32_t gst_width(const gst *g) {
    return g->doc.img.width;
}

int32_t gst_height(const gst *g) {
    return g->doc.img.height;
}

uint32_t gst_frames(const gst *g) {
    return g->doc.img.nframes;
}

uint32_t gst_frame(const gst *g) {
    return g->doc.frame;
}

uint32_t gst_duration(const gst *g, uint32_t frame) {
    return frame < g->doc.img.nframes ? g->doc.img.duration[frame] : 0;
}

uint32_t gst_colors(const gst *g) {
    return g->doc.img.ncolors;
}

uint32_t gst_color(const gst *g, uint32_t index) {
    return index < g->doc.img.ncolors ? g->doc.img.palette[index] : 0;
}

uint32_t gst_tags(const gst *g) {
    return g->doc.img.ntags;
}

const char *gst_tag_name(const gst *g, uint32_t tag) {
    return tag < g->doc.img.ntags ? g->doc.img.tags[tag].name : "";
}

uint32_t gst_tag_first(const gst *g, uint32_t tag) {
    return tag < g->doc.img.ntags ? g->doc.img.tags[tag].first : 0;
}

uint32_t gst_tag_last(const gst *g, uint32_t tag) {
    return tag < g->doc.img.ntags ? g->doc.img.tags[tag].last : 0;
}

uint32_t gst_tag_direction(const gst *g, uint32_t tag) {
    return tag < g->doc.img.ntags ? g->doc.img.tags[tag].direction : 0;
}

uint8_t gst_index_at(const gst *g, int32_t x, int32_t y) {
    return gesso_view(&g->doc, x, y);
}

int gst_selection(const gst *g, int32_t r[4]) {
    const gesso_doc *d = &g->doc;
    r[0] = d->sel.x;
    r[1] = d->sel.y;
    r[2] = d->sel.w;
    r[3] = d->sel.h;
    if (!d->selected) {
        return 0;
    }
    return d->floating ? 2 : 1;
}

gst_tool gst_tool_get(const gst *g) {
    return (gst_tool)g->ed.tool;
}

void gst_tool_set(gst *g, gst_tool t) {
    if (t >= GST_TOOL_COUNT) {
        return;
    }
    settle(g);
    editor_tool(&g->ed, (tool_id)t);
}

uint8_t gst_fg(const gst *g) {
    return g->ed.fg;
}

uint8_t gst_bg(const gst *g) {
    return g->ed.bg;
}

void gst_set_fg(gst *g, uint32_t index) {
    if (index < g->doc.img.ncolors) {
        g->ed.fg = (uint8_t)index;
    }
}

void gst_set_bg(gst *g, uint32_t index) {
    if (index < g->doc.img.ncolors) {
        g->ed.bg = (uint8_t)index;
    }
}

void gst_swap_colors(gst *g) {
    uint8_t t = g->ed.fg;
    g->ed.fg = g->ed.bg;
    g->ed.bg = t;
}

int gst_option_get(const gst *g, gst_option o) {
    switch (o) {
    case GST_PIXEL_PERFECT:
        return g->ed.pixel_perfect;
    case GST_MIRROR_X:
        return g->ed.mirror_x;
    case GST_MIRROR_Y:
        return g->ed.mirror_y;
    case GST_FILLED:
        return g->ed.filled;
    case GST_TRANSPARENT:
        return g->doc.transparent;
    default:
        return 0;
    }
}

void gst_option_set(gst *g, gst_option o, int on) {
    on = on != 0;
    switch (o) {
    case GST_PIXEL_PERFECT:
        g->ed.pixel_perfect = on;
        return;
    case GST_MIRROR_X:
        g->ed.mirror_x = on;
        return;
    case GST_MIRROR_Y:
        g->ed.mirror_y = on;
        return;
    case GST_FILLED:
        g->ed.filled = on;
        return;
    case GST_TRANSPARENT:
        gesso_selection_transparent(&g->doc, on);
        return;
    default:
        return;
    }
}

void gst_brush_set(gst *g, uint32_t size, int round) {
    gesso_brush(&g->doc, size, round);
}

uint32_t gst_brush_size(const gst *g) {
    return g->doc.brush;
}

int gst_brush_round(const gst *g) {
    return g->doc.brush_round;
}

static uint32_t editor_mods(uint32_t mods) {
    uint32_t m = 0;
    if (mods & GST_SHIFT) {
        m |= EDITOR_SHIFT;
    }
    if (mods & GST_ALT) {
        m |= EDITOR_ALT;
    }
    return m;
}

void gst_press(gst *g, int32_t x, int32_t y, int secondary) {
    settle(g);
    editor_press(&g->ed, x, y, secondary);
}

void gst_drag(gst *g, int32_t x, int32_t y, uint32_t mods) {
    editor_drag(&g->ed, x, y, editor_mods(mods));
}

void gst_release(gst *g, int32_t x, int32_t y, uint32_t mods) {
    editor_release(&g->ed, x, y, editor_mods(mods));
}

void gst_cancel(gst *g) {
    editor_cancel(&g->ed);
}

int gst_new_step(gst *g) {
    uint32_t s = gesso_step(&g->doc);
    if (s <= g->seen) {
        return 0;
    }
    g->seen = s;
    return 1;
}

int gst_undo(gst *g) {
    settle(g);
    editor_tool(&g->ed, g->ed.tool);
    int changed = gesso_undo(&g->doc);
    fix_colors(g);
    return changed;
}

int gst_redo(gst *g) {
    settle(g);
    editor_tool(&g->ed, g->ed.tool);
    int changed = gesso_redo(&g->doc);
    fix_colors(g);
    return changed;
}

int gst_can_redo(const gst *g) {
    return g->doc.top != g->doc.end && !g->doc.floating;
}

static int ok(gesso_status st) {
    return st == GESSO_OK;
}

int gst_select_all(gst *g) {
    settle(g);
    editor_tool(&g->ed, TOOL_SELECT);
    return ok(gesso_select(&g->doc, 0, 0, g->doc.img.width - 1, g->doc.img.height - 1));
}

int gst_deselect(gst *g) {
    settle(g);
    int had = g->doc.selected;
    gesso_drop(&g->doc);
    return had;
}

int gst_delete(gst *g) {
    settle(g);
    return ok(gesso_delete_selection(&g->doc));
}

int gst_copy(gst *g) {
    settle(g);
    return ok(gesso_copy(&g->doc));
}

int gst_cut(gst *g) {
    settle(g);
    return ok(gesso_cut(&g->doc));
}

int gst_paste(gst *g, int32_t x, int32_t y) {
    settle(g);
    editor_tool(&g->ed, TOOL_SELECT);
    return ok(gesso_paste(&g->doc, x, y));
}

int gst_move_selection(gst *g, int32_t dx, int32_t dy) {
    settle(g);
    return g->doc.selected && ok(gesso_move_selection(&g->doc, dx, dy));
}

int gst_flip(gst *g, int vertical) {
    settle(g);
    editor_tool(&g->ed, TOOL_SELECT);
    return ok(gesso_flip(&g->doc, vertical));
}

int gst_rotate(gst *g) {
    settle(g);
    editor_tool(&g->ed, TOOL_SELECT);
    return ok(gesso_rotate(&g->doc));
}

int gst_frame_select(gst *g, uint32_t frame) {
    settle(g);
    return ok(gesso_frame_select(&g->doc, frame));
}

int gst_frame_add(gst *g) {
    settle(g);
    return ok(gesso_frame_add(&g->doc));
}

int gst_frames_duplicate(gst *g, uint32_t first, uint32_t last) {
    settle(g);
    return ok(gesso_frames_duplicate(&g->doc, first, last));
}

int gst_frames_delete(gst *g, uint32_t first, uint32_t last) {
    settle(g);
    return ok(gesso_frames_delete(&g->doc, first, last));
}

int gst_frames_move(gst *g, uint32_t first, uint32_t last, uint32_t to) {
    settle(g);
    return ok(gesso_frames_move(&g->doc, first, last, to));
}

int gst_frames_duration(gst *g, uint32_t first, uint32_t last, uint32_t ms) {
    settle(g);
    return ok(gesso_frames_duration(&g->doc, first, last, ms));
}

int gst_frames_paste(gst *g, const uint32_t *argb, const uint32_t *duration, uint32_t count) {
    settle(g);
    const gesso_image *img = &g->doc.img;
    size_t n = (size_t)img->width * (size_t)img->height;
    if (count == 0 || count > GESSO_FRAMES_MAX) {
        return 0;
    }
    for (size_t i = 0; i < (size_t)count * n; i++) {
        scratch.pixels[i] = gesso_nearest(img, argb[i]);
    }
    return ok(gesso_frames_insert(&g->doc, scratch.pixels, duration, count));
}

int gst_tag_set(gst *g, const char *name, uint32_t first, uint32_t last, uint32_t direction,
                char err[GST_ERROR_MAX]) {
    settle(g);
    gesso_status st = gesso_tag_set(&g->doc, name, first, last, direction);
    if (st != GESSO_OK) {
        say(err, gesso_message(st), NULL);
        return 0;
    }
    return 1;
}

int gst_tag_delete(gst *g, const char *name) {
    settle(g);
    return ok(gesso_tag_delete(&g->doc, name));
}

/* The open group takes each new value in place of the last: the drag of a
   shape works the same way. */
int gst_color_set(gst *g, uint32_t index, uint32_t rgb) {
    if (index == 0 || index > g->doc.img.ncolors) {
        return 0;
    }
    if (g->live && g->live_index == index) {
        gesso_revert(&g->doc);
    } else {
        settle(g);
        editor_tool(&g->ed, g->ed.tool);
        gesso_drop(&g->doc);
        gesso_begin(&g->doc);
        g->live = 1;
        g->live_index = index;
    }
    return ok(gesso_color_set(&g->doc, index, rgb & 0xffffffu));
}

int gst_color_delete(gst *g, uint32_t index) {
    settle(g);
    editor_tool(&g->ed, g->ed.tool);
    if (!ok(gesso_color_delete(&g->doc, index))) {
        return 0;
    }
    uint8_t *tools[2] = {&g->ed.fg, &g->ed.bg};
    for (int i = 0; i < 2; i++) {
        if (*tools[i] >= index && *tools[i] > 0) {
            (*tools[i])--;
        }
    }
    return 1;
}

int gst_palette_load(gst *g, const uint32_t *colors, uint32_t count, int match) {
    settle(g);
    editor_tool(&g->ed, g->ed.tool);
    if (!ok(gesso_palette_load(&g->doc, colors, count, match))) {
        return 0;
    }
    fix_colors(g);
    return 1;
}

static uint8_t moved(uint8_t k, uint32_t from, uint32_t to) {
    if (k == from) {
        return (uint8_t)to;
    }
    if (from < to && k > from && k <= to) {
        return (uint8_t)(k - 1);
    }
    if (from > to && k >= to && k < from) {
        return (uint8_t)(k + 1);
    }
    return k;
}

int gst_color_move(gst *g, uint32_t from, uint32_t to) {
    settle(g);
    editor_tool(&g->ed, g->ed.tool);
    if (!ok(gesso_color_move(&g->doc, from, to))) {
        return 0;
    }
    g->ed.fg = moved(g->ed.fg, from, to);
    g->ed.bg = moved(g->ed.bg, from, to);
    return 1;
}

void gst_clip_argb(const gst *g, uint32_t *dst, int32_t *w, int32_t *h) {
    const gesso_patch *c = &g->doc.clip;
    size_t n = (size_t)c->w * (size_t)c->h;
    for (size_t i = 0; i < n; i++) {
        uint8_t v = c->pixels[i];
        dst[i] = v != 0 ? g->doc.img.palette[v] : 0;
    }
    *w = c->w;
    *h = c->h;
}

int gst_clip_from_argb(gst *g, const uint32_t *src, int32_t w, int32_t h) {
    if (w < 1 || h < 1 || w > GESSO_SIDE_MAX || h > GESSO_SIDE_MAX) {
        return 0;
    }
    size_t n = (size_t)w * (size_t)h;
    for (size_t i = 0; i < n; i++) {
        g->clip_index[i] = gesso_nearest(&g->doc.img, src[i]);
    }
    return ok(gesso_clip_set(&g->doc, g->clip_index, w, h));
}

void gst_frame_argb(const gst *g, uint32_t frame, uint32_t *dst) {
    const gesso_doc *d = &g->doc;
    const gesso_image *img = &d->img;
    if (frame >= img->nframes) {
        return;
    }
    const uint8_t *px = img->pixels + (size_t)frame * (size_t)img->width * (size_t)img->height;
    for (int32_t y = 0; y < img->height; y++) {
        for (int32_t x = 0; x < img->width; x++) {
            size_t i = (size_t)y * (size_t)img->width + (size_t)x;
            uint8_t v = frame == d->frame ? gesso_view(d, x, y) : px[i];
            dst[i] = v != 0 ? img->palette[v] : 0;
        }
    }
}

/* Squares of about 8 points whatever the zoom; one pixel each from 8 up. */
static uint32_t checker(int32_t x, int32_t y, int32_t zoom) {
    int32_t cell = zoom >= 8 ? 1 : 8 / (zoom < 1 ? 1 : zoom);
    return (x / cell + y / cell) % 2 != 0 ? CHECK_DARK : CHECK_LIGHT;
}

static uint32_t mix(uint32_t a, uint32_t b, uint32_t alpha) {
    uint32_t out = 0xff000000u;
    for (uint32_t shift = 0; shift < 24; shift += 8) {
        uint32_t x = (a >> shift) & 0xffu;
        uint32_t y = (b >> shift) & 0xffu;
        uint32_t v = (x * (255u - alpha) + y * alpha) / 255u;
        out |= v << shift;
    }
    return out;
}

void gst_render(const gst *g, uint32_t *dst, uint32_t frame, int32_t zoom, int onion) {
    const gesso_doc *d = &g->doc;
    const gesso_image *img = &d->img;
    if (frame >= img->nframes) {
        return;
    }
    size_t fs = (size_t)img->width * (size_t)img->height;
    const uint8_t *px = img->pixels + (size_t)frame * fs;
    const uint8_t *prev = onion && frame > 0 ? px - fs : NULL;
    const uint8_t *next = onion && frame + 1 < img->nframes ? px + fs : NULL;
    int active = frame == d->frame;
    for (int32_t y = 0; y < img->height; y++) {
        for (int32_t x = 0; x < img->width; x++) {
            size_t i = (size_t)y * (size_t)img->width + (size_t)x;
            uint8_t v = active ? gesso_view(d, x, y) : px[i];
            uint32_t c = v != 0 ? img->palette[v] : checker(x, y, zoom);
            if (v == 0 && prev != NULL && prev[i] != 0) {
                c = mix(c, img->palette[prev[i]], ONION_PREVIOUS);
            } else if (v == 0 && next != NULL && next[i] != 0) {
                c = mix(c, img->palette[next[i]], ONION_NEXT);
            }
            dst[i] = c;
        }
    }
}
