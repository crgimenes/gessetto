package doc

import "image/color"

// Rect draws the rectangle whose opposite corners are (x0, y0) and (x1, y1),
// both included: outline in line, area in fill, as style says.
func (d *Document) Rect(x0, y0, x1, y1 int, style Style, line, fill color.NRGBA, pen Pen) error {
	return d.boxShape(rectShape, x0, y0, x1, y1, style, line, fill, pen)
}

// Ellipse draws the ellipse inscribed in the rectangle (x0, y0)-(x1, y1).
// The algorithm is Alois Zingl's plotEllipseRect ("A Rasterizing Algorithm
// for Drawing Curves", 2012), all in integers, which handles even diameters
// without a lopsided pixel.
func (d *Document) Ellipse(x0, y0, x1, y1 int, style Style, line, fill color.NRGBA, pen Pen) error {
	return d.boxShape(ellipseShape, x0, y0, x1, y1, style, line, fill, pen)
}

func (d *Document) boxShape(geom func(x0, y0, x1, y1 int) shape, x0, y0, x1, y1 int, style Style, line, fill color.NRGBA, pen Pen) error {
	err := checkCoords(x0, y0, x1, y1)
	if err != nil {
		return err
	}
	if style == StyleFill {
		pen = Pen{}
	}
	d.paint(geom(insetForPen(x0, y0, x1, y1, pen)), style, line, fill, pen)
	return nil
}

// ellipseRect walks the ellipse in (x0, y0)-(x1, y1), with x0 <= x1 and
// y0 <= y1, calling span with the left and right x of each row it reaches.
func ellipseRect(x0, y0, x1, y1 int, span func(xa, xb, y int)) {
	a := int64(x1 - x0)
	b := int64(y1 - y0)
	b1 := b & 1
	dx := 4 * (1 - a) * b * b
	dy := 4 * (b1 + 1) * a * a
	err := dx + dy + b1*a*a
	yb := y0 + int((b+1)/2)
	yt := yb - int(b1)
	a8 := 8 * a * a
	b8 := 8 * b * b
	for x0 <= x1 {
		span(x0, x1, yb)
		span(x0, x1, yt)
		e2 := 2 * err
		if e2 <= dy {
			yb++
			yt--
			dy += a8
			err += dy
		}
		if e2 >= dx || 2*err > dy {
			x0++
			x1--
			dx += b8
			err += dx
		}
	}
	// A thin ellipse (a <= 1) leaves the loop before its tips; finish them.
	for int64(yb-yt) <= b {
		span(x0-1, x1+1, yb)
		span(x0-1, x1+1, yt)
		yb++
		yt--
	}
}
