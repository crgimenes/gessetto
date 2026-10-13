#include "gesso_c.h"

enum {
    PER_LINE = 8,
};

typedef struct {
    uint8_t *dst;
    size_t cap;
    size_t n;
} out;

static void put(out *o, const char *s) {
    for (; *s != '\0'; s++) {
        if (o->n < o->cap) {
            o->dst[o->n] = (uint8_t)*s;
        }
        o->n++;
    }
}

static void put_uint(out *o, uint64_t v) {
    char buf[24];
    size_t i = sizeof buf;
    buf[--i] = '\0';
    do {
        buf[--i] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v > 0);
    put(o, buf + i);
}

static void put_hex(out *o, uint32_t v, int digits) {
    static const char hex[] = "0123456789abcdef";
    char buf[11] = "0x";
    for (int i = 0; i < digits; i++) {
        buf[2 + i] = hex[(v >> (4u * (uint32_t)(digits - 1 - i))) & 0xfu];
    }
    buf[2 + digits] = '\0';
    put(o, buf);
}

static const char *base_name(const char *path) {
    const char *base = path;
    for (const char *s = path; *s != '\0'; s++) {
        if (*s == '/') {
            base = s + 1;
        }
    }
    return base;
}

void gesso_c_name(const char *from, char dst[GESSO_C_NAME_MAX]) {
    const char *base = base_name(from);
    size_t n = 0;
    if (base[0] >= '0' && base[0] <= '9') {
        dst[n++] = '_';
    }
    for (size_t i = 0; base[i] != '\0' && base[i] != '.' && n < GESSO_C_NAME_MAX - 1; i++) {
        char c = base[i];
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        int ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        dst[n++] = ok ? c : '_';
    }
    if (n == 0) {
        const char image[] = "image";
        for (size_t i = 0; i < sizeof image; i++) {
            dst[i] = image[i];
        }
        return;
    }
    dst[n] = '\0';
}

/* One enum line: "    NAME_suffix = v,". */
static void put_enum(out *o, const char *up, const char *suffix, uint64_t v) {
    put(o, "    ");
    put(o, up);
    put(o, suffix);
    put(o, " = ");
    put_uint(o, v);
    put(o, ",\n");
}

size_t gesso_c_header(const gesso_image *img, const char *name, const char *file, uint8_t *dst,
                      size_t cap) {
    out o = {dst, cap, 0};
    char up[GESSO_C_NAME_MAX];
    size_t i = 0;
    for (; i < GESSO_C_NAME_MAX - 1 && name[i] != '\0'; i++) {
        char c = name[i];
        up[i] = c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
    }
    up[i] = '\0';

    put(&o, "/* ");
    put(&o, base_name(file));
    put(&o, ", by gesso2c. Index 0 is transparent; colors are 0xAARRGGBB. */\n#ifndef ");
    put(&o, up);
    put(&o, "_GESSO_H\n#define ");
    put(&o, up);
    put(&o, "_GESSO_H\n\n#include <stdint.h>\n\nenum {\n");
    put_enum(&o, up, "_WIDTH", (uint64_t)img->width);
    put_enum(&o, up, "_HEIGHT", (uint64_t)img->height);
    put_enum(&o, up, "_COLORS", img->ncolors);
    put_enum(&o, up, "_FRAMES", img->nframes);
    put_enum(&o, up, "_TAGS", img->ntags);
    put(&o, "};\n\n");

    put(&o, "static const uint32_t ");
    put(&o, name);
    put(&o, "_palette[");
    put(&o, up);
    put(&o, "_COLORS] = {");
    for (uint32_t c = 0; c < img->ncolors; c++) {
        put(&o, c % PER_LINE == 0 ? "\n    " : " ");
        put_hex(&o, img->palette[c], 8);
        put(&o, ",");
    }
    put(&o, "\n};\n\n");

    put(&o, "static const uint16_t ");
    put(&o, name);
    put(&o, "_duration[");
    put(&o, up);
    put(&o, "_FRAMES] = {");
    for (uint32_t f = 0; f < img->nframes; f++) {
        put(&o, f % PER_LINE == 0 ? "\n    " : " ");
        put_uint(&o, img->duration[f]);
        put(&o, ",");
    }
    put(&o, "\n};\n\n");

    put(&o, "static const uint8_t ");
    put(&o, name);
    put(&o, "_pixels[");
    put(&o, up);
    put(&o, "_FRAMES][");
    put(&o, up);
    put(&o, "_WIDTH * ");
    put(&o, up);
    put(&o, "_HEIGHT] = {\n");
    size_t fs = (size_t)img->width * (size_t)img->height;
    for (uint32_t f = 0; f < img->nframes; f++) {
        const uint8_t *px = img->pixels + (size_t)f * fs;
        put(&o, "    {\n");
        for (int32_t y = 0; y < img->height; y++) {
            put(&o, "       ");
            for (int32_t x = 0; x < img->width; x++) {
                put(&o, " ");
                put_hex(&o, px[(size_t)y * (size_t)img->width + (size_t)x], 2);
                put(&o, ",");
            }
            put(&o, "\n");
        }
        put(&o, "    },\n");
    }
    put(&o, "};\n");

    if (img->ntags > 0) {
        put(&o, "\nstatic const struct {\n    const char *name;\n    uint16_t first;\n"
                "    uint16_t last;\n    uint8_t direction; /* 0 forward, 1 reverse, 2 ping-pong "
                "*/\n} ");
        put(&o, name);
        put(&o, "_tags[");
        put(&o, up);
        put(&o, "_TAGS] = {\n");
        for (uint32_t t = 0; t < img->ntags; t++) {
            put(&o, "    {\"");
            put(&o, img->tags[t].name);
            put(&o, "\", ");
            put_uint(&o, img->tags[t].first);
            put(&o, ", ");
            put_uint(&o, img->tags[t].last);
            put(&o, ", ");
            put_uint(&o, img->tags[t].direction);
            put(&o, "},\n");
        }
        put(&o, "};\n");
    }
    put(&o, "\n#endif\n");
    return o.n;
}
