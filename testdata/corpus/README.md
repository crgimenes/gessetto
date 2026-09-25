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
