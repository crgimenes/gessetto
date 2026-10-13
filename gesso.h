/* gesso: an indexed-color image with frames and tags, and the edits the
   gessetto editor makes to it, with undo.

   Freestanding: no allocation, nothing from libc but memcpy, memmove,
   memset and memcmp. The host owns the memory: a gesso_doc (large, meant to
   be static) and the block its history lives in.

   Index 0 is transparent. Coordinates are frame pixels; rectangles given as
   two corners include both, in any order, and are clipped to the frame.
   Every edit is one undo step unless it happens inside gesso_begin and
   gesso_end; an edit that changes nothing is no step at all. */
#ifndef GESSO_H
#define GESSO_H

#include <stddef.h>
#include <stdint.h>

enum {
    GESSO_VERSION = 1,
    GESSO_SIDE_MAX = 256,
    GESSO_COLORS_MAX = 256,
    GESSO_FRAMES_MAX = 256,
    GESSO_TAGS_MAX = 64,
    GESSO_TAG_NAME_MAX = 32, /* bytes, the terminating NUL included */
    GESSO_DURATION_DEFAULT = 100,
    GESSO_DURATION_MAX = 60000,
    GESSO_COORD_MAX = 32768,
    GESSO_FRAME_PIXELS = GESSO_SIDE_MAX * GESSO_SIDE_MAX,
};

typedef enum {
    GESSO_OK = 0,
    GESSO_ERR_NO_IMAGE,
    GESSO_ERR_SIZE,
    GESSO_ERR_COORD,
    GESSO_ERR_COLOR,
    GESSO_ERR_FRAME,
    GESSO_ERR_FRAMES_FULL,
    GESSO_ERR_LAST_FRAME,
    GESSO_ERR_DURATION,
    GESSO_ERR_PALETTE_FULL,
    GESSO_ERR_TAG_NAME,
    GESSO_ERR_TAG_RANGE,
    GESSO_ERR_TAGS_FULL,
    GESSO_ERR_NO_TAG,
    GESSO_ERR_NO_SELECTION,
    GESSO_ERR_CLIPBOARD,
    GESSO_ERR_TAG_DIRECTION,
} gesso_status;

/* How a tag's frames play. */
typedef enum {
    GESSO_FORWARD,
    GESSO_REVERSE,
    GESSO_PINGPONG,
    GESSO_DIRECTIONS,
} gesso_direction;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} gesso_rect;

typedef struct {
    char name[GESSO_TAG_NAME_MAX];
    uint32_t first;
    uint32_t last;
    uint32_t direction; /* a gesso_direction */
} gesso_tag;

/* Frame f's pixels are width*height bytes at pixels + f*width*height. */
typedef struct {
    int32_t width;
    int32_t height;
    uint32_t ncolors;
    uint32_t palette[GESSO_COLORS_MAX]; /* 0xffRRGGBB; entry 0 holds 0 */
    uint32_t nframes;
    uint32_t duration[GESSO_FRAMES_MAX]; /* milliseconds */
    uint32_t ntags;
    gesso_tag tags[GESSO_TAGS_MAX];
    uint8_t pixels[GESSO_FRAMES_MAX * GESSO_FRAME_PIXELS];
} gesso_image;

typedef struct {
    uint8_t pixels[GESSO_FRAME_PIXELS];
    int32_t w;
    int32_t h;
} gesso_patch;

typedef struct {
    gesso_image img;
    uint32_t frame;

    /* The selection rectangle; while floating, where the lifted pixels are. */
    int selected;
    int floating;
    int transparent; /* floating index 0 shows what is under it */
    gesso_rect sel;
    gesso_patch lifted;
    gesso_patch clip;
    gesso_patch scratch;

    uint8_t *log;
    size_t cap;
    size_t top; /* undo entries end here; redo entries run to end */
    size_t end;
    uint32_t depth;
    uint32_t group;
    uint32_t group_next;
    uint32_t float_group;
    uint64_t serial;
    uint64_t floor; /* the state when no entry is left below top */
    uint64_t saved;

    /* The pen: lines, the pencil and outlines stamp it at every point. */
    uint32_t brush;
    int brush_round;

    uint32_t stack[GESSO_FRAME_PIXELS];
} gesso_doc;

const char *gesso_message(gesso_status s);

/* log is the history's memory; the oldest steps go when it fills. */
void gesso_init(gesso_doc *d, uint8_t *log, size_t cap);
/* A width x height image, one blank frame, the PICO-8 colors at 1..16. The
   history starts empty. */
gesso_status gesso_new(gesso_doc *d, int32_t width, int32_t height);
/* An image built elsewhere (the file reader) becomes the document, with an
   empty history. */
void gesso_adopt(gesso_doc *d, const gesso_image *img);

uint8_t *gesso_frame_pixels(gesso_image *img, uint32_t frame);
/* The index shown at (x, y) of the active frame, floating pixels included;
   0 outside. */
uint8_t gesso_view(const gesso_doc *d, int32_t x, int32_t y);

enum {
    GESSO_BRUSH_MAX = 16,
};

/* The pen for lines, the pencil and outlines: size x size pixels, square or
   round (fills and filled shapes do not use it). An even size reaches one
   pixel further right and down than left and up. */
