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

func TestShapePreviewLeavesNoTrail(t *testing.T) {
	e := newTestEditor(t, 6, 6)
	e.tool = toolRect
	e.press(image.Pt(0, 0), false)
	e.drag(image.Pt(5, 5))
	e.drag(image.Pt(2, 2))
	e.release()
	if e.d.PixelAt(5, 5) != (color.NRGBA{}) {
		t.Fatal("the larger preview must be gone once the pointer moved back")
	}
	if e.d.PixelAt(2, 2) != e.fg || e.d.PixelAt(1, 1) != (color.NRGBA{}) {
		t.Fatal("want the outline of (0,0)-(2,2) only")
	}
	r, _ := e.takeChanged()
	if !image.Rect(0, 0, 6, 6).In(r) {
		t.Fatalf("changed %v must cover where the big preview was", r)
	}
	e.undo()
	if e.d.PixelAt(0, 0) != (color.NRGBA{}) || e.d.CanUndo() {
		t.Fatal("a shape must be one undo step")
	}
}

func TestConstrain(t *testing.T) {
	a := image.Pt(10, 10)
	tests := []struct {
		t       tool
		p, want image.Point
	}{
		{toolLine, image.Pt(20, 12), image.Pt(20, 10)},
		{toolLine, image.Pt(11, 3), image.Pt(10, 3)},
		{toolLine, image.Pt(16, 4), image.Pt(16, 4)},
		{toolRect, image.Pt(13, 20), image.Pt(20, 20)},
		{toolEllipse, image.Pt(4, 12), image.Pt(4, 16)},
		{toolRect, image.Pt(10, 15), image.Pt(15, 15)},
	}
	for _, tt := range tests {
		got := constrained(tt.t, a, tt.p)
		if got != tt.want {
			t.Errorf("constrained(%v, %v) = %v, want %v", tt.t, tt.p, got, tt.want)
		}
	}
}

func TestFillToolUsesToleranceAndSecondary(t *testing.T) {
	e := newTestEditor(t, 3, 1)
	_ = e.d.SetPixel(2, 0, color.NRGBA{A: 8})
	e.tool = toolFill
	e.tolerance = 8
	e.press(image.Pt(0, 0), true)
	e.release()
	for x := range 3 {
		if e.d.PixelAt(x, 0) != e.bg {
			t.Fatalf("(%d,0) = %v, want the background color", x, e.d.PixelAt(x, 0))
		}
	}
	if e.stroking {
		t.Fatal("a fill is one click, not a stroke")
	}
}

func TestThickLineRepaintsItsFootprint(t *testing.T) {
	e := newTestEditor(t, 12, 12)
	e.tool = toolLine
	e.pen = doc.Pen{Width: 5}
	e.press(image.Pt(5, 5), false)
	e.drag(image.Pt(6, 5))
	e.release()
	r, _ := e.takeChanged()
	if !image.Rect(3, 3, 9, 8).In(r) {
		t.Fatalf("changed %v must cover the 5px footprint around the line", r)
	}
	if e.d.PixelAt(3, 3) != e.fg || e.d.PixelAt(8, 7) != e.fg {
		t.Fatal("want the square footprint at both ends")
	}
}

func TestShapeColorsFollowPaint(t *testing.T) {
	e := newTestEditor(t, 5, 5)
	e.tool = toolRect
	e.style = doc.StyleBoth
	e.press(image.Pt(0, 0), true)
	e.drag(image.Pt(4, 4))
	e.release()
	if e.d.PixelAt(0, 0) != e.bg || e.d.PixelAt(2, 2) != e.fg {
		t.Fatal("the right button must swap: outline in the background, area in the foreground")
	}
}

func TestEraserHasItsOwnSize(t *testing.T) {
	e := newTestEditor(t, 8, 8)
	e.tool = toolRect
	e.style = doc.StyleFill
	e.press(image.Pt(0, 0), true)
	e.drag(image.Pt(7, 7))
	e.release()
	e.tool = toolEraser
	e.pen = doc.Pen{Width: 1}
	e.press(image.Pt(4, 4), false)
	e.release()
	for _, p := range []image.Point{{3, 3}, {6, 6}} {
		if e.d.PixelAt(p.X, p.Y) != (color.NRGBA{}) {
			t.Fatalf("%v not erased: the 4px eraser must cover (3,3)-(6,6)", p)
		}
	}
	if e.d.PixelAt(2, 2) == (color.NRGBA{}) || e.d.PixelAt(7, 7) == (color.NRGBA{}) {
		t.Fatal("the eraser reached past its 4px footprint")
	}
}

