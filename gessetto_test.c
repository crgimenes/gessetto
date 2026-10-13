/* The facade the app drives: the undo bridge above all, since a step
   counted twice or missed puts the platform's undo manager out of step with
   the history. Runs under the sanitizers. */
#include "gessetto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

/* What an undo manager would hold: one entry per step the facade reports. */
typedef struct {
    int undo;
    int redo;
} manager;

static void after(gst *g, manager *m) {
    if (gst_new_step(g)) {
        m->undo++;
        m->redo = 0;
    }
}

static void undo(gst *g, manager *m) {
    CHECK(m->undo > 0);
    m->undo--;
    (void)gst_undo(g);
    if (gst_can_redo(g)) {
        m->redo++;
    }
}

static void redo(gst *g, manager *m) {
    CHECK(m->redo > 0);
    m->redo--;
    if (gst_redo(g)) {
        m->undo++;
    }
}

static uint32_t pixel(const gst *g, int32_t x, int32_t y) {
    static uint32_t buf[GST_SIDE_MAX * GST_SIDE_MAX];
    gst_render(g, buf, gst_frame(g), 8, 0);
    return buf[y * gst_width(g) + x];
}

static void stroke(gst *g, manager *m, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    gst_press(g, x0, y0, 0);
    gst_drag(g, x1, y1, 0);
    gst_release(g, x1, y1, 0);
    after(g, m);
}

static gst *fresh(void) {
    gst *g = calloc(1, gst_size());
    if (g == NULL) {
        abort();
    }
    gst_init(g);
    return g;
}

static void test_steps(void) {
    gst *g = fresh();
    manager m = {0};
    uint32_t red = gst_color(g, 8) | 0xff000000u;
    gst_set_fg(g, 8);
    stroke(g, &m, 0, 0, 3, 0);
    stroke(g, &m, 0, 2, 3, 2);
    CHECK(m.undo == 2);
    CHECK(pixel(g, 2, 2) == red);

    /* A selection moved twice and dropped is one step. */
    gst_tool_set(g, GST_SELECT);
    gst_press(g, 0, 0, 0);
    gst_drag(g, 3, 0, 0);
    gst_release(g, 3, 0, 0);
    after(g, &m);
    CHECK(m.undo == 2);
    gst_press(g, 1, 0, 0);
    gst_drag(g, 1, 4, 0);
    gst_release(g, 1, 4, 0);
    after(g, &m);
    CHECK(m.undo == 3);
    CHECK(gst_move_selection(g, 1, 0));
    after(g, &m);
    CHECK(gst_deselect(g));
    after(g, &m);
    CHECK(m.undo == 3);
    CHECK(pixel(g, 1, 4) == red);
    CHECK(pixel(g, 0, 0) != red);

    undo(g, &m);
    CHECK(pixel(g, 0, 0) == red);
    CHECK(pixel(g, 1, 4) != red);
    undo(g, &m);
    undo(g, &m);
    CHECK(m.undo == 0 && m.redo == 3);
    CHECK(pixel(g, 0, 0) != red);
    redo(g, &m);
    redo(g, &m);
    redo(g, &m);
    CHECK(m.undo == 3 && m.redo == 0);
    CHECK(pixel(g, 1, 4) == red);

    /* Undo while a paste floats takes the paste back and leaves no redo. */
    CHECK(gst_select_all(g));
    CHECK(gst_copy(g));
    CHECK(gst_paste(g, 5, 5));
    after(g, &m);
    CHECK(m.undo == 4);
    undo(g, &m);
    CHECK(m.undo == 3 && m.redo == 0);
    CHECK(pixel(g, 6, 9) != red);

    /* A gesture cancelled leaves nothing to count. */
    gst_tool_set(g, GST_PENCIL);
    gst_press(g, 10, 10, 0);
    gst_drag(g, 12, 10, 0);
    gst_cancel(g);
    after(g, &m);
    CHECK(m.undo == 3);
    CHECK(pixel(g, 11, 10) != red);
    free(g);
}

