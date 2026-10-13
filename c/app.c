/* gessetto: the editor's window, on lavagna. */
#include "editor.h"
#include "gesso.h"
#include "gesso_filo.h"
#include "gesso_png.h"
#include "lavagna.h"
#include "ui.h"

enum {
    LOG_CAP = 64 * 1024 * 1024,
    SCRIPT_CAP = 32 * 1024 * 1024,
    TEXT_CAP = 48 * 1024 * 1024,
    PNG_FILE_MAX = 16 * 1024 * 1024,
    PATH_CAP = 4096,
    LABEL_MAX = 160,

    /* Layout, in points. */
    OPT_H = 28,
    STATUS_H = 18,
    PALETTE_H = 40,
    FRAMES_H = 44,
    TOOLS_W = 64,
    RIGHT_W = 136,
    BUTTON = 26,
    GAP = 2,
    PAD = 4,
    SWATCH = 16,
    SWATCH_MIN = 5,
    CELL_W = 28,
    CELL_H = 24,
    TAG_H = 12,
    PREVIEW = 120,
    OPTIONS_MAX = 12,

    NEW_SIDE = 32,
    DOUBLE_CLICK_NS = 400000000,
    ANTS_NS = 150000000,
    PINCH_STEP = 120,
    ONION_ALPHA = 96,
    GRID_ALPHA = 48,
};

enum {
    M_NEW = 1,
    M_OPEN,
    M_SAVE,
    M_SAVE_AS,
    M_UNDO,
    M_REDO,
    M_CUT,
    M_COPY,
    M_PASTE,
    M_DELETE,
    M_SELECT_ALL,
    M_DESELECT,
    M_FLIP_H,
    M_FLIP_V,
    M_ROTATE,
    M_TRANSPARENT,
    M_FRAME_NEW,
    M_FRAME_DUP,
    M_FRAME_DEL,
    M_FRAME_PREV,
    M_FRAME_NEXT,
    M_DURATION,
    M_TAG_NEW,
    M_TAG_DEL,
    M_PLAY,
    M_ZOOM_IN,
    M_ZOOM_OUT,
    M_ZOOM_FIT,
    M_GRID,
    M_ONION,
    M_COLOR_EDIT,
    M_COLOR_ADD,
    M_SWAP,
    M_IMPORT,
};

enum {
    A_NEW_SIZE = 100,
    A_SAVE_CHANGES,
    A_DURATION,
    A_TAG,
    A_TAG_DEL,
    A_COLOR,
    A_COLOR_ADD,
    A_ERROR,
};

enum {
    P_OPEN = 200,
    P_SAVE,
    P_IMPORT,
};

enum {
    AFTER_NOTHING,
    AFTER_QUIT,
    AFTER_NEW,
    AFTER_OPEN,
    AFTER_IMPORT,
    AFTER_OPEN_GIVEN,
    AFTER_IMPORT_GIVEN,
};

enum {
    T_NONE,
    T_CANVAS,
    T_PAN,
    T_FRAME,
};

enum {
    O_PERFECT = 1,
    O_MIRROR_X,
    O_MIRROR_Y,
    O_OUTLINE,
    O_FILLED,
    O_TRANSPARENT,
    O_FLIP_H,
    O_FLIP_V,
    O_ROTATE,
    O_GRID,
    O_ONION,
};

#define CHECK_LIGHT 0xffe0e0e0u
#define CHECK_DARK 0xffb0b0b0u

typedef struct {
    ui_rect opt;
    ui_rect tools;
    ui_rect canvas;
    ui_rect right;
    ui_rect frames;
    ui_rect palette;
    ui_rect status;
} layout;

typedef struct {
    const char *label;
    int id;
    int on;
    ui_rect r;
} option;

typedef struct {
    lavagna *lv;
    editor ed;
    int32_t s;
    int32_t zoom;
    int32_t panx;
    int32_t pany;
    int fit_pending;
    int grid;
    int onion;
    int playing;
    uint32_t preview;
    uint64_t preview_next;
    char path[PATH_CAP];
    char name[PATH_CAP];  /* shown and offered on save while there is no path */
    char given[PATH_CAP]; /* a file handed over from outside, waiting on unsaved changes */
    int after;
    int target;
    int space;
    int32_t pan_x0;
    int32_t pan_y0;
    int32_t pan_ox;
    int32_t pan_oy;
    int32_t hx;
    int32_t hy;
    int32_t pinch;
    uint64_t last_click;
    int32_t last_swatch;
    uint64_t ants_next;
    int32_t ants;
    uint32_t edit_color;
    /* Frames strip: the first cell shown, the frame it last showed active,
       a frame being dragged and where it would go, scroll left over. */
    uint32_t first_frame;
    uint32_t shown_frame;
    uint32_t drag_frame;
    uint32_t drop_frame;
    int32_t strip_scroll;
    int redraw;
    int was_dirty;
} app;

static gesso_doc doc;
static gesso_image scratch;
static gesso_script script;
static uint8_t log_mem[LOG_CAP];
static uint8_t script_mem[SCRIPT_CAP];
static uint8_t text[TEXT_CAP];
static uint8_t png_work[PNG_FILE_MAX + PNG_WORK_RAW];
static uint32_t png_argb[PNG_SIDE_MAX * PNG_SIDE_MAX];
static uint8_t png_index[PNG_SIDE_MAX * PNG_SIDE_MAX];
static uint32_t clip_argb[GESSO_FRAME_PIXELS];
static app the_app;

static const int32_t zooms[] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64};

static const char *const tool_names[TOOL_COUNT] = {
    "Pencil (B)", "Eraser (E)",    "Fill (G)",    "Picker (I)",
    "Line (N)",   "Rectangle (R)", "Ellipse (O)", "Select (M)",
};

static const lav_menu_item menu[] = {
    {"File", "New...", 'n', LAV_MOD_CMD, M_NEW},
    {"File", "Open...", 'o', LAV_MOD_CMD, M_OPEN},
    {"File", NULL, 0, 0, 0},
    {"File", "Save", 's', LAV_MOD_CMD, M_SAVE},
    {"File", "Save As...", 's', LAV_MOD_CMD | LAV_MOD_SHIFT, M_SAVE_AS},
    {"File", NULL, 0, 0, 0},
    {"File", "Import PNG...", 'i', LAV_MOD_CMD | LAV_MOD_SHIFT, M_IMPORT},
    {"Edit", "Undo", 'z', LAV_MOD_CMD, M_UNDO},
    {"Edit", "Redo", 'z', LAV_MOD_CMD | LAV_MOD_SHIFT, M_REDO},
    {"Edit", NULL, 0, 0, 0},
    {"Edit", "Cut", 'x', LAV_MOD_CMD, M_CUT},
    {"Edit", "Copy", 'c', LAV_MOD_CMD, M_COPY},
    {"Edit", "Paste", 'v', LAV_MOD_CMD, M_PASTE},
    {"Edit", "Delete", 0, 0, M_DELETE},
    {"Edit", NULL, 0, 0, 0},
    {"Edit", "Select All", 'a', LAV_MOD_CMD, M_SELECT_ALL},
    {"Edit", "Deselect", 'd', LAV_MOD_CMD, M_DESELECT},
    {"Image", "Flip Horizontal", 'h', LAV_MOD_SHIFT, M_FLIP_H},
    {"Image", "Flip Vertical", 'v', LAV_MOD_SHIFT, M_FLIP_V},
    {"Image", "Rotate 90", 'r', LAV_MOD_CMD, M_ROTATE},
    {"Image", NULL, 0, 0, 0},
    {"Image", "Transparent Selection", 0, 0, M_TRANSPARENT},
    {"Frame", "New Frame", 'n', LAV_MOD_ALT, M_FRAME_NEW},
    {"Frame", "Duplicate Frame", 'd', LAV_MOD_ALT, M_FRAME_DUP},
    {"Frame", "Delete Frame", 0, 0, M_FRAME_DEL},
    {"Frame", NULL, 0, 0, 0},
    {"Frame", "Previous Frame  ,", 0, 0, M_FRAME_PREV},
    {"Frame", "Next Frame  .", 0, 0, M_FRAME_NEXT},
    {"Frame", NULL, 0, 0, 0},
    {"Frame", "Duration...", 0, 0, M_DURATION},
    {"Frame", "New Tag...", 0, 0, M_TAG_NEW},
    {"Frame", "Delete Tag...", 0, 0, M_TAG_DEL},
    {"Frame", NULL, 0, 0, 0},
    {"Frame", "Play / Stop  P", 0, 0, M_PLAY},
    {"View", "Zoom In", '=', LAV_MOD_CMD, M_ZOOM_IN},
    {"View", "Zoom Out", '-', LAV_MOD_CMD, M_ZOOM_OUT},
    {"View", "Fit", '0', LAV_MOD_CMD, M_ZOOM_FIT},
    {"View", NULL, 0, 0, 0},
    {"View", "Grid", '\'', LAV_MOD_CMD, M_GRID},
    {"View", "Onion Skin", 0, 0, M_ONION},
    {"Colors", "Edit Color...", 0, 0, M_COLOR_EDIT},
    {"Colors", "Add Color...", 0, 0, M_COLOR_ADD},
    {"Colors", "Swap Colors  X", 0, 0, M_SWAP},
};

