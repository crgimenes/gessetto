/* PNG reading, freestanding: inflate (RFC 1951), zlib framing, chunk CRCs,
   every color type and bit depth, up to GESSO_SIDE_MAX on a side. Adam7
   interlacing is refused. The caller owns all memory. */
#ifndef PNG_H
#define PNG_H

#include <stddef.h>
#include <stdint.h>

enum {
    PNG_SIDE_MAX = 256,
    /* Scratch for the concatenated IDAT data and the filtered scanlines of
       the largest image (16-bit RGBA): the file's length plus this. */
    PNG_WORK_RAW = PNG_SIDE_MAX * (1 + PNG_SIDE_MAX * 8),
};

typedef enum {
    PNG_OK = 0,
    PNG_ERR_FORMAT,
    PNG_ERR_CRC,
    PNG_ERR_SIZE,
    PNG_ERR_UNSUPPORTED,
    PNG_ERR_DATA,
    PNG_ERR_MEMORY,
} png_status;

typedef struct {
    int32_t width;
    int32_t height;
    /* Every pixel as 0xAARRGGBB, width*height of them. */
    uint32_t *argb;
    /* An indexed PNG also gives its indices (width*height) and palette, alpha
       from tRNS included; ncolors is 0 for any other kind. */
    uint8_t *index;
    uint32_t palette[256];
    uint32_t ncolors;
} png_image;

const char *png_message(png_status s);
/* out->argb and out->index must hold PNG_SIDE_MAX * PNG_SIDE_MAX each. */
png_status png_decode(const uint8_t *data, size_t len, uint8_t *work, size_t work_cap,
                      png_image *out);

#endif
