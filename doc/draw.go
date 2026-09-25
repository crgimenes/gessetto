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
