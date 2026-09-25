// Package ops exposes document edits to Filo scripts. The builtins are the
// contract shared with every engine: the corpus in testdata/corpus runs them
// and states the pixels they must produce.
package ops

import (
	"context"
	"errors"
	"fmt"
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
	d *doc.Document
}

var empty = filo.VList(nil)

func (s *state) register(eng *filo.Engine) {
	eng.MustRegisterBuiltin("doc-new", s.docNew)
	eng.MustRegisterBuiltin("doc-pixel", s.withDoc(s.pixel))
	eng.MustRegisterBuiltin("doc-line", s.withDoc(s.line))
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
	return empty, s.d.Line(ints[0], ints[1], ints[2], ints[3], c)
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