func TestSelectToolMarqueeMoveAndDrop(t *testing.T) {
	e := newTestEditor(t, 8, 4)
	red := color.NRGBA{R: 0xff, A: 0xff}
	_ = e.d.Rect(0, 0, 1, 1, doc.StyleFill, red, red, doc.Pen{})
	e.setTool(toolSelect)

	e.press(image.Pt(0, 0), false)
	e.drag(image.Pt(1, 1))
	e.release()
	r, ok := e.d.Selection()
	if !ok || r != image.Rect(0, 0, 2, 2) {
		t.Fatalf("selection %v %v, want (0,0)-(2,2)", r, ok)
	}

	e.takeChanged()
	e.duplicate = true
	e.press(image.Pt(1, 1), false)
	e.drag(image.Pt(5, 3))
	e.release()
	r, _ = e.d.Selection()
	if r != image.Rect(4, 2, 6, 4) {
		t.Fatalf("moved to %v, want (4,2)-(6,4)", r)
	}
	changed, _ := e.takeChanged()
	if !image.Rect(0, 0, 6, 4).In(changed) {
		t.Fatalf("changed %v must cover where it was and where it went", changed)
	}

	e.setTool(toolPencil)
	_, ok = e.d.Selection()
	if ok {
		t.Fatal("switching tools must drop the selection")
	}
	if e.d.PixelAt(0, 0) != red || e.d.PixelAt(5, 3) != red {
		t.Fatal("Option-drag must leave the source and drop the copy")
	}
	e.undo()
	if e.d.PixelAt(5, 3) != (color.NRGBA{}) || e.d.PixelAt(0, 0) != red {
		t.Fatal("one undo must take back the whole duplicate")
	}
}

func TestSelectClickDeselects(t *testing.T) {
	e := newTestEditor(t, 4, 4)
	e.setTool(toolSelect)
	e.press(image.Pt(0, 0), false)
	e.drag(image.Pt(2, 2))
	e.release()
	e.press(image.Pt(3, 3), false)
	e.release()
	_, ok := e.d.Selection()
	if ok {
		t.Fatal("a click outside the selection must deselect")
	}
}

func TestSelectionKeyFollowsBackground(t *testing.T) {
	e := newTestEditor(t, 4, 1)
	blue := color.NRGBA{B: 0xff, A: 0xff}
	_ = e.d.SetPixel(0, 0, e.bg)
	_ = e.d.SetPixel(2, 0, blue)
	e.clearSel = true
	e.syncSelectionKey()
	e.setTool(toolSelect)
	e.d.Select(image.Rect(0, 0, 1, 1))
	e.press(image.Pt(0, 0), false)
	e.drag(image.Pt(2, 0))
	e.release()
	e.drop()
	if e.d.PixelAt(2, 0) != blue {
		t.Fatal("a transparent selection of the background color must not cover blue")
	}
}

func TestColorEraserOnlyTouchesTheForeground(t *testing.T) {
	e := newTestEditor(t, 6, 1)
	blue := color.NRGBA{B: 0xff, A: 0xff}
	_ = e.d.SetPixel(1, 0, e.fg)
	_ = e.d.SetPixel(2, 0, blue)
	e.tool = toolEraser
	e.press(image.Pt(0, 0), true)
	e.drag(image.Pt(5, 0))
	e.release()
	if e.d.PixelAt(1, 0) != e.bg || e.d.PixelAt(2, 0) != blue || e.d.PixelAt(4, 0) != (color.NRGBA{}) {
		t.Fatal("the right-button eraser must turn only the foreground color into the background")
	}
}

func TestMagnifierDoesNotDraw(t *testing.T) {
	e := newTestEditor(t, 2, 2)
	e.setTool(toolZoom)
	e.press(image.Pt(0, 0), false)
	e.release()
	if e.d.CanUndo() {
		t.Fatal("the magnifier must never edit the picture")
	}
}

func TestCurveToolLineThenTwoBends(t *testing.T) {
	e := newTestEditor(t, 17, 7)
	e.setTool(toolCurve)
	e.press(image.Pt(0, 6), false)
	e.drag(image.Pt(16, 6))
	e.release()
	if e.d.PixelAt(8, 6) != e.fg {
		t.Fatal("the first drag must lay a straight line")
	}
	e.press(image.Pt(0, 0), false)
	e.release()
	e.press(image.Pt(16, 0), false)
	e.release()
	if e.curveStage != 0 {
		t.Fatal("the second bend must finish the curve")
	}
	if e.d.PixelAt(8, 6) != (color.NRGBA{}) || e.d.PixelAt(8, 2) != e.fg {
		t.Fatal("want the line bent into an arch peaking near y=2")
	}
	e.undo()
	if e.d.CanUndo() || e.d.PixelAt(0, 6) != (color.NRGBA{}) {
		t.Fatal("the whole curve must be one undo step")
	}
}

func TestCurveCommitsOnToolSwitch(t *testing.T) {
	e := newTestEditor(t, 8, 3)
	e.setTool(toolCurve)
	e.press(image.Pt(0, 1), false)
	e.drag(image.Pt(7, 1))
	e.release()
	e.setTool(toolPencil)
	if e.curveStage != 0 || !e.d.CanUndo() || e.d.PixelAt(4, 1) != e.fg {
		t.Fatal("switching tools must keep the unbent curve as a line")
	}
}
