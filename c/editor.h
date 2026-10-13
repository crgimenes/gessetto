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
/* The press was not meant (a two-finger gesture took it): what it drew goes
   back, and nothing enters the history. */
void editor_cancel(editor *e);

#endif
