#include "ui.h"

#include "font.h"
#include "tools.h"

enum {
    FONT_FIRST = 32,
    FONT_LAST = 126,
    FONT_COLUMNS = FONT_WIDTH / UI_GLYPH_W,
};

int ui_inside(ui_rect r, int32_t x, int32_t y) {
    return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

static ui_rect clip(const ui_surface *s, ui_rect r) {
    int32_t x0 = r.x < 0 ? 0 : r.x;
    int32_t y0 = r.y < 0 ? 0 : r.y;
    int32_t x1 = r.x + r.w > s->w ? s->w : r.x + r.w;
    int32_t y1 = r.y + r.h > s->h ? s->h : r.y + r.h;
    ui_rect c = {x0, y0, x1 - x0, y1 - y0};
    if (c.w < 0) {
        c.w = 0;
    }
    if (c.h < 0) {
        c.h = 0;
    }
    return c;
}

void ui_fill(const ui_surface *s, ui_rect r, uint32_t c) {
    r = clip(s, r);
    for (int32_t y = r.y; y < r.y + r.h; y++) {
        uint32_t *row = s->px + (int64_t)y * s->stride;
        for (int32_t x = r.x; x < r.x + r.w; x++) {
            row[x] = c;
        }
    }
}

uint32_t ui_mix(uint32_t under, uint32_t over, uint32_t a) {
    uint32_t out = 0xff000000u;
    for (uint32_t shift = 0; shift < 24; shift += 8) {
        uint32_t u = under >> shift & 0xffu;
        uint32_t o = over >> shift & 0xffu;
        out |= ((o * a + u * (255u - a) + 127u) / 255u) << shift;
    }
    return out;
}

void ui_tint(const ui_surface *s, ui_rect r, uint32_t c, uint32_t alpha) {
    r = clip(s, r);
    for (int32_t y = r.y; y < r.y + r.h; y++) {
        uint32_t *row = s->px + (int64_t)y * s->stride;
        for (int32_t x = r.x; x < r.x + r.w; x++) {
            row[x] = ui_mix(row[x], c, alpha);
        }
    }
}

void ui_border(const ui_surface *s, ui_rect r, int32_t t, uint32_t c) {
    ui_fill(s, (ui_rect){r.x, r.y, r.w, t}, c);
    ui_fill(s, (ui_rect){r.x, r.y + r.h - t, r.w, t}, c);
    ui_fill(s, (ui_rect){r.x, r.y, t, r.h}, c);
    ui_fill(s, (ui_rect){r.x + r.w - t, r.y, t, r.h}, c);
}

/* Windows 95: white over light gray on the lit edges, black over dark
   gray on the others; sunken swaps them. */
void ui_bevel(const ui_surface *s, ui_rect r, int32_t t, int sunken) {
    uint32_t outer_lit = sunken ? UI_SHADOW : UI_LIGHT;
    uint32_t outer_dark = sunken ? UI_LIGHT : UI_DARK;
    uint32_t inner_lit = sunken ? UI_DARK : UI_FACE;
    uint32_t inner_dark = sunken ? UI_FACE : UI_SHADOW;
    ui_fill(s, (ui_rect){r.x, r.y + r.h - t, r.w, t}, outer_dark);
    ui_fill(s, (ui_rect){r.x + r.w - t, r.y, t, r.h}, outer_dark);
    ui_fill(s, (ui_rect){r.x, r.y, r.w - t, t}, outer_lit);
    ui_fill(s, (ui_rect){r.x, r.y, t, r.h - t}, outer_lit);
    ui_rect in = {r.x + t, r.y + t, r.w - 2 * t, r.h - 2 * t};
    ui_fill(s, (ui_rect){in.x, in.y + in.h - t, in.w, t}, inner_dark);
    ui_fill(s, (ui_rect){in.x + in.w - t, in.y, t, in.h}, inner_dark);
    ui_fill(s, (ui_rect){in.x, in.y, in.w - t, t}, inner_lit);
    ui_fill(s, (ui_rect){in.x, in.y, t, in.h - t}, inner_lit);
}

int32_t ui_text_width(const char *text, int32_t scale) {
    int32_t n = 0;
    while (text[n] != '\0') {
        n++;
    }
    return n * UI_GLYPH_W * scale;
}

int32_t ui_text(const ui_surface *s, int32_t x, int32_t y, int32_t scale, const char *text,
                uint32_t c) {
    int32_t x0 = x;
    for (const char *p = text; *p != '\0'; p++) {
        int ch = (unsigned char)*p;
        if (ch < FONT_FIRST || ch > FONT_LAST) {
            ch = '?';
        }
        int g = ch - FONT_FIRST;
        int32_t gx = (g % FONT_COLUMNS) * UI_GLYPH_W;
        int32_t gy = (g / FONT_COLUMNS) * UI_GLYPH_H;
        for (int32_t row = 0; row < UI_GLYPH_H; row++) {
            for (int32_t col = 0; col < UI_GLYPH_W; col++) {
                if (font_pixels[0][(gy + row) * FONT_WIDTH + gx + col] == 0) {
                    continue;
                }
                ui_fill(s, (ui_rect){x + col * scale, y + row * scale, scale, scale}, c);
            }
        }
        x += UI_GLYPH_W * scale;
    }
    return x - x0;
}

void ui_icon(const ui_surface *s, int32_t x, int32_t y, int32_t scale, int index) {
    for (int32_t row = 0; row < UI_ICON; row++) {
        for (int32_t col = 0; col < UI_ICON; col++) {
            uint8_t v = tools_pixels[0][row * TOOLS_WIDTH + index * UI_ICON + col];
            if (v == 0) {
                continue;
            }
            ui_fill(s, (ui_rect){x + col * scale, y + row * scale, scale, scale}, tools_palette[v]);
        }
    }
}