/* ---- small text helpers, no libc ---- */

static size_t slen(const char *s) {
    size_t n = 0;
    while (s[n] != '\0') {
        n++;
    }
    return n;
}

static void scopy(char *dst, size_t cap, const char *src) {
    if (cap == 0) {
        return;
    }
    size_t i = 0;
    for (; i + 1 < cap && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static void sappend(char *dst, size_t cap, const char *src) {
    size_t n = slen(dst);
    if (n >= cap) {
        return;
    }
    scopy(dst + n, cap - n, src);
}

static void sappend_int(char *dst, size_t cap, int64_t v) {
    char buf[24];
    size_t n = sizeof buf;
    buf[--n] = '\0';
    int neg = v < 0;
    uint64_t u = neg ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
    do {
        buf[--n] = (char)('0' + u % 10);
        u /= 10;
    } while (u > 0);
    if (neg) {
        buf[--n] = '-';
    }
    sappend(dst, cap, buf + n);
}

/* Reads a decimal number at *p, skipping spaces before it. */
static int read_int(const char **p, int32_t *out) {
    const char *s = *p;
    while (*s == ' ') {
        s++;
    }
    if (*s < '0' || *s > '9') {
        return 0;
    }
    int64_t v = 0;
    while (*s >= '0' && *s <= '9' && v < 1000000) {
        v = v * 10 + (*s - '0');
        s++;
    }
    *out = (int32_t)v;
    *p = s;
    return 1;
}

static int read_color(const char *s, uint32_t *rgb) {
    while (*s == ' ') {
        s++;
    }
    if (*s == '#') {
        s++;
    }
    uint32_t c = 0;
    for (int i = 0; i < 6; i++) {
        char h = s[i];
        uint32_t d = 0;
        if (h >= '0' && h <= '9') {
            d = (uint32_t)(h - '0');
        } else if (h >= 'a' && h <= 'f') {
            d = (uint32_t)(h - 'a' + 10);
        } else if (h >= 'A' && h <= 'F') {
            d = (uint32_t)(h - 'A' + 10);
        } else {
            return 0;
        }
        c = c << 4u | d;
    }
    *rgb = c;
    return 1;
}

static void hex_color(char *dst, uint32_t rgb) {
    static const char digit[] = "0123456789abcdef";
    dst[0] = '#';
    for (int i = 0; i < 6; i++) {
        dst[1 + i] = digit[rgb >> (20u - 4u * (uint32_t)i) & 0xfu];
    }
    dst[7] = '\0';
}

static const char *base_name(const char *path) {
    const char *b = path;
    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/') {
            b = p + 1;
        }
    }
    return b;
}

static int32_t floordiv(int32_t a, int32_t b) {
    int32_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        q--;
    }
    return q;
}

/* ---- layout ---- */

static layout lay(const app *a, int32_t w, int32_t h) {
    int32_t s = a->s;
    layout L;
    L.opt = (ui_rect){0, 0, w, OPT_H * s};
    L.status = (ui_rect){0, h - STATUS_H * s, w, STATUS_H * s};
    L.palette = (ui_rect){0, L.status.y - PALETTE_H * s, w, PALETTE_H * s};
    L.frames = (ui_rect){0, L.palette.y - FRAMES_H * s, w, FRAMES_H * s};
    int32_t mid_y = L.opt.h;
    int32_t mid_h = L.frames.y - mid_y;
    L.tools = (ui_rect){0, mid_y, TOOLS_W * s, mid_h};
    L.right = (ui_rect){w - RIGHT_W * s, mid_y, RIGHT_W * s, mid_h};
    L.canvas = (ui_rect){L.tools.w, mid_y, L.right.x - L.tools.w, mid_h};
    return L;
}

static ui_rect tool_button(const app *a, const layout *L, int i) {
    int32_t s = a->s;
    return (ui_rect){
        L->tools.x + (PAD + (i % 2) * (BUTTON + GAP)) * s,
        L->tools.y + (PAD + (i / 2) * (BUTTON + GAP)) * s,
        BUTTON * s,
        BUTTON * s,
    };
}

static int options(const app *a, const layout *L, option *out) {
    const editor *e = &a->ed;
    int n = 0;
    switch (e->tool) {
    case TOOL_PENCIL:
        out[n++] = (option){"Pixel perfect", O_PERFECT, e->pixel_perfect, {0}};
        break;
    case TOOL_RECT:
    case TOOL_ELLIPSE:
        out[n++] = (option){"Outline", O_OUTLINE, !e->filled, {0}};
        out[n++] = (option){"Filled", O_FILLED, e->filled, {0}};
        break;
    case TOOL_SELECT:
        out[n++] = (option){"Transparent", O_TRANSPARENT, doc.transparent, {0}};
        out[n++] = (option){"Flip H", O_FLIP_H, 0, {0}};
        out[n++] = (option){"Flip V", O_FLIP_V, 0, {0}};
        out[n++] = (option){"Rotate", O_ROTATE, 0, {0}};
        break;
    default:
        break;
    }
    if (e->tool != TOOL_SELECT && e->tool != TOOL_PICKER) {
        out[n++] = (option){"Mirror X", O_MIRROR_X, e->mirror_x, {0}};
        out[n++] = (option){"Mirror Y", O_MIRROR_Y, e->mirror_y, {0}};
    }
    int fixed = n;
    out[n++] = (option){"Grid", O_GRID, a->grid, {0}};
    out[n++] = (option){"Onion", O_ONION, a->onion, {0}};

    int32_t s = a->s;
    int32_t x = L->tools.w + PAD * s;
    int32_t y = L->opt.y + (OPT_H - 22) / 2 * s;
    for (int i = 0; i < fixed; i++) {
        int32_t w = ui_text_width(out[i].label, s) + 12 * s;
        out[i].r = (ui_rect){x, y, w, 22 * s};
        x += w + GAP * s;
    }
    x = L->opt.w - PAD * s;
    for (int i = n - 1; i >= fixed; i--) {
        int32_t w = ui_text_width(out[i].label, s) + 12 * s;
        x -= w;
        out[i].r = (ui_rect){x, y, w, 22 * s};
        x -= GAP * s;
    }
    return n;
}

/* Frames strip: four buttons, then one cell per frame, then the duration. */
enum {
    F_ADD,
    F_DUP,
    F_DEL,
    F_PLAY,
    F_BUTTONS,
};

static const char *const frame_buttons[F_BUTTONS] = {"+", "Dup", "Del", "Play"};

static ui_rect frame_button(const app *a, const layout *L, int i) {
    int32_t s = a->s;
    int32_t x = L->frames.x + PAD * s;
    for (int k = 0; k < i; k++) {
        x += ui_text_width(frame_buttons[k], s) + (12 + GAP) * s;
    }
    int32_t w = ui_text_width(frame_buttons[i], s) + 12 * s;
    return (ui_rect){x, L->frames.y + (PAD + TAG_H) * s, w, CELL_H * s};
}

static int32_t cells_x(const app *a, const layout *L) {
    ui_rect last = frame_button(a, L, F_BUTTONS - 1);
    return last.x + last.w + PAD * 2 * a->s;
}

static ui_rect frame_cell(const app *a, const layout *L, uint32_t f) {
    int32_t s = a->s;
    return (ui_rect){
        cells_x(a, L) + ((int32_t)f - (int32_t)a->first_frame) * (CELL_W + GAP) * s,
        L->frames.y + (PAD + TAG_H) * s,
        CELL_W * s,
        CELL_H * s,
    };
}

static ui_rect duration_button(const app *a, const layout *L);

