package main

import (
	"fmt"
	"image"
	"image/color"
	"math"
	"strconv"

	ui "github.com/crgimenes/minigui"
	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/inpututil"
	"github.com/hajimehoshi/ebiten/v2/vector"
)

var palette = parsePalette(ui.VGAPalette)

func parsePalette(hex []string) []color.NRGBA {
	out := make([]color.NRGBA, 0, len(hex))
	for _, h := range hex {
		n, err := strconv.ParseUint(h[1:], 16, 32)
		if err != nil {
			panic("bad palette entry " + h)
		}
		out = append(out, color.NRGBA{R: byte(n >> 16), G: byte(n >> 8), B: byte(n), A: 0xff}) // #nosec G115 -- truncation picks each byte
	}
	return out
}

// Palette geometry in logical pixels.
const (
	swatchPx  = 22
	colorsPx  = 34 // foreground/background indicator
	barPad    = 6
	wheelStep = 40 // pan distance per wheel unit, logical pixels
)

func (a *app) px(v float64) int { return int(math.Round(v * a.scale)) }

// paletteRects places the color indicator and the swatches on the first row
// of the bottom bar; swatches that do not fit the width are left out.
func (a *app) paletteRects() (colors image.Rectangle, swatches []image.Rectangle) {
	b := a.lay.bottom
	y := b.Min.Y + a.px(barPad)
	x := b.Min.X + a.px(barPad)
	colors = image.Rect(x, y, x+a.px(colorsPx), y+a.px(swatchPx))
	x = colors.Max.X + a.px(barPad)
	for range palette {
		r := image.Rect(x, y, x+a.px(swatchPx), y+a.px(swatchPx))
		if r.Max.X > b.Max.X-a.px(barPad) {
			break
		}
		swatches = append(swatches, r)
		x = r.Max.X + a.px(2)
	}
	return colors, swatches
}

var (
	mutedText = color.RGBA{0x99, 0x99, 0x99, 0xff}
	separator = color.RGBA{0x1a, 0x1a, 0x1a, 0xff}
	accent    = color.RGBA{0x2d, 0x5a, 0x88, 0xff}
	accentHot = color.RGBA{0x3a, 0x6f, 0xa6, 0xff}
)

func toolIcon(t tool) *ui.Icon {
	switch t {
	case toolEraser:
		return iconEraser
	case toolPicker:
		return iconPicker
	case toolFill:
		return iconFill
	case toolLine:
		return iconLine
	case toolRect:
		return iconRect
	case toolEllipse:
		return iconEllipse
	}
	return iconPencil
}

func (a *app) updatePanels() {
	in := a.in
	pad := float64(a.px(barPad))

	a.top.Begin(in, float64(a.lay.top.Min.X)+pad, float64(a.lay.top.Min.Y)+pad)
	if a.top.IconToggle("layers", iconLayers, "", a.lay.mode != panelHidden) {
		a.toggleLayers()
	}
	a.top.SameLine()
	if a.top.IconButton("undo", iconUndo, "Undo") {
		a.ed.undo()
	}
	a.top.SameLine()
	if a.top.IconButton("redo", iconRedo, "Redo") {
		a.ed.redo()
	}
	a.toolOptions()
	a.top.SameLine()
	a.top.Label("  " + toolHint(a.ed.tool))
	a.top.End()

	// Icons only, two to a row; the options bar names the tool in use.
	a.left.Begin(in, float64(a.lay.left.Min.X)+pad, float64(a.lay.left.Min.Y)+pad)
	for i, t := range tools {
		if i%2 == 1 {
			a.left.SameLine()
		}
		if a.left.IconToggle(ui.ID(t.name), toolIcon(t.t), "", a.ed.tool == t.t) {
			a.ed.release()
			a.ed.tool = t.t
		}
	}
	a.left.End()

	if a.lay.mode != panelHidden {
		a.updateLayers(pad)
	}

	a.updateStatus(pad)
	a.updatePaletteClicks()
}

// sectionLabel titles a panel section the house way: capitals, muted.
func sectionLabel(c *ui.Context, s string) {
	st := c.Style()
	muted := st
	muted.Text = mutedText
	c.SetStyle(muted)
	c.Label(s)
	c.SetStyle(st)
}

// primaryButton is the safe default of a dialog, drawn in the accent color
// and placed rightmost by its caller.
func primaryButton(c *ui.Context, id ui.ID, label string) bool {
	st := c.Style()
	p := st
	p.Button, p.ButtonHot, p.Border = accent, accentHot, accentHot
	c.SetStyle(p)
	clicked := c.Button(id, label)
	c.SetStyle(st)
	return clicked
}

