package main

import (
	"bytes"
	"image"
	"image/png"

	"github.com/hajimehoshi/ebiten/v2"
)

// shotAfter lets layout, fonts and the menu settle before the frame is kept.
const shotAfter = 10

// takeShot saves what the window shows, read back from the screen itself: an
// agent checks the interface from the pixels without screen-recording rights.
func (a *app) takeShot(screen *ebiten.Image) {
	if a.shotPath == "" {
		return
	}
	a.shotFrames++
	if a.shotFrames != shotAfter {
		return
	}
	b := screen.Bounds()
	img := image.NewRGBA(b)
	screen.ReadPixels(img.Pix)
	var buf bytes.Buffer
	a.shotErr = png.Encode(&buf, img)
	if a.shotErr == nil {
		a.shotErr = writeFileAtomic(a.shotPath, buf.Bytes())
	}
}
