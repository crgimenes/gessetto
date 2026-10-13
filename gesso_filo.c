#include "gesso_filo.h"

#include "filo_nolibc.h"

#include <string.h>

enum {
    STAGE_NONE,
    STAGE_IMAGE,
    STAGE_FRAMES,
};

enum {
    STEPS_FILE = 2000000,
    STEPS_OPS = 20000000,
    ARGS_MAX = 8,
};

#define META offsetof(gesso_image, pixels)

static gesso_script *script(filo_ctx *ctx) {
    return (gesso_script *)(void *)ctx;
}

static int nothing(filo_ctx *ctx, filo_value *out) {
    return filo_list(ctx, NULL, 0, out);
}

/* Filo puts the builtin's name before the message. */
static int fail(filo_ctx *ctx, const char *what) {
    (void)filo_fail(ctx, what);
    return FILO_ERR;
}

static int count(filo_ctx *ctx, uint32_t n, uint32_t want) {
    static const char *const expects[] = {
        "expects no arguments", "expects 1 argument",  "expects 2 arguments",
        "expects 3 arguments",  "expects 4 arguments", "expects 5 arguments",
    };
    if (n == want) {
        return FILO_OK;
    }
    return fail(ctx, expects[want]);
}

static int int_arg(filo_ctx *ctx, const filo_value *v, int32_t *out) {
    double x = 0;
    *out = 0;
    if (filo_arg_num(ctx, v, &x) != FILO_OK) {
        return FILO_ERR;
    }
    if (!(x >= -2147483647.0 && x <= 2147483647.0) || x != (double)(int32_t)x) {
        return fail(ctx, "expected an integer");
    }
    *out = (int32_t)x;
    return FILO_OK;
}

static int ints(filo_ctx *ctx, const filo_value *a, uint32_t n, uint32_t want, int32_t *v) {
    if (count(ctx, n, want) != FILO_OK) {
        return FILO_ERR;
    }
    for (uint32_t i = 0; i < want; i++) {
        if (int_arg(ctx, &a[i], &v[i]) != FILO_OK) {
            return FILO_ERR;
        }
    }
    return FILO_OK;
}

static int str_arg(filo_ctx *ctx, const filo_value *v, filo_str *out) {
    out->ptr = NULL;
    out->len = 0;
    if (filo_arg_str(ctx, v, out) != FILO_OK) {
        return FILO_ERR;
    }
    return FILO_OK;
}

