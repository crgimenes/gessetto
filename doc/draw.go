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

// Line is Bresenham's, endpoints included: integer-only so every engine
// lights the same pixels.
func (d *Document) Line(x0, y0, x1, y1 int, c color.NRGBA) error {
	err := checkCoords(x0, y0, x1, y1)
	if err != nil {
		return err
	}
	r := image.Rect(x0, y0, x1, y1).Canon()
	r.Max = r.Max.Add(image.Pt(1, 1))
	d.edit(r, func(p *image.NRGBA) {
		dx := abs(x1 - x0)
		dy := -abs(y1 - y0)
		sx := sign(x1 - x0)
		sy := sign(y1 - y0)
		e := dx + dy
		x, y := x0, y0
		for {
			if (image.Point{x, y}).In(p.Rect) {
				p.SetNRGBA(x, y, c)
			}
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
