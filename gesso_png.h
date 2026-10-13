/* A decoded PNG as a one-frame gesso image. Alpha under 128 is index 0. An
   indexed PNG keeps its palette's order (the artist's ramps), its
   transparent entries dropped; any other kind gets its colors in the order
   they first appear, row by row. More than 255 colors is an error. */
#ifndef GESSO_PNG_H
#define GESSO_PNG_H

#include "gesso.h"
#include "png.h"

gesso_status gesso_from_png(gesso_image *img, const png_image *p);

/* The palette index for a color from outside: 0 when its alpha is under
   128, the exact entry when there is one, else the nearest by RGB. */
uint8_t gesso_nearest(const gesso_image *img, uint32_t argb);

#endif
