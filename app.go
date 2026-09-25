package main

import (
	"fmt"
	"image/color"
	"io"

	"github.com/hajimehoshi/ebiten/v2"
)

var background = color.RGBA{0x2b, 0x2b, 0x2b, 0xff}

type app struct {
	debug bool
	log   io.Writer
}

func (a *app) Update() error {
	if ebiten.IsWindowBeingClosed() {
		return a.exit()
	}
	return nil
}

// exit is the only way out of the run loop: every exit path must pass here so
// that saving on quit, once it exists, cannot be skipped by one of them.
func (a *app) exit() error {
	a.event("exit", "")
	return ebiten.Termination
}

func (a *app) Draw(screen *ebiten.Image) {
	screen.Fill(background)
}

func (a *app) Layout(outsideWidth, outsideHeight int) (screenWidth, screenHeight int) {
	return outsideWidth, outsideHeight
}

func (a *app) event(name, fields string) {
	if !a.debug {
		return
	}
	_, _ = fmt.Fprintf(a.log, "gessetto event: %s %s\n", name, fields)
}