void gesso_brush(gesso_doc *d, uint32_t size, int round);

/* Pixel edits on the active frame. */
gesso_status gesso_pixel(gesso_doc *d, int32_t x, int32_t y, uint8_t c);
gesso_status gesso_line(gesso_doc *d, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t c);
gesso_status gesso_rect_draw(gesso_doc *d, int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                             uint8_t c, int filled);
gesso_status gesso_ellipse(gesso_doc *d, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t c,
                           int filled);
gesso_status gesso_fill(gesso_doc *d, int32_t x, int32_t y, uint8_t c);

/* Selection. The first move lifts the pixels and leaves index 0; lifting and
   dropping is one undo step, and undo while floating puts them back. */
gesso_status gesso_select(gesso_doc *d, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
gesso_status gesso_move_selection(gesso_doc *d, int32_t dx, int32_t dy);
gesso_status gesso_duplicate_selection(gesso_doc *d, int32_t dx, int32_t dy);
void gesso_selection_transparent(gesso_doc *d, int on);
gesso_status gesso_flip(gesso_doc *d, int vertical);
gesso_status gesso_rotate(gesso_doc *d);
void gesso_drop(gesso_doc *d);
gesso_status gesso_delete_selection(gesso_doc *d);
gesso_status gesso_copy(gesso_doc *d);
gesso_status gesso_cut(gesso_doc *d);
gesso_status gesso_paste(gesso_doc *d, int32_t x, int32_t y);
/* Puts w x h indices in the clipboard, for a paste from outside. */
gesso_status gesso_clip_set(gesso_doc *d, const uint8_t *px, int32_t w, int32_t h);

/* img (the file reader's, an import's) replaces the image: one undo step,
   unlike gesso_adopt. */
gesso_status gesso_replace(gesso_doc *d, const gesso_image *img);
/* The canvas becomes width x height, every frame anchored at the top left:
   what falls outside goes, what is new is index 0. One undo step. */
gesso_status gesso_resize(gesso_doc *d, int32_t width, int32_t height);

/* Frames, tags and palette. Frame edits drop a floating selection first. */
gesso_status gesso_frame_select(gesso_doc *d, uint32_t frame);
gesso_status gesso_frame_add(gesso_doc *d);
gesso_status gesso_frame_duplicate(gesso_doc *d);
gesso_status gesso_frame_delete(gesso_doc *d);
gesso_status gesso_frame_move(gesso_doc *d, uint32_t from, uint32_t to);
gesso_status gesso_frame_duration(gesso_doc *d, uint32_t frame, uint32_t ms);
/* The same on frames first..last, both included, as one undo step. Copies
   go after last and the active frame becomes the first copy; a move puts
   the first of them at to. */
gesso_status gesso_frames_duplicate(gesso_doc *d, uint32_t first, uint32_t last);
/* count frames of indices (width * height each) and their durations go in
   after the active frame, the first of them active: a paste of frames. */
gesso_status gesso_frames_insert(gesso_doc *d, const uint8_t *px, const uint32_t *duration,
                                 uint32_t count);
gesso_status gesso_frames_delete(gesso_doc *d, uint32_t first, uint32_t last);
gesso_status gesso_frames_move(gesso_doc *d, uint32_t first, uint32_t last, uint32_t to);
gesso_status gesso_frames_duration(gesso_doc *d, uint32_t first, uint32_t last, uint32_t ms);
gesso_status gesso_tag_set(gesso_doc *d, const char *name, uint32_t first, uint32_t last,
                           uint32_t direction);
gesso_status gesso_tag_delete(gesso_doc *d, const char *name);
/* index == ncolors appends a color. */
gesso_status gesso_color_set(gesso_doc *d, uint32_t index, uint32_t rgb);
/* Remove a color: its pixels take the nearest color left. Move one: pixels
   keep their colors, only the indices change. Both remap the clipboard. */
gesso_status gesso_color_delete(gesso_doc *d, uint32_t index);
/* count colors (0xRRGGBB) become the palette at 1..count. With match every
   pixel takes the nearest new color; without, pixels keep their index (the
   art recolored) and only those past count look for the nearest. */
gesso_status gesso_palette_load(gesso_doc *d, const uint32_t *colors, uint32_t count, int match);
gesso_status gesso_color_move(gesso_doc *d, uint32_t from, uint32_t to);

/* Name check shared with the file reader: 1 to 31 printable ASCII bytes,
   no quote or backslash. */
int gesso_tag_name_ok(const uint8_t *name, size_t len);

/* History. */
/* The image as it stands becomes the start: no undo, nothing unsaved. */
void gesso_forget(gesso_doc *d);
void gesso_begin(gesso_doc *d);
void gesso_end(gesso_doc *d);
/* Takes back what the open group did so far and keeps it open: a shape's
   preview is redrawn this way at every move of the drag. */
void gesso_revert(gesso_doc *d);
int gesso_undo(gesso_doc *d);
/* The step the image stands at: the newest undo group, or the floating
   selection's; 0 at the history's start. A new step is always larger than
   every one before it. */
uint32_t gesso_step(const gesso_doc *d);
int gesso_redo(gesso_doc *d);
void gesso_mark_saved(gesso_doc *d);
int gesso_dirty(const gesso_doc *d);

#endif
