package main

import (
	"image"
	"image/color"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/vector"
)

var (
	canvasBack = color.RGBA{0x1e, 0x1e, 0x1e, 0xff}
	checkLight = color.RGBA{0xcc, 0xcc, 0xcc, 0xff}
	checkDark  = color.RGBA{0x99, 0x99, 0x99, 0xff}
	gridColor  = color.RGBA{0x00, 0x00, 0x00, 0x40}
	edgeColor  = color.RGBA{0x00, 0x00, 0x00, 0xff}
)

// checkerCell is the side of a transparency checker square in logical pixels.
const checkerCell = 8

// canvasView keeps the GPU copy of the flattened document and redraws only
// the region the editor reports as changed.
type canvasView struct {
	img     *ebiten.Image
	flat    *image.NRGBA
	buf     []byte
	checker *ebiten.Image
}

func (c *canvasView) sync(e *editor) {
	b := e.d.Bounds()
	r, full := e.takeChanged()
	if c.img == nil || c.img.Bounds() != b {
		if c.img != nil {
			c.img.Deallocate()
		}
		c.img = ebiten.NewImage(b.Dx(), b.Dy())
		c.flat = image.NewNRGBA(b)
		full = true
	}
	if full {
		r = b
	}
	if r.Empty() {
		return
	}
	e.d.FlattenInto(c.flat, r)
	c.buf = premultiply(c.buf[:0], c.flat, r)
	c.img.SubImage(r).(*ebiten.Image).WritePixels(c.buf)
}

// premultiply converts straight alpha, as the document keeps it, to the
// premultiplied bytes Ebitengine takes.
func premultiply(dst []byte, src *image.NRGBA, r image.Rectangle) []byte {
	for y := r.Min.Y; y < r.Max.Y; y++ {
		i := src.PixOffset(r.Min.X, y)
		p := src.Pix[i : i+r.Dx()*4]
		for j := 0; j < len(p); j += 4 {
			a := uint32(p[j+3])
			dst = append(dst,
				byte((uint32(p[j])*a+127)/255),   // #nosec G115 -- c*a/255 <= 255
				byte((uint32(p[j+1])*a+127)/255), // #nosec G115 -- c*a/255 <= 255
				byte((uint32(p[j+2])*a+127)/255), // #nosec G115 -- c*a/255 <= 255
				p[j+3])
		}
	}
	return dst
}

// resizeChecker rebuilds the checker for a canvas area of size; it is drawn
// in screen space so it stays still while the image pans over it.
func (c *canvasView) resizeChecker(size image.Point, scale float64) {
	if c.checker != nil && c.checker.Bounds().Size() == size {
		return
	}
	if c.checker != nil {
		c.checker.Deallocate()
	}
	if size.X < 1 || size.Y < 1 {
		c.checker = nil
		return
	}
	cell := max(int(checkerCell*scale), 1)
	pix := make([]byte, size.X*size.Y*4)
	for y := range size.Y {
		for x := range size.X {
			col := checkLight
			if (x/cell+y/cell)%2 == 1 {
				col = checkDark
			}
			i := (y*size.X + x) * 4
			pix[i], pix[i+1], pix[i+2], pix[i+3] = col.R, col.G, col.B, col.A
		}
	}
	c.checker = ebiten.NewImage(size.X, size.Y)
	c.checker.WritePixels(pix)
}

func (c *canvasView) draw(screen *ebiten.Image, area image.Rectangle, v *view, grid bool) {
	dst := screen.SubImage(area).(*ebiten.Image)
	dst.Fill(canvasBack)
	if c.img == nil {
		return
	}
	shown := v.toScreen(c.img.Bounds())
	vis := shown.Intersect(area)
	if vis.Empty() {
		return
	}
	if c.checker != nil {
		op := &ebiten.DrawImageOptions{}
		op.GeoM.Translate(float64(vis.Min.X), float64(vis.Min.Y))
		dst.DrawImage(c.checker.SubImage(vis.Sub(area.Min)).(*ebiten.Image), op)
	}
	op := &ebiten.DrawImageOptions{Filter: ebiten.FilterNearest}
	op.GeoM.Scale(v.zoom(), v.zoom())
	op.GeoM.Translate(float64(v.origin.X), float64(v.origin.Y))
	dst.DrawImage(c.img, op)

	if grid && v.zoom() >= gridFrom {
		drawGrid(dst, vis, v)
	}
	vector.StrokeRect(dst, float32(shown.Min.X)-0.5, float32(shown.Min.Y)-0.5,
		float32(shown.Dx())+1, float32(shown.Dy())+1, 1, edgeColor, false)
}

// drawGrid draws one line per image pixel boundary inside vis only, so the
// cost follows the visible area rather than the image size.
func drawGrid(dst *ebiten.Image, vis image.Rectangle, v *view) {
	z := v.zoom()
	first := v.toDoc(vis.Min)
	last := v.toDoc(vis.Max)
	for x := first.X + 1; x <= last.X; x++ {
		sx := float32(v.origin.X) + float32(float64(x)*z)
		vector.FillRect(dst, sx, float32(vis.Min.Y), 1, float32(vis.Dy()), gridColor, false)
	}
	for y := first.Y + 1; y <= last.Y; y++ {
		sy := float32(v.origin.Y) + float32(float64(y)*z)
		vector.FillRect(dst, float32(vis.Min.X), sy, float32(vis.Dx()), 1, gridColor, false)
	}
}
