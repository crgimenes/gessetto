/* gesso2c: a .gesso image as a C header a game includes. */
#include "gesso.h"
#include "gesso_filo.h"

#include <stdio.h>
#include <string.h>

enum {
    MEM_CAP = 64 * 1024 * 1024,
    SRC_CAP = 64 * 1024 * 1024,
    NAME_MAX_LEN = 64,
    PER_LINE = 8,
};

static gesso_image img;
static gesso_script script;
static uint8_t mem[MEM_CAP];
static uint8_t src[SRC_CAP];

static const char usage[] =
    "usage: gesso2c [-n name] [file.gesso | -]\n"
    "\n"
    "Writes the image as a C header on stdout: its size, palette (0xAARRGGBB,\n"
    "index 0 transparent), frames as index bytes, durations and tags.\n"
    "Reads stdin without a file or with -.\n"
    "\n"
    "  -n name  identifier prefix (default: the file name, or \"image\")\n"
    "\n"
    "example: gesso2c heroi.gesso > heroi.h\n";

/* Lowercase letters, digits and _, not starting with a digit. */
static void make_name(const char *from, char *dst) {
    const char *base = strrchr(from, '/');
    base = base != NULL ? base + 1 : from;
    size_t n = 0;
    if (base[0] >= '0' && base[0] <= '9') {
        dst[n++] = '_';
    }
    for (size_t i = 0; base[i] != '\0' && base[i] != '.' && n < NAME_MAX_LEN - 1; i++) {
        char c = base[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        int ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        dst[n++] = ok ? c : '_';
    }
    if (n == 0) {
        memcpy(dst, "image", 6);
        return;
    }
    dst[n] = '\0';
}

static void upper(const char *name, char *dst) {
    size_t i = 0;
    for (; name[i] != '\0'; i++) {
        char c = name[i];
        dst[i] = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
    }
    dst[i] = '\0';
}

static void emit(const char *file, const char *name) {
    char up[NAME_MAX_LEN];
    upper(name, up);
    const char *base = strrchr(file, '/');
    file = base != NULL ? base + 1 : file;
    printf("/* %s, by gesso2c. Index 0 is transparent; colors are 0xAARRGGBB. */\n", file);
    printf("#ifndef %s_GESSO_H\n#define %s_GESSO_H\n\n#include <stdint.h>\n\n", up, up);
    printf("enum {\n    %s_WIDTH = %d,\n    %s_HEIGHT = %d,\n    %s_COLORS = %u,\n"
           "    %s_FRAMES = %u,\n    %s_TAGS = %u,\n};\n\n",
           up, img.width, up, img.height, up, img.ncolors, up, img.nframes, up, img.ntags);

    printf("static const uint32_t %s_palette[%s_COLORS] = {", name, up);
    for (uint32_t i = 0; i < img.ncolors; i++) {
        printf(i % PER_LINE == 0 ? "\n    " : " ");
        printf("0x%08x,", img.palette[i]);
    }
    printf("\n};\n\n");

    printf("static const uint16_t %s_duration[%s_FRAMES] = {", name, up);
    for (uint32_t i = 0; i < img.nframes; i++) {
        printf(i % PER_LINE == 0 ? "\n    " : " ");
        printf("%u,", img.duration[i]);
    }
    printf("\n};\n\n");

    printf("static const uint8_t %s_pixels[%s_FRAMES][%s_WIDTH * %s_HEIGHT] = {\n", name, up, up,
           up);
    for (uint32_t f = 0; f < img.nframes; f++) {
        const uint8_t *px = gesso_frame_pixels(&img, f);
        printf("    {\n");
        for (int32_t y = 0; y < img.height; y++) {
            printf("       ");
            for (int32_t x = 0; x < img.width; x++) {
                printf(" 0x%02x,", px[(size_t)y * (size_t)img.width + (size_t)x]);
            }
            printf("\n");
        }
        printf("    },\n");
    }
    printf("};\n");

    if (img.ntags > 0) {
        printf("\nstatic const struct {\n    const char *name;\n    uint16_t first;\n"
               "    uint16_t last;\n} %s_tags[%s_TAGS] = {\n",
               name, up);
        for (uint32_t i = 0; i < img.ntags; i++) {
            printf("    {\"%s\", %u, %u},\n", img.tags[i].name, img.tags[i].first,
                   img.tags[i].last);
        }
        printf("};\n");
    }
    printf("\n#endif\n");
}

int main(int argc, char **argv) {
    const char *file = NULL;
    const char *given = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            fputs(usage, stdout);
            return 0;
        }
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            given = argv[++i];
            continue;
        }
        if (file != NULL || (argv[i][0] == '-' && argv[i][1] != '\0')) {
            fputs(usage, stderr);
            return 2;
        }
        file = argv[i];
    }
    int from_stdin = file == NULL || strcmp(file, "-") == 0;
    FILE *in = from_stdin ? stdin : fopen(file, "rb");
    const char *label = from_stdin ? "stdin" : file;
    if (in == NULL) {
        fprintf(stderr, "gesso2c: cannot open %s\n", file);
        return 1;
    }
    size_t n = fread(src, 1, sizeof src, in);
    if (!from_stdin) {
        (void)fclose(in);
    }
    if (n == sizeof src) {
        fprintf(stderr, "gesso2c: %s: file too large\n", label);
        return 1;
    }

    char name[NAME_MAX_LEN];
    const char *base = from_stdin ? "image" : file;
    make_name(given != NULL ? given : base, name);

    if (gesso_script_file(&script, &img, mem, sizeof mem) != FILO_OK ||
        gesso_script_run(&script, src, n) != FILO_OK) {
        uint32_t line = 0;
        uint32_t col = 0;
        if (filo_error_at(&script.ctx, &line, &col)) {
            fprintf(stderr, "gesso2c: %s:%u:%u: %s\n", label, line, col, filo_error(&script.ctx));
            return 1;
        }
        fprintf(stderr, "gesso2c: %s: %s\n", label, filo_error(&script.ctx));
        return 1;
    }
    emit(label, name);
    return fflush(stdout) == 0 ? 0 : 1;
}
