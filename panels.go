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

func (a *app) updatePanels() {
	in := a.in
	pad := float64(a.px(barPad))

	a.top.Begin(in, float64(a.lay.top.Min.X)+pad, float64(a.lay.top.Min.Y)+pad)
	if a.top.Button("undo", "Undo") {
		a.ed.undo()
	}
	a.top.SameLine()
	if a.top.Button("redo", "Redo") {
		a.ed.redo()
	}
	a.top.SameLine()
	a.top.Label("  " + toolHint(a.ed.tool))
	a.top.End()

	a.left.Begin(in, float64(a.lay.left.Min.X)+pad, float64(a.lay.left.Min.Y)+pad)
	a.left.SetItemWidth(float64(a.lay.left.Dx()) - 2*pad)
	for _, t := range tools {
		if a.left.Toggle(ui.ID(t.name), t.name+" ("+t.key+")", a.ed.tool == t.t) {
			a.ed.release()
			a.ed.tool = t.t
		}
	}
	a.left.End()

	if a.lay.showRight {
		a.updateLayers(pad)
	}

	a.updateStatus(pad)
	a.updatePaletteClicks()
}

func toolHint(t tool) string {
	switch t {
	case toolEraser:
		return "Eraser: drag to clear pixels to transparent"
	case toolPicker:
		return "Picker: left click takes the foreground, right click the background"
	}
	return "Pencil: left draws the foreground color, right the background"
}

func (a *app) updateLayers(pad float64) {
	d := a.ed.d
	layers := d.Layers()
	a.right.Begin(a.in, float64(a.lay.right.Min.X)+pad, float64(a.lay.right.Min.Y)+pad)
	a.right.Label("Layers")
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
	}
	if a.right.Button("addlayer", "New Layer") {
		a.addLayer()
	}
	a.right.End()
}

func (a *app) updateStatus(pad float64) {
	b := a.lay.bottom
	y := float64(b.Min.Y) + pad + float64(a.px(swatchPx)) + pad
	a.bottom.Begin(a.in, float64(b.Min.X)+pad, y)
	size := a.ed.d.Bounds().Size()
	info := fmt.Sprintf("%d x %d", size.X, size.Y)
	if a.overCanvas {
		info += fmt.Sprintf("   %d, %d", a.hover.X, a.hover.Y)
	}
	a.bottom.Label(info)
	a.bottom.SameLine()
	if a.bottom.Button("zoomout", "-") {
		a.zoomBy(-1)
	}
	a.bottom.SameLine()
	a.bottom.Label(zoomLabel(a.v.zoom()))
	a.bottom.SameLine()
	if a.bottom.Button("zoomin", "+") {
		a.zoomBy(1)
	}
	a.bottom.SameLine()
	if a.bottom.Button("fit", "Fit") {
		a.fitView()
	}
	a.bottom.SameLine()
	if a.bottom.Toggle("grid", "Grid", a.grid) {
		a.grid = !a.grid
	}
	if a.status != "" {
		a.bottom.SameLine()
		a.bottom.Label("  " + a.status)
	}
	a.bottom.End()
}

func zoomLabel(z float64) string {
	if z >= 1 {
		return fmt.Sprintf("%gx", z)
	}
	return fmt.Sprintf("1/%gx", 1/z)
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
	a.overCanvas = p.In(a.lay.canvas)
	a.hover = a.v.toDoc(p)
	middle := ebiten.IsMouseButtonPressed(ebiten.MouseButtonMiddle)
	right := ebiten.IsMouseButtonPressed(ebiten.MouseButtonRight)
	space := ebiten.IsKeyPressed(ebiten.KeySpace)

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
