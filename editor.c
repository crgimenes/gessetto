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
    e->radius = 0;
    e->spray = 4;
    e->seed = 1;
    e->npoly = 0;
    e->bending = 0;
    e->pixel_perfect = 1;
    e->mirror_x = 0;
    e->mirror_y = 0;
    e->down = 0;
    e->moving = 0;
    e->npend = 0;
}

void editor_tool(editor *e, tool_id t) {
    editor_finish(e);
    if (e->down) {
        gesso_end(e->doc);
        e->down = 0;
    }
    if (t != TOOL_SELECT && t != TOOL_LASSO && t != TOOL_TEXT) {
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

/* ---- spray ---- */

enum {
    SPRAY_DOTS = 6, /* each sample of the gesture */
};

static void spray(editor *e, int32_t x, int32_t y) {
    int32_t mx[MIRRORS_MAX];
    int32_t my[MIRRORS_MAX];
    int n = mirrors(e, x, y, mx, my);
    for (int i = 0; i < n; i++) {
        (void)gesso_spray(e->doc, mx[i], my[i], e->spray, SPRAY_DOTS, e->seed, e->color);
    }
    e->seed = e->seed * 1103515245u + 12345u;
}

/* ---- polygon ---- */

/* What the polygon looks like now: the open path through its vertices
   while it is made, the closed shape at the end; every mirrored copy. */
static void polygon_draw(editor *e, int closed) {
    gesso_revert(e->doc);
    int32_t mx[MIRRORS_MAX][GESSO_POLYGON_MAX];
    int32_t my[MIRRORS_MAX][GESSO_POLYGON_MAX];
    int copies = 1;
    for (uint32_t i = 0; i < e->npoly; i++) {
        int32_t ax[MIRRORS_MAX];
        int32_t ay[MIRRORS_MAX];
        copies = mirrors(e, e->polyx[i], e->polyy[i], ax, ay);
        for (int k = 0; k < copies; k++) {
            mx[k][i] = ax[k];
            my[k][i] = ay[k];
        }
    }
    for (int k = 0; k < copies; k++) {
        if (closed && e->npoly >= 3) {
            (void)gesso_polygon(e->doc, mx[k], my[k], e->npoly, e->color, e->filled);
            continue;
        }
        for (uint32_t i = 0; i + 1 < e->npoly; i++) {
            (void)gesso_line(e->doc, mx[k][i], my[k][i], mx[k][i + 1], my[k][i + 1], e->color);
        }
        if (e->npoly == 1) {
            (void)gesso_pixel(e->doc, mx[k][0], my[k][0], e->color);
        }
    }
}

void editor_finish(editor *e) {
    if (e->bending) {
        e->bending = 0;
        e->down = 0;
        gesso_end(e->doc);
        return;
    }
    if (e->npoly == 0) {
        return;
    }
    polygon_draw(e, 1);
    e->npoly = 0;
    e->down = 0;
    gesso_end(e->doc);
}

static void polygon_press(editor *e, int32_t x, int32_t y) {
    if (e->npoly >= 3 && iabs(x - e->polyx[0]) <= 1 && iabs(y - e->polyy[0]) <= 1) {
        editor_finish(e);
        return;
    }
    if (e->npoly == 0) {
        gesso_begin(e->doc);
    }
    if (e->npoly == GESSO_POLYGON_MAX) {
        return;
    }
    e->polyx[e->npoly] = x;
    e->polyy[e->npoly] = y;
    e->npoly++;
    polygon_draw(e, 0);
}

/* ---- curve ---- */

/* The line from (x0, y0) to (cx1, cy1), bent toward the control point once
   it has one; every mirrored copy. */
static void curve_draw(editor *e) {
    gesso_revert(e->doc);
    int32_t ax[MIRRORS_MAX];
    int32_t ay[MIRRORS_MAX];
    int32_t bx[MIRRORS_MAX];
    int32_t by[MIRRORS_MAX];
    int32_t kx[MIRRORS_MAX];
    int32_t ky[MIRRORS_MAX];
    int n = mirrors(e, e->x0, e->y0, ax, ay);
    (void)mirrors(e, e->cx1, e->cy1, bx, by);
    (void)mirrors(e, e->ctlx, e->ctly, kx, ky);
    for (int i = 0; i < n; i++) {
        if (e->bending) {
            (void)gesso_curve(e->doc, ax[i], ay[i], kx[i], ky[i], bx[i], by[i], e->color);
        } else {
            (void)gesso_line(e->doc, ax[i], ay[i], bx[i], by[i], e->color);
        }
    }
}

static void curve_press(editor *e, int32_t x, int32_t y) {
    if (e->bending) {
        e->ctlx = x;
        e->ctly = y;
        curve_draw(e);
        return;
    }
    gesso_begin(e->doc);
    e->cx1 = x;
    e->cy1 = y;
    curve_draw(e);
}

static void curve_release(editor *e) {
    if (e->bending || (e->cx1 == e->x0 && e->cy1 == e->y0)) {
        e->bending = 0;
        gesso_end(e->doc);
        return;
    }
    e->bending = 1;
    e->ctlx = (e->x0 + e->cx1) / 2;
    e->ctly = (e->y0 + e->cy1) / 2;
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
    int line = e->tool == TOOL_LINE || e->tool == TOOL_CURVE;
    /* Lines snap to the nearest of flat, 2:1, 45 degrees, 1:2 and upright,
       the pixel-art angles; 2:1 in the pixels covered, so its steps are all
       2 long. */
    if (line && 4 * ay < ax) {
        *y = e->y0;
        return;
    }
    if (line && 4 * ax < ay) {
        *x = e->x0;
        return;
    }
    if (line && (4 * ay < 3 * ax || 4 * ax < 3 * ay)) {
        int wide = ax > ay;
        int32_t steps = ((wide ? ax : ay) + 2) / 2;
        int32_t along = 2 * steps - 1;
        int32_t across = steps - 1;
        *x = e->x0 + (dx < 0 ? -1 : 1) * (wide ? along : across);
        *y = e->y0 + (dy < 0 ? -1 : 1) * (wide ? across : along);
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
            (void)gesso_rect_round(e->doc, ax[i], ay[i], bx[i], by[i], e->radius, e->color,
                                   e->filled);
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

void editor_press(editor *e, int32_t x, int32_t y, int secondary) {
    gesso_doc *d = e->doc;
    if (e->down || d->img.nframes == 0) {
        return;
    }
    e->down = 1;
    if (e->tool == TOOL_CURVE && e->bending) {
        curve_press(e, x, y);
        return;
    }
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
    case TOOL_LASSO:
    case TOOL_TEXT:
        e->moving = gesso_selected(d, x, y);
        if (e->moving) {
            return;
        }
        if (e->tool == TOOL_TEXT) {
            gesso_drop(d);
            return;
        }
        if (e->tool == TOOL_LASSO) {
            (void)gesso_lasso_begin(d, x, y);
            return;
        }
        (void)gesso_select(d, x, y, x, y);
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
    case TOOL_SPRAY:
        gesso_begin(d);
        spray(e, x, y);
        return;
    case TOOL_CURVE:
        curve_press(e, x, y);
        return;
    case TOOL_POLYGON:
        polygon_press(e, x, y);
        if (e->npoly == 0) {
            e->down = 0;
        }
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
    case TOOL_LASSO:
    case TOOL_TEXT:
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
        if (e->tool == TOOL_TEXT) {
            return;
        }
        if (e->tool == TOOL_LASSO) {
            (void)gesso_lasso_to(d, x, y);
            return;
        }
        (void)gesso_select(d, e->x0, e->y0, x, y);
        return;
    case TOOL_PENCIL:
    case TOOL_ERASER:
        stroke_to(e, x, y);
        return;
    case TOOL_SPRAY:
        spray(e, x, y);
        return;
    case TOOL_POLYGON:
        if (e->npoly > 0) {
            e->polyx[e->npoly - 1] = x;
            e->polyy[e->npoly - 1] = y;
            polygon_draw(e, 0);
        }
        return;
    case TOOL_CURVE:
        if (e->bending) {
            e->ctlx = x;
            e->ctly = y;
        } else {
            constrain(e, &x, &y, mods);
            e->cx1 = x;
            e->cy1 = y;
        }
        curve_draw(e);
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
    case TOOL_POLYGON:
        return;
    case TOOL_CURVE:
        curve_release(e);
        return;
    case TOOL_PICKER:
        e->tool = e->previous;
        return;
    case TOOL_SELECT:
    case TOOL_LASSO:
    case TOOL_TEXT:
        if (e->tool == TOOL_LASSO && !e->moving) {
            gesso_lasso_end(e->doc);
        }
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
    if (e->npoly > 0 || e->bending) {
        e->bending = 0;
        gesso_revert(e->doc);
        gesso_end(e->doc);
        e->npoly = 0;
        e->down = 0;
        return;
    }
    if (!e->down) {
        return;
    }
    e->down = 0;
    switch (e->tool) {
    case TOOL_PICKER:
        e->tool = e->previous;
        return;
    case TOOL_SELECT:
    case TOOL_LASSO:
    case TOOL_TEXT:
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