func (a *app) drawSeparators(screen *ebiten.Image) {
	l := a.lay
	fillRect(screen, image.Rect(l.top.Min.X, l.top.Max.Y-1, l.top.Max.X, l.top.Max.Y), separator)
	fillRect(screen, image.Rect(l.bottom.Min.X, l.bottom.Min.Y, l.bottom.Max.X, l.bottom.Min.Y+1), separator)
	fillRect(screen, image.Rect(l.left.Max.X-1, l.left.Min.Y, l.left.Max.X, l.left.Max.Y), separator)
	if !l.right.Empty() {
		fillRect(screen, image.Rect(l.right.Min.X, l.right.Min.Y, l.right.Min.X+1, l.right.Max.Y), separator)
	}
}

// toolOptions shows what the active tool can be set to, next to Undo/Redo.
func (a *app) toolOptions() {
	switch a.ed.tool {
	case toolRect, toolEllipse:
		a.top.SameLine()
		if a.top.IconToggle("outline", toolIcon(a.ed.tool), "Outline", !a.ed.filled) {
			a.ed.filled = false
		}
		a.top.SameLine()
		if a.top.IconToggle("filled", iconFilled, "Filled", a.ed.filled) {
			a.ed.filled = true
		}
	case toolFill:
		a.top.SameLine()
		a.top.Label(fmt.Sprintf("  Tolerance %3d", a.ed.tolerance))
		a.top.SameLine()
		v := float64(a.ed.tolerance)
		if a.top.Slider("tolerance", &v, 0, 255) {
			a.ed.tolerance = int(math.Round(v))
		}
	}
}

func toolHint(t tool) string {
	for _, d := range tools {
		if d.t != t {
			continue
		}
		hint := d.name + " (" + d.key + ")"
		switch t {
		case toolEraser:
			return hint + ": clears pixels to transparent"
		case toolPicker:
			return hint + ": left takes the foreground, right the background"
		case toolFill:
			return hint + ": fills the area of one color on the active layer"
		case toolLine, toolRect, toolEllipse:
			return hint + ": drag; Shift keeps it straight or square"
		}
		return hint + ": left draws the foreground, right the background"
	}
	return ""
}

func (a *app) updateLayers(pad float64) {
	d := a.ed.d
	layers := d.Layers()
	a.right.Begin(a.in, float64(a.lay.right.Min.X)+pad, float64(a.lay.right.Min.Y)+pad)
	sectionLabel(&a.right, "LAYERS")
	names := make([]string, len(layers))
	for i, l := range layers {
		names[len(layers)-1-i] = l.Name
	}
	sel := len(layers) - 1 - d.Active()
	if a.right.List("layers", names, &sel) {
		a.ed.release()
		err := d.SelectLayer(len(layers) - 1 - sel)
		if err != nil {
			a.status = err.Error()
		}
		// Over the canvas the panel hides what was just chosen for.
		if a.lay.mode == panelOverlay {
			a.layersOpen = false
			a.relayout(a.screen)
		}
	}
	if a.right.IconButton("addlayer", iconAdd, "New Layer") {
		a.addLayer()
	}
	a.right.End()
}

func (a *app) updateStatus(pad float64) {
	b := a.lay.bottom
	y := float64(b.Min.Y) + pad + float64(a.px(swatchPx)) + pad
	// Buttons first: text whose width changes (zoom, cursor position, status)
	// goes after them, or every digit that changes would move the buttons.
	a.bottom.Begin(a.in, float64(b.Min.X)+pad, y)
	if a.bottom.IconButton("zoomout", iconZoomOut, "") {
		a.zoomBy(-1)
	}
	a.bottom.SameLine()
	if a.bottom.IconButton("zoomin", iconZoomIn, "") {
		a.zoomBy(1)
	}
	a.bottom.SameLine()
	if a.bottom.IconButton("fit", iconFit, "Fit") {
		a.fitView()
	}
	a.bottom.SameLine()
	if a.bottom.IconToggle("grid", iconGrid, "Grid", a.grid) {
		a.grid = !a.grid
	}
	size := a.ed.d.Bounds().Size()
	info := fmt.Sprintf("  %s   %d x %d", zoomLabel(a.v.zoom()/a.scale), size.X, size.Y)
	if a.overCanvas {
		info += fmt.Sprintf("   %d, %d", a.hover.X, a.hover.Y)
	}
	if a.status != "" {
		info += "   " + a.status
	}
	a.bottom.SameLine()
	a.bottom.Label(info)
	a.bottom.End()
}

func zoomLabel(z float64) string {
	if z >= 1 {
		return fmt.Sprintf("%gx", z)
	}
	return fmt.Sprintf("1/%gx", math.Round(1/z*100)/100)
}