static void test_color_live(void) {
    gst *g = fresh();
    manager m = {0};
    uint32_t before = gst_color(g, 3);
    CHECK(gst_color_set(g, 3, 0x102030));
    after(g, &m);
    CHECK(gst_color_set(g, 3, 0x405060));
    after(g, &m);
    CHECK(gst_color_set(g, 3, 0x708090));
    after(g, &m);
    CHECK(m.undo == 1);
    CHECK(gst_color(g, 3) == 0xff708090u);
    undo(g, &m);
    CHECK(gst_color(g, 3) == before);
    redo(g, &m);
    CHECK(gst_color(g, 3) == 0xff708090u);

    /* Something between two edits makes them two steps. */
    stroke(g, &m, 0, 0, 1, 1);
    CHECK(gst_color_set(g, 3, 0x111111));
    after(g, &m);
    CHECK(m.undo == 3);

    /* Moving a color carries the foreground with it; deleting one keeps
       the pixels looking alike. */
    gst_set_fg(g, 5);
    CHECK(gst_color_move(g, 5, 2) && gst_fg(g) == 2);
    after(g, &m);
    CHECK(gst_color_delete(g, 2) && gst_fg(g) == 1);
    after(g, &m);
    CHECK(!gst_color_delete(g, 0) && !gst_color_move(g, 1, 99));
    CHECK(m.undo == 5);
    undo(g, &m);
    undo(g, &m);
    CHECK(gst_color(g, 5) == 0xffab5236u);

    /* Appending a color, and undoing it, leaves the tools a real color. */
    uint32_t n = gst_colors(g);
    CHECK(gst_color_set(g, n, 0xabcdef));
    after(g, &m);
    CHECK(gst_colors(g) == n + 1);
    gst_set_fg(g, n);
    CHECK(gst_fg(g) == n);
    undo(g, &m);
    CHECK(gst_colors(g) == n);
    CHECK(gst_fg(g) < gst_colors(g));
    CHECK(!gst_color_set(g, 0, 0x123456));
    free(g);
}

/* A PNG from the decoder's corpus replaces the image as one step. */
static void test_import(gst *g) {
    char err[GST_ERROR_MAX];
    FILE *f = fopen("testdata/png/indexed-4.png", "rb");
    if (f == NULL) {
        fprintf(stderr, "testdata/png/indexed-4.png: cannot open\n");
        failures++;
        return;
    }
    static uint8_t png[65536];
    size_t n = fread(png, 1, sizeof png, f);
    fclose(f);
    int32_t w = gst_width(g);
    (void)gst_new_step(g);
    CHECK(gst_import_png(g, png, n, err));
    CHECK(gst_new_step(g));
    CHECK(gst_width(g) == 13 && gst_height(g) == 7 && gst_frames(g) == 1);
    CHECK(gst_undo(g) && gst_width(g) == w);
}

/* A 5 x 2 sheet of 2 x 2 tiles: two frames, the odd column left out. */
static void test_sheet(void) {
    gst *g = fresh();
    char err[GST_ERROR_MAX];
    const uint32_t A = 0xff112233u;
    const uint32_t B = 0xff445566u;
    const uint32_t sheet[10] = {A, 0, B, B, A, 0, A, 0x10ffffffu, B, A};
    (void)gst_new_step(g);
    CHECK(gst_import_sheet(g, sheet, 5, 2, 2, 2, err));
    CHECK(gst_new_step(g));
    CHECK(gst_width(g) == 2 && gst_height(g) == 2 && gst_frames(g) == 2);
    CHECK(gst_colors(g) == 3 && gst_color(g, 1) == A && gst_color(g, 2) == B);
    CHECK(gst_index_at(g, 0, 0) == 1 && gst_index_at(g, 1, 0) == 0 && gst_index_at(g, 1, 1) == 1);
    CHECK(gst_frame_select(g, 1));
    CHECK(gst_index_at(g, 0, 0) == 2 && gst_index_at(g, 1, 1) == 2);
    CHECK(!gst_import_sheet(g, sheet, 5, 2, 3, 3, err));
    CHECK(!gst_import_sheet(g, sheet, 5, 2, 0, 2, err));
    CHECK(gst_undo(g) && gst_width(g) == 32);

    /* 512 colors in two 16 x 16 frames: cut to 255 by median cut, each
       pixel close to its own color. */
    static uint32_t many[32 * 16];
    for (uint32_t i = 0; i < 32 * 16; i++) {
        many[i] = 0xff000000u | (i % 32u * 8u) << 16u | (i / 32u * 16u) << 8u | (i & 1u) * 64u;
    }
    CHECK(gst_import_sheet(g, many, 32, 16, 16, 16, err));
    CHECK(gst_frames(g) == 2 && gst_colors(g) == 256);
    for (int32_t i = 0; i < 32 * 16; i++) {
        int32_t x = i % 32;
        CHECK(gst_frame_select(g, (uint32_t)(x / 16)));
        uint32_t c = gst_color(g, gst_index_at(g, x % 16, i / 32));
        for (uint32_t shift = 0; shift < 24; shift += 8) {
            int32_t d = (int32_t)(c >> shift & 0xffu) - (int32_t)(many[i] >> shift & 0xffu);
            CHECK(d > -40 && d < 40);
        }
    }
    free(g);
}

