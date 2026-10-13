/* gessetto: one open document as the app sees it (the image, its history,
   the tools and their colors) behind an opaque handle. The Swift side sees
   only this header: gesso_doc's arrays are megabytes, and the importer
   would turn them into tuples.

   Every call runs on one thread at a time per document. Reading, importing,
   gst_write and gst_header also share one scratch area among all
   documents: the caller serializes those. */
#ifndef GESSETTO_H
#define GESSETTO_H

#include <stddef.h>
#include <stdint.h>

typedef struct gst gst;

enum {
    GST_SIDE_MAX = 256,
    GST_COLORS_MAX = 256,
    GST_FRAMES_MAX = 256,
    GST_TAG_NAME_MAX = 32,
    GST_DURATION_MAX = 60000,
    GST_ERROR_MAX = 320,
};

/* The same order as editor.h's tool_id. */
typedef enum {
    GST_PENCIL,
    GST_ERASER,
    GST_FILL,
    GST_PICKER,
    GST_LINE,
    GST_RECT,
    GST_ELLIPSE,
    GST_SELECT,
    GST_TOOL_COUNT,
} gst_tool;

typedef enum {
    GST_PIXEL_PERFECT,
    GST_MIRROR_X,
    GST_MIRROR_Y,
    GST_FILLED,
    GST_TRANSPARENT,
} gst_option;

#define GST_SHIFT 0x1u /* square, circle, 45-degree line */
#define GST_ALT 0x2u   /* moving a selection duplicates it */

/* The bytes a gst needs. The caller hands them over zeroed and keeps them
   for the document's life; most of it is history, never touched until
   used. */
size_t gst_size(void);
/* A blank 32 x 32 image with the PICO-8 colors. */
void gst_init(gst *g);

/* Each returns 1, or 0 with a message for a person in err. The canvas
   resized, every frame anchored at the top left: one undo step. */
int gst_resize(gst *g, int32_t width, int32_t height, char err[GST_ERROR_MAX]);
int gst_read(gst *g, const uint8_t *src, size_t len, char err[GST_ERROR_MAX]);
/* A PNG replaces the image, its colors the palette: one undo step. */
int gst_import_png(gst *g, const uint8_t *src, size_t len, char err[GST_ERROR_MAX]);
/* A sprite sheet (sw x sh pixels, 0xAARRGGBB) cut into w x h frames
   replaces the image: one undo step. */
int gst_import_sheet(gst *g, const uint32_t *argb, int32_t sw, int32_t sh, int32_t w, int32_t h,
                     char err[GST_ERROR_MAX]);
/* The image as a .gesso file, in the scratch area until the next call;
   a floating selection is dropped first. NULL when it does not fit. */
const uint8_t *gst_write(const gst *g, size_t *len);

/* The image as the C header gesso2c makes, identifiers from name (a file
   name will do), in the scratch area like gst_write. NULL when too large. */
const uint8_t *gst_header(const gst *g, const char *name, size_t *len);

int32_t gst_width(const gst *g);
int32_t gst_height(const gst *g);
uint32_t gst_frames(const gst *g);
uint32_t gst_frame(const gst *g);
uint32_t gst_duration(const gst *g, uint32_t frame);
/* Palette entries, index 0 (transparent) included. */
uint32_t gst_colors(const gst *g);
uint32_t gst_color(const gst *g, uint32_t index); /* 0xffRRGGBB; 0 for index 0 */
uint32_t gst_tags(const gst *g);
const char *gst_tag_name(const gst *g, uint32_t tag);
uint32_t gst_tag_first(const gst *g, uint32_t tag);
uint32_t gst_tag_last(const gst *g, uint32_t tag);
uint32_t gst_tag_direction(const gst *g, uint32_t tag);
/* The index shown at (x, y) of the active frame, floating pixels
   included; 0 outside. */
uint8_t gst_index_at(const gst *g, int32_t x, int32_t y);
/* 0 without a selection, 1 with one, 2 while it floats; r gets x, y, w, h. */
int gst_selection(const gst *g, int32_t r[4]);

gst_tool gst_tool_get(const gst *g);
void gst_tool_set(gst *g, gst_tool t);
uint8_t gst_fg(const gst *g);
uint8_t gst_bg(const gst *g);
void gst_set_fg(gst *g, uint32_t index);
void gst_set_bg(gst *g, uint32_t index);
void gst_swap_colors(gst *g);
int gst_option_get(const gst *g, gst_option o);
void gst_option_set(gst *g, gst_option o, int on);

