package main

import "image"

// Panel sizes in logical pixels; layoutFor multiplies them by the device
// scale because the screen is laid out in device pixels.
const (
	topH     = 34
	bottomH  = 68
	leftW    = 96
	rightW   = 200
	narrowAt = 720 // below this logical width the layers panel yields to the canvas
)

type layout struct {
	top, left, right, bottom, canvas image.Rectangle
	showRight                        bool
}

func layoutFor(w, h int, scale float64) layout {
	px := func(v float64) int { return int(v * scale) }
	var l layout
	l.top = image.Rect(0, 0, w, px(topH))
	l.bottom = image.Rect(0, h-px(bottomH), w, h)
	l.left = image.Rect(0, l.top.Max.Y, px(leftW), l.bottom.Min.Y)
	l.showRight = float64(w) >= narrowAt*scale
	canvasMaxX := w
	if l.showRight {
		l.right = image.Rect(w-px(rightW), l.top.Max.Y, w, l.bottom.Min.Y)
		canvasMaxX = l.right.Min.X
	}
	l.canvas = image.Rect(l.left.Max.X, l.top.Max.Y, canvasMaxX, l.bottom.Min.Y)
	return l
}