/* Frames pasted from colors land after the active one, colors matched. */
static void test_frames_paste(void) {
    gst *g = fresh();
    uint32_t px[32 * 32 * 2] = {0};
    px[0] = 0xffff004du;
    px[32 * 32 + 1] = 0xfffe014cu;
    const uint32_t ms[2] = {70, 80};
    (void)gst_new_step(g);
    CHECK(gst_frames_paste(g, px, ms, 2));
    CHECK(gst_new_step(g));
    CHECK(gst_frames(g) == 3 && gst_frame(g) == 1);
    CHECK(gst_index_at(g, 0, 0) == 9 && gst_duration(g, 1) == 70 && gst_duration(g, 2) == 80);
    CHECK(gst_frame_select(g, 2) && gst_index_at(g, 1, 0) == 9);
    const uint32_t bad[1] = {0};
    CHECK(!gst_frames_paste(g, px, bad, 1));
    CHECK(gst_undo(g) && gst_frames(g) == 1);
    free(g);
}

/* Red (9), blue (13) and black (1) under a palette of blue and red: with
   match each takes its nearest; without, black keeps index 1 (blue now)
   and the others, past the palette, still take their nearest. */
static void test_palette_load(void) {
    gst *g = fresh();
    manager m = {0};
    gst_set_fg(g, 9);
    stroke(g, &m, 0, 0, 0, 0);
    gst_set_fg(g, 13);
    stroke(g, &m, 1, 0, 1, 0);
    gst_set_fg(g, 1);
    stroke(g, &m, 2, 0, 2, 0);
    const uint32_t two[2] = {0x0000ff, 0xff0000};
    CHECK(gst_palette_load(g, two, 2, 1));
    CHECK(gst_colors(g) == 3 && gst_index_at(g, 0, 0) == 2 && gst_index_at(g, 1, 0) == 1);
    CHECK(gst_index_at(g, 2, 0) == 1);
    CHECK(gst_fg(g) < gst_colors(g));
    CHECK(gst_undo(g));
    CHECK(gst_palette_load(g, two, 2, 0));
    CHECK(gst_index_at(g, 0, 0) == 2 && gst_index_at(g, 1, 0) == 1 && gst_index_at(g, 2, 0) == 1);
    CHECK(!gst_palette_load(g, two, 0, 0));
    free(g);
}

/* The spray: the same gesture gives the same dots, all within its radius. */
static void test_spray(void) {
    static uint32_t a[GST_SIDE_MAX * GST_SIDE_MAX];
    static uint32_t b[GST_SIDE_MAX * GST_SIDE_MAX];
    for (int k = 0; k < 2; k++) {
        gst *g = fresh();
        gst_tool_set(g, GST_SPRAY);
        gst_spray_set(g, 5);
        gst_set_fg(g, 9);
        gst_press(g, 16, 16, 0);
        gst_drag(g, 17, 16, 0);
        gst_release(g, 17, 16, 0);
        gst_frame_argb(g, 0, k == 0 ? a : b);
        CHECK(gst_new_step(g));
        free(g);
    }
    CHECK(memcmp(a, b, (size_t)32 * 32 * sizeof a[0]) == 0);
    int dots = 0;
    for (int32_t y = 0; y < 32; y++) {
        for (int32_t x = 0; x < 32; x++) {
            if (a[y * 32 + x] == 0) {
                continue;
            }
            dots++;
            int32_t d0 = (x - 16) * (x - 16) + (y - 16) * (y - 16);
            int32_t d1 = (x - 17) * (x - 17) + (y - 16) * (y - 16);
            CHECK(d0 <= 25 || d1 <= 25);
        }
    }
    CHECK(dots >= 6);
}

