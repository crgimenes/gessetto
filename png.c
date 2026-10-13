#include "png.h"

#include <string.h>

/* ---- inflate, after Mark Adler's puff.c: canonical Huffman decoded a bit
   at a time, which is slow and short and easy to check ---- */

enum {
    MAXBITS = 15,
    MAXLCODES = 286,
    MAXDCODES = 30,
    MAXCODES = MAXLCODES + MAXDCODES,
    FIXLCODES = 288,
    CODE_LENGTHS = 19,
};

typedef struct {
    const uint8_t *in;
    size_t inlen;
    size_t inpos;
    uint32_t bitbuf;
    uint32_t bitcnt;
    uint8_t *out;
    size_t outcap;
    size_t outpos;
    int err;
} inflater;

typedef struct {
    uint16_t count[MAXBITS + 1];
    uint16_t symbol[FIXLCODES];
} huffman;

static const uint16_t lbase[29] = {
    3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23,  27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
};
static const uint16_t lext[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
};
static const uint16_t dbase[30] = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
};
static const uint16_t dext[30] = {
    0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
};

static uint32_t bits(inflater *s, uint32_t need) {
    uint32_t val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->inpos == s->inlen) {
            s->err = 1;
            return 0;
        }
        val |= (uint32_t)s->in[s->inpos++] << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return val & ((1u << need) - 1u);
}

static int stored(inflater *s) {
    s->bitbuf = 0;
    s->bitcnt = 0;
    if (s->inlen - s->inpos < 4) {
        return -1;
    }
    uint32_t len = (uint32_t)s->in[s->inpos] | (uint32_t)s->in[s->inpos + 1] << 8u;
    uint32_t nlen = (uint32_t)s->in[s->inpos + 2] | (uint32_t)s->in[s->inpos + 3] << 8u;
    s->inpos += 4;
    if (len != (~nlen & 0xffffu) || s->inlen - s->inpos < len || s->outcap - s->outpos < len) {
        return -1;
    }
    memcpy(s->out + s->outpos, s->in + s->inpos, len);
    s->inpos += len;
    s->outpos += len;
    return 0;
}

static int decode(inflater *s, const huffman *h) {
    int code = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len <= MAXBITS; len++) {
        code += (int)bits(s, 1);
        if (s->err) {
            return -1;
        }
        int count = h->count[len];
        if (code - count < first) {
            return h->symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first *= 2;
        code *= 2;
    }
    return -1;
}

/* Returns 0 for a complete code, more than 0 for an incomplete one, less
   than 0 for an over-subscribed one. */
static int construct(huffman *h, const uint16_t *length, int n) {
    for (int len = 0; len <= MAXBITS; len++) {
        h->count[len] = 0;
    }
    for (int sym = 0; sym < n; sym++) {
        h->count[length[sym]]++;
    }
    if (h->count[0] == n) {
        return 0;
    }
    int left = 1;
    for (int len = 1; len <= MAXBITS; len++) {
        left *= 2;
        left -= h->count[len];
        if (left < 0) {
            return left;
        }
    }
    uint16_t offs[MAXBITS + 1];
    offs[1] = 0;
    for (int len = 1; len < MAXBITS; len++) {
        offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
    }
    for (int sym = 0; sym < n; sym++) {
        if (length[sym] != 0) {
            h->symbol[offs[length[sym]]++] = (uint16_t)sym;
        }
    }
    return left;
}

