/* The tools without a window: each test builds a document, makes gestures
   and checks the pixels and the undo steps. */
#include "editor.h"
#include "gesso.h"

#include <stdio.h>
#include <string.h>

enum {
    LOG_CAP = 16 * 1024 * 1024,
};

static gesso_doc doc;
static uint8_t log_mem[LOG_CAP];
static editor ed;
static int failures;

static void fresh(int32_t w, int32_t h) {
    gesso_init(&doc, log_mem, sizeof log_mem);
    (void)gesso_new(&doc, w, h);
    editor_init(&ed, &doc);
}

/* rows: one string per row, a digit (hex) per pixel. */
static void expect(const char *test, const char *const *rows) {
    for (int32_t y = 0; y < doc.img.height; y++) {
        for (int32_t x = 0; x < doc.img.width; x++) {
            char c = rows[y][x];
            uint8_t want = (uint8_t)(c >= 'a' ? c - 'a' + 10 : c - '0');
            uint8_t got = gesso_view(&doc, x, y);
            if (got != want) {
                printf("FAIL %s: (%d,%d) is %u, want %u\n", test, x, y, got, want);
                failures++;
                return;
            }
        }
    }
}

static void check(const char *test, int ok, const char *what) {
    if (!ok) {
        printf("FAIL %s: %s\n", test, what);
        failures++;
    }
}

static void stroke(const int32_t *pts, int n, uint32_t mods) {
    editor_press(&ed, pts[0], pts[1], 0);
    for (int i = 1; i < n; i++) {
        editor_drag(&ed, pts[2 * (size_t)i], pts[2 * (size_t)i + 1], mods);
    }
    editor_release(&ed, pts[2 * (size_t)(n - 1)], pts[2 * (size_t)(n - 1) + 1], mods);
}

static void test_pixel_perfect(void) {
    static const int32_t pts[] = {0, 0, 1, 0, 1, 1, 2, 1, 2, 2};
    fresh(3, 3);
    stroke(pts, 5, 0);
    static const char *const want[] = {"100", "010", "001"};
    expect("pixel-perfect", want);
    check("pixel-perfect", gesso_undo(&doc) == 1, "stroke must undo");
    static const char *const blank[] = {"000", "000", "000"};
    expect("pixel-perfect undo is one step", blank);

    fresh(3, 3);
    ed.pixel_perfect = 0;
    stroke(pts, 5, 0);
    static const char *const raw[] = {"110", "011", "001"};
    expect("pixel-perfect off", raw);
}

/* A pen wider than a pixel has no single-pixel corners to take back. */
static void test_brush_skips_pixel_perfect(void) {
    static const int32_t pts[] = {0, 0, 1, 0, 1, 1};
    fresh(4, 4);
    gesso_brush(&doc, 2, 0);
    stroke(pts, 3, 0);
    static const char *const want[] = {"1110", "1110", "0110", "0000"};
    expect("brush with pixel-perfect on", want);
}

/* Three clicks, then a click on the first vertex: one closed triangle, one
   undo step; a cancelled one leaves nothing. */
static void test_polygon(void) {
    fresh(7, 7);
    editor_tool(&ed, TOOL_POLYGON);
    static const int32_t v[] = {0, 0, 6, 0, 0, 6, 0, 0};
    for (size_t i = 0; i < 4; i++) {
        editor_press(&ed, v[2 * i], v[2 * i + 1], 0);
        editor_release(&ed, v[2 * i], v[2 * i + 1], 0);
    }
    check("polygon", ed.npoly == 0, "closes on the first vertex");
    static const char *const want[] = {
        "1111111", "1000010", "1000100", "1001000", "1010000", "1100000", "1000000",
    };
    expect("polygon", want);
    check("polygon", gesso_undo(&doc) == 1, "one step");
    static const char *const blank[] = {
        "0000000", "0000000", "0000000", "0000000", "0000000", "0000000", "0000000",
    };
    expect("polygon undo", blank);

    editor_press(&ed, 1, 1, 0);
    editor_release(&ed, 1, 1, 0);
    editor_press(&ed, 5, 1, 0);
    editor_release(&ed, 5, 1, 0);
    editor_cancel(&ed);
    expect("polygon cancelled", blank);
    check("polygon cancelled", gesso_undo(&doc) == 0, "nothing to undo");
}