/* The pen: 1 to 16 pixels, square or round. */
void gst_brush_set(gst *g, uint32_t size, int round);
uint32_t gst_brush_size(const gst *g);
int gst_brush_round(const gst *g);

/* A gesture, in image pixels (outside the image is fine). */
void gst_press(gst *g, int32_t x, int32_t y, int secondary);
void gst_drag(gst *g, int32_t x, int32_t y, uint32_t mods);
void gst_release(gst *g, int32_t x, int32_t y, uint32_t mods);
/* The press was not meant: what it drew goes back, nothing is kept. */
void gst_cancel(gst *g);

/* The history, step by step, for the platform's undo manager: after an
   action, gst_new_step says whether it began a step (once per step, however
   many calls it takes). Undo and redo return whether anything changed. */
int gst_new_step(gst *g);
int gst_undo(gst *g);
int gst_redo(gst *g);
int gst_can_redo(const gst *g);

/* Edits; each returns 1, or 0 when it did nothing (no selection, a full
   palette, a value out of range). */
int gst_select_all(gst *g);
int gst_deselect(gst *g);
int gst_delete(gst *g);
int gst_copy(gst *g);
int gst_cut(gst *g);
int gst_paste(gst *g, int32_t x, int32_t y);
int gst_move_selection(gst *g, int32_t dx, int32_t dy);
int gst_flip(gst *g, int vertical);
int gst_rotate(gst *g);
int gst_frame_select(gst *g, uint32_t frame);
int gst_frame_add(gst *g);
/* Frames first..last, both included: copies go after last (the first copy
   becomes active), a move puts first at to. */
int gst_frames_duplicate(gst *g, uint32_t first, uint32_t last);
int gst_frames_delete(gst *g, uint32_t first, uint32_t last);
int gst_frames_move(gst *g, uint32_t first, uint32_t last, uint32_t to);
int gst_frames_duration(gst *g, uint32_t first, uint32_t last, uint32_t ms);
/* Frames copied as colors (0xAARRGGBB, width * height each, maybe from
   another image of this size) go in after the active frame, each color
   matched to this palette. Uses the scratch area. */
int gst_frames_paste(gst *g, const uint32_t *argb, const uint32_t *duration, uint32_t count);
/* direction: 0 forward, 1 reverse, 2 ping-pong. */
int gst_tag_set(gst *g, const char *name, uint32_t first, uint32_t last, uint32_t direction,
                char err[GST_ERROR_MAX]);
int gst_tag_delete(gst *g, const char *name);
/* Successive changes to the same color, with nothing else between them,
   are one step: a color panel sends one per movement. index == gst_colors
   appends. */
int gst_color_set(gst *g, uint32_t index, uint32_t rgb);

/* A color removed, its pixels taking the nearest left; a color moved, its
   pixels keeping it. The tools' colors follow. */
int gst_color_delete(gst *g, uint32_t index);
int gst_color_move(gst *g, uint32_t from, uint32_t to);
/* A new palette (0xRRGGBB, 1 to 255 colors): match moves every pixel to the
   nearest new color, otherwise indices stay. */
int gst_palette_load(gst *g, const uint32_t *colors, uint32_t count, int match);

/* The clipboard as ARGB (index 0 clear), for the system's; w and h are 0
   when it is empty. dst holds GST_SIDE_MAX * GST_SIDE_MAX. */
void gst_clip_argb(const gst *g, uint32_t *dst, int32_t *w, int32_t *h);
/* An image from the system's clipboard becomes the clipboard, its colors
   matched to the palette. */
int gst_clip_from_argb(gst *g, const uint32_t *src, int32_t w, int32_t h);

/* A frame as it is, for export: 0xAARRGGBB, index 0 clear, the floating
   selection where it floats. dst holds width * height. */
void gst_frame_argb(const gst *g, uint32_t frame, uint32_t *dst);

/* A frame as opaque 0xAARRGGBB, width * height: transparency as the
   checkerboard drawn at zoom (screen points per pixel), and with onion the
   previous frame faint under it, the next one fainter. The active frame shows the floating
   selection. */
void gst_render(const gst *g, uint32_t *dst, uint32_t frame, int32_t zoom, int onion);

#endif
