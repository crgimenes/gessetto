package main

import (
	"image"
	"image/color"

	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/inpututil"
)

// selectionKeys: arrows nudge by one pixel (ten with Shift), Delete and
// Backspace clear, Escape and Return drop.
func (a *app) selectionKeys() {
	if a.ed.curveStage != 0 && (inpututil.IsKeyJustPressed(ebiten.KeyEscape) || inpututil.IsKeyJustPressed(ebiten.KeyEnter)) {
		a.ed.commitCurve()
	}
	_, ok := a.ed.d.Selection()
	if !ok {
		return
	}
	step := 1
	if ebiten.IsKeyPressed(ebiten.KeyShift) {
		step = 10
	}
	for k, d := range map[ebiten.Key]image.Point{
		ebiten.KeyArrowLeft: {-step, 0}, ebiten.KeyArrowRight: {step, 0},
		ebiten.KeyArrowUp: {0, -step}, ebiten.KeyArrowDown: {0, step},
	} {
		held := inpututil.KeyPressDuration(k)
		if inpututil.IsKeyJustPressed(k) || held > 20 && held%3 == 0 {
			a.ed.moveSelection(d.X, d.Y)
		}
	}
	switch {
	case inpututil.IsKeyJustPressed(ebiten.KeyDelete) || inpututil.IsKeyJustPressed(ebiten.KeyBackspace):
		a.ed.deleteSelection()
	case inpututil.IsKeyJustPressed(ebiten.KeyEscape) || inpututil.IsKeyJustPressed(ebiten.KeyEnter):
		a.ed.drop()
	}
}

// antsDash is the length of a marching-ants dash in logical pixels.
const antsDash = 4

// drawAnts outlines the selection, or the marquee being dragged, with a
// black and white dashed border that crawls, as Paint's does: one white run
// per edge with black dashes over it, a handful of rectangles per frame.
func (a *app) drawAnts(screen *ebiten.Image) {
	r, ok := a.ed.d.Selection()
	if a.ed.marqueeing {
		r, ok = a.ed.marquee, true
	}
	if !ok {
		return
	}
	s := a.v.toScreen(r)
	dst := screen.SubImage(a.lay.canvas).(*ebiten.Image)
	dash := max(a.px(antsDash), 1)
	phase := a.frame / 4 % (2 * dash)
	edge := func(x, y, n int, horizontal bool) {
		seg := func(from, length int, c color.Color) {
			from, end := max(from, 0), min(from+length, n)
			if end <= from {
				return
			}
			if horizontal {
				fillRect(dst, image.Rect(x+from, y, x+end, y+1), c)
				return
			}
			fillRect(dst, image.Rect(x, y+from, x+1, y+end), c)
		}
		seg(0, n, color.White)
		for i := -phase; i < n; i += 2 * dash {
			seg(i, dash, color.Black)
		}
	}
	edge(s.Min.X, s.Min.Y, s.Dx(), true)
	edge(s.Min.X, s.Max.Y-1, s.Dx(), true)
	edge(s.Min.X, s.Min.Y, s.Dy(), false)
	edge(s.Max.X-1, s.Min.Y, s.Dy(), false)
}
