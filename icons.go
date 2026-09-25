package main

import (
	"embed"

	ui "github.com/crgimenes/minigui"
)

// Bootstrap Icons (https://icons.getbootstrap.com), MIT; the license travels
// with the files in assets/icons.
//
//go:embed assets/icons/*.svg
var iconFS embed.FS

func loadIcon(name string) *ui.Icon {
	data, err := iconFS.ReadFile("assets/icons/" + name + ".svg")
	if err != nil {
		panic(err)
	}
	return ui.MustIcon(data)
}

var (
	iconPencil  = loadIcon("pencil")
	iconEraser  = loadIcon("eraser")
	iconPicker  = loadIcon("eyedropper")
	iconUndo    = loadIcon("arrow-counterclockwise")
	iconRedo    = loadIcon("arrow-clockwise")
	iconAdd     = loadIcon("plus-lg")
	iconZoomIn  = loadIcon("zoom-in")
	iconZoomOut = loadIcon("zoom-out")
	iconFit     = loadIcon("fullscreen")
	iconGrid    = loadIcon("grid-3x3")
	iconLayers  = loadIcon("layers")
	iconFill    = loadIcon("paint-bucket")
	iconLine    = loadIcon("slash-lg")
	iconRect    = loadIcon("square")
	iconEllipse = loadIcon("circle")
	iconFilled  = loadIcon("square-fill")
)
