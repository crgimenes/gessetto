// Package ops exposes document edits to Filo scripts. The builtins are the
// contract shared with every engine: the corpus in testdata/corpus runs them
// and states the pixels they must produce.
package ops

import (
	"context"
	"errors"
	"fmt"
	"image"
	"image/color"
	"math"
	"strconv"
	"time"

	"github.com/crgimenes/filo"
	"github.com/crgimenes/gessetto/doc"
)

// A full-canvas loop of doc-pixel is one step per call plus the loop itself,
// so the step budget allows a few million edits.
var limits = filo.EvalConfig{
	StepLimit:      50_000_000,
	RecursionLimit: 256,
	Timeout:        60 * time.Second,
}

var errNoDocument = errors.New("no document: call doc-new or pass an input image")

// Apply runs src against d, which may be nil when the script creates the
// document with doc-new, and returns the resulting document. A failure inside
// the script comes back as a *filo.PositionError saying where.
func Apply(ctx context.Context, src string, d *doc.Document) (*doc.Document, error) {
	s := &state{d: d}
	eng := filo.NewEngine()
	s.register(eng)
	_, _, err := eng.RunScript(ctx, src, nil, limits)
	if err != nil {
		return nil, err
	}
	if s.d == nil {
		return nil, errNoDocument
	}
	return s.d, nil
}

type state struct {
	d   *doc.Document
	pen doc.Pen
	// clip is the script's own clipboard: the system one is the host's.
	clip *image.NRGBA
}

var empty = filo.VList(nil)

func (s *state) register(eng *filo.Engine) {
	eng.MustRegisterBuiltin("doc-new", s.docNew)
	eng.MustRegisterBuiltin("doc-pixel", s.withDoc(s.pixel))
	eng.MustRegisterBuiltin("doc-pen", s.setPen)
	eng.MustRegisterBuiltin("doc-line", s.withDoc(s.line))
	eng.MustRegisterBuiltin("doc-rect", s.withDoc(s.box(s.shapeDraw(false, doc.StyleOutline), 1)))
	eng.MustRegisterBuiltin("doc-fill-rect", s.withDoc(s.box(s.shapeDraw(false, doc.StyleFill), 1)))
	eng.MustRegisterBuiltin("doc-rect-both", s.withDoc(s.box(s.shapeDraw(false, doc.StyleBoth), 2)))
	eng.MustRegisterBuiltin("doc-ellipse", s.withDoc(s.box(s.shapeDraw(true, doc.StyleOutline), 1)))
	eng.MustRegisterBuiltin("doc-fill-ellipse", s.withDoc(s.box(s.shapeDraw(true, doc.StyleFill), 1)))
	eng.MustRegisterBuiltin("doc-ellipse-both", s.withDoc(s.box(s.shapeDraw(true, doc.StyleBoth), 2)))
	eng.MustRegisterBuiltin("doc-fill", s.withDoc(s.fill))
	eng.MustRegisterBuiltin("doc-recolor", s.withDoc(s.recolor))
	eng.MustRegisterBuiltin("doc-curve", s.withDoc(s.curve))
	eng.MustRegisterBuiltin("doc-select", s.withDoc(s.selectRect))
	eng.MustRegisterBuiltin("doc-move-selection", s.withDoc(s.moveSelection(false)))
	eng.MustRegisterBuiltin("doc-duplicate-selection", s.withDoc(s.moveSelection(true)))
	eng.MustRegisterBuiltin("doc-selection-mode", s.withDoc(s.selectionMode))
	eng.MustRegisterBuiltin("doc-copy", s.withDoc(s.noArgs(func() { s.copySelection() })))
	eng.MustRegisterBuiltin("doc-cut", s.withDoc(s.noArgs(func() {
		if s.copySelection() {
			s.d.DeleteSelection()
		}
	})))
	eng.MustRegisterBuiltin("doc-paste", s.withDoc(s.paste))
	eng.MustRegisterBuiltin("doc-drop", s.withDoc(s.noArgs(func() { s.d.Drop() })))
	eng.MustRegisterBuiltin("doc-delete-selection", s.withDoc(s.noArgs(func() { s.d.DeleteSelection() })))
	eng.MustRegisterBuiltin("doc-add-layer", s.withDoc(s.addLayer))
	eng.MustRegisterBuiltin("doc-select-layer", s.withDoc(s.selectLayer))
	eng.MustRegisterBuiltin("doc-undo", s.withDoc(s.undo))
	eng.MustRegisterBuiltin("doc-redo", s.withDoc(s.redo))
}

