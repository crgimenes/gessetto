package doc

import (
	"image"
	"image/color"
)

// Fill paints the 4-connected region of the active layer that holds the color
// found at (x, y), taking in neighbors whose every channel, alpha included,
// is within tolerance (0 to 255) of it. It reads the active layer only, as
// Paint's bucket does, not the composited image.
func (d *Document) Fill(x, y int, c color.NRGBA, tolerance int) {
	p := d.layers[d.active].Pix
	if !(image.Point{x, y}).In(p.Rect) {
		return
	}
	tolerance = min(max(tolerance, 0), 255)
	mask, box := floodMask(p, x, y, tolerance)
	d.edit(box, func(p *image.NRGBA) {
		w := p.Rect.Dx()
		for yy := box.Min.Y; yy < box.Max.Y; yy++ {
			for xx := box.Min.X; xx < box.Max.X; xx++ {
				if mask.has(yy*w + xx) {
					p.SetNRGBA(xx, yy, c)
				}
			}
		}
	})
}

type bitset []uint64

func (b bitset) has(i int) bool { return b[i>>6]&(1<<(i&63)) != 0 }
func (b bitset) set(i int)      { b[i>>6] |= 1 << (i & 63) }

// floodMask finds the region by scanline: each popped seed extends left and
// right along its row, then seeds the rows above and below. The stack holds
// at most a few entries per row span, never one per pixel.
func floodMask(p *image.NRGBA, x, y, tol int) (bitset, image.Rectangle) {
	w, h := p.Rect.Dx(), p.Rect.Dy()
	mask := make(bitset, (w*h+63)/64)
	i := p.PixOffset(x, y)
	target := [4]int{int(p.Pix[i]), int(p.Pix[i+1]), int(p.Pix[i+2]), int(p.Pix[i+3])}
	match := func(x, y int) bool {
		if mask.has(y*w + x) {
			return false
		}
		o := p.PixOffset(x, y)
		for k := range 4 {
			v := int(p.Pix[o+k]) - target[k]
			if v > tol || -v > tol {
				return false
			}
		}
		return true
	}
	box := image.Rectangle{Min: image.Pt(x, y), Max: image.Pt(x+1, y+1)}
	stack := []image.Point{{x, y}}
	for len(stack) > 0 {
		s := stack[len(stack)-1]
		stack = stack[:len(stack)-1]
		if !match(s.X, s.Y) {
			continue
		}
		l, r := s.X, s.X
		for l > 0 && match(l-1, s.Y) {
			l--
		}
		for r < w-1 && match(r+1, s.Y) {
			r++
		}
		for xx := l; xx <= r; xx++ {
			mask.set(s.Y*w + xx)
		}
		box = box.Union(image.Rect(l, s.Y, r+1, s.Y+1))
		for _, ny := range [2]int{s.Y - 1, s.Y + 1} {
			if ny < 0 || ny >= h {
				continue
			}
			// One seed per run of matching pixels on the neighbor row.
			inRun := false
			for xx := l; xx <= r; xx++ {
				m := match(xx, ny)
				if m && !inRun {
					stack = append(stack, image.Pt(xx, ny))
				}
				inRun = m
			}
		}
	}
	return mask, box
}