/* How many cells fit between the buttons and the duration. */
static uint32_t cells_visible(const app *a, const layout *L) {
    int32_t step = (CELL_W + GAP) * a->s;
    int32_t room = duration_button(a, L).x - PAD * a->s - cells_x(a, L) + GAP * a->s;
    return room > step ? (uint32_t)(room / step) : 1;
}

/* The frame whose cell is under x, the strip's ends for anything beyond. */
static uint32_t frame_at(const app *a, const layout *L, int32_t x) {
    int32_t step = (CELL_W + GAP) * a->s;
    int32_t i = (int32_t)a->first_frame + floordiv(x - cells_x(a, L), step);
    int32_t last = (int32_t)doc.img.nframes - 1;
    if (i < 0) {
        i = 0;
    }
    return (uint32_t)(i > last ? last : i);
}

static void clamp_strip(app *a, const layout *L) {
    uint32_t n = cells_visible(a, L);
    uint32_t max = doc.img.nframes > n ? doc.img.nframes - n : 0;
    if (a->first_frame > max) {
        a->first_frame = max;
    }
}

/* The active frame is kept in view when it changes; scrolling the strip
   may then leave it. */
static void follow_frame(app *a, const layout *L) {
    if (doc.frame != a->shown_frame) {
        uint32_t n = cells_visible(a, L);
        if (doc.frame < a->first_frame) {
            a->first_frame = doc.frame;
        }
        if (doc.frame >= a->first_frame + n) {
            a->first_frame = doc.frame - n + 1;
        }
        a->shown_frame = doc.frame;
    }
    clamp_strip(a, L);
}

static ui_rect duration_button(const app *a, const layout *L) {
    int32_t s = a->s;
    int32_t w = 64 * s;
    return (ui_rect){
        L->frames.x + L->frames.w - w - PAD * s,
        L->frames.y + (PAD + TAG_H) * s,
        w,
        CELL_H * s,
    };
}

/* Palette: the foreground and background wells, then the swatches in two
   rows, index 0 first; one more slot adds a color. */
/* Swatches narrow to fit a long palette in the strip, down to SWATCH_MIN. */
static ui_rect swatch(const app *a, const layout *L, uint32_t i) {
    int32_t s = a->s;
    uint32_t cols = (doc.img.ncolors + 2) / 2;
    int32_t x0 = L->palette.x + (PAD + 36 + PAD) * s;
    int32_t y0 = L->palette.y + PAD * s;
    int32_t side = (L->palette.w - x0 - PAD * s) / (int32_t)cols;
    if (side > SWATCH * s) {
        side = SWATCH * s;
    }
    if (side < SWATCH_MIN * s) {
        side = SWATCH_MIN * s;
    }
    return (ui_rect){
        x0 + (int32_t)(i % cols) * side,
        y0 + (int32_t)(i / cols) * SWATCH * s,
        side,
        SWATCH * s,
    };
}

static ui_rect well(const app *a, const layout *L, int background) {
    int32_t s = a->s;
    int32_t off = background ? 12 : 0;
    return (ui_rect){
        L->palette.x + (PAD + off) * s,
        L->palette.y + (PAD + off) * s,
        20 * s,
        20 * s,
    };
}

/* ---- view ---- */

static int zoom_index(int32_t z) {
    int best = 0;
    for (int i = 0; i < (int)(sizeof zooms / sizeof zooms[0]); i++) {
        if (zooms[i] <= z) {
            best = i;
        }
    }
    return best;
}

/* Changes the zoom keeping the image pixel under (x, y) where it is. */
static void zoom_at(app *a, const layout *L, int32_t z, int32_t x, int32_t y) {
    if (z == a->zoom) {
        return;
    }
    int32_t ix = floordiv(x - L->canvas.x - a->panx, a->zoom);
    int32_t iy = floordiv(y - L->canvas.y - a->pany, a->zoom);
    int32_t fx = x - L->canvas.x - a->panx - ix * a->zoom;
    int32_t fy = y - L->canvas.y - a->pany - iy * a->zoom;
    a->panx = x - L->canvas.x - ix * z - fx * z / a->zoom;
    a->pany = y - L->canvas.y - iy * z - fy * z / a->zoom;
    a->zoom = z;
}

static void zoom_step(app *a, const layout *L, int dir, int32_t x, int32_t y) {
    int i = zoom_index(a->zoom) + dir;
    int last = (int)(sizeof zooms / sizeof zooms[0]) - 1;
    if (i < 0) {
        i = 0;
    }
    if (i > last) {
        i = last;
    }
    zoom_at(a, L, zooms[i], x, y);
}

static void fit(app *a, const layout *L) {
    int32_t w = doc.img.width;
    int32_t h = doc.img.height;
    int32_t room_w = L->canvas.w - 32 * a->s;
    int32_t room_h = L->canvas.h - 32 * a->s;
    a->zoom = 1;
    for (int i = 0; i < (int)(sizeof zooms / sizeof zooms[0]); i++) {
        if (w * zooms[i] <= room_w && h * zooms[i] <= room_h) {
            a->zoom = zooms[i];
        }
    }
    a->panx = (L->canvas.w - w * a->zoom) / 2;
    a->pany = (L->canvas.h - h * a->zoom) / 2;
}

/* ---- drawing ---- */

static uint32_t checker(int32_t ix, int32_t iy, int32_t zoom) {
    int32_t cell = zoom >= 8 ? 1 : 8 / zoom;
    return (ix / cell + iy / cell) % 2 != 0 ? CHECK_DARK : CHECK_LIGHT;
}

static void draw_image(const ui_surface *s, ui_rect clipr, int32_t ox, int32_t oy, int32_t z,
                       uint32_t frame, int onion, int floating) {
    const gesso_image *img = &doc.img;
    int32_t ix0 = floordiv(clipr.x - ox, z);
    int32_t iy0 = floordiv(clipr.y - oy, z);
    int32_t ix1 = floordiv(clipr.x + clipr.w - ox - 1, z) + 1;
    int32_t iy1 = floordiv(clipr.y + clipr.h - oy - 1, z) + 1;
    ix0 = ix0 < 0 ? 0 : ix0;
    iy0 = iy0 < 0 ? 0 : iy0;
    ix1 = ix1 > img->width ? img->width : ix1;
    iy1 = iy1 > img->height ? img->height : iy1;
    const uint8_t *px = img->pixels + (size_t)frame * (size_t)img->width * (size_t)img->height;
    const uint8_t *prev = frame > 0 ? px - (size_t)img->width * (size_t)img->height : NULL;
    for (int32_t iy = iy0; iy < iy1; iy++) {
        for (int32_t ix = ix0; ix < ix1; ix++) {
            uint8_t v = floating ? gesso_view(&doc, ix, iy) : px[iy * img->width + ix];
            uint32_t c = v != 0 ? img->palette[v] : checker(ix, iy, z);
            if (v == 0 && onion && prev != NULL && prev[iy * img->width + ix] != 0) {
                c = ui_mix(c, img->palette[prev[iy * img->width + ix]], ONION_ALPHA);
            }
            ui_rect r = {ox + ix * z, oy + iy * z, z, z};
            int32_t x0 = r.x < clipr.x ? clipr.x : r.x;
            int32_t y0 = r.y < clipr.y ? clipr.y : r.y;
            int32_t x1 = r.x + z > clipr.x + clipr.w ? clipr.x + clipr.w : r.x + z;
            int32_t y1 = r.y + z > clipr.y + clipr.h ? clipr.y + clipr.h : r.y + z;
            ui_fill(s, (ui_rect){x0, y0, x1 - x0, y1 - y0}, c);
        }
    }
}

static void ants(const app *a, const ui_surface *s, ui_rect c, ui_rect r) {
    int32_t dash = 4 * a->s;
    int32_t per = r.w * 2 + r.h * 2;
    for (int32_t i = 0; i < per; i++) {
        int32_t x;
        int32_t y;
        if (i < r.w) {
            x = r.x + i;
            y = r.y;
        } else if (i < r.w + r.h) {
            x = r.x + r.w - 1;
            y = r.y + i - r.w;
        } else if (i < 2 * r.w + r.h) {
            x = r.x + r.w - 1 - (i - r.w - r.h);
            y = r.y + r.h - 1;
        } else {
            x = r.x;
            y = r.y + r.h - 1 - (i - 2 * r.w - r.h);
        }
        if (!ui_inside(c, x, y)) {
            continue;
        }
        uint32_t col = ((i + a->ants) / dash) % 2 == 0 ? 0xff000000u : 0xffffffffu;
        ui_fill(s, (ui_rect){x, y, 1, 1}, col);
    }
}

