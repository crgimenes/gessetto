package main

import (
	"image"
	"testing"
)

func TestToDocFloorsBothWays(t *testing.T) {
	v := view{step: stepFor(8), origin: image.Pt(100, 50)}
	tests := []struct {
		screen, want image.Point
	}{
		{image.Pt(100, 50), image.Pt(0, 0)},
		{image.Pt(107, 57), image.Pt(0, 0)},
		{image.Pt(108, 58), image.Pt(1, 1)},
		{image.Pt(99, 49), image.Pt(-1, -1)},
		{image.Pt(92, 42), image.Pt(-1, -1)},
		{image.Pt(91, 41), image.Pt(-2, -2)},
	}
	for _, tt := range tests {
		got := v.toDoc(tt.screen)
		if got != tt.want {
			t.Errorf("toDoc(%v) = %v, want %v", tt.screen, got, tt.want)
		}
	}
}

func TestZoomAtKeepsPixelUnderPointer(t *testing.T) {
	v := view{step: stepFor(4), origin: image.Pt(10, 20)}
	anchor := image.Pt(210, 120)
	before := v.toDoc(anchor)
	v.zoomAt(anchor, 3)
	if v.zoom() != 12 {
		t.Fatalf("zoom %v, want 12", v.zoom())
	}
	after := v.toDoc(anchor)
	if after != before {
		t.Fatalf("pixel under pointer moved from %v to %v", before, after)
	}
	v.zoomAt(anchor, -100)
	if v.step != 0 {
		t.Fatalf("step %d, want clamped to 0", v.step)
	}
}

func TestFitCapsAndCenters(t *testing.T) {
	var v view
	area := image.Rect(0, 0, 800, 600)
	v.fit(area, image.Pt(16, 16), 16)
	if v.zoom() != 16 {
		t.Fatalf("zoom %v, want the 16 cap for a sprite", v.zoom())
	}
	if v.origin != image.Pt(272, 172) {
		t.Fatalf("origin %v, want centered at (272,172)", v.origin)
	}
	v.fit(area, image.Pt(4000, 1000), 16)
	if v.zoom() != 0.125 {
		t.Fatalf("zoom %v, want 0.125 for a large image", v.zoom())
	}
}
