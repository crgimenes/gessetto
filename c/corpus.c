/* Runs corpus cases (testdata/corpus/README.md): each directory holds
   ops.filo and want.gesso, or error.txt. With -update, writes want.gesso
   from the engine instead; check it by hand before keeping it. */
#include "gesso.h"
#include "gesso_filo.h"

#include <stdio.h>
#include <string.h>

enum {
    LOG_CAP = 64 * 1024 * 1024,
    MEM_CAP = 64 * 1024 * 1024,
    TEXT_CAP = 64 * 1024 * 1024,
    PATH_MAX_LEN = 1024,
};

static gesso_doc doc;
static gesso_image img;
static gesso_script script;
static uint8_t log_mem[LOG_CAP];
static uint8_t script_mem[MEM_CAP];
static uint8_t src[TEXT_CAP];
static uint8_t got[TEXT_CAP];
static uint8_t want[TEXT_CAP];
static uint8_t again[TEXT_CAP];

static long read_file(const char *dir, const char *name, uint8_t *dst, size_t cap) {
    char path[PATH_MAX_LEN];
    (void)snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    size_t n = fread(dst, 1, cap, f);
    (void)fclose(f);
    if (n == cap) {
        return -1;
    }
    return (long)n;
}

static int write_file(const char *dir, const char *name, const uint8_t *data, size_t len) {
    char path[PATH_MAX_LEN];
    (void)snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return 0;
    }
    size_t n = fwrite(data, 1, len, f);
    return fclose(f) == 0 && n == len;
}

static int failed(const char *dir, const char *why, const char *detail) {
    printf("FAIL %s: %s%s\n", dir, why, detail);
    return 0;
}

static const char *where(void) {
    uint32_t line = 0;
    uint32_t col = 0;
    if (filo_error_at(&script.ctx, &line, &col)) {
        static char buf[FILO_ERROR_MAX + 32];
        (void)snprintf(buf, sizeof buf, "%u:%u: %s", line, col, filo_error(&script.ctx));
        return buf;
    }
    return filo_error(&script.ctx);
}

static int expect_error(const char *dir, int rc, long n) {
    want[n] = '\0';
    while (n > 0 && (want[n - 1] == '\n' || want[n - 1] == '\r')) {
        want[--n] = '\0';
    }
    if (rc == FILO_OK) {
        return failed(dir, "expected an error containing: ", (const char *)want);
    }
    if (strstr(filo_error(&script.ctx), (const char *)want) == NULL) {
        return failed(dir, "wrong error: ", where());
    }
    return 1;
}

static int run_case(const char *dir, int update) {
    long n = read_file(dir, "ops.filo", src, sizeof src);
    if (n < 0) {
        return failed(dir, "no ops.filo", "");
    }
    gesso_init(&doc, log_mem, sizeof log_mem);
    if (gesso_script_ops(&script, &doc, script_mem, sizeof script_mem) != FILO_OK) {
        return failed(dir, "script setup: ", filo_error(&script.ctx));
    }
    int rc = gesso_script_run(&script, src, (size_t)n);
    long e = read_file(dir, "error.txt", want, sizeof want - 1);
    if (e >= 0) {
        return expect_error(dir, rc, e);
    }
    if (rc != FILO_OK) {
        return failed(dir, "", where());
    }
    gesso_drop(&doc);
    size_t len = gesso_write(&doc.img, got, sizeof got);
    if (update) {
        if (!write_file(dir, "want.gesso", got, len)) {
            return failed(dir, "cannot write", "");
        }
        return 1;
    }
    long w = read_file(dir, "want.gesso", want, sizeof want);
    if (w < 0) {
        return failed(dir, "no want.gesso or error.txt", "");
    }
    if ((size_t)w != len || memcmp(got, want, len) != 0) {
        (void)write_file(dir, "got.gesso", got, len);
        return failed(dir, "differs from want.gesso; see got.gesso", "");
    }
    if (gesso_script_file(&script, &img, script_mem, sizeof script_mem) != FILO_OK ||
        gesso_script_run(&script, want, (size_t)w) != FILO_OK) {
        return failed(dir, "want.gesso does not read back: ", where());
    }
    size_t back = gesso_write(&img, again, sizeof again);
    if (back != len || memcmp(again, want, len) != 0) {
        return failed(dir, "want.gesso does not write back the same", "");
    }
    return 1;
}

int main(int argc, char **argv) {
    int update = 0;
    int bad = 0;
    int ran = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("usage: corpus [-update] case-dir...\n"
                   "Runs each case's ops.filo against want.gesso or error.txt.\n"
                   "  -update  write want.gesso from the engine (check it by hand)\n");
            return 0;
        }
        if (strcmp(argv[i], "-update") == 0) {
            update = 1;
            continue;
        }
        ran++;
        if (!run_case(argv[i], update)) {
            bad++;
        }
    }
    printf("%d cases, %d failed\n", ran, bad);
    return bad == 0 && ran > 0 ? 0 : 1;
}
