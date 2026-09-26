package main

import (
	"image"
	"sync"

	"github.com/crgimenes/native/pointer"
	"github.com/hajimehoshi/ebiten/v2"
)

// pinchStep is how much trackpad magnification makes one zoom step. A
// deliberate pinch adds up to about 0.5, so it moves two or three steps.
const pinchStep = 0.18

// pinch gathers trackpad magnification, which arrives on the main thread
// from the native monitor, for Update to turn into zoom steps.
type pinch struct {
	mu      sync.Mutex
	pending float64
	acc     float64
	started bool
}

// startPinch installs the monitor once; where there is none (not macOS),
// the wheel with Cmd/Ctrl still zooms.
func (a *app) startPinch() {
	if a.pinch.started {
		return
	}
	a.pinch.started = true
	var err error
	ebiten.RunOnMainThread(func() {
		_, err = pointer.Watch(func(ev pointer.Event) {
			if ev.Kind != pointer.Magnify {
				return
			}
			a.pinch.mu.Lock()
			a.pinch.pending += ev.Magnification
			a.pinch.mu.Unlock()
		})
	})
	if err != nil {
		a.event("pinch", "watch=false")
	}
}

// applyPinch zooms around the pointer when it is over the canvas, around
// the canvas center otherwise.
func (a *app) applyPinch() {
	a.pinch.mu.Lock()
	a.pinch.acc += a.pinch.pending
	a.pinch.pending = 0
	a.pinch.mu.Unlock()
	anchor := image.Pt(int(a.in.MouseX), int(a.in.MouseY))
	if !anchor.In(a.lay.canvas) {
		anchor = a.lay.canvas.Min.Add(a.lay.canvas.Size().Div(2))
	}
	for a.pinch.acc >= pinchStep {
		a.v.zoomAt(anchor, 1)
		a.pinch.acc -= pinchStep
	}
	for a.pinch.acc <= -pinchStep {
		a.v.zoomAt(anchor, -1)
		a.pinch.acc += pinchStep
	}
}
