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
| `(line x0 y0 x1 y1 c)` | both ends included, measured in pixels covered (n along the long side, m across): pixel i < n/2 takes row ⌊(2i+1)m / 2n⌋, the rest the first half turned about the middle; drawn from the end nearer the origin along the long side, so either order gives the same pixels; polygon edges are these lines too |
| `(rect x0 y0 x1 y1 c)` / `(rect-fill ...)` | outline / area of the rectangle with those corners, both included |
| `(curve x0 y0 cx cy x1 y1 c)` | quadratic Bezier from (x0, y0) to (x1, y1) bent toward (cx, cy): 2·max(Σ|dx|, Σ|dy|)+1 steps, each point ((n−i)²p0 + 2i(n−i)c + i²p1)/n² rounded to nearest, joined by Bresenham, then made pixel-perfect from each end toward the middle: a pixel whose last kept neighbour and next one are diagonal neighbours (the corner of an L) is dropped, so a symmetric curve stays symmetric; stamped with the pen |
| `(spray x y r count seed c)` | count dots inside the circle of radius r around (x, y), placed by xorshift32 from seed (0 counts as 1): x offset from the step's value modulo 2r+1, y from its quotient; a point outside the circle is drawn again, up to 4·count+4 tries. Its golden pins the generator; that the dots stay in the circle and repeat with the seed is checked in `gessetto_test.c` |
| `(rect-round x0 y0 x1 y1 r c)` / `(rect-round-fill ...)` | a rectangle with corners rounded to radius r (at most half the shorter side; 0 is square): each corner is a quarter of the midpoint circle of radius r, and the fill takes the same reach on every row as the outline |
| `(ellipse x0 y0 x1 y1 c)` / `(ellipse-fill ...)` | outline / area of the ellipse inscribed in that rectangle |
| `(fill x y c)` | 4-connected fill of the pixels with the index at (x, y) |
| `(select x0 y0 x1 y1)` | select that rectangle, clipped to the image; drops any selection first |
| `(text x y "s" c)` | s in the built-in 5x9 font, color c, floating at (x, y): 6 pixels a character, 10 a line (`\n` starts one); only the glyphs land; anything but printable ASCII is one `?` per UTF-8 character |
| `(lasso x0 y0 x1 y1 ...)` | select what the path through those points, closed back to the first, encloses, the path included; points off the image are pulled to its edge; moves, flips, rotations, copies and deletes then touch only those pixels, and a copy leaves the rest index 0 |
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