static int hex(uint8_t c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int color_arg(filo_ctx *ctx, const filo_value *v, uint32_t *rgb) {
    filo_str s;
    *rgb = 0;
    if (str_arg(ctx, v, &s) != FILO_OK) {
        return FILO_ERR;
    }
    if (s.len != 7 || s.ptr[0] != '#') {
        return fail(ctx, "colors are \"#rrggbb\"");
    }
    uint32_t c = 0;
    for (uint32_t i = 1; i < 7; i++) {
        int h = hex(s.ptr[i]);
        if (h < 0) {
            return fail(ctx, "colors are \"#rrggbb\"");
        }
        c = c << 4u | (uint32_t)h;
    }
    *rgb = c;
    return FILO_OK;
}

static int index_ok(filo_ctx *ctx, int32_t v, uint8_t *c) {
    if (v < 0 || v >= GESSO_COLORS_MAX) {
        return fail(ctx, gesso_message(GESSO_ERR_COLOR));
    }
    *c = (uint8_t)v;
    return FILO_OK;
}

static int done(filo_ctx *ctx, gesso_status st, filo_value *out) {
    if (st != GESSO_OK) {
        return fail(ctx, gesso_message(st));
    }
    return nothing(ctx, out);
}

/* A declaration in an ops script builds the image before any edit: the
   history starts after it. */
static void settle(gesso_script *s) {
    if (s->doc != NULL) {
        gesso_forget(s->doc);
    }
}

/* ---- the file's builtins ---- */

static int b_gesso(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    gesso_script *s = script(ctx);
    int32_t v[3];
    if (ints(ctx, a, n, 3, v) != FILO_OK) {
        return FILO_ERR;
    }
    if (s->stage != STAGE_NONE) {
        return fail(ctx, "image already defined");
    }
    if (v[0] != GESSO_VERSION) {
        return fail(ctx, "unsupported version");
    }
    if (v[1] < 1 || v[2] < 1 || v[1] > GESSO_SIDE_MAX || v[2] > GESSO_SIDE_MAX) {
        return fail(ctx, gesso_message(GESSO_ERR_SIZE));
    }
    gesso_image *img = s->img;
    memset(img, 0, META);
    img->width = v[1];
    img->height = v[2];
    img->ncolors = 1;
    s->stage = STAGE_IMAGE;
    settle(s);
    return nothing(ctx, out);
}

static int b_palette(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    gesso_script *s = script(ctx);
    gesso_image *img = s->img;
    if (s->stage != STAGE_IMAGE || img->ncolors != 1) {
        return fail(ctx, "goes once, after gesso and before the frames");
    }
    if (n >= GESSO_COLORS_MAX) {
        return fail(ctx, gesso_message(GESSO_ERR_PALETTE_FULL));
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t rgb;
        if (color_arg(ctx, &a[i], &rgb) != FILO_OK) {
            return FILO_ERR;
        }
        img->palette[i + 1] = 0xff000000u | rgb;
    }
    img->ncolors = n + 1;
    settle(s);
    return nothing(ctx, out);
}

static int b_frame(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    gesso_script *s = script(ctx);
    gesso_image *img = s->img;
    if (s->stage == STAGE_NONE) {
        return fail(ctx, gesso_message(GESSO_ERR_NO_IMAGE));
    }
    if (n < 1 || n - 1 != (uint32_t)img->height) {
        return fail(ctx, "expects the duration and one string per row");
    }
    if (img->nframes == GESSO_FRAMES_MAX) {
        return fail(ctx, gesso_message(GESSO_ERR_FRAMES_FULL));
    }
    int32_t ms;
    if (int_arg(ctx, &a[0], &ms) != FILO_OK) {
        return FILO_ERR;
    }
    if (ms < 1 || ms > GESSO_DURATION_MAX) {
        return fail(ctx, gesso_message(GESSO_ERR_DURATION));
    }
    uint8_t *f = gesso_frame_pixels(img, img->nframes);
    for (uint32_t y = 0; y < (uint32_t)img->height; y++) {
        filo_str row;
        if (str_arg(ctx, &a[y + 1], &row) != FILO_OK) {
            return FILO_ERR;
        }
        if (row.len != 2 * (uint32_t)img->width) {
            return fail(ctx, "a row has two hex digits per pixel");
        }
        for (uint32_t x = 0; x < (uint32_t)img->width; x++) {
            int hi = hex(row.ptr[(size_t)2 * x]);
            int lo = hex(row.ptr[(size_t)2 * x + 1]);
            if (hi < 0 || lo < 0) {
                return fail(ctx, "a row has two hex digits per pixel");
            }
            uint32_t c = (uint32_t)hi << 4u | (uint32_t)lo;
            if (c >= img->ncolors) {
                return fail(ctx, gesso_message(GESSO_ERR_COLOR));
            }
            f[(size_t)y * (size_t)img->width + x] = (uint8_t)c;
        }
    }
    img->duration[img->nframes] = (uint32_t)ms;
    img->nframes++;
    s->stage = STAGE_FRAMES;
    settle(s);
    return nothing(ctx, out);
}

/* How a direction is written; forward is the default and never written. */
static const char *const directions[GESSO_DIRECTIONS] = {"forward", "reverse", "pingpong"};

static int b_tag(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    gesso_script *s = script(ctx);
    gesso_image *img = s->img;
    filo_str name;
    int32_t first;
    int32_t last;
    if (n != 3 && n != 4) {
        return fail(ctx, "expects 3 or 4 arguments");
    }
    if (str_arg(ctx, &a[0], &name) != FILO_OK || int_arg(ctx, &a[1], &first) != FILO_OK ||
        int_arg(ctx, &a[2], &last) != FILO_OK) {
        return FILO_ERR;
    }
    uint32_t dir = GESSO_FORWARD;
    if (n == 4) {
        filo_str d;
        if (str_arg(ctx, &a[3], &d) != FILO_OK) {
            return FILO_ERR;
        }
        dir = GESSO_DIRECTIONS;
        for (uint32_t k = 0; k < GESSO_DIRECTIONS; k++) {
            size_t len = strlen(directions[k]);
            if (d.len == len && memcmp(d.ptr, directions[k], len) == 0) {
                dir = k;
            }
        }
        if (dir == GESSO_DIRECTIONS) {
            return fail(ctx, "direction must be \"forward\", \"reverse\" or \"pingpong\"");
        }
    }
    if (!gesso_tag_name_ok(name.ptr, name.len)) {
        return fail(ctx, gesso_message(GESSO_ERR_TAG_NAME));
    }
    if (first < 0 || last < first || (uint32_t)last >= img->nframes) {
        return fail(ctx, gesso_message(GESSO_ERR_TAG_RANGE));
    }
    char cname[GESSO_TAG_NAME_MAX] = {0};
    memcpy(cname, name.ptr, name.len);
    if (s->doc != NULL) {
        return done(ctx, gesso_tag_set(s->doc, cname, (uint32_t)first, (uint32_t)last, dir), out);
    }
    for (uint32_t i = 0; i < img->ntags; i++) {
        if (memcmp(img->tags[i].name, cname, sizeof cname) == 0) {
            return fail(ctx, "duplicate tag");
        }
    }
    if (img->ntags == GESSO_TAGS_MAX) {
        return fail(ctx, gesso_message(GESSO_ERR_TAGS_FULL));
    }
    gesso_tag *t = &img->tags[img->ntags++];
    memcpy(t->name, cname, sizeof cname);
    t->first = (uint32_t)first;
    t->last = (uint32_t)last;
    t->direction = dir;
    return nothing(ctx, out);
}

/* ---- the ops script's builtins ---- */

static gesso_doc *doc(filo_ctx *ctx) {
    return script(ctx)->doc;
}

static int b_new(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    gesso_script *s = script(ctx);
    int32_t v[2];
    if (ints(ctx, a, n, 2, v) != FILO_OK) {
        return FILO_ERR;
    }
    if (s->stage != STAGE_NONE) {
        return fail(ctx, "image already defined");
    }
    gesso_status st = gesso_new(s->doc, v[0], v[1]);
    if (st == GESSO_OK) {
        s->stage = STAGE_FRAMES;
    }
    return done(ctx, st, out);
}

static int b_pixel(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    int32_t v[3];
    uint8_t c;
    if (ints(ctx, a, n, 3, v) != FILO_OK || index_ok(ctx, v[2], &c) != FILO_OK) {
        return FILO_ERR;
    }
    return done(ctx, gesso_pixel(doc(ctx), v[0], v[1], c), out);
}

static int b_line(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    int32_t v[5];
    uint8_t c;
    if (ints(ctx, a, n, 5, v) != FILO_OK || index_ok(ctx, v[4], &c) != FILO_OK) {
        return FILO_ERR;
    }
    return done(ctx, gesso_line(doc(ctx), v[0], v[1], v[2], v[3], c), out);
}

typedef gesso_status (*shape_fn)(gesso_doc *, int32_t, int32_t, int32_t, int32_t, uint8_t, int);

static int shape(filo_ctx *ctx, shape_fn fn, int filled, const filo_value *a, uint32_t n,
                 filo_value *out) {
    int32_t v[5];
    uint8_t c;
    if (ints(ctx, a, n, 5, v) != FILO_OK || index_ok(ctx, v[4], &c) != FILO_OK) {
        return FILO_ERR;
    }
    return done(ctx, fn(doc(ctx), v[0], v[1], v[2], v[3], c, filled), out);
}

static int b_rect(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    return shape(ctx, gesso_rect_draw, 0, a, n, out);
}

static int b_rect_fill(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    return shape(ctx, gesso_rect_draw, 1, a, n, out);
}

static int b_ellipse(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    return shape(ctx, gesso_ellipse, 0, a, n, out);
}

static int b_ellipse_fill(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    return shape(ctx, gesso_ellipse, 1, a, n, out);
}

static int b_fill(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    int32_t v[3];
    uint8_t c;
    if (ints(ctx, a, n, 3, v) != FILO_OK || index_ok(ctx, v[2], &c) != FILO_OK) {
        return FILO_ERR;
    }
    return done(ctx, gesso_fill(doc(ctx), v[0], v[1], c), out);
}

static int b_select(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    int32_t v[4];
    if (ints(ctx, a, n, 4, v) != FILO_OK) {
        return FILO_ERR;
    }
    return done(ctx, gesso_select(doc(ctx), v[0], v[1], v[2], v[3]), out);
}

static int b_move_selection(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    int32_t v[2];
    if (ints(ctx, a, n, 2, v) != FILO_OK) {
        return FILO_ERR;
    }
    return done(ctx, gesso_move_selection(doc(ctx), v[0], v[1]), out);
}

static int b_duplicate_selection(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    int32_t v[2];
    if (ints(ctx, a, n, 2, v) != FILO_OK) {
        return FILO_ERR;
    }
    return done(ctx, gesso_duplicate_selection(doc(ctx), v[0], v[1]), out);
}

static int b_selection_transparent(filo_ctx *ctx, const filo_value *a, uint32_t n,
                                   filo_value *out) {
    if (count(ctx, n, 1) != FILO_OK) {
        return FILO_ERR;
    }
    if (a[0].kind != FILO_BOOL) {
        return fail(ctx, "expected #t or #f");
    }
    gesso_selection_transparent(doc(ctx), a[0].u.b ? 1 : 0);
    return nothing(ctx, out);
}

static int b_flip(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    filo_str s;
    if (count(ctx, n, 1) != FILO_OK || str_arg(ctx, &a[0], &s) != FILO_OK) {
        return FILO_ERR;
    }
    if (s.len != 1 || (s.ptr[0] != 'h' && s.ptr[0] != 'v')) {
        return fail(ctx, "expected \"h\" or \"v\"");
    }
    return done(ctx, gesso_flip(doc(ctx), s.ptr[0] == 'v'), out);
}

static int b_rotate(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    (void)a;
    if (count(ctx, n, 0) != FILO_OK) {
        return FILO_ERR;
    }
    return done(ctx, gesso_rotate(doc(ctx)), out);
}

static int b_drop(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    (void)a;
    if (count(ctx, n, 0) != FILO_OK) {
        return FILO_ERR;
    }
    gesso_drop(doc(ctx));
    return nothing(ctx, out);
}

static int b_delete_selection(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    (void)a;
    if (count(ctx, n, 0) != FILO_OK) {
        return FILO_ERR;
    }
    return done(ctx, gesso_delete_selection(doc(ctx)), out);
}

static int b_copy(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    (void)a;
    if (count(ctx, n, 0) != FILO_OK) {
        return FILO_ERR;
    }
    return done(ctx, gesso_copy(doc(ctx)), out);
}

static int b_cut(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    (void)a;
    if (count(ctx, n, 0) != FILO_OK) {
        return FILO_ERR;
    }
    return done(ctx, gesso_cut(doc(ctx)), out);
}

static int b_paste(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    int32_t v[2];
    if (ints(ctx, a, n, 2, v) != FILO_OK) {
        return FILO_ERR;
    }
    return done(ctx, gesso_paste(doc(ctx), v[0], v[1]), out);
}

static int b_resize(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    int32_t v[2];
    if (ints(ctx, a, n, 2, v) != FILO_OK) {
        return FILO_ERR;
    }
    return done(ctx, gesso_resize(doc(ctx), v[0], v[1]), out);
}

static int b_frame_add(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    (void)a;
    if (count(ctx, n, 0) != FILO_OK) {
        return FILO_ERR;
    }
    return done(ctx, gesso_frame_add(doc(ctx)), out);
}

static int b_frame_duplicate(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    (void)a;
    if (count(ctx, n, 0) != FILO_OK) {
        return FILO_ERR;
    }
    return done(ctx, gesso_frame_duplicate(doc(ctx)), out);
}

static int b_frame_delete(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    (void)a;
    if (count(ctx, n, 0) != FILO_OK) {
        return FILO_ERR;
    }
    return done(ctx, gesso_frame_delete(doc(ctx)), out);
}

static int frame_index(filo_ctx *ctx, int32_t v, uint32_t *out) {
    if (v < 0) {
        return fail(ctx, gesso_message(GESSO_ERR_FRAME));
    }
    *out = (uint32_t)v;
    return FILO_OK;
}

static int b_frame_move(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    int32_t v[2];
    uint32_t from;
    uint32_t to;
    if (ints(ctx, a, n, 2, v) != FILO_OK || frame_index(ctx, v[0], &from) != FILO_OK ||
        frame_index(ctx, v[1], &to) != FILO_OK) {
        return FILO_ERR;
    }
    return done(ctx, gesso_frame_move(doc(ctx), from, to), out);
}

static int b_frame_select(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    int32_t v[1];
    uint32_t f;
    if (ints(ctx, a, n, 1, v) != FILO_OK || frame_index(ctx, v[0], &f) != FILO_OK) {
        return FILO_ERR;
    }
    return done(ctx, gesso_frame_select(doc(ctx), f), out);
}

static int b_duration(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    int32_t v[2];
    uint32_t f;
    if (ints(ctx, a, n, 2, v) != FILO_OK || frame_index(ctx, v[0], &f) != FILO_OK) {
        return FILO_ERR;
    }
    if (v[1] < 0) {
        return fail(ctx, gesso_message(GESSO_ERR_DURATION));
    }
    return done(ctx, gesso_frame_duration(doc(ctx), f, (uint32_t)v[1]), out);
}

/* (frames-x first last ...): the frames as indices, checked like one. */
static int frame_range(filo_ctx *ctx, const filo_value *a, uint32_t n, uint32_t want, uint32_t *v) {
    int32_t raw[3];
    if (ints(ctx, a, n, want, raw) != FILO_OK) {
        return FILO_ERR;
    }
    for (uint32_t i = 0; i < want; i++) {
        if (frame_index(ctx, raw[i], &v[i]) != FILO_OK) {
            return FILO_ERR;
        }
    }
    return FILO_OK;
}

static int b_frames_duplicate(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    uint32_t v[2];
    if (frame_range(ctx, a, n, 2, v) != FILO_OK) {
        return FILO_ERR;
    }
    return done(ctx, gesso_frames_duplicate(doc(ctx), v[0], v[1]), out);
}

static int b_frames_delete(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    uint32_t v[2];
    if (frame_range(ctx, a, n, 2, v) != FILO_OK) {
        return FILO_ERR;
    }
    return done(ctx, gesso_frames_delete(doc(ctx), v[0], v[1]), out);
}

static int b_frames_move(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    uint32_t v[3];
    if (frame_range(ctx, a, n, 3, v) != FILO_OK) {
        return FILO_ERR;
    }
    return done(ctx, gesso_frames_move(doc(ctx), v[0], v[1], v[2]), out);
}

static int b_frames_duration(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    int32_t v[3];
    uint32_t first;
    uint32_t last;
    if (ints(ctx, a, n, 3, v) != FILO_OK || frame_index(ctx, v[0], &first) != FILO_OK ||
        frame_index(ctx, v[1], &last) != FILO_OK) {
        return FILO_ERR;
    }
    if (v[2] < 0) {
        return fail(ctx, gesso_message(GESSO_ERR_DURATION));
    }
    return done(ctx, gesso_frames_duration(doc(ctx), first, last, (uint32_t)v[2]), out);
}

static int b_color_delete(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    int32_t v[1];
    if (ints(ctx, a, n, 1, v) != FILO_OK) {
        return FILO_ERR;
    }
    if (v[0] < 0) {
        return fail(ctx, gesso_message(GESSO_ERR_COLOR));
    }
    return done(ctx, gesso_color_delete(doc(ctx), (uint32_t)v[0]), out);
}

static int b_color_move(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    int32_t v[2];
    if (ints(ctx, a, n, 2, v) != FILO_OK) {
        return FILO_ERR;
    }
    if (v[0] < 0 || v[1] < 0) {
        return fail(ctx, gesso_message(GESSO_ERR_COLOR));
    }
    return done(ctx, gesso_color_move(doc(ctx), (uint32_t)v[0], (uint32_t)v[1]), out);
}

static int b_brush(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    int32_t size;
    if (n != 1 && n != 2) {
        return fail(ctx, "expects 1 or 2 arguments");
    }
    if (int_arg(ctx, &a[0], &size) != FILO_OK) {
        return FILO_ERR;
    }
    if (size < 1 || size > GESSO_BRUSH_MAX) {
        return fail(ctx, "brush size must be 1 to 16");
    }
    int round = 0;
    if (n == 2) {
        filo_str form;
        if (str_arg(ctx, &a[1], &form) != FILO_OK) {
            return FILO_ERR;
        }
        round = form.len == 5 && memcmp(form.ptr, "round", 5) == 0;
        if (!round && !(form.len == 6 && memcmp(form.ptr, "square", 6) == 0)) {
            return fail(ctx, "brush shape must be \"square\" or \"round\"");
        }
    }
    gesso_brush(doc(ctx), (uint32_t)size, round);
    return nothing(ctx, out);
}

static int b_untag(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    filo_str name;
    if (count(ctx, n, 1) != FILO_OK || str_arg(ctx, &a[0], &name) != FILO_OK) {
        return FILO_ERR;
    }
    if (!gesso_tag_name_ok(name.ptr, name.len)) {
        return fail(ctx, gesso_message(GESSO_ERR_TAG_NAME));
    }
    char cname[GESSO_TAG_NAME_MAX] = {0};
    memcpy(cname, name.ptr, name.len);
    return done(ctx, gesso_tag_delete(doc(ctx), cname), out);
}

static int b_color(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    int32_t i;
    uint32_t rgb;
    if (count(ctx, n, 2) != FILO_OK || int_arg(ctx, &a[0], &i) != FILO_OK ||
        color_arg(ctx, &a[1], &rgb) != FILO_OK) {
        return FILO_ERR;
    }
    if (i < 0) {
        return fail(ctx, gesso_message(GESSO_ERR_COLOR));
    }
    return done(ctx, gesso_color_set(doc(ctx), (uint32_t)i, rgb), out);
}

static int b_undo(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    (void)a;
    if (count(ctx, n, 0) != FILO_OK) {
        return FILO_ERR;
    }
    *out = filo_bool(gesso_undo(doc(ctx)) != 0);
    return FILO_OK;
}

static int b_redo(filo_ctx *ctx, const filo_value *a, uint32_t n, filo_value *out) {
    (void)a;
    if (count(ctx, n, 0) != FILO_OK) {
        return FILO_ERR;
    }
    *out = filo_bool(gesso_redo(doc(ctx)) != 0);
    return FILO_OK;
}

typedef struct {
    const char *name;
    filo_builtin fn;
} builtin;

static const builtin file_builtins[] = {
    {"gesso", b_gesso},
    {"palette", b_palette},
    {"frame", b_frame},
    {"tag", b_tag},
};

static const builtin ops_builtins[] = {
    {"new", b_new},
    {"pixel", b_pixel},
    {"line", b_line},
    {"rect", b_rect},
    {"rect-fill", b_rect_fill},
    {"ellipse", b_ellipse},
    {"ellipse-fill", b_ellipse_fill},
    {"fill", b_fill},
    {"select", b_select},
    {"move-selection", b_move_selection},
    {"duplicate-selection", b_duplicate_selection},
    {"selection-transparent", b_selection_transparent},
    {"flip", b_flip},
    {"rotate", b_rotate},
    {"drop", b_drop},
    {"delete-selection", b_delete_selection},
    {"copy", b_copy},
    {"cut", b_cut},
    {"paste", b_paste},
    {"resize", b_resize},
    {"frame-add", b_frame_add},
    {"frame-duplicate", b_frame_duplicate},
    {"frame-delete", b_frame_delete},
    {"frame-move", b_frame_move},
    {"frame-select", b_frame_select},
    {"duration", b_duration},
    {"frames-duplicate", b_frames_duplicate},
    {"frames-delete", b_frames_delete},
    {"frames-move", b_frames_move},
    {"frames-duration", b_frames_duration},
    {"untag", b_untag},
    {"brush", b_brush},
    {"color", b_color},
    {"color-delete", b_color_delete},
    {"color-move", b_color_move},
    {"undo", b_undo},
    {"redo", b_redo},
};

static int register_all(filo_ctx *ctx, const builtin *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (filo_register_builtin(ctx, b[i].name, b[i].fn) != FILO_OK) {
            return FILO_ERR;
        }
    }
    return FILO_OK;
}

