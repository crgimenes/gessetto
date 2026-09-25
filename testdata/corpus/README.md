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
| `(doc-line x0 y0 x1 y1 color)` | Bresenham line, endpoints included, replacing pixels |
| `(doc-rect x0 y0 x1 y1 color)` / `(doc-fill-rect ...)` | outline / area of the rectangle with those opposite corners, both included |
| `(doc-ellipse x0 y0 x1 y1 color)` / `(doc-fill-ellipse ...)` | outline / area of the ellipse inscribed in that rectangle |
| `(doc-fill x y color tolerance)` | 4-connected bucket fill of the active layer, tolerance 0 to 255 |
| `(doc-add-layer name)` | transparent layer above the active one, which it becomes |
| `(doc-select-layer i)` | make layer `i` (0 is the bottom) active |
| `(doc-undo)` / `(doc-redo)` | `#t` if something was undone/redone, `#f` if not |

Colors are `"#rrggbb"` (opaque) or `"#rrggbbaa"`. Coordinates and sizes are
integers; a fraction is an error, not a position to round. Drawing replaces
pixels, alpha included, and clips at the canvas edge. Coordinates beyond
32768 in either direction are an error.

Each edit is one undo step. A new edit after an undo discards the redo steps.

## Rules every engine must follow

Line, as in `doc/draw.go`:

    dx = |x1-x0|, dy = -|y1-y0|, sx = sign(x1-x0), sy = sign(y1-y0), e = dx+dy
    loop: plot (x,y); stop at (x1,y1)
          e2 = 2e
          if e2 >= dy: e += dy, x += sx
          if e2 <= dx: e += dx, y += sy

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