// Filo names the failing builtin in its own error, so messages here start
// at the argument.
func (s *state) withDoc(fn func(args []filo.Value) (filo.Value, error)) filo.Builtin {
	return func(_ context.Context, args []filo.Value) (filo.Value, error) {
		if s.d == nil {
			return filo.Value{}, errNoDocument
		}
		return fn(args)
	}
}

func (s *state) docNew(_ context.Context, args []filo.Value) (filo.Value, error) {
	if s.d != nil {
		return filo.Value{}, errors.New("a document is already open")
	}
	ints, err := intArgs(args, "width", "height")
	if err != nil {
		return filo.Value{}, err
	}
	d, err := doc.New(ints[0], ints[1])
	if err != nil {
		return filo.Value{}, err
	}
	s.d = d
	return empty, nil
}

func (s *state) pixel(args []filo.Value) (filo.Value, error) {
	if len(args) != 3 {
		return filo.Value{}, fmt.Errorf("want (x y color), got %d arguments", len(args))
	}
	ints, err := intArgs(args[:2], "x", "y")
	if err != nil {
		return filo.Value{}, err
	}
	c, err := colorArg(args[2])
	if err != nil {
		return filo.Value{}, err
	}
	return empty, s.d.SetPixel(ints[0], ints[1], c)
}

func (s *state) line(args []filo.Value) (filo.Value, error) {
	if len(args) != 5 {
		return filo.Value{}, fmt.Errorf("want (x0 y0 x1 y1 color), got %d arguments", len(args))
	}
	ints, err := intArgs(args[:4], "x0", "y0", "x1", "y1")
	if err != nil {
		return filo.Value{}, err
	}
	c, err := colorArg(args[4])
	if err != nil {
		return filo.Value{}, err
	}
	return empty, s.d.Line(ints[0], ints[1], ints[2], ints[3], c, s.pen)
}

type boxDraw func(x0, y0, x1, y1 int, line, fill color.NRGBA) error

// The document may not exist yet when builtins are registered, so each call
// looks it up through s.
func (s *state) shapeDraw(ellipse bool, style doc.Style) boxDraw {
	return func(x0, y0, x1, y1 int, line, fill color.NRGBA) error {
		if ellipse {
			return s.d.Ellipse(x0, y0, x1, y1, style, line, fill, s.pen)
		}
		return s.d.Rect(x0, y0, x1, y1, style, line, fill, s.pen)
	}
}

// box reads (x0 y0 x1 y1 color) or, with two colors, (x0 y0 x1 y1 outline
// fill): two opposite corners, both included.
func (s *state) box(draw boxDraw, colors int) func(args []filo.Value) (filo.Value, error) {
	return func(args []filo.Value) (filo.Value, error) {
		if len(args) != 4+colors {
			if colors == 2 {
				return filo.Value{}, fmt.Errorf("want (x0 y0 x1 y1 outline fill), got %d arguments", len(args))
			}
			return filo.Value{}, fmt.Errorf("want (x0 y0 x1 y1 color), got %d arguments", len(args))
		}
		ints, err := intArgs(args[:4], "x0", "y0", "x1", "y1")
		if err != nil {
			return filo.Value{}, err
		}
		line, err := colorArg(args[4])
		if err != nil {
			return filo.Value{}, err
		}
		fill := line
		if colors == 2 {
			fill, err = colorArg(args[5])
			if err != nil {
				return filo.Value{}, err
			}
		}
		return empty, draw(ints[0], ints[1], ints[2], ints[3], line, fill)
	}
}

// selectRect takes two opposite corners, both included, as the shapes do.
func (s *state) selectRect(args []filo.Value) (filo.Value, error) {
	ints, err := intArgs(args, "x0", "y0", "x1", "y1")
	if err != nil {
		return filo.Value{}, err
	}
	r := image.Rect(ints[0], ints[1], ints[2], ints[3]).Canon()
	r.Max = r.Max.Add(image.Pt(1, 1))
	s.d.Select(r)
	return empty, nil
}

