/* A gesso image as the C header a game includes: size, palette, durations,
   frames as index bytes and tags, all static const. Freestanding: what
   gesso2c prints and what the app exports are the same bytes. */
#ifndef GESSO_C_H
#define GESSO_C_H

#include "gesso.h"

enum {
    GESSO_C_NAME_MAX = 64,
};

/* The identifier prefix from a file name: the part after the last '/' and
   before the first '.', lowercase letters, digits and _, never starting with
   a digit; "image" when nothing is left. */
void gesso_c_name(const char *from, char dst[GESSO_C_NAME_MAX]);
/* The header for img, labeled with file (its part after the last '/').
   Returns the length it needs; writes nothing past cap. */
size_t gesso_c_header(const gesso_image *img, const char *name, const char *file, uint8_t *dst,
                      size_t cap);

#endif