static int codes(inflater *s, const huffman *lencode, const huffman *distcode) {
    for (;;) {
        int symbol = decode(s, lencode);
        if (symbol < 0) {
            return -1;
        }
        if (symbol < 256) {
            if (s->outpos == s->outcap) {
                return -1;
            }
            s->out[s->outpos++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == 256) {
            return 0;
        }
        symbol -= 257;
        if (symbol >= 29) {
            return -1;
        }
        size_t len = lbase[symbol] + bits(s, lext[symbol]);
        int dsym = decode(s, distcode);
        if (dsym < 0 || dsym >= 30) {
            return -1;
        }
        size_t dist = dbase[dsym] + bits(s, dext[dsym]);
        if (s->err || dist > s->outpos || s->outcap - s->outpos < len) {
            return -1;
        }
        for (size_t i = 0; i < len; i++) {
            s->out[s->outpos] = s->out[s->outpos - dist];
            s->outpos++;
        }
    }
}

static int fixed(inflater *s) {
    huffman lencode;
    huffman distcode;
    uint16_t lengths[FIXLCODES];
    for (int sym = 0; sym < FIXLCODES; sym++) {
        uint16_t len = 8;
        if (sym >= 144 && sym < 256) {
            len = 9;
        } else if (sym >= 256 && sym < 280) {
            len = 7;
        }
        lengths[sym] = len;
    }
    (void)construct(&lencode, lengths, FIXLCODES);
    for (int sym = 0; sym < MAXDCODES; sym++) {
        lengths[sym] = 5;
    }
    (void)construct(&distcode, lengths, MAXDCODES);
    return codes(s, &lencode, &distcode);
}

static int dynamic(inflater *s) {
    static const uint8_t order[CODE_LENGTHS] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
    };
    int nlen = (int)bits(s, 5) + 257;
    int ndist = (int)bits(s, 5) + 1;
    int ncode = (int)bits(s, 4) + 4;
    if (s->err || nlen > MAXLCODES || ndist > MAXDCODES) {
        return -1;
    }
    uint16_t lengths[MAXCODES];
    int index = 0;
    for (; index < ncode; index++) {
        lengths[order[index]] = (uint16_t)bits(s, 3);
    }
    for (; index < CODE_LENGTHS; index++) {
        lengths[order[index]] = 0;
    }
    huffman lencode;
    huffman distcode;
    if (s->err || construct(&lencode, lengths, CODE_LENGTHS) != 0) {
        return -1;
    }
    index = 0;
    while (index < nlen + ndist) {
        int symbol = decode(s, &lencode);
        if (symbol < 0) {
            return -1;
        }
        if (symbol < 16) {
            lengths[index++] = (uint16_t)symbol;
            continue;
        }
        uint16_t len = 0;
        int repeat = 0;
        if (symbol == 16) {
            if (index == 0) {
                return -1;
            }
            len = lengths[index - 1];
            repeat = 3 + (int)bits(s, 2);
        } else if (symbol == 17) {
            repeat = 3 + (int)bits(s, 3);
        } else {
            repeat = 11 + (int)bits(s, 7);
        }
        if (s->err || index + repeat > nlen + ndist) {
            return -1;
        }
        while (repeat-- > 0) {
            lengths[index++] = len;
        }
    }
    if (lengths[256] == 0) {
        return -1;
    }
    /* An incomplete code is only allowed as a single code of length 1. */
    int left = construct(&lencode, lengths, nlen);
    if (left < 0 || (left > 0 && nlen - lencode.count[0] != 1)) {
        return -1;
    }
    left = construct(&distcode, lengths + nlen, ndist);
    if (left < 0 || (left > 0 && ndist - distcode.count[0] != 1)) {
        return -1;
    }
    return codes(s, &lencode, &distcode);
}

static int inflate(inflater *s) {
    uint32_t last = 0;
    do {
        last = bits(s, 1);
        uint32_t type = bits(s, 2);
        if (s->err) {
            return -1;
        }
        int rc = -1;
        if (type == 0) {
            rc = stored(s);
        } else if (type == 1) {
            rc = fixed(s);
        } else if (type == 2) {
            rc = dynamic(s);
        }
        if (rc != 0) {
            return -1;
        }
    } while (last == 0);
    return 0;
}