func (s *state) moveSelection(duplicate bool) func(args []filo.Value) (filo.Value, error) {
	return func(args []filo.Value) (filo.Value, error) {
		ints, err := intArgs(args, "dx", "dy")
		if err != nil {
			return filo.Value{}, err
		}
		return empty, s.d.MoveSelection(ints[0], ints[1], duplicate)
	}
}

// selectionMode reads ("opaque") or ("transparent" key-color).
func (s *state) selectionMode(args []filo.Value) (filo.Value, error) {
	if len(args) == 0 {
		return filo.Value{}, errors.New(`want ("opaque") or ("transparent" color)`)
	}
	mode, err := args[0].AsString()
	if err != nil {
		return filo.Value{}, fmt.Errorf("mode: %w", err)
	}
	switch {
	case mode == "opaque" && len(args) == 1:
		s.d.SetSelectionKey(nil)
		return empty, nil
	case mode == "transparent" && len(args) == 2:
		c, err := colorArg(args[1])
		if err != nil {
			return filo.Value{}, err
		}
		s.d.SetSelectionKey(&c)
		return empty, nil
	}
	return filo.Value{}, fmt.Errorf(`want ("opaque") or ("transparent" color), got %q with %d arguments`, mode, len(args))
}

func (s *state) copySelection() bool {
	img, ok := s.d.SelectionImage()
	if ok {
		s.clip = img
	}
	return ok
}

func (s *state) paste(args []filo.Value) (filo.Value, error) {
	ints, err := intArgs(args, "x", "y")
	if err != nil {
		return filo.Value{}, err
	}
	if s.clip == nil {
		return filo.Value{}, errors.New("nothing copied yet")
	}
	return empty, s.d.Paste(s.clip, image.Pt(ints[0], ints[1]))
}

func (s *state) noArgs(f func()) func(args []filo.Value) (filo.Value, error) {
	return func(args []filo.Value) (filo.Value, error) {
		if len(args) != 0 {
			return filo.Value{}, fmt.Errorf("want no arguments, got %d", len(args))
		}
		f()
		return empty, nil
	}
}

// curve reads (x0 y0 cx1 cy1 cx2 cy2 x1 y1 color).
func (s *state) curve(args []filo.Value) (filo.Value, error) {
	if len(args) != 9 {
		return filo.Value{}, fmt.Errorf("want (x0 y0 cx1 cy1 cx2 cy2 x1 y1 color), got %d arguments", len(args))
	}
	v, err := intArgs(args[:8], "x0", "y0", "cx1", "cy1", "cx2", "cy2", "x1", "y1")
	if err != nil {
		return filo.Value{}, err
	}
	c, err := colorArg(args[8])
	if err != nil {
		return filo.Value{}, err
	}
	return empty, s.d.Curve(image.Pt(v[0], v[1]), image.Pt(v[2], v[3]), image.Pt(v[4], v[5]), image.Pt(v[6], v[7]), c, s.pen)
}

// recolor reads (x0 y0 x1 y1 from to): the color eraser along a line.
func (s *state) recolor(args []filo.Value) (filo.Value, error) {
	if len(args) != 6 {
		return filo.Value{}, fmt.Errorf("want (x0 y0 x1 y1 from to), got %d arguments", len(args))
	}
	ints, err := intArgs(args[:4], "x0", "y0", "x1", "y1")
	if err != nil {
		return filo.Value{}, err
	}
	from, err := colorArg(args[4])
	if err != nil {
		return filo.Value{}, err
	}
	to, err := colorArg(args[5])
	if err != nil {
		return filo.Value{}, err
	}
	return empty, s.d.Recolor(ints[0], ints[1], ints[2], ints[3], from, to, s.pen)
}

func (s *state) fill(args []filo.Value) (filo.Value, error) {
	if len(args) != 4 {
		return filo.Value{}, fmt.Errorf("want (x y color tolerance), got %d arguments", len(args))
	}
	ints, err := intArgs([]filo.Value{args[0], args[1], args[3]}, "x", "y", "tolerance")
	if err != nil {
		return filo.Value{}, err
	}
	if ints[2] < 0 || ints[2] > 255 {
		return filo.Value{}, fmt.Errorf("tolerance: want 0 to 255, got %d", ints[2])
	}
	c, err := colorArg(args[2])
	if err != nil {
		return filo.Value{}, err
	}
	s.d.Fill(ints[0], ints[1], c, ints[2])
	return empty, nil
}

