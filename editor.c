#include "editor.h"

static int32_t iabs(int32_t v) {
    return v < 0 ? -v : v;
}

void editor_init(editor *e, gesso_doc *doc) {
    e->doc = doc;
    e->tool = TOOL_PENCIL;
    e->previous = TOOL_PENCIL;
    e->fg = 1;
    e->bg = 0;
    e->filled = 0;
    e->pixel_perfect = 1;
    e->mirror_x = 0;
    e->mirror_y = 0;
    e->down = 0;
    e->moving = 0;
    e->npend = 0;
}

void editor_tool(editor *e, tool_id t) {
    if (e->down) {
        gesso_end(e->doc);
        e->down = 0;
    }
    if (t != TOOL_SELECT) {
        gesso_drop(e->doc);
    }
    if (t == TOOL_PICKER && e->tool != TOOL_PICKER) {
        e->previous = e->tool;
    }
    e->tool = t;
}

/* The places a point lands with the mirrors on: itself first. */
static int mirrors(const editor *e, int32_t x, int32_t y, int32_t *mx, int32_t *my) {
    int32_t w = e->doc->img.width;
    int32_t h = e->doc->img.height;
    int n = 0;
    mx[n] = x;
    my[n++] = y;
    if (e->mirror_x) {
        mx[n] = w - 1 - x;
        my[n++] = y;
    }
    if (e->mirror_y) {
        mx[n] = x;
        my[n++] = h - 1 - y;
    }
    if (e->mirror_x && e->mirror_y) {
        mx[n] = w - 1 - x;
        my[n++] = h - 1 - y;
    }
    return n;
}

/* ---- pencil and eraser ---- */

static void paint(editor *e, int32_t x, int32_t y) {
    int32_t mx[MIRRORS_MAX];
    int32_t my[MIRRORS_MAX];
    int n = mirrors(e, x, y, mx, my);
    uint8_t under[MIRRORS_MAX];
    for (int i = 0; i < n; i++) {
        under[i] = gesso_view(e->doc, mx[i], my[i]);
    }
    for (int i = 0; i < n; i++) {
        (void)gesso_pixel(e->doc, mx[i], my[i], e->color);
    }
    if (!e->pixel_perfect || e->tool != TOOL_PENCIL || e->doc->brush > 1) {
        return;
    }
    /* An L corner: the stroke went a -> b -> (x, y) with a and (x, y)
       diagonal neighbors, so b goes back to what it covered. */
    if (e->npend == 2 && iabs(x - e->px[0]) == 1 && iabs(y - e->py[0]) == 1) {
        int32_t bx[MIRRORS_MAX];
        int32_t by[MIRRORS_MAX];
        int m = mirrors(e, e->px[1], e->py[1], bx, by);
        for (int i = m - 1; i >= 0; i--) {
            (void)gesso_pixel(e->doc, bx[i], by[i], e->under[i]);
        }
        /* A mirrored copy of b may sit on (x, y)'s copies: paint those again. */
        for (int i = 0; i < n; i++) {
            (void)gesso_pixel(e->doc, mx[i], my[i], e->color);
        }
    } else if (e->npend == 2) {
        e->px[0] = e->px[1];
        e->py[0] = e->py[1];
    } else {
        e->npend++;
    }
    e->px[e->npend - 1] = x;
    e->py[e->npend - 1] = y;
    for (int i = 0; i < n; i++) {
        e->under[i] = under[i];
    }
}

/* Every pixel from the last point to (x1, y1), the last point excluded. */
static void stroke_to(editor *e, int32_t x1, int32_t y1) {
    int32_t x = e->lx;
    int32_t y = e->ly;
    int32_t dx = iabs(x1 - x);
    int32_t dy = -iabs(y1 - y);
    int32_t sx = x < x1 ? 1 : -1;
    int32_t sy = y < y1 ? 1 : -1;
    int32_t err = dx + dy;
    while (x != x1 || y != y1) {
        int32_t e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y += sy;
        }
        paint(e, x, y);
    }
    e->lx = x1;
    e->ly = y1;
}

/* ---- shapes ---- */

static void constrain(const editor *e, int32_t *x, int32_t *y, uint32_t mods) {
    if (!(mods & EDITOR_SHIFT)) {
        return;
    }
    int32_t dx = *x - e->x0;
    int32_t dy = *y - e->y0;
    int32_t ax = iabs(dx);
    int32_t ay = iabs(dy);
    if (e->tool == TOOL_LINE && ax > 2 * ay) {
        *y = e->y0;
        return;
    }
    if (e->tool == TOOL_LINE && ay > 2 * ax) {
        *x = e->x0;
        return;
    }
    int32_t side = ax > ay ? ax : ay;
    *x = e->x0 + (dx < 0 ? -side : side);
    *y = e->y0 + (dy < 0 ? -side : side);
}