static int open_script(gesso_script *s, gesso_image *img, gesso_doc *d, uint8_t *mem, size_t cap) {
    s->img = img;
    s->doc = d;
    s->stage = STAGE_NONE;
    size_t half = cap / 2;
    if (filo_init(&s->ctx, &filo_nolibc_host, mem, half, mem + half, cap - half) != FILO_OK) {
        return FILO_ERR;
    }
    return register_all(&s->ctx, file_builtins, sizeof file_builtins / sizeof file_builtins[0]);
}

int gesso_script_file(gesso_script *s, gesso_image *out, uint8_t *mem, size_t cap) {
    if (open_script(s, out, NULL, mem, cap) != FILO_OK) {
        return FILO_ERR;
    }
    out->nframes = 0;
    /* A file names no globals of its own: a typo fails at load. */
    filo_seal_globals(&s->ctx);
    return FILO_OK;
}

int gesso_script_ops(gesso_script *s, gesso_doc *d, uint8_t *mem, size_t cap) {
    if (open_script(s, &d->img, d, mem, cap) != FILO_OK) {
        return FILO_ERR;
    }
    if (d->img.nframes > 0) {
        s->stage = STAGE_FRAMES;
    }
    return register_all(&s->ctx, ops_builtins, sizeof ops_builtins / sizeof ops_builtins[0]);
}

