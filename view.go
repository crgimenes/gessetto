package main

import (
	"image"
	"math"
)

// Zoom steps in screen pixels per image pixel. At 1 and above every image
// pixel is a whole number of screen pixels, so pixel art stays crisp.
var zoomSteps = []float64{0.125, 0.25, 0.5, 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64}

// gridFrom is the zoom from which the pixel grid is drawn; below it the lines
// would cover the picture.
const gridFrom = 8

// view maps the document onto the canvas area of the screen. origin is where
// image pixel (0, 0) lands, in screen pixels.
type view struct {
	step   int
	origin image.Point
}

func (v *view) zoom() float64 { return zoomSteps[v.step] }

// toDoc is the image pixel under screen point p; points left of or above the
// image give negative coordinates, not zero.
func (v *view) toDoc(p image.Point) image.Point {
	z := v.zoom()
	return image.Pt(
		int(math.Floor(float64(p.X-v.origin.X)/z)),
		int(math.Floor(float64(p.Y-v.origin.Y)/z)),
	)
}

func (v *view) toScreen(r image.Rectangle) image.Rectangle {
	z := v.zoom()
	return image.Rect(
		v.origin.X+int(math.Floor(float64(r.Min.X)*z)),
		v.origin.Y+int(math.Floor(float64(r.Min.Y)*z)),
		v.origin.X+int(math.Ceil(float64(r.Max.X)*z)),
		v.origin.Y+int(math.Ceil(float64(r.Max.Y)*z)),
	)
}

// zoomAt changes the zoom by delta steps keeping the image point under anchor
// where it is, so zooming follows the pointer.
func (v *view) zoomAt(anchor image.Point, delta int) {
	next := min(max(v.step+delta, 0), len(zoomSteps)-1)
	if next == v.step {
		return
	}
	k := zoomSteps[next] / v.zoom()
	v.origin = image.Pt(
		anchor.X-int(math.Round(float64(anchor.X-v.origin.X)*k)),
		anchor.Y-int(math.Round(float64(anchor.Y-v.origin.Y)*k)),
	)
	v.step = next
}

// fit picks the largest zoom that shows the whole image inside area, capped
// at maxZoom so a tiny sprite does not open as a few giant squares, and
// centers it.
func (v *view) fit(area image.Rectangle, size image.Point, maxZoom float64) {
	v.step = 0
	for i, z := range zoomSteps {
		if z > maxZoom {
			break
		}
		if float64(size.X)*z <= float64(area.Dx()) && float64(size.Y)*z <= float64(area.Dy()) {
			v.step = i
		}
	}
	v.center(area, size)
}

func (v *view) center(area image.Rectangle, size image.Point) {
	z := v.zoom()
	w := int(math.Round(float64(size.X) * z))
	h := int(math.Round(float64(size.Y) * z))
	v.origin = image.Pt(area.Min.X+(area.Dx()-w)/2, area.Min.Y+(area.Dy()-h)/2)
}
