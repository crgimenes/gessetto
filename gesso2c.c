/* gesso2c: a .gesso image as a C header a game includes. */
#include "gesso.h"
#include "gesso_c.h"
#include "gesso_filo.h"

#include <stdio.h>
#include <string.h>

enum {
    MEM_CAP = 64 * 1024 * 1024,
    SRC_CAP = 64 * 1024 * 1024,
    OUT_CAP = 64 * 1024 * 1024,
};

static gesso_image img;
static gesso_script script;
static uint8_t mem[MEM_CAP];
static uint8_t src[SRC_CAP];
static uint8_t text[OUT_CAP];

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

    char name[GESSO_C_NAME_MAX];
    const char *base = from_stdin ? "image" : file;
    gesso_c_name(given != NULL ? given : base, name);

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
    size_t len = gesso_c_header(&img, name, label, text, sizeof text);
    if (len > sizeof text) {
        fprintf(stderr, "gesso2c: %s: header too large\n", label);
        return 1;
    }
    return fwrite(text, 1, len, stdout) == len && fflush(stdout) == 0 ? 0 : 1;
}
