package doc

import (
	"image"
	"image/color"
)

// Flatten composites the layers bottom to top with straight-alpha "over".
// The formula and its rounding are part of the corpus contract: every engine
// must produce these exact bytes.
//
//	a   = sa*255 + da*(255-sa)
//	out.A = round(a / 255)
//	out.C = round((sc*sa*255 + dc*da*(255-sa)) / a), 0 when a == 0
//
// round(n/d) is (n + d/2) / d in integers.
func (d *Document) Flatten() *image.NRGBA {
	out := image.NewNRGBA(d.Bounds())
	d.FlattenInto(out, out.Rect)
	return out
}

// FlattenInto composites only r into dst, which covers at least r, so a view
// can refresh what one stroke touched instead of the whole canvas.
func (d *Document) FlattenInto(dst *image.NRGBA, r image.Rectangle) {
	r = r.Intersect(d.Bounds()).Intersect(dst.Rect)
	row := r.Dx() * 4
	var withFloat []byte
	for y := r.Min.Y; y < r.Max.Y; y++ {
		o := dst.PixOffset(r.Min.X, y)
		clear(dst.Pix[o : o+row])
		for li, l := range d.layers {
			i := l.Pix.PixOffset(r.Min.X, y)
			src := l.Pix.Pix[i : i+row]
			if d.sel != nil && d.sel.float != nil && li == d.sel.layer {
				withFloat = append(withFloat[:0], src...)
				d.floatRow(withFloat, li, r.Min.X, y)
				src = withFloat
			}
			over(dst.Pix[o:o+row], src)
		}
	}
}

// PixelAt is the composited color at (x, y), what an eyedropper picks.
func (d *Document) PixelAt(x, y int) color.NRGBA {
	out := image.NewNRGBA(image.Rect(x, y, x+1, y+1))
	d.FlattenInto(out, out.Rect)
	return out.NRGBAAt(x, y)
}

func over(dst, src []byte) {
	for i := 0; i+3 < len(dst) && i+3 < len(src); i += 4 {
		sa := uint32(src[i+3])
		if sa == 0 {
			continue
		}
		if sa == 255 {
			copy(dst[i:i+4], src[i:i+4])
			continue
		}
		da := uint32(dst[i+3])
		a := sa*255 + da*(255-sa)
		for c := range 3 {
			n := uint32(src[i+c])*sa*255 + uint32(dst[i+c])*da*(255-sa)
			dst[i+c] = byte((n + a/2) / a) // #nosec G115 -- a weighted mean of bytes, at most 255
		}
		dst[i+3] = byte((a + 127) / 255) // #nosec G115 -- a <= 255*255
	}
}
