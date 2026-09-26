// Package doc is the drawing document: layers of straight-alpha RGBA pixels,
// the edits that change them and the history that undoes those edits. It knows
// nothing about windows or scripting, so the headless mode, the tests and the
// app all drive the same code.
package doc

import (
	"errors"
	"fmt"
	"image"
	"image/color"
	"unicode/utf8"
)

const (
	// yagni: fixed ceilings until the open question on canvas size is settled.
	MaxSide     = 8192
	maxDocBytes = 1 << 30

	maxLayerName = 256
)

var (
	ErrSize      = errors.New("document size out of range")
	ErrTooBig    = errors.New("document would exceed the memory ceiling")
	ErrNoLayer   = errors.New("no such layer")
	ErrLayerName = errors.New("invalid layer name")
)

type Layer struct {
	Name string
	Pix  *image.NRGBA
}

type Document struct {
	width, height int
	layers        []*Layer
	active        int
	hist          history
	sel           *selection
	selKey        *color.NRGBA
}

func New(width, height int) (*Document, error) {
	err := checkSize(width, height, 1)
	if err != nil {
		return nil, err
	}
	d := &Document{width: width, height: height}
	d.layers = []*Layer{d.newLayer("Background")}
	d.hist.max = maxHistoryBytes
	return d, nil
}

func FromImage(img image.Image) (*Document, error) {
	b := img.Bounds()
	d, err := New(b.Dx(), b.Dy())
	if err != nil {
		return nil, err
	}
	pix := d.layers[0].Pix
	for y := range d.height {
		for x := range d.width {
			c := color.NRGBAModel.Convert(img.At(b.Min.X+x, b.Min.Y+y)).(color.NRGBA)
			pix.SetNRGBA(x, y, c)
		}
	}
	return d, nil
}

func checkSize(width, height, layers int) error {
	if width < 1 || height < 1 || width > MaxSide || height > MaxSide {
		return fmt.Errorf("%w: %dx%d, want 1 to %d per side", ErrSize, width, height, MaxSide)
	}
	if width*height*4*layers > maxDocBytes {
		return fmt.Errorf("%w: %d layers of %dx%d", ErrTooBig, layers, width, height)
	}
	return nil
}

func (d *Document) newLayer(name string) *Layer {
	return &Layer{Name: name, Pix: image.NewNRGBA(image.Rect(0, 0, d.width, d.height))}
}

func (d *Document) Bounds() image.Rectangle { return image.Rect(0, 0, d.width, d.height) }

// Layers is bottom first. The slice is the document's own: read, don't keep.
func (d *Document) Layers() []*Layer { return d.layers }

func (d *Document) Active() int { return d.active }

func (d *Document) SelectLayer(i int) error {
	d.Drop()
	d.EndGroup()
	if i < 0 || i >= len(d.layers) {
		return fmt.Errorf("%w: %d, document has %d", ErrNoLayer, i, len(d.layers))
	}
	d.active = i
	return nil
}

// AddLayer inserts a transparent layer above the active one and makes it
// active.
func (d *Document) AddLayer(name string) error {
	d.Drop()
	d.EndGroup()
	if name == "" || !utf8.ValidString(name) || utf8.RuneCountInString(name) > maxLayerName {
		return fmt.Errorf("%w: want 1 to %d characters of UTF-8", ErrLayerName, maxLayerName)
	}
	err := checkSize(d.width, d.height, len(d.layers)+1)
	if err != nil {
		return err
	}
	at := d.active + 1
	l := d.newLayer(name)
	d.insertLayer(at, l)
	d.hist.push(entry{kind: addLayer, layer: at, added: l, size: len(l.Pix.Pix)})
	return nil
}

func (d *Document) insertLayer(at int, l *Layer) {
	d.layers = append(d.layers[:at], append([]*Layer{l}, d.layers[at:]...)...)
	d.active = at
}

func (d *Document) removeLayer(at int) {
	d.layers = append(d.layers[:at], d.layers[at+1:]...)
	// AddLayer always inserts right above the active layer, so undoing it
	// gives the selection back to the layer that had it.
	d.active = at - 1
}