static uint32_t adler32(const uint8_t *p, size_t n) {
    uint32_t a = 1;
    uint32_t b = 0;
    for (size_t i = 0; i < n; i++) {
        a = (a + p[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return b << 16u | a;
}

static uint32_t be32(const uint8_t *p) {
    return (uint32_t)p[0] << 24u | (uint32_t)p[1] << 16u | (uint32_t)p[2] << 8u | p[3];
}

/* zlib framing (RFC 1950) around the deflate data: no preset dictionary,
   and the Adler-32 of what came out must match. */
static png_status unzlib(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen) {
    if (inlen < 6 || (in[0] & 0x0fu) != 8 || ((uint32_t)in[0] << 8u | in[1]) % 31u != 0 ||
        (in[1] & 0x20u) != 0) {
        return PNG_ERR_DATA;
    }
    inflater s = {in + 2, inlen - 6, 0, 0, 0, out, outlen, 0, 0};
    if (inflate(&s) != 0 || s.outpos != outlen) {
        return PNG_ERR_DATA;
    }
    if (be32(in + inlen - 4) != adler32(out, outlen)) {
        return PNG_ERR_DATA;
    }
    return PNG_OK;
}

/* ---- PNG ---- */

enum {
    CT_GRAY = 0,
    CT_RGB = 2,
    CT_INDEXED = 3,
    CT_GRAY_ALPHA = 4,
    CT_RGBA = 6,
};

static const char *const messages[] = {
    [PNG_OK] = "ok",
    [PNG_ERR_FORMAT] = "not a PNG file",
    [PNG_ERR_CRC] = "damaged PNG (bad CRC)",
    [PNG_ERR_SIZE] = "PNG larger than 256 x 256",
    [PNG_ERR_UNSUPPORTED] = "unsupported PNG (interlaced or unknown kind)",
    [PNG_ERR_DATA] = "damaged PNG data",
    [PNG_ERR_MEMORY] = "PNG too large for the work memory",
};

const char *png_message(png_status s) {
    if ((size_t)s >= sizeof messages / sizeof messages[0]) {
        return "unknown error";
    }
    return messages[s];
}

/* Bitwise, no table: PNGs here are small. */
static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) {
            c = (c & 1u) != 0 ? 0xedb88320u ^ (c >> 1u) : c >> 1u;
        }
    }
    return c ^ 0xffffffffu;
}

static uint32_t channels(uint8_t ct) {
    switch (ct) {
    case CT_RGB:
        return 3;
    case CT_GRAY_ALPHA:
        return 2;
    case CT_RGBA:
        return 4;
    default:
        return 1;
    }
}

static int depth_ok(uint8_t ct, uint8_t depth) {
    switch (ct) {
    case CT_GRAY:
        return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
    case CT_INDEXED:
        return depth == 1 || depth == 2 || depth == 4 || depth == 8;
    case CT_RGB:
    case CT_GRAY_ALPHA:
    case CT_RGBA:
        return depth == 8 || depth == 16;
    default:
        return 0;
    }
}

static uint8_t paeth(uint8_t a, uint8_t b, uint8_t c) {
    int p = (int)a + (int)b - (int)c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) {
        return a;
    }
    return pb <= pc ? b : c;
}

static png_status unfilter(uint8_t *raw, uint32_t h, size_t rowbytes, size_t bpp) {
    const uint8_t *prior = NULL;
    for (uint32_t y = 0; y < h; y++) {
        uint8_t *row = raw + (size_t)y * (rowbytes + 1);
        uint8_t type = row[0];
        uint8_t *cur = row + 1;
        for (size_t i = 0; i < rowbytes; i++) {
            uint8_t a = i >= bpp ? cur[i - bpp] : 0;
            uint8_t b = prior != NULL ? prior[i] : 0;
            uint8_t c = prior != NULL && i >= bpp ? prior[i - bpp] : 0;
            switch (type) {
            case 0:
                break;
            case 1:
                cur[i] = (uint8_t)(cur[i] + a);
                break;
            case 2:
                cur[i] = (uint8_t)(cur[i] + b);
                break;
            case 3:
                cur[i] = (uint8_t)(cur[i] + (a + b) / 2);
                break;
            case 4:
                cur[i] = (uint8_t)(cur[i] + paeth(a, b, c));
                break;
            default:
                return PNG_ERR_DATA;
            }
        }
        prior = cur;
    }
    return PNG_OK;
}

/* Sample x of a row, of depth bits (packed high bits first, or big-endian
   16-bit). */
