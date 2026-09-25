package main

import (
	"bytes"
	"fmt"
	"image"
	"image/color"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/crgimenes/gessetto/doc"
	ui "github.com/crgimenes/minigui"
	"github.com/crgimenes/native/filedialog"
	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/inpututil"
)

type modalKind int

const (
	modalNone modalKind = iota
	modalNew
	modalUnsaved
)

// modal is the one dialog that can be open. Dialogs are only for decisions:
// a new image's size, and what to do with unsaved changes.
type modal struct {
	kind  modalKind
	w, h  string
	err   string
	next  func()
	fresh bool
	rect  image.Rectangle
}

var (
	dimColor     = color.RGBA{0, 0, 0, 0x99}
	dialogBack   = color.RGBA{0x33, 0x33, 0x33, 0xff}
	destructive  = color.RGBA{0xff, 0x73, 0x69, 0xff}
	defaultNewPx = 64
)

func (a *app) openNewDialog() {
	a.modal = modal{
		kind:  modalNew,
		w:     strconv.Itoa(defaultNewPx),
		h:     strconv.Itoa(defaultNewPx),
		fresh: true,
	}
}

func (a *app) closeModal() {
	a.dlg.ClearFocus()
	a.modal = modal{}
}

func (a *app) updateModal() {
	m := &a.modal
	s := a.scale
	w, h := int(340*s), int(190*s)
	c := a.screen.Div(2)
	m.rect = image.Rect(c.X-w/2, c.Y-h/2, c.X+w/2, c.Y+h/2)
	pad := float64(a.px(barPad * 2))
	a.dlg.Begin(a.in, float64(m.rect.Min.X)+pad, float64(m.rect.Min.Y)+pad)
	escape := inpututil.IsKeyJustPressed(ebiten.KeyEscape)

	switch m.kind {
	case modalNew:
		a.newDialog(escape)
	case modalUnsaved:
		a.unsavedDialog(escape)
	}
	a.dlg.End()
}

func (a *app) newDialog(escape bool) {
	m := &a.modal
	a.dlg.Label("New image (pixels)")
	a.dlg.Label("Width")
	a.dlg.SameLine()
	a.dlg.TextField("w", &m.w)
	if m.fresh {
		a.dlg.Focus("w")
		m.fresh = false
	}
	a.dlg.Label("Height")
	a.dlg.SameLine()
	a.dlg.TextField("h", &m.h)
	if m.err != "" {
		a.redLabel(m.err)
	}
	cancel := a.dlg.Button("cancel", "Cancel")
	a.dlg.SameLine()
	create := a.dlg.Button("create", "Create")
	create = create || a.dlg.Submitted("w") || a.dlg.Submitted("h")
	switch {
	case escape || cancel:
		a.closeModal()
	case create:
		a.createNew()
	}
}

func (a *app) createNew() {
	m := &a.modal
	w, errW := strconv.Atoi(strings.TrimSpace(m.w))
	h, errH := strconv.Atoi(strings.TrimSpace(m.h))
	if errW != nil || errH != nil {
		m.err = "Width and height must be whole numbers."
		return
	}
	d, err := doc.New(w, h)
	if err != nil {
		m.err = fmt.Sprintf("From 1 to %d pixels per side.", doc.MaxSide)
		return
	}
	a.closeModal()
	a.load(d, "")
	a.status = ""
}

func (a *app) unsavedDialog(escape bool) {
	m := &a.modal
	a.dlg.Label("Save changes to " + a.docName() + "?")
	a.dlg.Label("Unsaved changes are lost if you don't save.")
	a.dlg.Label("")
	// Destructive on the far left in red, the safe default on the right.
	discard := a.redButton("discard", "Don't Save")
	a.dlg.SameLine()
	cancel := a.dlg.Button("cancel", "Cancel")
	a.dlg.SameLine()
	save := a.dlg.Button("save", "Save")
	next := m.next
	switch {
	case escape || cancel:
		a.closeModal()
	case discard:
		a.closeModal()
		next()
	case save:
		a.closeModal()
		if a.save(false) {
			next()
		}
	}
}

func (a *app) redLabel(s string) {
	st := a.dlg.Style()
	red := st
	red.Text = destructive
	a.dlg.SetStyle(red)
	a.dlg.Label(s)
	a.dlg.SetStyle(st)
}

func (a *app) redButton(id, label string) bool {
	st := a.dlg.Style()
	red := st
	red.Text = destructive
	a.dlg.SetStyle(red)
	clicked := a.dlg.Button(ui.ID(id), label)
	a.dlg.SetStyle(st)
	return clicked
}

func (a *app) drawModal(screen *ebiten.Image) {
	fillRect(screen, screen.Bounds(), dimColor)
	fillRect(screen, a.modal.rect, dialogBack)
	strokeRect(screen, a.modal.rect, color.RGBA{0x66, 0x66, 0x66, 0xff})
	a.dlg.Render(screen)
}

var pngOnly = []string{"png"}

// openFile shows the system panel; it must run on the main thread and blocks
// the frame until the user answers, which is what a modal panel means.
func (a *app) openFile() {
	var path string
	ebiten.RunOnMainThread(func() {
		path = filedialog.Open(filedialog.Options{Title: "Open image", Extensions: pngOnly})
	})
	if path == "" {
		return
	}
	d, err := readPNG(path)
	if err != nil {
		a.status = "Could not open: " + err.Error()
		a.event("open", "ok=false")
		return
	}
	a.load(d, path)
	a.status = ""
	a.event("open", "ok=true")
}

// save writes the flattened PNG, asking for a path when there is none or
// when asked to; it reports whether the document is now saved.
func (a *app) save(as bool) bool {
	a.ed.release()
	path := a.path
	if as || path == "" {
		name := "untitled.png"
		if a.path != "" {
			name = filepath.Base(a.path)
		}
		ebiten.RunOnMainThread(func() {
			path = filedialog.Save(filedialog.Options{Title: "Save image", Filename: name, Extensions: pngOnly})
		})
		if path == "" {
			return false
		}
		if !strings.EqualFold(filepath.Ext(path), ".png") {
			path += ".png"
		}
	}
	var buf bytes.Buffer
	err := a.ed.d.EncodePNG(&buf)
	if err == nil {
		err = writeFileAtomic(path, buf.Bytes())
	}
	if err != nil {
		a.status = "Could not save: " + err.Error()
		a.event("save", "ok=false")
		return false
	}
	a.ed.d.MarkSaved()
	a.path = path
	a.status = "Saved " + filepath.Base(path)
	n := len(a.ed.d.Layers())
	if n > 1 {
		a.status += fmt.Sprintf(" (PNG keeps one flattened image; the %d layers stay only while the document is open)", n)
	}
	a.event("save", "ok=true")
	return true
}
