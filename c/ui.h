/* Drawing for the editor's own interface: rectangles, the classic raised and
   sunken bevels, the bitmap font of assets/font.gesso and the icons of
   assets/tools.gesso, at an integer scale. Pixels are 0xAARRGGBB. */
#ifndef UI_H
#define UI_H

#include <stdint.h>

#define UI_FACE 0xffc0c0c0u
#define UI_LIGHT 0xffffffffu
#define UI_SHADOW 0xff808080u
#define UI_DARK 0xff000000u
#define UI_TEXT 0xff000000u
#define UI_DIM 0xff606060u
#define UI_ACCENT 0xff000080u
#define UI_WORKSPACE 0xff808080u

enum {
    UI_GLYPH_W = 6, /* cell, spacing included */
    UI_GLYPH_H = 10,
    UI_ICON = 16,
};

typedef struct {
    uint32_t *px;
    int32_t w;
    int32_t h;
    int32_t stride;
} ui_surface;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} ui_rect;

int ui_inside(ui_rect r, int32_t x, int32_t y);
void ui_fill(const ui_surface *s, ui_rect r, uint32_t c);
/* over on under at alpha/255. */
uint32_t ui_mix(uint32_t under, uint32_t over, uint32_t alpha);
/* c over the pixels at alpha/255. */
void ui_tint(const ui_surface *s, ui_rect r, uint32_t c, uint32_t alpha);
void ui_border(const ui_surface *s, ui_rect r, int32_t t, uint32_t c);
/* The classic 3D edge, t pixels per line: raised for a button, sunken for
   a pressed one or a well. */
void ui_bevel(const ui_surface *s, ui_rect r, int32_t t, int sunken);
/* Returns the width drawn. */
int32_t ui_text(const ui_surface *s, int32_t x, int32_t y, int32_t scale, const char *text,
                uint32_t c);
int32_t ui_text_width(const char *text, int32_t scale);
void ui_icon(const ui_surface *s, int32_t x, int32_t y, int32_t scale, int index);

#endif
