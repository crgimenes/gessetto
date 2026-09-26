package doc

import (
	"image"
	"image/color"
)

// shape is geometry without color: the pixels of its outline and the rows it
// covers. Painting is a separate step, so width, fill styles and new shapes
// each change one side only.
type shape struct {
	bounds  image.Rectangle // every pixel the shape can touch
	outline func(plot func(x, y int))
	// area walks the rows the shape covers, outline included; nil for a
	// shape with no inside, like a line.
	area func(span func(xa, xb, y int))
}

// lineShape is Bresenham's, endpoints included: integer-only so every engine
// lights the same pixels.
func lineShape(x0, y0, x1, y1 int) shape {
	r := image.Rect(x0, y0, x1, y1).Canon()
	r.Max = r.Max.Add(image.Pt(1, 1))
	return shape{
		bounds: r,
		outline: func(plot func(x, y int)) {
			dx := abs(x1 - x0)
			dy := -abs(y1 - y0)
			sx := sign(x1 - x0)
			sy := sign(y1 - y0)
			e := dx + dy
			x, y := x0, y0
			for {
				plot(x, y)
				if x == x1 && y == y1 {
					return
				}
				e2 := 2 * e
				if e2 >= dy {
					e += dy
					x += sx
				}
				if e2 <= dx {
					e += dx
					y += sy
				}
			}
		},
	}
}

// rectShape is the rectangle with corners (x0, y0) and (x1, y1), both
// included.
func rectShape(x0, y0, x1, y1 int) shape {
	r := image.Rect(x0, y0, x1, y1).Canon()
	r.Max = r.Max.Add(image.Pt(1, 1))
	return shape{
		bounds: r,
		outline: func(plot func(x, y int)) {
			for y := r.Min.Y; y < r.Max.Y; y++ {
				edge := y == r.Min.Y || y == r.Max.Y-1
				for x := r.Min.X; x < r.Max.X; x++ {
					if edge || x == r.Min.X || x == r.Max.X-1 {
						plot(x, y)
					}
				}
			}
		},
		area: func(span func(xa, xb, y int)) {
			for y := r.Min.Y; y < r.Max.Y; y++ {
				span(r.Min.X, r.Max.X-1, y)
			}
		},
	}
}

// ellipseShape is the ellipse inscribed in the rectangle (x0, y0)-(x1, y1).
func ellipseShape(x0, y0, x1, y1 int) shape {
	box := image.Rect(x0, y0, x1, y1).Canon()
	walk := func(span func(xa, xb, y int)) {
		ellipseRect(box.Min.X, box.Min.Y, box.Max.X, box.Max.Y, span)
	}
	r := box
	r.Max = r.Max.Add(image.Pt(1, 1))
	return shape{
		bounds: r,
		outline: func(plot func(x, y int)) {
			walk(func(xa, xb, y int) {
				plot(xa, y)
				plot(xb, y)
			})
		},
		area: walk,
	}
}

// MaxWidth caps the pen; the footprint is stamped at every outline pixel.
const MaxWidth = 64

// Pen is how outlines are drawn: Width pixels across (0 counts as 1), with a
// square or round tip.
type Pen struct {
	Width int
	Round bool
}

func (p Pen) width() int { return min(max(p.Width, 1), MaxWidth) }

// footprint lists the offsets a pen covers around a point. Pixel i of w sits
// at 2i-(w-1) in doubled coordinates; a round tip keeps the offsets with
// di²+dj² <= w²-w, all in integers.
func (p Pen) footprint() []image.Point {
	w := p.width()
	lo := -(w - 1) / 2
	var out []image.Point
	for j := range w {
		for i := range w {
			di, dj := 2*i-(w-1), 2*j-(w-1)
			if p.Round && di*di+dj*dj > w*w-w {
				continue
			}
			out = append(out, image.Pt(lo+i, lo+j))
		}
	}
	return out
}

// reach is how far a footprint extends up-left (lo) and down-right (hi) of
// its center.
func (p Pen) reach() (lo, hi int) {
	w := p.width()
	return (w - 1) / 2, w / 2
}

// Style is how a closed shape is painted, as in Paint: its outline in one
// color, its area in another, or both, the outline on top.
type Style int

const (
	StyleOutline Style = iota
	StyleBoth
	StyleFill
)

