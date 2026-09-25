package main

import "image"

// Panel sizes in logical pixels; layoutFor multiplies them by the device
// scale because the screen is laid out in device pixels.
const (
	topH     = 34
	bottomH  = 68
	leftW    = 76
	rightW   = 200
	narrowAt = 720 // below this logical width the layers panel yields to the canvas
)

// panelMode is where the layers panel sits. Docked takes room from the
// canvas; overlay lies over it, for a window too narrow to give that room.
type panelMode int

const (
	panelHidden panelMode = iota
	panelDocked
	panelOverlay
)

type layout struct {
	top, left, right, bottom, canvas image.Rectangle
	mode                             panelMode
}

func narrow(w int, scale float64) bool { return float64(w) < narrowAt*scale }

func layoutFor(w, h int, scale float64, mode panelMode) layout {
	px := func(v float64) int { return int(v * scale) }
	l := layout{mode: mode,
		top:    image.Rect(0, 0, w, px(topH)),
		bottom: image.Rect(0, h-px(bottomH), w, h)}
	l.left = image.Rect(0, l.top.Max.Y, px(leftW), l.bottom.Min.Y)
	l.canvas = image.Rect(l.left.Max.X, l.top.Max.Y, w, l.bottom.Min.Y)
	if mode == panelHidden {
		return l
	}
	l.right = image.Rect(w-px(rightW), l.top.Max.Y, w, l.bottom.Min.Y)
	if mode == panelDocked {
		l.canvas.Max.X = l.right.Min.X
	}
	return l
}
