package main

import (
	"image"
	"testing"
)

func TestLayoutTilesTheScreen(t *testing.T) {
	for _, tt := range []struct {
		w, h  int
		scale float64
		mode  panelMode
	}{
		{1280, 800, 1, panelDocked},
		{2560, 1600, 2, panelDocked},
		{1280, 800, 1, panelHidden},
		{390, 700, 1, panelHidden},
		{780, 1400, 2, panelHidden},
	} {
		l := layoutFor(tt.w, tt.h, tt.scale, tt.mode)
		parts := []image.Rectangle{l.top, l.bottom, l.left, l.canvas}
		if tt.mode == panelDocked {
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

func TestOverlayKeepsTheCanvas(t *testing.T) {
	l := layoutFor(390, 700, 1, panelOverlay)
	if l.right.Empty() || !l.right.Overlaps(l.canvas) {
		t.Fatalf("overlay panel %v must lie over the canvas %v", l.right, l.canvas)
	}
	if l.canvas != layoutFor(390, 700, 1, panelHidden).canvas {
		t.Fatal("an overlay must not shrink the canvas")
	}
}

func TestNarrowThreshold(t *testing.T) {
	if !narrow(719, 1) || narrow(720, 1) || !narrow(1439, 2) || narrow(1440, 2) {
		t.Fatal("narrow must switch at 720 logical pixels")
	}
}