// setPen is state, like picking up another pen: it applies to the outlines
// drawn after it, so the other builtins keep their arguments.
func (s *state) setPen(_ context.Context, args []filo.Value) (filo.Value, error) {
	if len(args) != 2 {
		return filo.Value{}, fmt.Errorf("want (width tip), got %d arguments", len(args))
	}
	ints, err := intArgs(args[:1], "width")
	if err != nil {
		return filo.Value{}, err
	}
	if ints[0] < 1 || ints[0] > doc.MaxWidth {
		return filo.Value{}, fmt.Errorf("width: want 1 to %d, got %d", doc.MaxWidth, ints[0])
	}
	tip, err := args[1].AsString()
	if err != nil {
		return filo.Value{}, fmt.Errorf("tip: %w", err)
	}
	if tip != "square" && tip != "round" {
		return filo.Value{}, fmt.Errorf("tip: want \"square\" or \"round\", got %q", tip)
	}
	s.pen = doc.Pen{Width: ints[0], Round: tip == "round"}
	return empty, nil
}

func (s *state) addLayer(args []filo.Value) (filo.Value, error) {
	if len(args) != 1 {
		return filo.Value{}, fmt.Errorf("want (name), got %d arguments", len(args))
	}
	name, err := args[0].AsString()
	if err != nil {
		return filo.Value{}, fmt.Errorf("name: %w", err)
	}
	return empty, s.d.AddLayer(name)
}

func (s *state) selectLayer(args []filo.Value) (filo.Value, error) {
	ints, err := intArgs(args, "index")
	if err != nil {
		return filo.Value{}, err
	}
	return empty, s.d.SelectLayer(ints[0])
}

func (s *state) undo(args []filo.Value) (filo.Value, error) {
	if len(args) != 0 {
		return filo.Value{}, fmt.Errorf("want no arguments, got %d", len(args))
	}
	return filo.VBool(s.d.Undo()), nil
}

func (s *state) redo(args []filo.Value) (filo.Value, error) {
	if len(args) != 0 {
		return filo.Value{}, fmt.Errorf("want no arguments, got %d", len(args))
	}
	return filo.VBool(s.d.Redo()), nil
}

// intArgs refuses fractions like nth does: a coordinate names a pixel that
// exists, and a fraction is the author's mistake, not a position to round.
func intArgs(args []filo.Value, names ...string) ([]int, error) {
	if len(args) != len(names) {
		return nil, fmt.Errorf("want %d arguments %v, got %d", len(names), names, len(args))
	}
	out := make([]int, len(args))
	for i, a := range args {
		v, err := a.AsNumber()
		if err != nil {
			return nil, fmt.Errorf("%s: %w", names[i], err) // #nosec G602 -- len(names) == len(args), checked above
		}
		if v != math.Trunc(v) || math.Abs(v) > doc.MaxCoord {
			return nil, fmt.Errorf("%s: want an integer from -%d to %d, got %s", names[i], doc.MaxCoord, doc.MaxCoord, a) // #nosec G602 -- len(names) == len(args), checked above
		}
		out[i] = int(v)
	}
	return out, nil
}

// colorArg reads "#rrggbb" (opaque) or "#rrggbbaa", straight alpha.
func colorArg(v filo.Value) (color.NRGBA, error) {
	s, err := v.AsString()
	if err != nil {
		return color.NRGBA{}, fmt.Errorf("color: %w", err)
	}
	if len(s) != 7 && len(s) != 9 || s[0] != '#' {
		return color.NRGBA{}, fmt.Errorf("color: want \"#rrggbb\" or \"#rrggbbaa\", got %q", s)
	}
	n, err := strconv.ParseUint(s[1:], 16, 32)
	if err != nil {
		return color.NRGBA{}, fmt.Errorf("color: want hex digits, got %q", s)
	}
	if len(s) == 7 {
		n = n<<8 | 0xff
	}
	return color.NRGBA{R: byte(n >> 24), G: byte(n >> 16), B: byte(n >> 8), A: byte(n)}, nil // #nosec G115 -- truncation picks each byte
}
