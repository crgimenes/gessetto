package doc

import (
	"image"
	"slices"
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
// saved is the cursor value that matches the file on disk, -1 when no reachable
// state does.
type history struct {
	list    []entry
	cursor  int
	bytes   int
	max     int
	dropped int
	saved   int

	grouping bool
	group    []entry
}

func (h *history) push(e entry) {
	if h.grouping {
		h.group = append(h.group, e)
		return
	}
	for _, r := range h.list[h.cursor:] {
		h.bytes -= r.size
	}
	if h.saved > h.cursor {
		h.saved = -1
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
	h.saved -= n
	if h.saved < 0 {
		h.saved = -1
	}
}

// BeginGroup collects the following pixel edits into one undo step, the way a
// stroke is drawn from many segments but undone at once. Edits must stay on
// one layer until EndGroup; SelectLayer ends the group.
func (d *Document) BeginGroup() {
	d.EndGroup()
	d.hist.grouping = true
}

func (d *Document) EndGroup() {
	h := &d.hist
	if !h.grouping {
		return
	}
	h.grouping = false
	g := h.group
	h.group = nil
	if len(g) == 0 {
		return
	}
	r := g[0].rect
	for _, e := range g[1:] {
		r = r.Union(e.rect)
	}
	p := d.layers[g[0].layer].Pix
	after := copyRect(p, r)
	// Rebuild the state before the group by laying each edit's "before" back
	// over the current pixels, newest first.
	scratch := image.NewNRGBA(r)
	putRect(scratch, r, after)
	for _, v := range slices.Backward(g) {
		putRect(scratch, v.rect, v.before)
	}
	before := copyRect(scratch, r)
	h.push(entry{
		kind:   pixels,
		layer:  g[0].layer,
		rect:   r,
		before: before,
		after:  after,
		size:   len(before) + len(after),
	})
}

// RevertGroup undoes the edits made since BeginGroup and keeps grouping: a
// shape tool redraws its preview this way on every pointer move, so the
// preview is the real drawing and releasing the pointer only commits it.
func (d *Document) RevertGroup() {
	h := &d.hist
	for _, e := range slices.Backward(h.group) {
		putRect(d.layers[e.layer].Pix, e.rect, e.before)
	}
	h.group = h.group[:0]
}

// Dirty reports whether the document differs from what was last saved.
func (d *Document) Dirty() bool { return d.hist.cursor != d.hist.saved }

func (d *Document) MarkSaved() {
	d.Drop()
	d.EndGroup()
	d.hist.saved = d.hist.cursor
}

// Dropped counts the oldest edits discarded to stay under the history budget;
// they can no longer be undone.
func (d *Document) Dropped() int { return d.hist.dropped }

func (d *Document) CanUndo() bool { return d.hist.cursor > 0 }
func (d *Document) CanRedo() bool { return d.hist.cursor < len(d.hist.list) }

// Undo with a floating selection puts its pixels back where they were.
func (d *Document) Undo() bool {
	if d.cancelFloat() {
		return true
	}
	d.sel = nil
	d.EndGroup()
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
	d.Drop()
	d.EndGroup()
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
