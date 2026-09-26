package doc

import (
	"image"
	"image/color"
)

// selection is a rectangle of the active layer. Moving it lifts its pixels
// into float, which rides above the layer, shown by Flatten, until it is
// dropped back in. Lift and drop are one undo step.
type selection struct {
	rect  image.Rectangle // where it is now
	float *image.NRGBA    // the lifted pixels, bounds at rect; nil until moved
	layer int
}

// SetSelectionKey makes selections transparent, as Paint's option does: where
// the floating pixels are this color, or fully transparent, what is below
// shows through. nil makes them opaque, covering everything they are over.
func (d *Document) SetSelectionKey(key *color.NRGBA) {
	if key == nil {
		d.selKey = nil
		return
	}
	k := *key
	d.selKey = &k
}

// seeThrough reports whether a floating pixel lets the layer below show.
func (d *Document) seeThrough(px []byte) bool {
	if d.selKey == nil {
		return false
	}
	k := d.selKey
	return px[3] == 0 || px[0] == k.R && px[1] == k.G && px[2] == k.B && px[3] == k.A
}

// overlay lays the floating pixels of row y, from xa to xb, over dst, which
// holds that row starting at x0.
func (d *Document) overlay(dst []byte, f *image.NRGBA, x0, xa, xb, y int) {
	i := f.PixOffset(xa, y)
	src := f.Pix[i : i+(xb-xa)*4]
	out := dst[(xa-x0)*4 : (xb-x0)*4]
	if d.selKey == nil {
		copy(out, src)
		return
	}
	for j := 0; j+3 < len(src); j += 4 {
		if !d.seeThrough(src[j : j+4]) {
			copy(out[j:j+4], src[j:j+4])
		}
	}
}

// Select marks r, clipped to the canvas, dropping any selection first. An
// empty rectangle only deselects.
func (d *Document) Select(r image.Rectangle) {
	d.Drop()
	r = r.Canon().Intersect(d.Bounds())
	if r.Empty() {
		return
	}
	d.sel = &selection{rect: r, layer: d.active}
}

// Selection is where the selection is now, and whether there is one.
func (d *Document) Selection() (image.Rectangle, bool) {
	if d.sel == nil {
		return image.Rectangle{}, false
	}
	return d.sel.rect, true
}

// Floating reports whether the selection has been lifted off its layer.
func (d *Document) Floating() bool { return d.sel != nil && d.sel.float != nil }

// MoveSelection shifts the selection by (dx, dy). The first move lifts its
// pixels, leaving transparency behind, or with duplicate a copy of them.
func (d *Document) MoveSelection(dx, dy int, duplicate bool) error {
	s := d.sel
	if s == nil {
		return nil
	}
	moved := s.rect.Add(image.Pt(dx, dy))
	err := checkCoords(moved.Min.X, moved.Min.Y, moved.Max.X, moved.Max.Y)
	if err != nil {
		return err
	}
	if s.float == nil {
		d.lift(duplicate)
	}
	s.rect = moved
	s.float.Rect = moved
	return nil
}

func (d *Document) lift(duplicate bool) {
	s := d.sel
	p := d.layers[s.layer].Pix
	s.float = image.NewNRGBA(s.rect)
	putRect(s.float, s.rect, copyRect(p, s.rect))
	d.BeginGroup()
	if duplicate {
		return
	}
	d.edit(s.rect, func(p *image.NRGBA) {
		clearRect(p, s.rect)
	})
}

// Drop puts a floating selection back into its layer, where it is now, and
// deselects. The part outside the canvas is lost.
func (d *Document) Drop() {
	s := d.sel
	if s == nil {
		return
	}
	d.sel = nil
	if s.float == nil {
		return
	}
	r := s.rect.Intersect(d.Bounds())
	d.edit(r, func(p *image.NRGBA) {
		for y := r.Min.Y; y < r.Max.Y; y++ {
			i := p.PixOffset(r.Min.X, y)
			d.overlay(p.Pix[i:i+r.Dx()*4], s.float, r.Min.X, r.Min.X, r.Max.X, y)
		}
	})
	d.EndGroup()
}

// DeleteSelection clears the selected pixels (a floating selection is simply
// discarded) and deselects.
func (d *Document) DeleteSelection() {
	s := d.sel
	if s == nil {
		return
	}
	d.sel = nil
	if s.float != nil {
		d.EndGroup()
		return
	}
	d.edit(s.rect, func(p *image.NRGBA) {
		clearRect(p, s.rect)
	})
}

// cancelFloat undoes a lift: the pixels go back where they came from and the
// selection returns to its source rectangle.
func (d *Document) cancelFloat() bool {
	s := d.sel
	if s == nil || s.float == nil {
		return false
	}
	d.RevertGroup()
	d.hist.grouping = false
	d.sel = nil
	return true
}

// floatRow overwrites row y, xs from x0, of the active layer's pixels with the
// floating selection, when it covers them. Pixels are replaced, not blended,
// as drawing does.
func (d *Document) floatRow(dst []byte, layer, x0, y int) {
	s := d.sel
	if s == nil || s.float == nil || layer != s.layer || y < s.rect.Min.Y || y >= s.rect.Max.Y {
		return
	}
	xa := max(x0, s.rect.Min.X)
	xb := min(x0+len(dst)/4, s.rect.Max.X)
	if xa >= xb {
		return
	}
	d.overlay(dst, s.float, x0, xa, xb, y)
}

func clearRect(p *image.NRGBA, r image.Rectangle) {
	for y := r.Min.Y; y < r.Max.Y; y++ {
		i := p.PixOffset(r.Min.X, y)
		clear(p.Pix[i : i+r.Dx()*4])
	}
}

// SelectionImage copies the selected pixels, floating or not, from the
// selection's layer.
func (d *Document) SelectionImage() (*image.NRGBA, bool) {
	s := d.sel
	if s == nil {
		return nil, false
	}
	src := s.float
	if src == nil {
		src = d.layers[s.layer].Pix
	}
	out := image.NewNRGBA(image.Rectangle{Max: s.rect.Size()})
	putRect(out, out.Rect, copyRect(src, s.rect))
	return out, true
}

// Paste lays img on the active layer as a floating selection with its top
// left at at, as a duplicate would: dropping it is one undo step with the
// paste.
func (d *Document) Paste(img image.Image, at image.Point) error {
	b := img.Bounds()
	err := checkSize(b.Dx(), b.Dy(), 1)
	if err != nil {
		return err
	}
	r := image.Rectangle{Min: at, Max: at.Add(b.Size())}
	err = checkCoords(r.Min.X, r.Min.Y, r.Max.X, r.Max.Y)
	if err != nil {
		return err
	}
	d.Drop()
	f := image.NewNRGBA(r)
	n, fast := img.(*image.NRGBA)
	for y := range b.Dy() {
		if fast {
			i := n.PixOffset(b.Min.X, b.Min.Y+y)
			copy(f.Pix[y*f.Stride:], n.Pix[i:i+b.Dx()*4])
			continue
		}
		for x := range b.Dx() {
			f.SetNRGBA(r.Min.X+x, r.Min.Y+y, color.NRGBAModel.Convert(img.At(b.Min.X+x, b.Min.Y+y)).(color.NRGBA))
		}
	}
	d.BeginGroup()
	d.sel = &selection{rect: r, float: f, layer: d.active}
	return nil
}
