package doc

import (
	"image"
)

const (
	// yagni: fixed budget; becomes a setting if real documents hit it.
	maxHistoryBytes = 256 << 20

	// What one entry costs beyond its pixels (struct, slice allocations, list
	// growth), measured at ~270 bytes with a million one-pixel edits. Charged
	// so those edits cannot hide behind their 8 bytes of pixels.
	entryOverhead = 272
)

type entryKind int

const (
	pixels entryKind = iota
	addLayer
)

type entry struct {
	kind  entryKind
	layer int

	rect          image.Rectangle
	before, after []byte

	added *Layer

	size int
}

// history keeps undo entries in list[:cursor] and redo entries after it.
type history struct {
	list    []entry
	cursor  int
	bytes   int
	max     int
	dropped int
}

func (h *history) push(e entry) {
	for _, r := range h.list[h.cursor:] {
		h.bytes -= r.size
	}
	e.size += entryOverhead
	h.list = append(h.list[:h.cursor], e)
	h.cursor++
	h.bytes += e.size
	n := 0
	for h.bytes > h.max && n < len(h.list) {
		h.bytes -= h.list[n].size
		n++
	}
	if n == 0 {
		return
	}
	h.list = append(h.list[:0], h.list[n:]...)
	h.cursor -= n
	h.dropped += n
}

// Dropped counts the oldest edits discarded to stay under the history budget;
// they can no longer be undone.
func (d *Document) Dropped() int { return d.hist.dropped }

func (d *Document) CanUndo() bool { return d.hist.cursor > 0 }
func (d *Document) CanRedo() bool { return d.hist.cursor < len(d.hist.list) }

func (d *Document) Undo() bool {
	if !d.CanUndo() {
		return false
	}
	d.hist.cursor--
	e := d.hist.list[d.hist.cursor]
	switch e.kind {
	case pixels:
		putRect(d.layers[e.layer].Pix, e.rect, e.before)
	case addLayer:
		d.removeLayer(e.layer)
	}
	return true
}

func (d *Document) Redo() bool {
	if !d.CanRedo() {
		return false
	}
	e := d.hist.list[d.hist.cursor]
	d.hist.cursor++
	switch e.kind {
	case pixels:
		putRect(d.layers[e.layer].Pix, e.rect, e.after)
	case addLayer:
		d.insertLayer(e.layer, e.added)
	}
	return true
}

func copyRect(p *image.NRGBA, r image.Rectangle) []byte {
	out := make([]byte, 0, r.Dx()*r.Dy()*4)
	for y := r.Min.Y; y < r.Max.Y; y++ {
		i := p.PixOffset(r.Min.X, y)
		out = append(out, p.Pix[i:i+r.Dx()*4]...)
	}
	return out
}

func putRect(p *image.NRGBA, r image.Rectangle, src []byte) {
	row := r.Dx() * 4
	for y := r.Min.Y; y < r.Max.Y; y++ {
		i := p.PixOffset(r.Min.X, y)
		copy(p.Pix[i:i+row], src[(y-r.Min.Y)*row:])
	}
}
