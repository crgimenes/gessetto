# Corpus

Each directory is one case of the gesso engine. A case holds:

- `ops.filo`: the script, run with the builtins below and Filo's core.
- `want.gesso`: the image when the script ends (a selection still floating is
  dropped first), as `gesso_write` writes it, compared byte for byte. The
  runner also reads `want.gesso` back and writes it again, which must give
  the same bytes. Or instead:
- `error.txt`: the script must fail, and the error must contain this text.

`make corpus` runs every case under the sanitizers. To add one, write
`ops.filo`, run `./build/corpus -update testdata/corpus/<case>/`, and check
the rows of `want.gesso` against values worked out by hand: a golden that
only repeats the engine proves nothing.

## The file

A `.gesso` file is Filo that only describes an image, in this order:

| Form | Meaning |
|---|---|
| `(gesso 1 w h)` | version 1, width and height from 1 to 256 |
| `(palette "#rrggbb" ...)` | colors for indices 1 and up, at most 255; index 0 is transparent |
| `(frame ms "row" ...)` | one frame: duration 1 to 60000 ms, then one string per row, two hex digits per pixel, each index below the palette's size; at most 256 frames |
| `(tag "name" first last ["direction"])` | frames first..last, both included; name 1 to 31 printable ASCII bytes, no `"` or `\`, unique; direction `"forward"` (the default, never written), `"reverse"` or `"pingpong"` |

Hex digits may be either case; the writer uses lowercase and puts the index
of each palette color in a comment. A file runs with only these four and the
core builtins, a step limit, and no globals of its own.

## Builtins of an ops script

The four above build an image (an ops script may start with them), or:

| Call | Effect |
|---|---|
| `(new w h)` | a blank w x h image, one frame of 100 ms, the PICO-8 colors at indices 1 to 16 |
| `(brush size ["square"\|"round"])` | the pen for what follows (1 to 16, square by default): lines, pixels and outlines stamp it at every point, fills ignore it; an even size reaches one further right and down; round keeps the cells whose centers fall within the circle of that diameter |
| `(pixel x y c)` | set one pixel of the active frame |
| `(line x0 y0 x1 y1 c)` | Bresenham line, both ends included |
| `(rect x0 y0 x1 y1 c)` / `(rect-fill ...)` | outline / area of the rectangle with those corners, both included |
| `(ellipse x0 y0 x1 y1 c)` / `(ellipse-fill ...)` | outline / area of the ellipse inscribed in that rectangle |
| `(fill x y c)` | 4-connected fill of the pixels with the index at (x, y) |
| `(select x0 y0 x1 y1)` | select that rectangle, clipped to the image; drops any selection first |
| `(move-selection dx dy)` | the first move lifts the pixels, leaving index 0; later moves shift them |
| `(duplicate-selection dx dy)` | as move, but the first move leaves the pixels in place |
| `(selection-transparent b)` | `#t`: index 0 of the floating pixels shows what is under them, on screen and on drop |
| `(flip "h")` / `(flip "v")` / `(rotate)` | flip or turn clockwise the floating pixels, lifting the selection first, or the whole frame when nothing is selected; rotation keeps the middle, `(w-h)/2` and `(h-w)/2` in C division |
| `(drop)` | put the floating pixels into the frame and deselect |
| `(delete-selection)` | clear the selected pixels (or discard the floating ones) and deselect |
| `(copy)` / `(cut)` / `(paste x y)` | the script's clipboard; pasted pixels float with their top left at (x, y) |
| `(resize w h)` | every frame becomes w x h, anchored at the top left: what falls outside goes, what is new is index 0 |
| `(frame-add)` / `(frame-duplicate)` | a blank / copied frame after the active one, which it becomes, with its duration |
| `(frame-delete)` | delete the active frame; tags over it shrink, a tag of only that frame goes |
| `(frame-move from to)` / `(frame-select i)` | move a frame (tags keep their numbers) / make frame i active |
| `(duration i ms)` | the duration of frame i |
| `(frames-duplicate a b)` | copies of frames a..b right after b; the first copy becomes active |
| `(frames-delete a b)` | frames a..b go; a tag wholly inside them goes, the others close the gap |
| `(frames-move a b to)` | frames a..b move so a lands at index to; tags keep their numbers |
| `(frames-duration a b ms)` | the duration of frames a..b |
| `(tag "name" first last)` / `(untag "name")` | set or replace / delete a tag |
| `(color i "#rrggbb")` | set palette entry i, 1 up to the palette's size, which appends |
| `(color-delete i)` | color i goes; its pixels take the nearest color left (index 0 when none), the indices above it come down by one |
| `(color-move from to)` | color from moves to index to, the ones between shift over; pixels keep their colors |
| `(undo)` / `(redo)` | `#t` if something was undone / redone |

Colors are indices; coordinates and sizes are integers, a fraction is an
error. Coordinates beyond 32768 either way are an error. Drawing clips at the
edge of the image.

Every edit is one undo step; an edit that changes nothing is none. Any edit
after an undo discards the redo steps. Lifting a selection and dropping it is
one step, and undo while it floats puts the pixels back. Drawing, frame,
tag, palette edits and redo drop a floating selection first.

## Rules every engine follows

Line:

    dx = |x1-x0|, dy = -|y1-y0|, sx = sign(x1-x0), sy = sign(y1-y0), e = dx+dy
    loop: plot (x,y); stop at (x1,y1)
          e2 = 2e
          if e2 >= dy: e += dy, x += sx
          if e2 <= dx: e += dx, y += sy

Ellipse in the box (x0, y0)-(x1, y1), x0 <= x1 and y0 <= y1, after Alois
Zingl's plotEllipseRect, in 64-bit integers. `span(xa, xb, y)` plots xa and xb
for an outline, every x from xa to xb for a filled ellipse:

    a = x1-x0, b = y1-y0, b1 = b mod 2
    dx = 4(1-a)b², dy = 4(b1+1)a², err = dx+dy+b1·a²
    yb = y0 + (b+1)/2, yt = yb - b1
    while x0 <= x1:
        span(x0, x1, yb); span(x0, x1, yt)
        e2 = 2err
        if e2 <= dy: yb++, yt--, dy += 8a², err += dy
        if e2 >= dx or 2err > dy: x0++, x1--, dx += 8b², err += dx
    while yb-yt <= b:
        span(x0-1, x1+1, yb); span(x0-1, x1+1, yt); yb++, yt--

These are the Go engine's rules (`../../testdata/corpus/README.md`): the
cases `pixel-basic`, `line-*`, `rect`, `ellipse`, `selection`,
`selection-undo` and `undo-redo` give the same pixels as the Go engine's
goldens, each color mapped to its index.
