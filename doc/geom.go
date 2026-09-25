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
		if style != StyleOutline && s.area != nil {
			s.area(func(xa, xb, y int) {
				for x := xa; x <= xb; x++ {
					setClipped(p, x, y, fill)
				}
			})
		}
		if style == StyleFill && s.area != nil {
			return
		}
		fp := pen.footprint()
		s.outline(func(x, y int) {
			for _, o := range fp {
				setClipped(p, x+o.X, y+o.Y, line)
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

func setClipped(p *image.NRGBA, x, y int, c color.NRGBA) {
	if (image.Point{x, y}).In(p.Rect) {
		p.SetNRGBA(x, y, c)
	}
}
