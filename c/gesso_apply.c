/* gesso-apply: runs a gessetto script on an image and writes the result,
   for agents and scripts that draw without a window. */
#include "gesso.h"
#include "gesso_filo.h"

#include <stdio.h>
#include <string.h>

enum {
    LOG_CAP = 64 * 1024 * 1024,
    MEM_CAP = 64 * 1024 * 1024,
    TEXT_CAP = 48 * 1024 * 1024,
};

static gesso_doc doc;
static gesso_image start;
static gesso_script script;
static uint8_t log_mem[LOG_CAP];
static uint8_t mem[MEM_CAP];
static uint8_t text[TEXT_CAP];

static const char usage[] =
    "usage: gesso-apply [-in image.gesso] script.filo\n"
    "\n"
    "Runs the script's edits (new, pixel, line, rect, fill, select, frame-add,\n"
    "tag, undo...; see testdata/corpus/README.md) and writes the image as a\n"
    ".gesso file on stdout. Reads the script from stdin with -.\n"
    "\n"
    "  -in image.gesso  start from this image instead of (new w h)\n"
    "\n"
    "example: gesso-apply -in heroi.gesso blink.filo > heroi-blink.gesso\n";

static long slurp(const char *path, uint8_t *dst, size_t cap) {
    int from_stdin = strcmp(path, "-") == 0;
    FILE *f = from_stdin ? stdin : fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    size_t n = fread(dst, 1, cap, f);
    if (!from_stdin) {
        (void)fclose(f);
    }
    return n == cap ? -1 : (long)n;
}

static int failed(const char *label) {
    uint32_t line = 0;
    uint32_t col = 0;
    if (filo_error_at(&script.ctx, &line, &col)) {
        fprintf(stderr, "gesso-apply: %s:%u:%u: %s\n", label, line, col, filo_error(&script.ctx));
        return 1;
    }
    fprintf(stderr, "gesso-apply: %s: %s\n", label, filo_error(&script.ctx));
    return 1;
}

int main(int argc, char **argv) {
    const char *in = NULL;
    const char *file = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            fputs(usage, stdout);
            return 0;
        }
        if (strcmp(argv[i], "-in") == 0 && i + 1 < argc) {
            in = argv[++i];
            continue;
        }
        if (file != NULL || (argv[i][0] == '-' && argv[i][1] != '\0')) {
            fputs(usage, stderr);
            return 2;
        }
        file = argv[i];
    }
    if (file == NULL) {
        file = "-";
    }
    gesso_init(&doc, log_mem, sizeof log_mem);
    if (in != NULL) {
        long n = slurp(in, text, sizeof text);
        if (n < 0) {
            fprintf(stderr, "gesso-apply: cannot read %s\n", in);
            return 1;
        }
        if (gesso_script_file(&script, &start, mem, sizeof mem) != FILO_OK ||
            gesso_script_run(&script, text, (size_t)n) != FILO_OK) {
            return failed(in);
        }
        gesso_adopt(&doc, &start);
    }
    long n = slurp(file, text, sizeof text);
    const char *label = strcmp(file, "-") == 0 ? "stdin" : file;
    if (n < 0) {
        fprintf(stderr, "gesso-apply: cannot read %s\n", label);
        return 1;
    }
    if (gesso_script_ops(&script, &doc, mem, sizeof mem) != FILO_OK ||
        gesso_script_run(&script, text, (size_t)n) != FILO_OK) {
        return failed(label);
    }
    if (doc.img.nframes == 0) {
        fprintf(stderr, "gesso-apply: %s made no image: start it with (new w h)\n", label);
        return 1;
    }
    gesso_drop(&doc);
    size_t len = gesso_write(&doc.img, text, sizeof text);
    if (len > sizeof text || fwrite(text, 1, len, stdout) != len || fflush(stdout) != 0) {
        fprintf(stderr, "gesso-apply: cannot write the image\n");
        return 1;
    }
    return 0;
}
