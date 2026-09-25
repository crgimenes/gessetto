package main

import (
	"bytes"
	_ "embed"
	"image"
	"image/png"

	"github.com/hajimehoshi/ebiten/v2"
)

// Windows and Linux take the window icon from here; macOS uses the bundle's
// assets/gessetto.icns. 256px reduction of the assets/gessetto.png master.
//
//go:embed assets/icon.png
var iconPNG []byte

func setWindowIcon() error {
	img, err := png.Decode(bytes.NewReader(iconPNG))
	if err != nil {
		return err
	}
	ebiten.SetWindowIcon([]image.Image{img})
	return nil
}
