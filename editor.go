package main

import (
	"image"
	"image/color"

	"github.com/crgimenes/gessetto/doc"
)

type tool int

const (
	toolPencil tool = iota
	toolEraser
	toolPicker
)

var tools = []struct {
	t    tool
	name string
	key  string
}{
	{toolPencil, "Pencil", "B"},
	{toolEraser, "Eraser", "E"},
	{toolPicker, "Picker", "I"},
}

// editor is the document plus what the tools need, kept apart from Ebitengine
// so a stroke can be driven from a test the way the pointer drives it.
type editor struct {
	d    *doc.Document
	tool tool
	fg   color.NRGBA
	bg   color.NRGBA

	stroking  bool
	secondary bool
	last      image.Point

	// changed is the part of the image to repaint on screen; full asks for
	// all of it, after undo or a layer change.
	changed image.Rectangle
	full    bool
}

func newEditor(d *doc.Document) *editor {
	return &editor{
		d:    d,
		fg:   color.NRGBA{A: 0xff},
		bg:   color.NRGBA{R: 0xff, G: 0xff, B: 0xff, A: 0xff},
		full: true,
	}
}

// press starts a stroke at image point p; secondary is the right button, which
// paints or picks the background color.
func (e *editor) press(p image.Point, secondary bool) {
	e.release()
	p = clampCoord(p)
	e.secondary = secondary
	e.last = p
	if e.tool == toolPicker {
		e.pick(p)
		e.stroking = true
		return
	}
	e.d.BeginGroup()
	e.stroking = true
	_ = e.d.SetPixel(p.X, p.Y, e.paint()) // clampCoord keeps p in range
	e.touch(image.Rectangle{Min: p, Max: p.Add(image.Pt(1, 1))})
}

func (e *editor) drag(p image.Point) {
	if !e.stroking {
		return
	}
	p = clampCoord(p)
	if p == e.last {
		return
	}
	if e.tool == toolPicker {
		e.pick(p)
		e.last = p
		return
	}
	_ = e.d.Line(e.last.X, e.last.Y, p.X, p.Y, e.paint()) // clampCoord keeps p in range
	r := image.Rectangle{Min: e.last, Max: p}.Canon()
	e.touch(image.Rectangle{Min: r.Min, Max: r.Max.Add(image.Pt(1, 1))})
	e.last = p
}

func (e *editor) release() {
	if !e.stroking {
		return
	}
	e.stroking = false
	e.d.EndGroup()
}

func (e *editor) paint() color.NRGBA {
	switch {
	case e.tool == toolEraser:
		return color.NRGBA{}
	case e.secondary:
		return e.bg
	}
	return e.fg
}

func (e *editor) pick(p image.Point) {
	if !p.In(e.d.Bounds()) {
		return
	}
	c := e.d.PixelAt(p.X, p.Y)
	if e.secondary {
		e.bg = c
		return
	}
	e.fg = c
}

func (e *editor) undo() {
	e.release()
	if e.d.Undo() {
		e.full = true
	}
}

func (e *editor) redo() {
	e.release()
	if e.d.Redo() {
		e.full = true
	}
}

func (e *editor) touch(r image.Rectangle) {
	e.changed = e.changed.Union(r.Intersect(e.d.Bounds()))
}

// takeChanged hands the region to repaint to the view and forgets it.
func (e *editor) takeChanged() (r image.Rectangle, full bool) {
	r, full = e.changed, e.full
	e.changed, e.full = image.Rectangle{}, false
	return r, full
}

func clampCoord(p image.Point) image.Point {
	return image.Pt(
		min(max(p.X, -doc.MaxCoord), doc.MaxCoord),
		min(max(p.Y, -doc.MaxCoord), doc.MaxCoord),
	)
}
