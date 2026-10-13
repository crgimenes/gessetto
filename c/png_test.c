/* Runs testdata/png: each name.png decodes and converts to name.gesso
   exactly, or fails with a message containing name.error. The expected
   files come from Go's image/png decoder. */
#include "gesso.h"
#include "gesso_filo.h"
#include "gesso_png.h"
#include "png.h"

#include <stdio.h>
#include <string.h>

enum {
    FILE_CAP = 4 * 1024 * 1024,
    TEXT_CAP = 4 * 1024 * 1024,
    PATH_CAP = 1024,
};

static uint8_t data[FILE_CAP];
static uint8_t work[FILE_CAP + PNG_WORK_RAW];
static uint32_t argb[PNG_SIDE_MAX * PNG_SIDE_MAX];
static uint8_t index_px[PNG_SIDE_MAX * PNG_SIDE_MAX];
static gesso_image img;
static uint8_t got[TEXT_CAP];
static uint8_t want[TEXT_CAP];

static long slurp(const char *path, uint8_t *dst, size_t cap) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    size_t n = fread(dst, 1, cap, f);
    (void)fclose(f);
    return n == cap ? -1 : (long)n;
}

static int run(const char *png_path) {
    char base[PATH_CAP];
    char path[PATH_CAP];
    size_t n = strlen(png_path);
    if (n < 5 || n >= sizeof base) {
        return 0;
    }
    memcpy(base, png_path, n - 4);
    base[n - 4] = '\0';
    long len = slurp(png_path, data, sizeof data);
    if (len < 0) {
        printf("FAIL %s: cannot read\n", png_path);
        return 0;
    }
    png_image p = {.argb = argb, .index = index_px};
    const char *err = NULL;
    png_status ps = png_decode(data, (size_t)len, work, sizeof work, &p);
    if (ps != PNG_OK) {
        err = png_message(ps);
    } else {
        gesso_status gs = gesso_from_png(&img, &p);
        if (gs != GESSO_OK) {
            err = gesso_message(gs);
        }
    }
    (void)snprintf(path, sizeof path, "%s.error", base);
    long e = slurp(path, want, sizeof want - 1);
    if (e >= 0) {
        while (e > 0 && (want[e - 1] == '\n' || want[e - 1] == '\r')) {
            e--;
        }
        want[e] = '\0';
        if (err == NULL || strstr(err, (const char *)want) == NULL) {
            printf("FAIL %s: want an error with \"%s\", got \"%s\"\n", png_path, want,
                   err != NULL ? err : "success");
            return 0;
        }
        return 1;
    }
    if (err != NULL) {
        printf("FAIL %s: %s\n", png_path, err);
        return 0;
    }
    size_t out = gesso_write(&img, got, sizeof got);
    (void)snprintf(path, sizeof path, "%s.gesso", base);
    long w = slurp(path, want, sizeof want);
    if (w < 0 || (size_t)w != out || memcmp(got, want, out) != 0) {
        printf("FAIL %s: differs from %s\n%.*s\n", png_path, path, (int)out, got);
        return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    int bad = 0;
    for (int i = 1; i < argc; i++) {
        if (!run(argv[i])) {
            bad++;
        }
    }
    printf("png: %d files, %d failed\n", argc - 1, bad);
    return bad == 0 && argc > 1 ? 0 : 1;
}
