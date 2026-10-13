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

static void test_files(void) {
    gst *g = fresh();
    manager m = {0};
    char err[GST_ERROR_MAX];
    gst_set_fg(g, 12);
    stroke(g, &m, 0, 0, 31, 31);
    CHECK(gst_frame_add(g));
    CHECK(gst_tag_set(g, "walk", 0, 1, err));
    CHECK(!gst_tag_set(g, "bad\"name", 0, 1, err));
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
    free(g);
}

int main(void) {
    test_steps();
    test_color_live();
    test_files();
    test_clipboard_and_render();
    if (failures > 0) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    printf("gessetto: ok\n");
    return 0;
}
