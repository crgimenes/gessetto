package main

import (
	"fmt"
	"image"
	"image/color"
	"io"
	"path/filepath"
	"runtime"

	"github.com/crgimenes/gessetto/doc"
	ui "github.com/crgimenes/minigui"
	"github.com/crgimenes/native/alert"
	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/inpututil"
)

const baseTitle = "gessetto"

var panelBack = color.RGBA{0x2b, 0x2b, 0x2b, 0xff}

type app struct {
	debug bool
	log   io.Writer

	ed   *editor
	path string

	v      view
	grid   bool
	cv     canvasView
	lay    layout
	scale  float64
	screen image.Point
	fitted bool

	top, left, right, bottom, dlg ui.Context
	inject                        ui.Input
	in                            ui.Input

	menu  menuState
	title string

	panning    bool
	panFrom    image.Point
	panOrigin  image.Point
	wheelZoom  float64
	hover      image.Point
	cursor     ebiten.CursorShapeType
	overCanvas bool

	modal  modal
	status string
	quit   bool

	// Hidden by choice and opened for lack of room are separate: widening the
	// window brings back a panel the user never hid, and keeps away one they did.
	layersHidden bool
	layersOpen   bool

	perf perf

	frame int
	pinch pinch

	shotPath   string
	shotFrames int
	shotErr    error
}

func newApp(d *doc.Document, path string, debug bool, log io.Writer) *app {
	a := &app{debug: debug, log: log, ed: newEditor(d), path: path, grid: true, scale: 1}
	a.perf = perf{enabled: debug, log: log}
	a.setScale(ebiten.Monitor().DeviceScaleFactor())
	return a
}

// setScale restyles the widgets for a device scale; the screen is laid out
// in device pixels so text and pixel art stay sharp on high-density displays.
func (a *app) setScale(s float64) {
	if s <= 0 {
		s = 1
	}
	a.scale = s
	face, err := ui.SystemFace(13 * s)
	if err != nil {
		a.status = "No system font found; using the built-in one."
		a.event("font", "found=false")
	}
	st := ui.DefaultStyle()
	st.Face = face
	st.Text = color.RGBA{0xe6, 0xe6, 0xe6, 0xff}
	st.Border = color.RGBA{0x55, 0x55, 0x55, 0xff}
	st.Button = color.RGBA{0x3a, 0x3a, 0x3a, 0xff}
	st.ButtonHot = color.RGBA{0x4a, 0x4a, 0x4a, 0xff}
	st.ButtonOn = color.RGBA{0x2d, 0x5a, 0x88, 0xff}
	st.Field = color.RGBA{0x1a, 0x1a, 0x1a, 0xff}
	st.Focus = color.RGBA{0x4a, 0x9e, 0xff, 0xff}
	st.Selection = color.RGBA{0x2d, 0x5a, 0x88, 0xff}
	st.RowH *= s
	st.Pad *= s
	st.Gap *= s
	st.FieldW *= s
	for _, c := range []*ui.Context{&a.top, &a.left, &a.bottom, &a.dlg} {
		c.SetStyle(st)
	}
	st.FieldW = (rightW - 2*barPad) * s
	a.right.SetStyle(st)
}

func (a *app) Update() error {
	t0 := a.perf.now()
	defer func() {
		a.perf.add(&a.perf.upd, t0)
		a.perf.tick()
	}()
	a.syncMenu()
	a.startPinch()
	a.menu.drain()
	if ebiten.IsWindowBeingClosed() {
		a.requestQuit()
	}
	if a.quit {
		return a.exit()
	}
	if a.shotPath != "" && a.shotFrames >= shotAfter {
		return a.exit()
	}

	a.in = ui.InputFromEbiten()
	a.mergeInjected()

	if a.modal.kind != modalNone {
		a.updateModal()
		a.updateTitle()
		return nil
	}
	if !a.typing() {
		a.runShortcuts()
		a.toolKeys()
	}
	a.updatePanels()
	a.applyPinch()
	a.updateCanvas()
	a.updateTitle()
	return nil
}

func (a *app) mergeInjected() {
	a.in.Copy = a.in.Copy || a.inject.Copy
	a.in.Cut = a.in.Cut || a.inject.Cut
	a.in.Paste = a.in.Paste || a.inject.Paste
	a.in.SelectAll = a.in.SelectAll || a.inject.SelectAll
	a.inject = ui.Input{}
}

func (a *app) toolKeys() {
	for _, t := range tools {
		if inpututil.IsKeyJustPressed(ebiten.KeyA + ebiten.Key(t.key[0]-'A')) {
			a.ed.setTool(t.t)
		}
	}
	a.selectionKeys()
	if inpututil.IsKeyJustPressed(ebiten.KeyX) {
		a.ed.fg, a.ed.bg = a.ed.bg, a.ed.fg
	}
}

// requestQuit is where closing the window, Cmd+Q and the menu all arrive.
func (a *app) requestQuit() {
	a.guard(func() { a.quit = true })
}

// exit is the only way out of the run loop; requestQuit has already offered
// to save.
func (a *app) exit() error {
	a.event("exit", "")
	return ebiten.Termination
}

