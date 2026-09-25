package main

import (
	"io/fs"
	"testing"

	ui "github.com/crgimenes/minigui"
)

// Every embedded icon must parse, not only the ones a test happens to touch.
func TestEmbeddedIconsParse(t *testing.T) {
	names, err := fs.Glob(iconFS, "assets/icons/*.svg")
	if err != nil || len(names) == 0 {
		t.Fatalf("no icons embedded: %v", err)
	}
	for _, n := range names {
		data, err := iconFS.ReadFile(n)
		if err != nil {
			t.Fatal(err)
		}
		_, err = ui.NewIcon(data)
		if err != nil {
			t.Errorf("%s: %v", n, err)
		}
	}
}