static uint32_t sample(const uint8_t *row, uint32_t x, uint8_t depth) {
    switch (depth) {
    case 16:
        return (uint32_t)row[2 * (size_t)x] << 8u | row[2 * (size_t)x + 1];
    case 8:
        return row[x];
    default: {
        uint32_t per = 8u / depth;
        uint32_t shift = 8u - depth * (x % per + 1u);
        return (uint32_t)(row[x / per] >> shift) & ((1u << depth) - 1u);
    }
    }
}

static uint8_t to8(uint32_t v, uint8_t depth) {
    switch (depth) {
    case 16:
        return (uint8_t)(v >> 8u);
    case 8:
        return (uint8_t)v;
    default:
        return (uint8_t)(v * 255u / ((1u << depth) - 1u));
    }
}

typedef struct {
    uint32_t width;
    uint32_t height;
    uint8_t depth;
    uint8_t ct;
    int has_key;
    uint32_t key[3]; /* tRNS of gray or RGB, in the image's depth */
} header;

static png_status pixels(const header *hd, const uint8_t *raw, size_t rowbytes, png_image *out) {
    uint32_t ch = channels(hd->ct);
    for (uint32_t y = 0; y < hd->height; y++) {
        const uint8_t *row = raw + (size_t)y * (rowbytes + 1) + 1;
        for (uint32_t x = 0; x < hd->width; x++) {
            size_t at = (size_t)y * hd->width + x;
            uint32_t s[4] = {0, 0, 0, 0};
            for (uint32_t k = 0; k < ch; k++) {
                s[k] = sample(row, x * ch + k, hd->depth);
            }
            uint32_t r = 0;
            uint32_t g = 0;
            uint32_t b = 0;
            uint32_t a = 255;
            switch (hd->ct) {
            case CT_INDEXED:
                if (s[0] >= out->ncolors) {
                    return PNG_ERR_DATA;
                }
                out->index[at] = (uint8_t)s[0];
                out->argb[at] = out->palette[s[0]];
                continue;
            case CT_GRAY:
                r = g = b = to8(s[0], hd->depth);
                a = hd->has_key && s[0] == hd->key[0] ? 0 : 255;
                break;
            case CT_GRAY_ALPHA:
                r = g = b = to8(s[0], hd->depth);
                a = to8(s[1], hd->depth);
                break;
            case CT_RGB:
                r = to8(s[0], hd->depth);
                g = to8(s[1], hd->depth);
                b = to8(s[2], hd->depth);
                a = hd->has_key && s[0] == hd->key[0] && s[1] == hd->key[1] && s[2] == hd->key[2]
                        ? 0
                        : 255;
                break;
            default:
                r = to8(s[0], hd->depth);
                g = to8(s[1], hd->depth);
                b = to8(s[2], hd->depth);
                a = to8(s[3], hd->depth);
                break;
            }
            out->argb[at] = a << 24u | r << 16u | g << 8u | b;
        }
    }
    return PNG_OK;
}

static png_status chunk_ihdr(header *hd, const uint8_t *d, uint32_t len) {
    if (len != 13) {
        return PNG_ERR_FORMAT;
    }
    hd->width = be32(d);
    hd->height = be32(d + 4);
    hd->depth = d[8];
    hd->ct = d[9];
    if (hd->width == 0 || hd->height == 0 || !depth_ok(hd->ct, hd->depth) || d[10] != 0 ||
        d[11] != 0 || d[12] > 1) {
        return PNG_ERR_FORMAT;
    }
    if (d[12] == 1) {
        return PNG_ERR_UNSUPPORTED;
    }
    if (hd->width > PNG_SIDE_MAX || hd->height > PNG_SIDE_MAX) {
        return PNG_ERR_SIZE;
    }
    return PNG_OK;
}