static void draw_canvas(const app *a, const ui_surface *s, const layout *L) {
    ui_rect c = L->canvas;
    ui_fill(s, c, UI_WORKSPACE);
    int32_t z = a->zoom;
    int32_t ox = c.x + a->panx;
    int32_t oy = c.y + a->pany;
    draw_image(s, c, ox, oy, z, doc.frame, a->onion, 1);
    if (a->grid && z >= 4) {
        for (int32_t ix = 1; ix < doc.img.width; ix++) {
            int32_t x = ox + ix * z;
            int32_t y0 = oy < c.y ? c.y : oy;
            int32_t y1 = oy + doc.img.height * z > c.y + c.h ? c.y + c.h : oy + doc.img.height * z;
            if (x >= c.x && x < c.x + c.w && y1 > y0) {
                ui_tint(s, (ui_rect){x, y0, 1, y1 - y0}, 0xff000000u, GRID_ALPHA);
            }
        }
        for (int32_t iy = 1; iy < doc.img.height; iy++) {
            int32_t y = oy + iy * z;
            int32_t x0 = ox < c.x ? c.x : ox;
            int32_t x1 = ox + doc.img.width * z > c.x + c.w ? c.x + c.w : ox + doc.img.width * z;
            if (y >= c.y && y < c.y + c.h && x1 > x0) {
                ui_tint(s, (ui_rect){x0, y, x1 - x0, 1}, 0xff000000u, GRID_ALPHA);
            }
        }
    }
    if (doc.selected) {
        ui_rect r = {ox + doc.sel.x * z, oy + doc.sel.y * z, doc.sel.w * z, doc.sel.h * z};
        ants(a, s, c, r);
    }
}

static void draw_tools(const app *a, const ui_surface *s, const layout *L) {
    ui_fill(s, L->tools, UI_FACE);
    for (int i = 0; i < TOOL_COUNT; i++) {
        ui_rect r = tool_button(a, L, i);
        int on = a->ed.tool == (tool_id)i;
        ui_fill(s, r, on ? UI_LIGHT : UI_FACE);
        ui_bevel(s, r, a->s, on);
        int32_t off = (BUTTON - UI_ICON) / 2 * a->s + (on ? a->s : 0);
        ui_icon(s, r.x + off, r.y + off, a->s, i);
    }
}

static void button(const app *a, const ui_surface *s, ui_rect r, const char *label, int on) {
    ui_fill(s, r, on ? UI_LIGHT : UI_FACE);
    ui_bevel(s, r, a->s, on);
    int32_t tx = r.x + (r.w - ui_text_width(label, a->s)) / 2 + (on ? a->s : 0);
    int32_t ty = r.y + (r.h - 9 * a->s) / 2 + (on ? a->s : 0);
    ui_text(s, tx, ty, a->s, label, UI_TEXT);
}

static void draw_options(const app *a, const ui_surface *s, const layout *L) {
    ui_fill(s, L->opt, UI_FACE);
    ui_fill(s, (ui_rect){0, L->opt.h - a->s, L->opt.w, a->s}, UI_SHADOW);
    ui_text(s, PAD * a->s, L->opt.y + (OPT_H - 9) / 2 * a->s, a->s, "gessetto", UI_DIM);
    option items[OPTIONS_MAX];
    int n = options(a, L, items);
    for (int i = 0; i < n; i++) {
        button(a, s, items[i].r, items[i].label, items[i].on);
    }
}

static void draw_preview(const app *a, const ui_surface *s, const layout *L) {
    ui_fill(s, L->right, UI_FACE);
    ui_fill(s, (ui_rect){L->right.x, L->right.y, a->s, L->right.h}, UI_SHADOW);
    int32_t sp = a->s;
    ui_text(s, L->right.x + PAD * 2 * sp, L->right.y + PAD * sp, sp, "PREVIEW", UI_DIM);
    ui_rect box = {
        L->right.x + (RIGHT_W - PREVIEW) / 2 * sp,
        L->right.y + (PAD + 14) * sp,
        PREVIEW * sp,
        PREVIEW * sp,
    };
    ui_bevel(s, (ui_rect){box.x - 2 * sp, box.y - 2 * sp, box.w + 4 * sp, box.h + 4 * sp}, sp, 1);
    ui_fill(s, box, UI_WORKSPACE);
    int32_t z = box.w / doc.img.width;
    int32_t zh = box.h / doc.img.height;
    z = zh < z ? zh : z;
    z = z < 1 ? 1 : z;
    int32_t ox = box.x + (box.w - doc.img.width * z) / 2;
    int32_t oy = box.y + (box.h - doc.img.height * z) / 2;
    uint32_t f = a->playing ? a->preview : doc.frame;
    draw_image(s, box, ox, oy, z, f, 0, f == doc.frame);
}

static const gesso_tag *tag_at(uint32_t f) {
    for (uint32_t i = 0; i < doc.img.ntags; i++) {
        if (f >= doc.img.tags[i].first && f <= doc.img.tags[i].last) {
            return &doc.img.tags[i];
        }
    }
    return NULL;
}

static void draw_frames(const app *a, const ui_surface *s, const layout *L) {
    int32_t sp = a->s;
    ui_fill(s, L->frames, UI_FACE);
    ui_fill(s, (ui_rect){0, L->frames.y, L->frames.w, sp}, UI_LIGHT);
    for (int i = 0; i < F_BUTTONS; i++) {
        const char *label = i == F_PLAY && a->playing ? "Stop" : frame_buttons[i];
        button(a, s, frame_button(a, L, i), label, i == F_PLAY && a->playing);
    }
    ui_rect dur = duration_button(a, L);
    uint32_t end = a->first_frame + cells_visible(a, L);
    for (uint32_t f = a->first_frame; f < doc.img.nframes && f < end; f++) {
        ui_rect r = frame_cell(a, L, f);
        int on = f == doc.frame;
        ui_fill(s, r, on ? UI_ACCENT : UI_LIGHT);
        ui_bevel(s, r, sp, 1);
        if (a->target == T_FRAME && f == a->drop_frame && f != a->drag_frame) {
            ui_border(s, r, 2 * sp, UI_DARK);
        }
        char num[8] = "";
        sappend_int(num, sizeof num, (int64_t)f + 1);
        ui_text(s, r.x + (r.w - ui_text_width(num, sp)) / 2, r.y + (r.h - 9 * sp) / 2, sp, num,
                on ? UI_LIGHT : UI_TEXT);
    }
    for (uint32_t i = 0; i < doc.img.ntags; i++) {
        const gesso_tag *t = &doc.img.tags[i];
        if (t->last < a->first_frame || t->first >= end) {
            continue;
        }
        ui_rect first = frame_cell(a, L, t->first < a->first_frame ? a->first_frame : t->first);
        ui_rect last = frame_cell(a, L, t->last >= end ? end - 1 : t->last);
        ui_rect bar = {
            first.x,
            L->frames.y + PAD * sp,
            last.x + last.w - first.x,
            (TAG_H - 2) * sp,
        };
        ui_fill(s, bar, UI_SHADOW);
        ui_text(s, bar.x + 2 * sp, bar.y, sp, t->name, UI_LIGHT);
    }
    char ms[24] = "";
    sappend_int(ms, sizeof ms, doc.img.duration[doc.frame]);
    sappend(ms, sizeof ms, " ms");
    button(a, s, dur, ms, 0);
}