int gesso_script_run(gesso_script *s, const uint8_t *src, size_t len) {
    if (len >= 3 && src[0] == 0xef && src[1] == 0xbb && src[2] == 0xbf) {
        src += 3;
        len -= 3;
    }
    filo_prog prog;
    if (filo_compile(&s->ctx, src, len, &prog) != FILO_OK) {
        return FILO_ERR;
    }
    filo_limits lim = {s->doc != NULL ? STEPS_OPS : STEPS_FILE, FILO_RECURSION_LIMIT_DEFAULT};
    if (filo_run(&s->ctx, &prog, &lim, NULL) != FILO_OK) {
        return FILO_ERR;
    }
    if (s->doc == NULL && s->stage == STAGE_NONE) {
        return filo_fail(&s->ctx, "not a gesso image");
    }
    if (s->doc == NULL && s->img->nframes == 0) {
        return filo_fail(&s->ctx, "the image has no frames");
    }
    return FILO_OK;
}

/* ---- writer ---- */

typedef struct {
    uint8_t *dst;
    size_t cap;
    size_t len;
} sink;

static void put(sink *o, const char *s, size_t n) {
    if (o->len + n <= o->cap) {
        memcpy(o->dst + o->len, s, n);
    }
    o->len += n;
}

static void put_str(sink *o, const char *s) {
    put(o, s, strlen(s));
}

