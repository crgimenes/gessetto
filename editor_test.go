package main

import (
	"image"
	"image/color"
	"testing"

	"github.com/crgimenes/gessetto/doc"
)

func newTestEditor(t *testing.T, w, h int) *editor {
	t.Helper()
	d, err := doc.New(w, h)
	if err != nil {
		t.Fatal(err)
	}
	e := newEditor(d)
	e.takeChanged()
	return e
}

func TestPencilStrokeIsOneUndoStep(t *testing.T) {
	e := newTestEditor(t, 5, 1)
	e.press(image.Pt(0, 0), false)
	e.drag(image.Pt(2, 0))
	e.drag(image.Pt(4, 0))
	e.release()
	for x := range 5 {
		if e.d.PixelAt(x, 0) != e.fg {
			t.Fatalf("(%d,0) = %v, want the foreground", x, e.d.PixelAt(x, 0))
		}
	}
	r, full := e.takeChanged()
	if full || r != image.Rect(0, 0, 5, 1) {
		t.Fatalf("changed %v full=%v, want (0,0)-(5,1) only", r, full)
	}
	e.undo()
	if e.d.PixelAt(4, 0) != (color.NRGBA{}) || e.d.CanUndo() {
		t.Fatal("one undo must remove the whole stroke")
	}
}

func TestSecondaryPaintsBackgroundAndEraserClears(t *testing.T) {
	e := newTestEditor(t, 2, 1)
	e.press(image.Pt(0, 0), true)
	e.release()
	if e.d.PixelAt(0, 0) != e.bg {
		t.Fatalf("got %v, want the background color", e.d.PixelAt(0, 0))
	}
	e.tool = toolEraser
	e.press(image.Pt(0, 0), false)
	e.release()
	if e.d.PixelAt(0, 0) != (color.NRGBA{}) {
		t.Fatalf("got %v, want transparent", e.d.PixelAt(0, 0))
	}
}

func TestPickerSetsColorsWithoutEditing(t *testing.T) {
	e := newTestEditor(t, 2, 1)
	green := color.NRGBA{G: 0xff, A: 0xff}
	_ = e.d.SetPixel(1, 0, green)
	e.tool = toolPicker
	e.press(image.Pt(1, 0), true)
	e.release()
	if e.bg != green {
		t.Fatalf("bg %v, want picked green", e.bg)
	}
	e.press(image.Pt(7, 7), false)
	e.release()
	if e.fg != (color.NRGBA{A: 0xff}) {
		t.Fatalf("fg %v, want unchanged when picking off the canvas", e.fg)
	}
	e.d.Undo()
	if e.d.CanUndo() {
		t.Fatal("picking must not add undo steps")
	}
}

func TestStrokeFarOffCanvasIsClamped(t *testing.T) {
	e := newTestEditor(t, 2, 2)
	e.press(image.Pt(-1_000_000, 0), false)
	e.drag(image.Pt(1_000_000, 0))
	e.release()
	if e.d.PixelAt(0, 0) != e.fg || e.d.PixelAt(1, 0) != e.fg {
		t.Fatal("the part of the stroke on the canvas must be drawn")
	}
}
