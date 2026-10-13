/* The tools: pointer gestures in image coordinates turned into gesso edits,
   one undo step per gesture. No window here, so it is tested without one. */
#ifndef EDITOR_H
#define EDITOR_H

#include "gesso.h"

typedef enum {
    TOOL_PENCIL,
    TOOL_ERASER,
    TOOL_FILL,
    TOOL_PICKER,
    TOOL_LINE,
    TOOL_RECT,
    TOOL_ELLIPSE,
    TOOL_SELECT,
    TOOL_SPRAY,
    TOOL_POLYGON,
    TOOL_CURVE,
    TOOL_LASSO,
    TOOL_TEXT, /* the app asks for the text; here it moves what was placed */
    TOOL_COUNT,
} tool_id;

#define EDITOR_SHIFT 0x1u /* square, circle, 45-degree line */
#define EDITOR_ALT 0x2u   /* moving a selection duplicates it */

enum {
    MIRRORS_MAX = 4,
};

typedef struct {
    gesso_doc *doc;
    tool_id tool;
    tool_id previous; /* where the picker goes back to */
    uint8_t fg;
    uint8_t bg;
    int filled;
    int32_t radius; /* the rectangle's corners */
    int32_t spray;  /* the spray's radius */
    uint32_t seed;  /* the spray's next dots */

    /* A polygon being made, one click a vertex: what is drawn so far stays
       in an open undo group until it closes. */
    uint32_t npoly;
    int32_t polyx[GESSO_POLYGON_MAX];
    int32_t polyy[GESSO_POLYGON_MAX];

    /* A curve: the first drag lays the line (bending, then 1), the second
       pulls its control point; the group stays open between them. */
    int bending;
    int32_t cx1;
    int32_t cy1;
    int32_t ctlx;
    int32_t ctly;
    int pixel_perfect;
    int mirror_x;
    int mirror_y;

    int down;
    int secondary;
    uint8_t color;
    int32_t x0;
    int32_t y0;
    int32_t lx;
    int32_t ly;
    int moving;
    int moved;

    /* Pixel-perfect: the stroke's last two pixels, and what the newer one
       covered at each mirrored place. */
    int npend;
    int32_t px[2];
    int32_t py[2];
    uint8_t under[MIRRORS_MAX];
} editor;

void editor_init(editor *e, gesso_doc *doc);
/* Changing tool ends what the old one was doing: a floating selection drops. */
void editor_tool(editor *e, tool_id t);
void editor_press(editor *e, int32_t x, int32_t y, int secondary);
void editor_drag(editor *e, int32_t x, int32_t y, uint32_t mods);
void editor_release(editor *e, int32_t x, int32_t y, uint32_t mods);
/* A polygon in the making closes (it needs three vertices; fewer draw a
   line or a point), a curve waiting for its bend stays straight. Changing
   tool does it too. */
void editor_finish(editor *e);

/* The press was not meant (a two-finger gesture took it): what it drew goes
   back, and nothing enters the history; a polygon in the making goes too. */
void editor_cancel(editor *e);

#endif