static void draw_palette(const app *a, const ui_surface *s, const layout *L) {
    int32_t sp = a->s;
    ui_fill(s, L->palette, UI_FACE);
    const gesso_image *img = &doc.img;
    for (int bgw = 1; bgw >= 0; bgw--) {
        ui_rect w = well(a, L, bgw);
        uint8_t v = bgw ? a->ed.bg : a->ed.fg;
        ui_bevel(s, w, sp, 1);
        ui_rect in = {w.x + 2 * sp, w.y + 2 * sp, w.w - 4 * sp, w.h - 4 * sp};
        ui_fill(s, in, v != 0 ? img->palette[v] : CHECK_LIGHT);
        if (v == 0) {
            ui_fill(s, (ui_rect){in.x, in.y, in.w / 2, in.h / 2}, CHECK_DARK);
            ui_fill(s,
                    (ui_rect){in.x + in.w / 2, in.y + in.h / 2, in.w - in.w / 2, in.h - in.h / 2},
                    CHECK_DARK);
        }
    }
    for (uint32_t i = 0; i <= img->ncolors && i < GESSO_COLORS_MAX; i++) {
        ui_rect r = swatch(a, L, i);
        if (r.x + r.w > L->palette.w) {
            continue;
        }
        int32_t edge = r.w >= 10 * sp ? sp : 0;
        if (edge > 0) {
            ui_bevel(s, r, edge, 1);
        }
        ui_rect in = {r.x + 2 * edge, r.y + 2 * edge, r.w - 4 * edge, r.h - 4 * edge};
        if (i == img->ncolors) {
            ui_fill(s, in, UI_FACE);
            ui_text(s, in.x + (in.w - 5 * sp) / 2, in.y + (in.h - 9 * sp) / 2 + sp, sp, "+",
                    UI_TEXT);
            continue;
        }
        ui_fill(s, in, i != 0 ? img->palette[i] : CHECK_LIGHT);
        if (i == 0) {
            ui_fill(s, (ui_rect){in.x, in.y, in.w / 2, in.h / 2}, CHECK_DARK);
            ui_fill(s,
                    (ui_rect){in.x + in.w / 2, in.y + in.h / 2, in.w - in.w / 2, in.h - in.h / 2},
                    CHECK_DARK);
        }
        if (i == a->ed.fg) {
            ui_border(s, in, sp, 0xffffffffu);
            ui_border(s, (ui_rect){in.x + sp, in.y + sp, in.w - 2 * sp, in.h - 2 * sp}, sp,
                      0xff000000u);
        }
    }
}

static void draw_status(const app *a, const ui_surface *s, const layout *L) {
    int32_t sp = a->s;
    ui_fill(s, L->status, UI_FACE);
    ui_fill(s, (ui_rect){0, L->status.y, L->status.w, sp}, UI_LIGHT);
    char line[LABEL_MAX] = "";
    int32_t ix = floordiv(a->hx - L->canvas.x - a->panx, a->zoom);
    int32_t iy = floordiv(a->hy - L->canvas.y - a->pany, a->zoom);
    if (ui_inside(L->canvas, a->hx, a->hy)) {
        sappend_int(line, sizeof line, ix);
        sappend(line, sizeof line, ", ");
        sappend_int(line, sizeof line, iy);
    }
    sappend(line, sizeof line, "    ");
    sappend_int(line, sizeof line, doc.img.width);
    sappend(line, sizeof line, " x ");
    sappend_int(line, sizeof line, doc.img.height);
    sappend(line, sizeof line, "    ");
    sappend_int(line, sizeof line, (int64_t)a->zoom * 100);
    sappend(line, sizeof line, "%    frame ");
    sappend_int(line, sizeof line, (int64_t)doc.frame + 1);
    sappend(line, sizeof line, "/");
    sappend_int(line, sizeof line, doc.img.nframes);
    const gesso_tag *t = tag_at(doc.frame);
    if (t != NULL) {
        sappend(line, sizeof line, " (");
        sappend(line, sizeof line, t->name);
        sappend(line, sizeof line, ")");
    }
    sappend(line, sizeof line, "    ");
    sappend(line, sizeof line, tool_names[a->ed.tool]);
    ui_text(s, PAD * sp, L->status.y + (STATUS_H - 9) / 2 * sp, sp, line, UI_TEXT);
}

static void draw(app *a, lav_buffer b) {
    ui_surface s = {b.pixels, b.width, b.height, b.stride};
    layout L = lay(a, b.width, b.height);
    draw_canvas(a, &s, &L);
    draw_tools(a, &s, &L);
    draw_options(a, &s, &L);
    draw_preview(a, &s, &L);
    draw_frames(a, &s, &L);
    draw_palette(a, &s, &L);
    draw_status(a, &s, &L);
    lav_dirty(a->lv, 0, 0, b.width, b.height);
}

/* ---- documents ---- */

static void title(app *a) {
    char t[PATH_CAP];
    scopy(t, sizeof t, a->path[0] != '\0' ? base_name(a->path) : a->name);
    sappend(t, sizeof t, " - gessetto");
    lav_set_title(a->lv, t);
    a->was_dirty = -1;
}

static void error(app *a, const char *what, const char *detail) {
    static const char *const ok[] = {"OK"};
    lav_alert(a->lv, A_ERROR, what, detail, ok, 1, NULL);
}

static void new_image(app *a, int32_t w, int32_t h) {
    if (gesso_new(&doc, w, h) != GESSO_OK) {
        error(a, "Cannot create the image", gesso_message(GESSO_ERR_SIZE));
        return;
    }
    a->path[0] = '\0';
    scopy(a->name, sizeof a->name, "untitled.gesso");
    a->fit_pending = 1;
    a->playing = 0;
    title(a);
}

static void import_path(app *a, const char *path) {
    int64_t n = lav_file_read(a->lv, path, text, PNG_FILE_MAX);
    if (n < 0 || n >= PNG_FILE_MAX) {
        error(a, "Cannot read the file", path);
        return;
    }
    png_image p = {.argb = png_argb, .index = png_index};
    png_status ps = png_decode(text, (size_t)n, png_work, sizeof png_work, &p);
    if (ps != PNG_OK) {
        error(a, "Cannot import the PNG", png_message(ps));
        return;
    }
    gesso_status gs = gesso_from_png(&scratch, &p);
    if (gs != GESSO_OK) {
        error(a, "Cannot import the PNG",
              gs == GESSO_ERR_PALETTE_FULL ? "It has more than 255 colors." : gesso_message(gs));
        return;
    }
    gesso_adopt(&doc, &scratch);
    a->path[0] = '\0';
    scopy(a->name, sizeof a->name, base_name(path));
    size_t len = slen(a->name);
    if (len > 4 && a->name[len - 4] == '.') {
        a->name[len - 4] = '\0';
    }
    sappend(a->name, sizeof a->name, ".gesso");
    a->fit_pending = 1;
    a->playing = 0;
    title(a);
}

static void open_path(app *a, const char *path) {
    int64_t n = lav_file_read(a->lv, path, text, sizeof text);
    if (n < 0 || (uint64_t)n >= sizeof text) {
        error(a, "Cannot read the file", path);
        return;
    }
    if (gesso_script_file(&script, &scratch, script_mem, sizeof script_mem) != FILO_OK ||
        gesso_script_run(&script, text, (size_t)n) != FILO_OK) {
        char msg[LABEL_MAX + FILO_ERROR_MAX] = "";
        uint32_t line = 0;
        uint32_t col = 0;
        if (filo_error_at(&script.ctx, &line, &col)) {
            sappend(msg, sizeof msg, "line ");
            sappend_int(msg, sizeof msg, line);
            sappend(msg, sizeof msg, ": ");
        }
        sappend(msg, sizeof msg, filo_error(&script.ctx));
        error(a, "Not a gesso image", msg);
        return;
    }
    gesso_adopt(&doc, &scratch);
    scopy(a->path, sizeof a->path, path);
    a->fit_pending = 1;
    a->playing = 0;
    title(a);
}

static int save_path(app *a, const char *path) {
    editor_tool(&a->ed, a->ed.tool);
    gesso_drop(&doc);
    size_t n = gesso_write(&doc.img, text, sizeof text);
    if (n > sizeof text || !lav_file_write(a->lv, path, text, n)) {
        error(a, "Cannot save the file", path);
        return 0;
    }
    gesso_mark_saved(&doc);
    scopy(a->path, sizeof a->path, path);
    title(a);
    return 1;
}

static void ask_new_size(app *a) {
    static const char *const b[] = {"Create", "Cancel"};
    lav_alert(a->lv, A_NEW_SIZE, "New image", "Width x height, 1 to 256.", b, 2, "32x32");
}

static void proceed(app *a) {
    int after = a->after;
    a->after = AFTER_NOTHING;
    switch (after) {
    case AFTER_QUIT:
        lav_quit(a->lv);
        return;
    case AFTER_NEW:
        ask_new_size(a);
        return;
    case AFTER_OPEN:
        lav_open_panel(a->lv, P_OPEN, "gesso");
        return;
    case AFTER_IMPORT:
        lav_open_panel(a->lv, P_IMPORT, "png");
        return;
    case AFTER_OPEN_GIVEN:
        open_path(a, a->given);
        return;
    case AFTER_IMPORT_GIVEN:
        import_path(a, a->given);
        return;
    default:
        return;
    }
}

/* Unsaved changes are asked about before anything replaces them. */
static void guard(app *a, int after) {
    a->after = after;
    if (!gesso_dirty(&doc)) {
        proceed(a);
        return;
    }
    static const char *const b[] = {"Save", "Don't Save", "Cancel"};
    lav_alert(a->lv, A_SAVE_CHANGES, "Save the changes?",
              "Your changes are lost if you don't save them.", b, 3, NULL);
}

