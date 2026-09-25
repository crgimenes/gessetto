package main

import (
	"bytes"
	"image/png"
	"testing"
)

func TestEmbeddedIcon(t *testing.T) {
	cfg, err := png.DecodeConfig(bytes.NewReader(iconPNG))
	if err != nil {
		t.Fatal(err)
	}
	if cfg.Width != 256 || cfg.Height != 256 {
		t.Fatalf("icon is %dx%d, want 256x256", cfg.Width, cfg.Height)
	}
}