static void test_files(void) {
    gst *g = fresh();
    manager m = {0};
    char err[GST_ERROR_MAX];
    gst_set_fg(g, 12);
    stroke(g, &m, 0, 0, 31, 31);
    CHECK(gst_frame_add(g));
    CHECK(gst_tag_set(g, "walk", 0, 1, 2, err));
    CHECK(!gst_tag_set(g, "bad\"name", 0, 1, 0, err));
    CHECK(strlen(err) > 0);

    /* A floating selection is saved where it floats, and keeps floating. */
    CHECK(gst_frame_select(g, 0));
    gst_tool_set(g, GST_SELECT);
    gst_press(g, 0, 0, 0);
    gst_drag(g, 3, 3, 0);
    gst_release(g, 3, 3, 0);
    CHECK(gst_move_selection(g, 10, 0));
    size_t len = 0;
    const uint8_t *src = gst_write(g, &len);
    if (src == NULL || len == 0) {
        fprintf(stderr, "gst_write wrote nothing\n");
        exit(1);
    }
    uint8_t *copy = malloc(len);
    if (copy == NULL) {
        abort();
    }
    memcpy(copy, src, len);
    int32_t r[4];
    CHECK(gst_selection(g, r) == 2);

    gst *h = fresh();
    CHECK(gst_read(h, copy, len, err));
    CHECK(gst_frames(h) == 2 && gst_tags(h) == 1);
    CHECK(strcmp(gst_tag_name(h, 0), "walk") == 0 && gst_tag_last(h, 0) == 1);
    CHECK(gst_tag_direction(h, 0) == 2);
    CHECK(!gst_tag_set(h, "walk", 0, 1, 3, err));
    CHECK(pixel(h, 11, 1) == (gst_color(h, 12) | 0xff000000u));
    CHECK(pixel(h, 1, 1) != (gst_color(h, 12) | 0xff000000u));

    const char *bad = "(gesso 1 2 2)\n(frame 100 \"0000\" \"00\")\n";
    CHECK(!gst_read(h, (const uint8_t *)bad, strlen(bad), err));
    CHECK(strncmp(err, "line ", 5) == 0);
    CHECK(!gst_import_png(h, (const uint8_t *)bad, strlen(bad), err));
    test_import(h);
    (void)gst_new_step(h);
    CHECK(gst_resize(h, 16, 2, err) && gst_width(h) == 16 && gst_height(h) == 2);
    CHECK(gst_new_step(h));
    CHECK(!gst_resize(h, 300, 2, err) && !gst_resize(h, 0, 2, err));
    CHECK(gst_undo(h) && gst_width(h) == 32 && gst_height(h) == 32);
    free(copy);
    free(h);
    free(g);
}

static void test_clipboard_and_render(void) {
    gst *g = fresh();
    static uint32_t argb[GST_SIDE_MAX * GST_SIDE_MAX];
    int32_t w = -1;
    int32_t h = -1;
    gst_clip_argb(g, argb, &w, &h);
    CHECK(w == 0 && h == 0);

    const uint32_t in[4] = {0, 0xffff004du, 0xfffe014cu, 0x10ffffffu};
    CHECK(gst_clip_from_argb(g, in, 2, 2));
    CHECK(gst_paste(g, 0, 0));
    CHECK(pixel(g, 1, 0) == 0xffff004du);
    CHECK(pixel(g, 0, 1) == 0xffff004du);
    gst_clip_argb(g, argb, &w, &h);
    CHECK(w == 2 && h == 2 && argb[0] == 0 && argb[3] == 0);
    CHECK(gst_index_at(g, 1, 0) == 9 && gst_index_at(g, 0, 0) == 0 && gst_index_at(g, -1, 0) == 0);
    gst_frame_argb(g, 0, argb);
    CHECK(argb[0] == 0 && argb[1] == 0xffff004du && argb[32] == 0xffff004du && argb[33] == 0);
    CHECK(!gst_clip_from_argb(g, in, 0, 2));

    /* The checkerboard is 8 points a square, and onion skin shows the
       frame before where this one is clear. */
    static uint32_t buf[GST_SIDE_MAX * GST_SIDE_MAX];
    CHECK(gst_deselect(g));
    gst_render(g, buf, 0, 1, 0);
    CHECK(buf[2 * 32 + 2] == buf[7 * 32 + 7] && buf[2 * 32 + 2] != buf[8 * 32 + 2]);
    CHECK(gst_frame_add(g));
    gst_render(g, buf, 1, 8, 0);
    uint32_t plain = buf[1];
    gst_render(g, buf, 1, 8, 1);
    CHECK(buf[1] != plain && buf[5] == plain);
    uint32_t previous = buf[1];
    CHECK(gst_frame_select(g, 0));
    gst_render(g, buf, 0, 8, 1);
    CHECK(buf[1] == 0xffff004du);
    CHECK(gst_frame_select(g, 1));
    CHECK(gst_clip_from_argb(g, in, 2, 2) && gst_paste(g, 4, 0) && gst_deselect(g));
    gst_render(g, buf, 0, 8, 1);
    CHECK(buf[5] != plain && buf[5] != previous);
    gst_render(g, buf, 0, 8, 0);
    CHECK(buf[5] == plain);
    free(g);
}

int main(void) {
    test_steps();
    test_color_live();
    test_files();
    test_sheet();
    test_frames_paste();
    test_palette_load();
    test_spray();
    test_clipboard_and_render();
    if (failures > 0) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    printf("gessetto: ok\n");
    return 0;
}