static void save(app *a) {
    if (a->path[0] != '\0') {
        if (save_path(a, a->path)) {
            proceed(a);
        }
        return;
    }
    lav_save_panel(a->lv, P_SAVE, "gesso", a->name);
}

/* ---- commands ---- */

static void ask(app *a, int id, const char *heading, const char *message, const char *initial) {
    static const char *const b[] = {"OK", "Cancel"};
    lav_alert(a->lv, (uint32_t)id, heading, message, b, 2, initial);
}

static void select_tool(app *a, tool_id t) {
    editor_tool(&a->ed, t);
}

static void frame_go(int dir) {
    uint32_t n = doc.img.nframes;
    uint32_t f = dir < 0 ? (doc.frame + n - 1) % n : (doc.frame + 1) % n;
    (void)gesso_frame_select(&doc, f);
}

/* What was copied goes to the system too, as an image other apps take. */
static void to_system(app *a) {
    const gesso_patch *c = &doc.clip;
    size_t n = (size_t)c->w * (size_t)c->h;
    for (size_t i = 0; i < n; i++) {
        uint8_t v = c->pixels[i];
        clip_argb[i] = v != 0 ? doc.img.palette[v] : 0;
    }
    (void)lav_clipboard_set_image(a->lv, clip_argb, c->w, c->h);
}

/* An image on the system clipboard becomes the clipboard, its colors matched
   to the palette; without one, the last copy stays. 0 when it cannot be. */
static int from_system(app *a) {
    int32_t w = 0;
    int32_t h = 0;
    int got = lav_clipboard_get_image(a->lv, clip_argb, GESSO_FRAME_PIXELS, &w, &h);
    if (got == 0) {
        return 1;
    }
    if (got < 0 || w > GESSO_SIDE_MAX || h > GESSO_SIDE_MAX) {
        error(a, "Cannot paste the image", "It is larger than 256 x 256.");
        return 0;
    }
    size_t n = (size_t)w * (size_t)h;
    for (size_t i = 0; i < n; i++) {
        png_index[i] = gesso_nearest(&doc.img, clip_argb[i]);
    }
    return gesso_clip_set(&doc, png_index, w, h) == GESSO_OK;
}

static void command(app *a, const layout *L, uint32_t id) {
    char value[LABEL_MAX] = "";
    switch (id) {
    case M_NEW:
        guard(a, AFTER_NEW);
        return;
    case M_OPEN:
        guard(a, AFTER_OPEN);
        return;
    case M_SAVE:
        save(a);
        return;
    case M_SAVE_AS:
        lav_save_panel(a->lv, P_SAVE, "gesso", a->path[0] != '\0' ? base_name(a->path) : a->name);
        return;
    case M_IMPORT:
        guard(a, AFTER_IMPORT);
        return;
    case M_UNDO:
        editor_tool(&a->ed, a->ed.tool);
        (void)gesso_undo(&doc);
        return;
    case M_REDO:
        (void)gesso_redo(&doc);
        return;
    case M_CUT:
        if (gesso_copy(&doc) == GESSO_OK) {
            to_system(a);
            (void)gesso_delete_selection(&doc);
        }
        return;
    case M_COPY:
        if (gesso_copy(&doc) == GESSO_OK) {
            to_system(a);
        }
        return;
    case M_PASTE: {
        int32_t x = floordiv(-a->panx, a->zoom);
        int32_t y = floordiv(-a->pany, a->zoom);
        if (!from_system(a)) {
            return;
        }
        select_tool(a, TOOL_SELECT);
        (void)gesso_paste(&doc, x < 0 ? 0 : x, y < 0 ? 0 : y);
        return;
    }
    case M_DELETE:
        (void)gesso_delete_selection(&doc);
        return;
    case M_SELECT_ALL:
        select_tool(a, TOOL_SELECT);
        (void)gesso_select(&doc, 0, 0, doc.img.width - 1, doc.img.height - 1);
        return;
    case M_DESELECT:
        gesso_drop(&doc);
        return;
    case M_FLIP_H:
    case M_FLIP_V:
        select_tool(a, TOOL_SELECT);
        (void)gesso_flip(&doc, id == M_FLIP_V);
        return;
    case M_ROTATE:
        select_tool(a, TOOL_SELECT);
        (void)gesso_rotate(&doc);
        return;
    case M_TRANSPARENT:
        gesso_selection_transparent(&doc, !doc.transparent);
        return;
    case M_FRAME_NEW:
        (void)gesso_frame_add(&doc);
        return;
    case M_FRAME_DUP:
        (void)gesso_frame_duplicate(&doc);
        return;
    case M_FRAME_DEL:
        (void)gesso_frame_delete(&doc);
        return;
    case M_FRAME_PREV:
        frame_go(-1);
        return;
    case M_FRAME_NEXT:
        frame_go(1);
        return;
    case M_DURATION:
        sappend_int(value, sizeof value, doc.img.duration[doc.frame]);
        ask(a, A_DURATION, "Frame duration", "Milliseconds, 1 to 60000.", value);
        return;
    case M_TAG_NEW:
        scopy(value, sizeof value, "walk ");
        sappend_int(value, sizeof value, (int64_t)doc.frame + 1);
        sappend(value, sizeof value, "-");
        sappend_int(value, sizeof value, doc.img.nframes);
        ask(a, A_TAG, "New tag", "A name, then the first and last frames.", value);
        return;
    case M_TAG_DEL: {
        const gesso_tag *t = tag_at(doc.frame);
        ask(a, A_TAG_DEL, "Delete tag", "The tag's name.", t != NULL ? t->name : "");
        return;
    }
    case M_PLAY:
        a->playing = !a->playing;
        a->preview = doc.frame;
        a->preview_next = 0;
        return;
    case M_ZOOM_IN:
    case M_ZOOM_OUT:
        zoom_step(a, L, id == M_ZOOM_IN ? 1 : -1, L->canvas.x + L->canvas.w / 2,
                  L->canvas.y + L->canvas.h / 2);
        return;
    case M_ZOOM_FIT:
        fit(a, L);
        return;
    case M_GRID:
        a->grid = !a->grid;
        return;
    case M_ONION:
        a->onion = !a->onion;
        return;
    case M_COLOR_EDIT:
        if (a->ed.fg == 0) {
            error(a, "Index 0 is transparent", "Pick another color to edit it.");
            return;
        }
        a->edit_color = a->ed.fg;
        hex_color(value, doc.img.palette[a->ed.fg] & 0xffffffu);
        ask(a, A_COLOR, "Edit color", "#rrggbb", value);
        return;
    case M_COLOR_ADD:
        hex_color(value, a->ed.fg != 0 ? doc.img.palette[a->ed.fg] & 0xffffffu : 0);
        ask(a, A_COLOR_ADD, "Add color", "#rrggbb", value);
        return;
    case M_SWAP: {
        uint8_t t = a->ed.fg;
        a->ed.fg = a->ed.bg;
        a->ed.bg = t;
        return;
    }
    default:
        return;
    }
}

