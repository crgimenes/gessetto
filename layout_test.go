package main

import (
	"image"
	"testing"
)

func TestLayoutTilesTheScreen(t *testing.T) {
	for _, tt := range []struct {
		w, h      int
		scale     float64
		showRight bool
	}{
		{1280, 800, 1, true},
		{2560, 1600, 2, true},
		{390, 700, 1, false},
		{780, 1400, 2, false},
	} {
		l := layoutFor(tt.w, tt.h, tt.scale)
		if l.showRight != tt.showRight {
			t.Errorf("%dx%d@%v: showRight %v, want %v", tt.w, tt.h, tt.scale, l.showRight, tt.showRight)
		}
		parts := []image.Rectangle{l.top, l.bottom, l.left, l.canvas}
		if l.showRight {
			parts = append(parts, l.right)
		}
		area := 0
		for i, a := range parts {
			area += a.Dx() * a.Dy()
			for _, b := range parts[i+1:] {
				if a.Overlaps(b) {
					t.Errorf("%dx%d@%v: %v overlaps %v", tt.w, tt.h, tt.scale, a, b)
				}
			}
		}
		if area != tt.w*tt.h {
			t.Errorf("%dx%d@%v: panels cover %d pixels, want %d", tt.w, tt.h, tt.scale, area, tt.w*tt.h)
		}
		if l.canvas.Empty() {
			t.Errorf("%dx%d@%v: no room left for the canvas", tt.w, tt.h, tt.scale)
		}
	}
}
