package doc

import (
	"bytes"
	"errors"
	"fmt"
	"image"
	"image/color"
)

// MaxCoord bounds coordinates: a line is walked pixel by pixel, clipped or
// not, so an endpoint far off the canvas would cost time for nothing.
const MaxCoord = 4 * MaxSide

var ErrCoord = errors.New("coordinate out of range")

// Drawing replaces pixels, alpha included, the way a pencil does in Paint;
// blending happens only between layers. Anything outside the canvas is
// clipped, so a stroke may run past the edge.

func (d *Document) SetPixel(x, y int, c color.NRGBA) error {
	err := checkCoords(x, y)
	if err != nil {
		return err
	}
	d.edit(image.Rect(x, y, x+1, y+1), func(p *image.NRGBA) {
		p.SetNRGBA(x, y, c)
	})
	return nil
}

// Line runs through the pen's center: a thick line is centered on the
// pixels a thin one would light.
func (d *Document) Line(x0, y0, x1, y1 int, c color.NRGBA, pen Pen) error {
	err := checkCoords(x0, y0, x1, y1)
	if err != nil {
		return err
	}
	d.paint(lineShape(x0, y0, x1, y1), StyleOutline, c, c, pen)
	return nil
}

// Curve is the cubic Bézier from p0 to p3 pulled toward p1 and p2, Paint's
// curve tool: a line bent twice.
func (d *Document) Curve(p0, p1, p2, p3 image.Point, c color.NRGBA, pen Pen) error {
	err := checkCoords(p0.X, p0.Y, p1.X, p1.Y, p2.X, p2.Y, p3.X, p3.Y)
	if err != nil {
		return err
	}
	d.paint(curveShape(p0, p1, p2, p3), StyleOutline, c, c, pen)
	return nil
}

// Recolor is Paint's color eraser: along the line, under the pen, pixels of
// exactly the color from become to; every other pixel is left alone.
func (d *Document) Recolor(x0, y0, x1, y1 int, from, to color.NRGBA, pen Pen) error {
	err := checkCoords(x0, y0, x1, y1)
	if err != nil {
		return err
	}
	s := lineShape(x0, y0, x1, y1)
	lo, hi := pen.reach()
	bounds := image.Rectangle{Min: s.bounds.Min.Sub(image.Pt(lo, lo)), Max: s.bounds.Max.Add(image.Pt(hi, hi))}
	fp := pen.footprint()
	d.edit(bounds, func(p *image.NRGBA) {
		clip := bounds.Intersect(p.Rect)
		s.outline(func(x, y int) {
			for _, o := range fp {
				q := image.Pt(x+o.X, y+o.Y)
				if q.In(clip) && p.NRGBAAt(q.X, q.Y) == from {
					p.SetNRGBA(q.X, q.Y, to)
				}
			}
		})
	})
	return nil
}

// edit runs draw on the active layer and records the part of r inside the
// canvas for undo; draw must not touch pixels outside r.
func (d *Document) edit(r image.Rectangle, draw func(p *image.NRGBA)) {
	r = r.Intersect(d.Bounds())
	if r.Empty() {
		return
	}
	p := d.layers[d.active].Pix
	before := copyRect(p, r)
	draw(p)
	after := copyRect(p, r)
	// An edit that changed nothing, like filling a region with its own
	// color, is no step to undo.
	if bytes.Equal(before, after) {
		return
	}
	d.hist.push(entry{
		kind:   pixels,
		layer:  d.active,
		rect:   r,
		before: before,
		after:  after,
		size:   len(before) + len(after),
	})
}

func checkCoords(vs ...int) error {
	for _, v := range vs {
		if v < -MaxCoord || v > MaxCoord {
			return fmt.Errorf("%w: %d, want -%d to %d", ErrCoord, v, MaxCoord, MaxCoord)
		}
	}
	return nil
}

func abs(v int) int {
	if v < 0 {
		return -v
	}
	return v
}

func sign(v int) int {
	switch {
	case v > 0:
		return 1
	case v < 0:
		return -1
	}
	return 0
}