/* A drag lays the line, a second drag bends it: one step. A curve left
   unbent stays the line when the tool changes. */
static void test_curve(void) {
    fresh(9, 5);
    editor_tool(&ed, TOOL_CURVE);
    editor_press(&ed, 0, 4, 0);
    editor_drag(&ed, 8, 4, 0);
    editor_release(&ed, 8, 4, 0);
    check("curve", ed.bending == 1, "waits for its bend");
    editor_press(&ed, 4, 0, 0);
    editor_release(&ed, 4, 0, 0);
    static const char *const want[] = {
        "000000000", "000000000", "001111100", "010000010", "100000001",
    };
    expect("curve", want);
    check("curve", gesso_undo(&doc) == 1, "one step");
    check("curve", gesso_undo(&doc) == 0, "only one");

    editor_press(&ed, 0, 1, 0);
    editor_drag(&ed, 8, 1, 0);
    editor_release(&ed, 8, 1, 0);
    editor_tool(&ed, TOOL_PENCIL);
    static const char *const straight[] = {
        "000000000", "111111111", "000000000", "000000000", "000000000",
    };
    expect("curve unbent", straight);
}

static void test_mirror(void) {
    fresh(5, 3);
    ed.mirror_x = 1;
    ed.mirror_y = 1;
    static const int32_t pts[] = {0, 0, 1, 0};
    ed.pixel_perfect = 0;
    stroke(pts, 2, 0);
    static const char *const want[] = {"11011", "00000", "11011"};
    expect("mirror", want);
}

static void test_shape_preview(void) {
    fresh(4, 4);
    ed.tool = TOOL_LINE;
    editor_press(&ed, 0, 0, 0);
    editor_drag(&ed, 3, 0, 0);
    editor_drag(&ed, 0, 3, 0);
    editor_release(&ed, 0, 3, 0);
    static const char *const want[] = {"1000", "1000", "1000", "1000"};
    expect("line preview leaves only the last line", want);
    check("line preview", gesso_undo(&doc) == 1 && gesso_undo(&doc) == 0, "one undo step");
}

static void test_constrain(void) {
    fresh(5, 5);
    ed.tool = TOOL_LINE;
    editor_press(&ed, 0, 0, 0);
    editor_release(&ed, 4, 3, EDITOR_SHIFT);
    static const char *const diag[] = {"10000", "01000", "00100", "00010", "00001"};
    expect("shift line snaps to 45 degrees", diag);

    fresh(6, 6);
    ed.tool = TOOL_LINE;
    editor_press(&ed, 0, 0, 0);
    editor_release(&ed, 4, 2, EDITOR_SHIFT);
    static const char *const two[] = {"110000", "001100", "000011", "000000", "000000", "000000"};
    expect("shift line snaps to 2:1, steps of 2", two);
    fresh(3, 6);
    ed.tool = TOOL_LINE;
    editor_press(&ed, 0, 0, 0);
    editor_release(&ed, 2, 4, EDITOR_SHIFT);
    static const char *const tall[] = {"100", "100", "010", "010", "001", "001"};
    expect("and to 1:2", tall);

    fresh(5, 3);
    ed.tool = TOOL_RECT;
    ed.filled = 1;
    editor_press(&ed, 0, 0, 1);
    editor_release(&ed, 4, 1, EDITOR_SHIFT);
    static const char *const square[] = {"00000", "00000", "00000"};
    expect("secondary paints the background (index 0)", square);
}

static void test_picker(void) {
    fresh(2, 1);
    (void)gesso_pixel(&doc, 1, 0, 9);
    editor_tool(&ed, TOOL_FILL);
    editor_tool(&ed, TOOL_PICKER);
    editor_press(&ed, 1, 0, 0);
    editor_release(&ed, 1, 0, 0);
    check("picker", ed.fg == 9, "takes the color");
    check("picker", ed.tool == TOOL_FILL, "goes back to the previous tool");
    editor_press(&ed, 0, 0, 0);
    editor_release(&ed, 0, 0, 0);
    static const char *const want[] = {"99"};
    expect("fill with the picked color", want);
}

