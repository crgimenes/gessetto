# Corpus

Each directory is one case. Every gessetto engine runs every case and must
produce the same bytes: this is the parity oracle between the Go engine and
any other one.

A case holds:

- `ops.filo`: the script. It runs with the `doc-*` builtins below and nothing
  else of the host.
- `in.png` (optional): the starting document, one layer. Without it the
  script must call `doc-new`.
- `want.png`: the flattened result, compared pixel by pixel as straight-alpha
  8-bit RGBA. Or instead:
- `error.txt`: the script must fail, and the error message must contain this
  text.

## Builtins

| Call | Effect |
|---|---|
| `(doc-new w h)` | new document, one transparent layer "Background"; fails if one is open |
| `(doc-pixel x y color)` | replace one pixel of the active layer |
| `(doc-pen width tip)` | outlines drawn after it use this pen: width 1 to 64, tip `"square"` or `"round"`; starts as 1 `"square"` |
| `(doc-line x0 y0 x1 y1 color)` | Bresenham line, endpoints included, replacing pixels |
| `(doc-rect x0 y0 x1 y1 color)` / `(doc-fill-rect ...)` | outline / area of the rectangle with those opposite corners, both included |
| `(doc-ellipse x0 y0 x1 y1 color)` / `(doc-fill-ellipse ...)` | outline / area of the ellipse inscribed in that rectangle |
| `(doc-rect-both x0 y0 x1 y1 outline fill)` / `(doc-ellipse-both ...)` | area in fill, then outline in outline on top |
| `(doc-curve x0 y0 cx1 cy1 cx2 cy2 x1 y1 color)` | cubic Bézier from (x0, y0) to (x1, y1) with those controls, drawn with the pen |
| `(doc-recolor x0 y0 x1 y1 from to)` | color eraser: along the line, under the pen, pixels exactly from become to |
| `(doc-fill x y color tolerance)` | 4-connected bucket fill of the active layer, tolerance 0 to 255 |
| `(doc-select x0 y0 x1 y1)` | select that rectangle of the active layer, corners included, clipped to the canvas; drops any selection first |
| `(doc-move-selection dx dy)` | the first move lifts the pixels, leaving transparency; later moves shift the floating pixels |
| `(doc-duplicate-selection dx dy)` | as move, but the first move leaves the pixels in place |
| `(doc-selection-mode "opaque")` / `(doc-selection-mode "transparent" color)` | opaque floating pixels cover what they are over; transparent ones let it show where they are the key color or have alpha 0 |
| `(doc-copy)` / `(doc-cut)` | copy the selected pixels to the script's clipboard (cut also clears them and deselects) |
| `(doc-paste x y)` | the copied pixels float on the active layer with their top left at (x, y), as a duplicate would |
| `(doc-drop)` | put the floating pixels into their layer, replacing what is there, and deselect |
| `(doc-delete-selection)` | clear the selected pixels (or discard the floating ones) and deselect |
| `(doc-add-layer name)` | transparent layer above the active one, which it becomes |
| `(doc-select-layer i)` | make layer `i` (0 is the bottom) active |
| `(doc-undo)` / `(doc-redo)` | `#t` if something was undone/redone, `#f` if not |

Colors are `"#rrggbb"` (opaque) or `"#rrggbbaa"`. Coordinates and sizes are
integers; a fraction is an error, not a position to round. Drawing replaces
pixels, alpha included, and clips at the canvas edge. Coordinates beyond
32768 in either direction are an error.

Each edit is one undo step. A new edit after an undo discards the redo steps.
Lifting a selection and dropping it is one step; undo while it floats puts
the pixels back where they came from. Adding or selecting a layer and redo
drop a floating selection first. A selection still floating when the script
ends shows in the flattened picture over its layer: its pixels replace the
layer's there, transparent ones included.

## Rules every engine must follow

Line, as in `doc/draw.go`:

    dx = |x1-x0|, dy = -|y1-y0|, sx = sign(x1-x0), sy = sign(y1-y0), e = dx+dy
    loop: plot (x,y); stop at (x1,y1)
          e2 = 2e
          if e2 >= dy: e += dy, x += sx
          if e2 <= dx: e += dx, y += sy

Pen: the footprint of width w covers offsets lo..lo+w-1 on each axis, with
lo = -((w-1)/2). A round tip keeps the offset (i, j), i and j from 0 to w-1,
only when di²+dj² <= w²-w, where di = 2i-(w-1) and dj = 2j-(w-1). Every
outline pixel stamps the footprint. A line is centered on its Bresenham
pixels; a rectangle or ellipse is drawn on its box inset by (w-1)/2 at the
top-left and w/2 at the bottom-right, so the thick outline grows inward. A
box thinner than the pen collapses to its middle. Filled shapes ignore the
pen. A shape with both paints its whole area first (the rows the thin shape
covers, outline included) and then stamps the outline over it.

Ellipse in the box (x0, y0)-(x1, y1), x0 <= x1 and y0 <= y1, after Alois
Zingl's plotEllipseRect, in 64-bit integers. `span(xa, xb, y)` plots xa and xb
for an outline, every x from xa to xb for a filled ellipse:

    a = x1-x0, b = y1-y0, b1 = b & 1
    dx = 4(1-a)b², dy = 4(b1+1)a², err = dx+dy+b1·a²
    yb = y0 + (b+1)/2, yt = yb - b1
    while x0 <= x1:
        span(x0, x1, yb); span(x0, x1, yt)
        e2 = 2err
        if e2 <= dy: yb++, yt--, dy += 8a², err += dy
        if e2 >= dx or 2err > dy: x0++, x1--, dx += 8b², err += dx
    while yb-yt <= b:
        span(x0-1, x1+1, yb); span(x0-1, x1+1, yt); yb++, yt--

Curve: legs = the sum of max(|dx|, |dy|) over the three legs of the control
polygon; n = clamp(legs/2, 1, 256) segments. Point i of n, with u = n-i, is
(p0·u³ + 3·p1·u²·i + 3·p2·u·i² + p3·i³) / n³ per axis, in 64-bit integers
rounded half up (for negative sums too: floor((2·num + n³) / (2·n³))). Each
segment is a line from the previous point to this one, the first starting at
p0; every pixel of those lines stamps the pen.

Fill takes the color at (x, y) on the active layer and spreads to the four
neighbors whose R, G, B and A each differ from it by at most the tolerance.
An edit that changes no pixel is not an undo step.

Flatten, bottom layer first, over a transparent canvas, in integers:

    a     = sa*255 + da*(255-sa)
    out.A = round(a / 255)
    out.C = round((sc*sa*255 + dc*da*(255-sa)) / a), 0 when a == 0
    round(n/d) = (n + d/2) / d, integer division

## Adding a case

Write `ops.filo` (and `in.png` if needed), then generate `want.png` with
`go test ./ops -run TestCorpus/<case> -update`. The file comes from the Go
engine, so check its pixels against values worked out by hand before
keeping it: a golden that only repeats the engine proves nothing.