static void shape(editor *e, int32_t x1, int32_t y1) {
    int32_t ax[MIRRORS_MAX];
    int32_t ay[MIRRORS_MAX];
    int32_t bx[MIRRORS_MAX];
    int32_t by[MIRRORS_MAX];
    int n = mirrors(e, e->x0, e->y0, ax, ay);
    (void)mirrors(e, x1, y1, bx, by);
    for (int i = 0; i < n; i++) {
        switch (e->tool) {
        case TOOL_LINE:
            (void)gesso_line(e->doc, ax[i], ay[i], bx[i], by[i], e->color);
            break;
        case TOOL_RECT:
            (void)gesso_rect_draw(e->doc, ax[i], ay[i], bx[i], by[i], e->color, e->filled);
            break;
        case TOOL_ELLIPSE:
            (void)gesso_ellipse(e->doc, ax[i], ay[i], bx[i], by[i], e->color, e->filled);
            break;
        default:
            break;
        }
    }
}

/* ---- gestures ---- */

static void pick(editor *e, int32_t x, int32_t y, int secondary) {
    uint8_t c = gesso_view(e->doc, x, y);
    if (secondary) {
        e->bg = c;
        return;
    }
    e->fg = c;
}

static int inside_selection(const gesso_doc *d, int32_t x, int32_t y) {
    return d->selected && x >= d->sel.x && y >= d->sel.y && x < d->sel.x + d->sel.w &&
           y < d->sel.y + d->sel.h;
}

void editor_press(editor *e, int32_t x, int32_t y, int secondary) {
    gesso_doc *d = e->doc;
    if (e->down || d->img.nframes == 0) {
        return;
    }
    e->down = 1;
    e->secondary = secondary;
    e->x0 = x;
    e->y0 = y;
    e->lx = x;
    e->ly = y;
    e->color = secondary ? e->bg : e->fg;
    e->npend = 0;
    e->moved = 0;
    switch (e->tool) {
    case TOOL_PICKER:
        pick(e, x, y, secondary);
        return;
    case TOOL_SELECT:
        e->moving = inside_selection(d, x, y);
        if (!e->moving) {
            (void)gesso_select(d, x, y, x, y);
        }
        return;
    case TOOL_FILL: {
        int32_t mx[MIRRORS_MAX];
        int32_t my[MIRRORS_MAX];
        int n = mirrors(e, x, y, mx, my);
        gesso_begin(d);
        for (int i = 0; i < n; i++) {
            (void)gesso_fill(d, mx[i], my[i], e->color);
        }
        gesso_end(d);
        e->down = 0;
        return;
    }
    case TOOL_ERASER:
        e->color = 0;
        gesso_begin(d);
        paint(e, x, y);
        return;
    case TOOL_PENCIL:
        gesso_begin(d);
        paint(e, x, y);
        return;
    default:
        gesso_begin(d);
        shape(e, x, y);
        return;
    }
}

void editor_drag(editor *e, int32_t x, int32_t y, uint32_t mods) {
    gesso_doc *d = e->doc;
    if (!e->down) {
        return;
    }
    switch (e->tool) {
    case TOOL_PICKER:
        pick(e, x, y, e->secondary);
        return;
    case TOOL_SELECT:
        if (e->moving) {
            if (x == e->lx && y == e->ly) {
                return;
            }
            if (!d->floating && (mods & EDITOR_ALT)) {
                (void)gesso_duplicate_selection(d, x - e->lx, y - e->ly);
            } else {
                (void)gesso_move_selection(d, x - e->lx, y - e->ly);
            }
            e->lx = x;
            e->ly = y;
            return;
        }
        e->moved = e->moved || x != e->x0 || y != e->y0;
        (void)gesso_select(d, e->x0, e->y0, x, y);
        return;
    case TOOL_PENCIL:
    case TOOL_ERASER:
        stroke_to(e, x, y);
        return;
    case TOOL_LINE:
    case TOOL_RECT:
    case TOOL_ELLIPSE:
        constrain(e, &x, &y, mods);
        gesso_revert(d);
        shape(e, x, y);
        return;
    default:
        return;
    }
}

void editor_release(editor *e, int32_t x, int32_t y, uint32_t mods) {
    if (!e->down) {
        return;
    }
    editor_drag(e, x, y, mods);
    e->down = 0;
    switch (e->tool) {
    case TOOL_PICKER:
        e->tool = e->previous;
        return;
    case TOOL_SELECT:
        /* A click outside the selection, without a drag, deselects. */
        if (!e->moving && !e->moved) {
            gesso_drop(e->doc);
        }
        e->moving = 0;
        return;
    default:
        gesso_end(e->doc);
        return;
    }
}

void editor_cancel(editor *e) {
    if (!e->down) {
        return;
    }
    e->down = 0;
    switch (e->tool) {
    case TOOL_PICKER:
        e->tool = e->previous;
        return;
    case TOOL_SELECT:
        if (!e->moving) {
            gesso_drop(e->doc);
        }
        e->moving = 0;
        return;
    default:
        gesso_revert(e->doc);
        gesso_end(e->doc);
        return;
    }
}