// paint draws s on the active layer as one edit: the area in fill, then the
// outline with the pen in line, as the style asks. Pixels are replaced, not
// blended, and clipped.
func (d *Document) paint(s shape, style Style, line, fill color.NRGBA, pen Pen) {
	lo, hi := pen.reach()
	bounds := image.Rectangle{Min: s.bounds.Min.Sub(image.Pt(lo, lo)), Max: s.bounds.Max.Add(image.Pt(hi, hi))}
	d.edit(bounds, func(p *image.NRGBA) {
		// Clipped to the edit's own bounds, not the canvas: a pixel outside
		// them would change without undo knowing, so a shape with wrong
		// bounds shows as missing pixels instead.
		clip := bounds.Intersect(p.Rect)
		set := func(x, y int, c color.NRGBA) {
			if (image.Point{x, y}).In(clip) {
				p.SetNRGBA(x, y, c)
			}
		}
		if style != StyleOutline && s.area != nil {
			s.area(func(xa, xb, y int) {
				for x := xa; x <= xb; x++ {
					set(x, y, fill)
				}
			})
		}
		if style == StyleFill && s.area != nil {
			return
		}
		fp := pen.footprint()
		s.outline(func(x, y int) {
			for _, o := range fp {
				set(x+o.X, y+o.Y, line)
			}
		})
	})
}

// insetForPen shrinks a box so a pen centered on its edge stays inside it:
// thick outlines grow inward from the dragged box, as in Paint. A box
// thinner than the pen collapses to its middle.
func insetForPen(x0, y0, x1, y1 int, pen Pen) (int, int, int, int) {
	r := image.Rect(x0, y0, x1, y1).Canon()
	lo, hi := pen.reach()
	ax, bx := r.Min.X+lo, r.Max.X-hi
	ay, by := r.Min.Y+lo, r.Max.Y-hi
	if ax > bx {
		ax = (r.Min.X + r.Max.X) / 2
		bx = ax
	}
	if ay > by {
		ay = (r.Min.Y + r.Max.Y) / 2
		by = ay
	}
	return ax, ay, bx, by
}

// maxCurveSegments caps how finely a curve is cut into lines.
const maxCurveSegments = 256

// curveShape is the cubic Bézier from p0 to p3 with controls p1 and p2, cut
// into n straight segments joined by Bresenham lines. Every point is an exact
// fraction over n³ rounded half up, so every engine lands on the same pixels.
func curveShape(p0, p1, p2, p3 image.Point) shape {
	legs := chebyshev(p0, p1) + chebyshev(p1, p2) + chebyshev(p2, p3)
	n := int64(min(max(legs/2, 1), maxCurveSegments))
	at := func(i int64) image.Point {
		u := n - i
		w0, w1, w2, w3 := u*u*u, 3*u*u*i, 3*u*i*i, i*i*i
		den := n * n * n
		x := w0*int64(p0.X) + w1*int64(p1.X) + w2*int64(p2.X) + w3*int64(p3.X)
		y := w0*int64(p0.Y) + w1*int64(p1.Y) + w2*int64(p2.Y) + w3*int64(p3.Y)
		return image.Pt(int(roundDiv(x, den)), int(roundDiv(y, den)))
	}
	return shape{
		bounds: Cover(p0, p1, p2, p3),
		outline: func(plot func(x, y int)) {
			prev := p0
			for i := int64(1); i <= n; i++ {
				next := at(i)
				lineShape(prev.X, prev.Y, next.X, next.Y).outline(plot)
				prev = next
			}
		},
	}
}

// Cover is the smallest rectangle holding every point, each as a pixel. It
// does not start from an empty rectangle: Union ignores empty ones, so a
// rectangle grown from points by Union stays one pixel.
func Cover(pts ...image.Point) image.Rectangle {
	r := image.Rectangle{Min: pts[0], Max: pts[0].Add(image.Pt(1, 1))}
	for _, q := range pts[1:] {
		r.Min.X, r.Min.Y = min(r.Min.X, q.X), min(r.Min.Y, q.Y)
		r.Max.X, r.Max.Y = max(r.Max.X, q.X+1), max(r.Max.Y, q.Y+1)
	}
	return r
}

func chebyshev(a, b image.Point) int { return max(abs(a.X-b.X), abs(a.Y-b.Y)) }

// roundDiv is n/d rounded half up, for d > 0 and n of either sign.
func roundDiv(n, d int64) int64 {
	q := 2*n + d
	r := q / (2 * d)
	if q%(2*d) < 0 {
		r--
	}
	return r
}