static void test_select_move(void) {
    fresh(4, 2);
    (void)gesso_rect_draw(&doc, 0, 0, 1, 1, 4, 1);
    editor_tool(&ed, TOOL_SELECT);
    editor_press(&ed, 0, 0, 0);
    editor_release(&ed, 1, 1, 0);
    check("select", doc.selected && doc.sel.w == 2 && doc.sel.h == 2, "drag selects");
    editor_press(&ed, 1, 1, 0);
    editor_drag(&ed, 2, 1, 0);
    editor_release(&ed, 3, 1, 0);
    editor_tool(&ed, TOOL_PENCIL);
    static const char *const moved[] = {"0044", "0044"};
    expect("drag inside moves", moved);
    check("select move", gesso_undo(&doc) == 1, "undo");
    static const char *const back[] = {"4400", "4400"};
    expect("lift and drop are one step", back);

    editor_tool(&ed, TOOL_SELECT);
    editor_press(&ed, 0, 0, 0);
    editor_release(&ed, 1, 1, 0);
    editor_press(&ed, 0, 0, 0);
    editor_release(&ed, 2, 0, EDITOR_ALT);
    editor_press(&ed, 0, 1, 0);
    editor_release(&ed, 0, 1, 0);
    static const char *const dup[] = {"4444", "4444"};
    expect("option-drag duplicates; a click outside drops", dup);
    check("click outside", !doc.selected, "deselects");
}

static void test_lasso(void) {
    fresh(4, 3);
    (void)gesso_rect_draw(&doc, 0, 0, 3, 2, 4, 1);
    (void)gesso_pixel(&doc, 2, 2, 7);
    editor_tool(&ed, TOOL_LASSO);
    editor_press(&ed, 0, 0, 0);
    editor_drag(&ed, 2, 0, 0);
    editor_release(&ed, 0, 2, 0);
    check("lasso", doc.selected && doc.masked, "drag selects");
    editor_press(&ed, 2, 2, 0);
    editor_release(&ed, 2, 2, 0);
    check("lasso", !doc.selected, "a click outside the path, inside its box, deselects");

    editor_press(&ed, 0, 0, 0);
    editor_drag(&ed, 2, 0, 0);
    editor_release(&ed, 0, 2, 0);
    editor_press(&ed, 0, 0, 0);
    editor_release(&ed, 1, 0, 0);
    editor_tool(&ed, TOOL_PENCIL);
    static const char *const want[] = {"0444", "0444", "0474"};
    expect("lasso moves only what the path encloses", want);
}

static void test_eraser(void) {
    fresh(3, 1);
    (void)gesso_line(&doc, 0, 0, 2, 0, 7);
    editor_tool(&ed, TOOL_ERASER);
    static const int32_t pts[] = {0, 0, 1, 0};
    stroke(pts, 2, 0);
    static const char *const want[] = {"007"};
    expect("eraser leaves index 0", want);
}

static void test_cancel(void) {
    fresh(3, 1);
    static const int32_t pts[] = {0, 0, 2, 0};
    editor_press(&ed, pts[0], pts[1], 0);
    editor_drag(&ed, pts[2], pts[3], 0);
    editor_cancel(&ed);
    static const char *const want[] = {"000"};
    expect("cancel takes the stroke back", want);
    check("cancel", gesso_undo(&doc) == 0, "leaves no undo step");
}

int main(void) {
    test_pixel_perfect();
    test_brush_skips_pixel_perfect();
    test_polygon();
    test_curve();
    test_mirror();
    test_shape_preview();
    test_constrain();
    test_picker();
    test_select_move();
    test_lasso();
    test_eraser();
    test_cancel();
    if (failures == 0) {
        printf("editor: ok\n");
    }
    return failures == 0 ? 0 : 1;
}
