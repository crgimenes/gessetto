package main

import (
	"image"
	"testing"
)

func TestPinchBecomesZoomSteps(t *testing.T) {
	a := &app{}
	a.lay.canvas = image.Rect(0, 0, 400, 300)
	a.v.step = stepFor(4)
	a.in.MouseX, a.in.MouseY = 100, 100
	before := a.v.toDoc(image.Pt(100, 100))

	a.pinch.pending = 2*pinchStep + 0.01
	a.applyPinch()
	if a.v.zoom() != 8 {
		t.Fatalf("zoom %v, want two steps up from 4 to 8", a.v.zoom())
	}
	if a.v.toDoc(image.Pt(100, 100)) != before {
		t.Fatal("the pixel under the pointer must stay put")
	}
	a.pinch.pending = -0.05
	a.applyPinch()
	if a.v.zoom() != 8 {
		t.Fatal("a small pinch back only cancels the remainder, no step yet")
	}
	a.pinch.pending = -pinchStep
	a.applyPinch()
	if a.v.zoom() != 6 {
		t.Fatalf("zoom %v, want one step down to 6", a.v.zoom())
	}
}

func TestMagnifyClickZoomsAtThePoint(t *testing.T) {
	a := &app{}
	a.lay.canvas = image.Rect(0, 0, 400, 300)
	a.v.step = stepFor(2)
	p := image.Pt(150, 90)
	before := a.v.toDoc(p)
	a.in.MouseClicked = true
	a.magnify(p)
	if a.v.zoom() != 3 || a.v.toDoc(p) != before {
		t.Fatalf("zoom %v, pixel %v; want one step in, same pixel under the click", a.v.zoom(), a.v.toDoc(p))
	}
}