// guard runs next now, or after the user decides what to do with unsaved
// changes.
func (a *app) guard(next func()) {
	a.ed.release()
	if !a.ed.d.Dirty() {
		next()
		return
	}
	choice, err := showAlert(alert.Options{
		Title:   "Save changes to " + a.docName() + "?",
		Message: "Unsaved changes are lost if you don't save.",
		Buttons: []alert.Button{
			{Title: "Save"},
			{Title: "Cancel", Cancel: true},
			{Title: "Don't Save", Destructive: true},
		},
	})
	if err != nil {
		a.modal = modal{kind: modalUnsaved, next: next}
		return
	}
	switch choice.Button {
	case 0:
		if a.save(false) {
			next()
		}
	case 2:
		next()
	}
}

func (a *app) docName() string {
	if a.path == "" {
		return "Untitled"
	}
	return filepath.Base(a.path)
}

// updateTitle waits for the menu on Windows: the menu finds the window by its
// initial title.
func (a *app) updateTitle() {
	if mainWindowHandle() == nil && runtime.GOOS == "windows" && !a.menu.failed {
		return
	}
	t := a.docName() + " - " + baseTitle
	if a.ed.d.Dirty() {
		t = a.docName() + " (edited) - " + baseTitle
	}
	if t != a.title {
		a.title = t
		ebiten.SetWindowTitle(t)
	}
}

func (a *app) load(d *doc.Document, path string) {
	e := newEditor(d)
	e.tool, e.fg, e.bg = a.ed.tool, a.ed.fg, a.ed.bg
	a.ed = e
	a.path = path
	a.fitView()
}

func (a *app) addLayer() {
	err := a.ed.d.AddLayer(fmt.Sprintf("Layer %d", len(a.ed.d.Layers())+1))
	if err != nil {
		a.status = "Could not add a layer: " + err.Error()
		return
	}
	a.ed.full = true
}

func (a *app) zoomBy(delta int) {
	a.v.zoomAt(a.lay.canvas.Min.Add(a.lay.canvas.Size().Div(2)), delta)
}

// fitCap is the largest zoom Fit picks, in logical pixels per image pixel.
const fitCap = 16

func (a *app) fitView() {
	a.v.fit(a.lay.canvas, a.ed.d.Bounds().Size(), fitCap*a.scale)
}

func (a *app) actualPixels() {
	a.v.step = stepFor(1)
	a.v.center(a.lay.canvas, a.ed.d.Bounds().Size())
}

func stepFor(z float64) int {
	for i, s := range zoomSteps {
		if s == z {
			return i
		}
	}
	return 0
}

func (a *app) Layout(outsideWidth, outsideHeight int) (screenWidth, screenHeight int) {
	s := ebiten.Monitor().DeviceScaleFactor()
	if s != a.scale {
		a.setScale(s)
	}
	w, h := int(float64(outsideWidth)*a.scale), int(float64(outsideHeight)*a.scale)
	a.relayout(image.Pt(w, h))
	return w, h
}

func (a *app) panelMode(w int) panelMode {
	if narrow(w, a.scale) {
		if a.layersOpen {
			return panelOverlay
		}
		return panelHidden
	}
	if a.layersHidden {
		return panelHidden
	}
	return panelDocked
}

func (a *app) relayout(screen image.Point) {
	mode := a.panelMode(screen.X)
	if screen == a.screen && mode == a.lay.mode {
		return
	}
	a.screen = screen
	a.lay = layoutFor(screen.X, screen.Y, a.scale, mode)
	a.cv.resizeChecker(a.lay.canvas.Size(), a.scale)
	if !a.fitted {
		a.fitView()
		a.fitted = true
	}
}

func (a *app) toggleLayers() {
	if narrow(a.screen.X, a.scale) {
		a.layersOpen = !a.layersOpen
	} else {
		a.layersHidden = !a.layersHidden
	}
	a.relayout(a.screen)
}

func (a *app) Draw(screen *ebiten.Image) {
	t0 := a.perf.now()
	defer a.perf.add(&a.perf.draw, t0)
	a.cv.sync(a.ed)
	a.perf.add(&a.perf.sync, t0)
	a.frame++
	a.cv.draw(screen, a.lay.canvas, &a.v, a.grid)
	a.drawAnts(screen)
	if a.overCanvas && a.hover.In(a.ed.d.Bounds()) && a.v.zoom() >= footprintFrom*a.scale {
		a.drawFootprint(screen)
	}
	for _, r := range []image.Rectangle{a.lay.top, a.lay.left, a.lay.right, a.lay.bottom} {
		if !r.Empty() {
			screen.SubImage(r).(*ebiten.Image).Fill(panelBack)
		}
	}
	a.drawSeparators(screen)
	a.drawPalette(screen)
	a.top.Render(screen)
	a.left.Render(screen)
	// A hidden panel skips its frame, so its last commands must not be drawn.
	if a.lay.mode != panelHidden {
		a.right.Render(screen)
	}
	a.bottom.Render(screen)
	if a.modal.kind != modalNone {
		a.drawModal(screen)
	}
	a.takeShot(screen)
}

func (a *app) event(name, fields string) {
	if !a.debug {
		return
	}
	_, _ = fmt.Fprintf(a.log, "gessetto event: %s %s\n", name, fields)
}
