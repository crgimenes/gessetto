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
	toolFill
	toolLine
	toolRect
	toolEllipse
)

var tools = []struct {
	t    tool
	name string
	key  string
}{
	{toolPencil, "Pencil", "B"},
	{toolEraser, "Eraser", "E"},
	{toolPicker, "Picker", "I"},
	{toolFill, "Fill", "G"},
	{toolLine, "Line", "L"},
	{toolRect, "Rectangle", "R"},
	{toolEllipse, "Ellipse", "O"},
}

func (t tool) shape() bool { return t == toolLine || t == toolRect || t == toolEllipse }

// editor is the document plus what the tools need, kept apart from Ebitengine
// so a stroke can be driven from a test the way the pointer drives it.
type editor struct {
	d    *doc.Document
	tool tool
	fg   color.NRGBA
	bg   color.NRGBA

	// Tool options.
	pen       doc.Pen
	eraser    doc.Pen
	style     doc.Style
	tolerance int

	stroking  bool
	secondary bool
	last      image.Point
	anchor    image.Point
	shapeRect image.Rectangle
	// constrain is Shift: lines snap to 45 degrees, boxes become squares.
	constrain bool

	// changed is the part of the image to repaint on screen; full asks for
	// all of it, after undo or a layer change.
	changed image.Rectangle
	full    bool
}

func newEditor(d *doc.Document) *editor {
	return &editor{
		d:  d,
		fg: color.NRGBA{A: 0xff},
		bg: color.NRGBA{R: 0xff, G: 0xff, B: 0xff, A: 0xff},
		// Paint's smallest eraser.
		eraser: doc.Pen{Width: 4},
		full:   true,
	}
}

// press starts a stroke at image point p; secondary is the right button, which
// paints or picks the background color.
func (e *editor) press(p image.Point, secondary bool) {
	e.release()
	p = clampCoord(p)
	e.secondary = secondary
	e.last = p
	switch {
	case e.tool == toolPicker:
		e.pick(p)
		e.stroking = true
		return
	case e.tool == toolFill:
		e.fill(p)
		return
	case e.tool.shape():
		e.d.BeginGroup()
		e.stroking = true
		e.anchor = p
		e.shapeRect = image.Rectangle{}
		e.drawShape(p)
		return
	}
	e.d.BeginGroup()
	e.stroking = true
	e.strokeTo(p)
}

// cursorPen is what the next click stamps, for the outline under the pointer.
func (e *editor) cursorPen() doc.Pen {
	switch {
	case e.tool == toolEraser:
		return e.eraser
	case e.tool.shape() && (e.tool == toolLine || e.style != doc.StyleFill):
		return e.pen
	}
	return doc.Pen{}
}

// strokePen is the pencil's single pixel, or the eraser's own size.
func (e *editor) strokePen() doc.Pen {
	if e.tool == toolEraser {
		return e.eraser
	}
	return doc.Pen{}
}

func (e *editor) strokeTo(p image.Point) {
	pen := e.strokePen()
	_ = e.d.Line(e.last.X, e.last.Y, p.X, p.Y, e.paint(), pen) // clampCoord keeps p in range
	r := image.Rectangle{Min: e.last, Max: p}.Canon()
	w := pen.Width
	e.touch(image.Rectangle{Min: r.Min.Sub(image.Pt(w, w)), Max: r.Max.Add(image.Pt(w+1, w+1))})
	e.last = p
}

func (e *editor) drag(p image.Point) {
	if !e.stroking {
		return
	}
	p = clampCoord(p)
	if p == e.last {
		return
	}
	if e.tool.shape() {
		e.last = p
		e.d.RevertGroup()
		e.drawShape(p)
		return
	}
	if e.tool == toolPicker {
		e.pick(p)
		e.last = p
		return
	}
	e.strokeTo(p)
}

func (e *editor) release() {
	if !e.stroking {
		return
	}
	e.stroking = false
	e.d.EndGroup()
}

// drawShape draws the shape from the anchor to p, repainting both where the
// previous preview was and where this one is.
func (e *editor) drawShape(p image.Point) {
	a := e.anchor
	if e.constrain {
		p = clampCoord(constrained(e.tool, a, p))
	}
	// As in Paint: outline in the foreground, area in the background; the
	// right button swaps them.
	line, fill := e.fg, e.bg
	if e.secondary {
		line, fill = fill, line
	}
	switch e.tool {
	case toolLine:
		_ = e.d.Line(a.X, a.Y, p.X, p.Y, line, e.pen) // clampCoord keeps p in range
	case toolRect:
		_ = e.d.Rect(a.X, a.Y, p.X, p.Y, e.style, line, fill, e.pen)
	case toolEllipse:
		_ = e.d.Ellipse(a.X, a.Y, p.X, p.Y, e.style, line, fill, e.pen)
	}
	r := image.Rectangle{Min: a, Max: p}.Canon()
	// A line's thick outline reaches past its endpoints.
	w := e.pen.Width
	r = image.Rectangle{Min: r.Min.Sub(image.Pt(w, w)), Max: r.Max.Add(image.Pt(w+1, w+1))}
	e.touch(e.shapeRect)
	e.touch(r)
	e.shapeRect = r
}

// constrained snaps a line to the nearest multiple of 45 degrees and a box to
// a square, keeping the side that follows the pointer farther.
func constrained(t tool, a, p image.Point) image.Point {
	d := p.Sub(a)
	ax, ay := abs(d.X), abs(d.Y)
	if t == toolLine {
		switch {
		case ax > 2*ay:
			return image.Pt(p.X, a.Y)
		case ay > 2*ax:
			return image.Pt(a.X, p.Y)
		}
	}
	n := max(ax, ay)
	return a.Add(image.Pt(sign(d.X)*n, sign(d.Y)*n))
}

func (e *editor) fill(p image.Point) {
	if !p.In(e.d.Bounds()) {
		return
	}
	e.d.Fill(p.X, p.Y, e.paint(), e.tolerance)
	e.full = true
}

func abs(v int) int {
	if v < 0 {
		return -v
	}
	return v
}

// sign treats 0 as positive so a square dragged straight along one axis
// still grows.
func sign(v int) int {
	if v < 0 {
		return -1
	}
	return 1
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