static png_status chunk_trns(header *hd, png_image *out, const uint8_t *d, uint32_t len) {
    if (hd->ct == CT_INDEXED) {
        if (len > out->ncolors) {
            return PNG_ERR_FORMAT;
        }
        for (uint32_t i = 0; i < len; i++) {
            out->palette[i] = (out->palette[i] & 0x00ffffffu) | (uint32_t)d[i] << 24u;
        }
        return PNG_OK;
    }
    uint32_t n = 0;
    if (hd->ct == CT_GRAY) {
        n = 1;
    } else if (hd->ct == CT_RGB) {
        n = 3;
    }
    if (n == 0 || len != 2 * n) {
        return PNG_ERR_FORMAT;
    }
    for (uint32_t k = 0; k < n; k++) {
        hd->key[k] = (uint32_t)d[2 * (size_t)k] << 8u | d[2 * (size_t)k + 1];
    }
    hd->has_key = 1;
    return PNG_OK;
}

png_status png_decode(const uint8_t *data, size_t len, uint8_t *work, size_t work_cap,
                      png_image *out) {
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (len < 8 || memcmp(data, signature, 8) != 0) {
        return PNG_ERR_FORMAT;
    }
    header hd = {0};
    out->ncolors = 0;
    size_t idat = 0;
    size_t pos = 8;
    int seen_ihdr = 0;
    for (;;) {
        if (len - pos < 12) {
            return PNG_ERR_FORMAT;
        }
        uint32_t clen = be32(data + pos);
        const uint8_t *type = data + pos + 4;
        const uint8_t *d = type + 4;
        if (clen > len - pos - 12) {
            return PNG_ERR_FORMAT;
        }
        if (be32(d + clen) != crc32(type, (size_t)clen + 4)) {
            return PNG_ERR_CRC;
        }
        pos += (size_t)clen + 12;
        png_status st = PNG_OK;
        if (memcmp(type, "IHDR", 4) == 0) {
            st = chunk_ihdr(&hd, d, clen);
            seen_ihdr = 1;
        } else if (!seen_ihdr) {
            return PNG_ERR_FORMAT;
        } else if (memcmp(type, "PLTE", 4) == 0) {
            if (clen % 3 != 0 || clen == 0 || clen > 768) {
                return PNG_ERR_FORMAT;
            }
            out->ncolors = clen / 3;
            for (uint32_t i = 0; i < out->ncolors; i++) {
                out->palette[i] = 0xff000000u | (uint32_t)d[3 * (size_t)i] << 16u |
                                  (uint32_t)d[3 * (size_t)i + 1] << 8u | d[3 * (size_t)i + 2];
            }
        } else if (memcmp(type, "tRNS", 4) == 0) {
            st = chunk_trns(&hd, out, d, clen);
        } else if (memcmp(type, "IDAT", 4) == 0) {
            if (work_cap - idat < clen) {
                return PNG_ERR_MEMORY;
            }
            memcpy(work + idat, d, clen);
            idat += clen;
        } else if (memcmp(type, "IEND", 4) == 0) {
            break;
        } else if ((type[0] & 0x20u) == 0) {
            return PNG_ERR_UNSUPPORTED;
        }
        if (st != PNG_OK) {
            return st;
        }
    }
    if (!seen_ihdr || idat == 0 || (hd.ct == CT_INDEXED && out->ncolors == 0)) {
        return PNG_ERR_FORMAT;
    }
    if (hd.ct != CT_INDEXED) {
        out->ncolors = 0;
    }
    uint32_t bitspp = channels(hd.ct) * hd.depth;
    size_t rowbytes = ((size_t)hd.width * bitspp + 7) / 8;
    size_t bpp = bitspp >= 8 ? bitspp / 8 : 1;
    size_t rawlen = (size_t)hd.height * (rowbytes + 1);
    if (work_cap - idat < rawlen) {
        return PNG_ERR_MEMORY;
    }
    uint8_t *raw = work + idat;
    png_status st = unzlib(work, idat, raw, rawlen);
    if (st == PNG_OK) {
        st = unfilter(raw, hd.height, rowbytes, bpp);
    }
    if (st == PNG_OK) {
        out->width = (int32_t)hd.width;
        out->height = (int32_t)hd.height;
        st = pixels(&hd, raw, rowbytes, out);
    }
    return st;
}