static void answer(app *a, const lav_event *ev) {
    const char *txt = lav_alert_text(a->lv);
    const char *p = txt;
    int32_t v1 = 0;
    int32_t v2 = 0;
    uint32_t rgb = 0;
    switch (ev->id) {
    case A_SAVE_CHANGES:
        if (ev->button == 0) {
            save(a);
        } else if (ev->button == 1) {
            proceed(a);
        } else {
            a->after = AFTER_NOTHING;
        }
        return;
    case A_NEW_SIZE:
        if (ev->button != 0) {
            return;
        }
        if (!read_int(&p, &v1)) {
            error(a, "Cannot create the image", "Write the size as 32x32.");
            return;
        }
        while (*p == ' ' || *p == 'x' || *p == 'X') {
            p++;
        }
        if (!read_int(&p, &v2)) {
            v2 = v1;
        }
        new_image(a, v1, v2);
        return;
    case A_DURATION:
        if (ev->button == 0 && read_int(&p, &v1) &&
            gesso_frame_duration(&doc, doc.frame, (uint32_t)v1) != GESSO_OK) {
            error(a, "Duration out of range", "Milliseconds, 1 to 60000.");
        }
        return;
    case A_TAG: {
        if (ev->button != 0) {
            return;
        }
        char name[GESSO_TAG_NAME_MAX] = "";
        size_t n = 0;
        while (*p == ' ') {
            p++;
        }
        while (*p != '\0' && *p != ' ' && n + 1 < sizeof name) {
            name[n++] = *p++;
        }
        name[n] = '\0';
        int ok = read_int(&p, &v1);
        while (*p == ' ' || *p == '-') {
            p++;
        }
        if (!read_int(&p, &v2)) {
            v2 = v1;
        }
        gesso_status st = GESSO_ERR_TAG_RANGE;
        if (ok && v1 >= 1 && v2 >= v1) {
            st = gesso_tag_set(&doc, name, (uint32_t)v1 - 1, (uint32_t)v2 - 1);
        }
        if (st != GESSO_OK) {
            error(a, "Cannot set the tag", gesso_message(st));
        }
        return;
    }
    case A_TAG_DEL:
        if (ev->button == 0 && gesso_tag_delete(&doc, txt) != GESSO_OK) {
            error(a, "Cannot delete the tag", gesso_message(GESSO_ERR_NO_TAG));
        }
        return;
    case A_COLOR:
    case A_COLOR_ADD:
        if (ev->button != 0) {
            return;
        }
        if (!read_color(txt, &rgb)) {
            error(a, "Not a color", "Write it as #rrggbb.");
            return;
        }
        if (ev->id == A_COLOR_ADD) {
            uint32_t i = doc.img.ncolors;
            if (gesso_color_set(&doc, i, rgb) == GESSO_OK) {
                a->ed.fg = (uint8_t)i;
                return;
            }
            error(a, "Cannot add the color", gesso_message(GESSO_ERR_PALETTE_FULL));
            return;
        }
        (void)gesso_color_set(&doc, a->edit_color, rgb);
        return;
    default:
        return;
    }
}