func (a *app) updatePaletteClicks() {
	left := a.in.MouseClicked
	right := inpututil.IsMouseButtonJustPressed(ebiten.MouseButtonRight)
	if !left && !right {
		return
	}
	p := image.Pt(int(a.in.MouseX), int(a.in.MouseY))
	colors, swatches := a.paletteRects()
	if p.In(colors) {
		a.ed.fg, a.ed.bg = a.ed.bg, a.ed.fg
		return
	}
	for i, r := range swatches {
		if !p.In(r) {
			continue
		}
		if right {
			a.ed.bg = palette[i]
			return
		}
		a.ed.fg = palette[i]
		return
	}
}

func (a *app) drawPalette(screen *ebiten.Image) {
	colors, swatches := a.paletteRects()
	border := color.RGBA{0x77, 0x77, 0x77, 0xff}
	for i, r := range swatches {
		fillRect(screen, r, palette[i])
		strokeRect(screen, r, border)
	}
	// Background square behind and below-right, foreground on top-left.
	side := colors.Dy() * 3 / 4
	bg := image.Rect(colors.Max.X-side, colors.Max.Y-side, colors.Max.X, colors.Max.Y)
	fg := image.Rect(colors.Min.X, colors.Min.Y, colors.Min.X+side, colors.Min.Y+side)
	fillRect(screen, bg, a.ed.bg)
	strokeRect(screen, bg, border)
	fillRect(screen, fg, a.ed.fg)
	strokeRect(screen, fg, color.White)
}

func fillRect(dst *ebiten.Image, r image.Rectangle, c color.Color) {
	vector.FillRect(dst, float32(r.Min.X), float32(r.Min.Y), float32(r.Dx()), float32(r.Dy()), c, false)
}

func strokeRect(dst *ebiten.Image, r image.Rectangle, c color.Color) {
	vector.StrokeRect(dst, float32(r.Min.X)+0.5, float32(r.Min.Y)+0.5, float32(r.Dx())-1, float32(r.Dy())-1, 1, c, false)
}

func (a *app) updateCanvas() {
	p := image.Pt(int(a.in.MouseX), int(a.in.MouseY))
	a.overCanvas = p.In(a.lay.canvas) && !p.In(a.lay.right)
	a.hover = a.v.toDoc(p)
	middle := ebiten.IsMouseButtonPressed(ebiten.MouseButtonMiddle)
	right := ebiten.IsMouseButtonPressed(ebiten.MouseButtonRight)
	space := ebiten.IsKeyPressed(ebiten.KeySpace)
	a.ed.constrain = ebiten.IsKeyPressed(ebiten.KeyShift)
	defer a.updateCursor(space)

	switch {
	case a.panning:
		if middle || space && a.in.MouseDown {
			a.v.origin = a.panOrigin.Add(p.Sub(a.panFrom))
			return
		}
		a.panning = false
	case a.ed.stroking:
		held := a.in.MouseDown
		if a.ed.secondary {
			held = right
		}
		if held {
			a.ed.drag(a.hover)
			return
		}
		a.ed.release()
	case !a.overCanvas:
		return
	case inpututil.IsMouseButtonJustPressed(ebiten.MouseButtonMiddle) || space && a.in.MouseClicked:
		a.panning = true
		a.panFrom = p
		a.panOrigin = a.v.origin
		return
	case a.in.MouseClicked:
		a.ed.press(a.hover, false)
	case inpututil.IsMouseButtonJustPressed(ebiten.MouseButtonRight):
		a.ed.press(a.hover, true)
	}
	if a.overCanvas {
		a.wheel(p)
	}
}

// updateCursor uses the system's own shapes: a cursor the app drew itself
// would trail the pointer by a frame, which is what makes drawing feel slow.
func (a *app) updateCursor(space bool) {
	shape := ebiten.CursorShapeDefault
	switch {
	case a.panning || a.overCanvas && space:
		shape = ebiten.CursorShapeMove
	case a.overCanvas || a.ed.stroking:
		shape = ebiten.CursorShapeCrosshair
	}
	if shape != a.cursor {
		a.cursor = shape
		ebiten.SetCursorShape(shape)
	}
}

// wheel pans, or zooms around the pointer with Cmd/Ctrl held. Trackpads send
// small fractions, so zoom steps are taken from an accumulated delta.
func (a *app) wheel(p image.Point) {
	wx, wy := ebiten.Wheel()
	if wx == 0 && wy == 0 {
		return
	}
	if ebiten.IsKeyPressed(ebiten.KeyMeta) || ebiten.IsKeyPressed(ebiten.KeyControl) {
		a.wheelZoom += wy
		for a.wheelZoom >= 1 {
			a.v.zoomAt(p, 1)
			a.wheelZoom--
		}
		for a.wheelZoom <= -1 {
			a.v.zoomAt(p, -1)
			a.wheelZoom++
		}
		return
	}
	step := float64(a.px(wheelStep))
	a.v.origin = a.v.origin.Add(image.Pt(int(math.Round(wx*step)), int(math.Round(wy*step))))
}