static void put_uint(sink *o, uint32_t v) {
    char buf[10];
    size_t n = sizeof buf;
    do {
        buf[--n] = (char)('0' + v % 10);
        v /= 10;
    } while (v > 0);
    put(o, buf + n, sizeof buf - n);
}

static void put_hex(sink *o, uint32_t v, int digits) {
    static const char digit[] = "0123456789abcdef";
    char buf[8] = {0};
    for (int i = digits - 1; i >= 0; i--) {
        buf[i] = digit[v & 0xfu];
        v >>= 4u;
    }
    put(o, buf, (size_t)digits);
}

size_t gesso_write(const gesso_image *img, uint8_t *dst, size_t cap) {
    sink o = {dst, cap, 0};
    put_str(&o, "; gesso image: index 0 is transparent, the palette starts at 01\n(gesso ");
    put_uint(&o, GESSO_VERSION);
    put_str(&o, " ");
    put_uint(&o, (uint32_t)img->width);
    put_str(&o, " ");
    put_uint(&o, (uint32_t)img->height);
    put_str(&o, ")\n(palette");
    for (uint32_t i = 1; i < img->ncolors; i++) {
        put_str(&o, "\n  \"#");
        put_hex(&o, img->palette[i] & 0xffffffu, 6);
        put_str(&o, i + 1 == img->ncolors ? "\") ; " : "\" ; ");
        put_hex(&o, i, 2);
    }
    put_str(&o, img->ncolors == 1 ? ")\n" : "\n");
    size_t n = (size_t)img->width * (size_t)img->height;
    for (uint32_t f = 0; f < img->nframes; f++) {
        const uint8_t *px = img->pixels + (size_t)f * n;
        put_str(&o, "(frame ");
        put_uint(&o, img->duration[f]);
        for (int32_t y = 0; y < img->height; y++) {
            put_str(&o, "\n  \"");
            for (int32_t x = 0; x < img->width; x++) {
                put_hex(&o, px[(size_t)y * (size_t)img->width + (size_t)x], 2);
            }
            put_str(&o, "\"");
        }
        put_str(&o, ")\n");
    }
    for (uint32_t i = 0; i < img->ntags; i++) {
        put_str(&o, "(tag \"");
        put_str(&o, img->tags[i].name);
        put_str(&o, "\" ");
        put_uint(&o, img->tags[i].first);
        put_str(&o, " ");
        put_uint(&o, img->tags[i].last);
        if (img->tags[i].direction != GESSO_FORWARD && img->tags[i].direction < GESSO_DIRECTIONS) {
            put_str(&o, " \"");
            put_str(&o, directions[img->tags[i].direction]);
            put_str(&o, "\"");
        }
        put_str(&o, ")\n");
    }
    return o.len;
}