static char lower(char c) {
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

static int has_extension(const char *path, const char *ext) {
    size_t n = slen(path);
    size_t e = slen(ext);
    if (n < e) {
        return 0;
    }
    for (size_t i = 0; i < e; i++) {
        if (lower(path[n - e + i]) != ext[i]) {
            return 0;
        }
    }
    return 1;
}

/* A file from the Finder, the Dock or Files: a PNG is imported. */
static void given(app *a) {
    scopy(a->given, sizeof a->given, lav_path(a->lv));
    guard(a, has_extension(a->given, ".png") ? AFTER_IMPORT_GIVEN : AFTER_OPEN_GIVEN);
}

static void panel(app *a, const lav_event *ev) {
    if (ev->button == 0) {
        a->after = AFTER_NOTHING;
        return;
    }
    if (ev->id == P_OPEN) {
        open_path(a, lav_path(a->lv));
        return;
    }
    if (ev->id == P_IMPORT) {
        import_path(a, lav_path(a->lv));
        return;
    }
    if (save_path(a, lav_path(a->lv))) {
        proceed(a);
    }
}

/* ---- input ---- */

static uint32_t editor_mods(uint32_t mods) {
    uint32_t m = 0;
    if (mods & LAV_MOD_SHIFT) {
        m |= EDITOR_SHIFT;
    }
    if (mods & LAV_MOD_ALT) {
        m |= EDITOR_ALT;
    }
    return m;
}

static void image_point(const app *a, const layout *L, int32_t x, int32_t y, int32_t *ix,
                        int32_t *iy) {
    *ix = floordiv(x - L->canvas.x - a->panx, a->zoom);
    *iy = floordiv(y - L->canvas.y - a->pany, a->zoom);
}

static void option_click(app *a, int id) {
    editor *e = &a->ed;
    switch (id) {
    case O_PERFECT:
        e->pixel_perfect = !e->pixel_perfect;
        return;
    case O_MIRROR_X:
        e->mirror_x = !e->mirror_x;
        return;
    case O_MIRROR_Y:
        e->mirror_y = !e->mirror_y;
        return;
    case O_OUTLINE:
        e->filled = 0;
        return;
    case O_FILLED:
        e->filled = 1;
        return;
    case O_TRANSPARENT:
        gesso_selection_transparent(&doc, !doc.transparent);
        return;
    case O_FLIP_H:
        (void)gesso_flip(&doc, 0);
        return;
    case O_FLIP_V:
        (void)gesso_flip(&doc, 1);
        return;
    case O_ROTATE:
        (void)gesso_rotate(&doc);
        return;
    case O_GRID:
        a->grid = !a->grid;
        return;
    case O_ONION:
        a->onion = !a->onion;
        return;
    default:
        return;
    }
}

static void palette_click(app *a, const layout *L, const lav_event *ev) {
    if (ui_inside(well(a, L, 0), ev->x, ev->y) || ui_inside(well(a, L, 1), ev->x, ev->y)) {
        command(a, L, M_SWAP);
        return;
    }
    for (uint32_t i = 0; i <= doc.img.ncolors && i < GESSO_COLORS_MAX; i++) {
        if (!ui_inside(swatch(a, L, i), ev->x, ev->y)) {
            continue;
        }
        if (i == doc.img.ncolors) {
            command(a, L, M_COLOR_ADD);
            return;
        }
        int twice = (int32_t)i == a->last_swatch && ev->time_ns - a->last_click < DOUBLE_CLICK_NS;
        a->last_swatch = (int32_t)i;
        a->last_click = ev->time_ns;
        if (ev->button == LAV_BUTTON_SECONDARY) {
            a->ed.bg = (uint8_t)i;
            return;
        }
        a->ed.fg = (uint8_t)i;
        if (twice) {
            command(a, L, M_COLOR_EDIT);
        }
        return;
    }
}

static void frames_click(app *a, const layout *L, const lav_event *ev) {
    static const uint32_t actions[F_BUTTONS] = {M_FRAME_NEW, M_FRAME_DUP, M_FRAME_DEL, M_PLAY};
    for (int i = 0; i < F_BUTTONS; i++) {
        if (ui_inside(frame_button(a, L, i), ev->x, ev->y)) {
            command(a, L, actions[i]);
            return;
        }
    }
    if (ui_inside(duration_button(a, L), ev->x, ev->y)) {
        command(a, L, M_DURATION);
        return;
    }
    uint32_t end = a->first_frame + cells_visible(a, L);
    for (uint32_t f = a->first_frame; f < doc.img.nframes && f < end; f++) {
        if (ui_inside(frame_cell(a, L, f), ev->x, ev->y)) {
            (void)gesso_frame_select(&doc, f);
            a->target = T_FRAME;
            a->drag_frame = f;
            a->drop_frame = f;
            return;
        }
    }
}

static void press(app *a, const layout *L, const lav_event *ev) {
    if (ui_inside(L->canvas, ev->x, ev->y)) {
        if (a->space || ev->button == LAV_BUTTON_MIDDLE) {
            a->target = T_PAN;
            a->pan_x0 = ev->x;
            a->pan_y0 = ev->y;
            a->pan_ox = a->panx;
            a->pan_oy = a->pany;
            return;
        }
        int32_t ix;
        int32_t iy;
        image_point(a, L, ev->x, ev->y, &ix, &iy);
        a->target = T_CANVAS;
        editor_press(&a->ed, ix, iy, ev->button == LAV_BUTTON_SECONDARY);
        return;
    }
    for (int i = 0; i < TOOL_COUNT; i++) {
        if (ui_inside(tool_button(a, L, i), ev->x, ev->y)) {
            select_tool(a, (tool_id)i);
            return;
        }
    }
    option items[OPTIONS_MAX];
    int n = options(a, L, items);
    for (int i = 0; i < n; i++) {
        if (ui_inside(items[i].r, ev->x, ev->y)) {
            option_click(a, items[i].id);
            return;
        }
    }
    if (ui_inside(L->frames, ev->x, ev->y)) {
        frames_click(a, L, ev);
        return;
    }
    if (ui_inside(L->palette, ev->x, ev->y)) {
        palette_click(a, L, ev);
    }
}

static void move(app *a, const layout *L, const lav_event *ev) {
    a->hx = ev->x;
    a->hy = ev->y;
    int32_t ix;
    int32_t iy;
    switch (a->target) {
    case T_PAN:
        a->panx = a->pan_ox + ev->x - a->pan_x0;
        a->pany = a->pan_oy + ev->y - a->pan_y0;
        return;
    case T_CANVAS:
        image_point(a, L, ev->x, ev->y, &ix, &iy);
        editor_drag(&a->ed, ix, iy, editor_mods(ev->mods));
        return;
    case T_FRAME:
        a->drop_frame = frame_at(a, L, ev->x);
        return;
    default:
        return;
    }
}

static void release(app *a, const layout *L, const lav_event *ev) {
    if (a->target == T_FRAME) {
        a->drop_frame = frame_at(a, L, ev->x);
        if (a->drop_frame != a->drag_frame) {
            (void)gesso_frame_move(&doc, a->drag_frame, a->drop_frame);
        }
    }
    if (a->target == T_CANVAS) {
        int32_t ix;
        int32_t iy;
        image_point(a, L, ev->x, ev->y, &ix, &iy);
        editor_release(&a->ed, ix, iy, editor_mods(ev->mods));
    }
    a->target = T_NONE;
}

static void cancel(app *a) {
    if (a->target == T_CANVAS) {
        editor_cancel(&a->ed);
    }
    a->target = T_NONE;
}

/* The app may be ended in the background: a document with a file is saved.
   yagni: one never saved stays unsaved, until the iPad's iCloud flow says
   where it would go. */
static void suspend(app *a) {
    cancel(a);
    if (gesso_dirty(&doc) && a->path[0] != '\0') {
        (void)save_path(a, a->path);
    }
}

static void key(app *a, const layout *L, const lav_event *ev) {
    static const struct {
        uint32_t key;
        tool_id tool;
    } tool_keys[] = {
        {'b', TOOL_PENCIL}, {'e', TOOL_ERASER}, {'g', TOOL_FILL},    {'i', TOOL_PICKER},
        {'n', TOOL_LINE},   {'r', TOOL_RECT},   {'o', TOOL_ELLIPSE}, {'m', TOOL_SELECT},
    };
    if (ev->key == ' ') {
        a->space = ev->type == LAV_EV_KEY_DOWN;
        return;
    }
    if (ev->type != LAV_EV_KEY_DOWN || (ev->mods & (LAV_MOD_CMD | LAV_MOD_CTRL)) != 0) {
        return;
    }
    int32_t step = (ev->mods & LAV_MOD_SHIFT) ? 10 : 1;
    switch (ev->key) {
    case LAV_KEY_LEFT:
        (void)gesso_move_selection(&doc, -step, 0);
        return;
    case LAV_KEY_RIGHT:
        (void)gesso_move_selection(&doc, step, 0);
        return;
    case LAV_KEY_UP:
        (void)gesso_move_selection(&doc, 0, -step);
        return;
    case LAV_KEY_DOWN:
        (void)gesso_move_selection(&doc, 0, step);
        return;
    case LAV_KEY_DELETE:
    case LAV_KEY_BACKSPACE:
        (void)gesso_delete_selection(&doc);
        return;
    case LAV_KEY_ESCAPE:
    case LAV_KEY_ENTER:
        gesso_drop(&doc);
        return;
    case 'x':
        command(a, L, M_SWAP);
        return;
    case ',':
        command(a, L, M_FRAME_PREV);
        return;
    case '.':
        command(a, L, M_FRAME_NEXT);
        return;
    case 'p':
        command(a, L, M_PLAY);
        return;
    default:
        break;
    }
    if (ev->mods & LAV_MOD_SHIFT) {
        return;
    }
    for (size_t i = 0; i < sizeof tool_keys / sizeof tool_keys[0]; i++) {
        if (tool_keys[i].key == ev->key) {
            select_tool(a, tool_keys[i].tool);
            return;
        }
    }
}

static void scroll_strip(app *a, const layout *L, const lav_event *ev) {
    int32_t step = (CELL_W + GAP) * a->s;
    a->strip_scroll -= ev->scroll_x + ev->scroll_y;
    while (a->strip_scroll >= step) {
        a->strip_scroll -= step;
        a->first_frame++;
    }
    while (a->strip_scroll <= -step) {
        a->strip_scroll += step;
        if (a->first_frame > 0) {
            a->first_frame--;
        }
    }
    clamp_strip(a, L);
}

static void scroll(app *a, const layout *L, const lav_event *ev) {
    if (ui_inside(L->frames, ev->x, ev->y)) {
        scroll_strip(a, L, ev);
        return;
    }
    if (ev->mods & LAV_MOD_CMD) {
        a->pinch += ev->scroll_y * 4;
    } else {
        a->panx += ev->scroll_x;
        a->pany += ev->scroll_y;
        return;
    }
    while (a->pinch >= PINCH_STEP || a->pinch <= -PINCH_STEP) {
        int dir = a->pinch > 0 ? 1 : -1;
        a->pinch -= dir * PINCH_STEP;
        zoom_step(a, L, dir, ev->x, ev->y);
    }
}

static void pinch(app *a, const layout *L, const lav_event *ev) {
    a->pinch += ev->zoom;
    while (a->pinch >= PINCH_STEP || a->pinch <= -PINCH_STEP) {
        int dir = a->pinch > 0 ? 1 : -1;
        a->pinch -= dir * PINCH_STEP;
        zoom_step(a, L, dir, ev->x, ev->y);
    }
}

static void cursor(app *a, const layout *L) {
    lav_cursor_kind k = LAV_CURSOR_ARROW;
    if (a->target == T_PAN) {
        k = LAV_CURSOR_GRAB;
    } else if (ui_inside(L->canvas, a->hx, a->hy)) {
        k = a->space ? LAV_CURSOR_HAND : LAV_CURSOR_CROSSHAIR;
    }
    lav_cursor(a->lv, k);
}

static void tick(app *a, uint64_t now) {
    if (a->playing && now >= a->preview_next) {
        if (a->preview_next != 0) {
            a->preview = (a->preview + 1) % doc.img.nframes;
        }
        a->preview_next = now + (uint64_t)doc.img.duration[a->preview] * 1000000u;
        a->redraw = 1;
    }
    if (doc.selected && now >= a->ants_next) {
        a->ants++;
        a->ants_next = now + ANTS_NS;
        a->redraw = 1;
    }
    int dirty = gesso_dirty(&doc);
    if (dirty != a->was_dirty) {
        lav_set_edited(a->lv, dirty);
        a->was_dirty = dirty;
    }
}

void lav_setup(lav_config *cfg) {
    cfg->title = "gessetto";
    cfg->menu = menu;
    cfg->menu_len = sizeof menu / sizeof menu[0];
    cfg->user = &the_app;
}

static void start(app *a) {
    gesso_init(&doc, log_mem, sizeof log_mem);
    (void)gesso_new(&doc, NEW_SIDE, NEW_SIDE);
    editor_init(&a->ed, &doc);
    a->zoom = 1;
    a->grid = 1;
    a->onion = 0;
    a->fit_pending = 1;
    a->last_swatch = -1;
    a->was_dirty = -1;
    scopy(a->name, sizeof a->name, "untitled.gesso");
    title(a);
}

void lav_frame(lavagna *lv, void *user) {
    app *a = user;
    if (a->lv == NULL) {
        a->lv = lv;
        start(a);
    }
    a->s = lav_device_scale(lv);
    lav_buffer b = lav_pixels(lv);
    layout L = lay(a, b.width, b.height);
    lav_event ev;
    while (lav_poll(lv, &ev)) {
        a->redraw = 1;
        switch (ev.type) {
        case LAV_EV_POINTER_DOWN:
            press(a, &L, &ev);
            break;
        case LAV_EV_POINTER_MOVE:
            move(a, &L, &ev);
            break;
        case LAV_EV_POINTER_UP:
            release(a, &L, &ev);
            break;
        case LAV_EV_POINTER_CANCEL:
            cancel(a);
            break;
        case LAV_EV_SUSPEND:
            suspend(a);
            break;
        case LAV_EV_KEY_DOWN:
        case LAV_EV_KEY_UP:
            key(a, &L, &ev);
            break;
        case LAV_EV_SCROLL:
            scroll(a, &L, &ev);
            break;
        case LAV_EV_PINCH:
            pinch(a, &L, &ev);
            break;
        case LAV_EV_MENU:
            command(a, &L, ev.id);
            break;
        case LAV_EV_ALERT:
            answer(a, &ev);
            break;
        case LAV_EV_PANEL:
            panel(a, &ev);
            break;
        case LAV_EV_OPEN:
            given(a);
            break;
        case LAV_EV_CLOSE:
            guard(a, AFTER_QUIT);
            break;
        default:
            break;
        }
    }
    if (a->fit_pending) {
        fit(a, &L);
        a->fit_pending = 0;
    }
    follow_frame(a, &L);
    cursor(a, &L);
    tick(a, lav_time_ns(lv));
    if (a->redraw) {
        draw(a, b);
        a->redraw = 0;
    }
}
