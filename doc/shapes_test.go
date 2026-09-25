package doc

import (
	"image/color"
	"strings"
	"testing"
)

var ink = color.NRGBA{R: 0xff, A: 0xff}

// pattern draws the active layer as rows of '#' (any alpha) and '.'.
func pattern(d *Document) string {
	img := d.Flatten()
	var b strings.Builder
	for y := range d.height {
		for x := range d.width {
			c := byte('.')
			if img.NRGBAAt(x, y).A > 0 {
				c = '#'
			}
			b.WriteByte(c)
		}
		b.WriteByte('\n')
	}
	return b.String()
}

func TestShapes(t *testing.T) {
	tests := []struct {
		name string
		w, h int
		draw func(d *Document) error
		want string
	}{
		{"rect outline", 4, 3, func(d *Document) error { return d.Rect(3, 2, 0, 0, ink, false) }, "####\n#..#\n####\n"},
		{"rect filled clipped", 3, 2, func(d *Document) error { return d.Rect(1, -5, 9, 0, ink, true) }, ".##\n...\n"},
		{"circle 5", 5, 5, func(d *Document) error { return d.Ellipse(0, 0, 4, 4, ink, false) }, ".###.\n#...#\n#...#\n#...#\n.###.\n"},
		{"even ellipse", 6, 4, func(d *Document) error { return d.Ellipse(0, 0, 5, 3, ink, false) }, ".####.\n#....#\n#....#\n.####.\n"},
		{"filled ellipse", 7, 3, func(d *Document) error { return d.Ellipse(6, 2, 0, 0, ink, true) }, ".#####.\n#######\n.#####.\n"},
		{"thin ellipse keeps its tips", 2, 5, func(d *Document) error { return d.Ellipse(0, 0, 1, 4, ink, false) }, "##\n##\n##\n##\n##\n"},
		{"zero-width ellipse is a line", 1, 4, func(d *Document) error { return d.Ellipse(0, 0, 0, 3, ink, false) }, "#\n#\n#\n#\n"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			d, err := New(tt.w, tt.h)
			if err != nil {
				t.Fatal(err)
			}
			err = tt.draw(d)
			if err != nil {
				t.Fatal(err)
			}
			got := pattern(d)
			if got != tt.want {
				t.Fatalf("got\n%swant\n%s", got, tt.want)
			}
		})
	}
}

func TestFill(t *testing.T) {
	d, err := New(5, 4)
	if err != nil {
		t.Fatal(err)
	}
	wall := color.NRGBA{B: 0xff, A: 0xff}
	// A wall with a diagonal gap: 4-connected fill must not leak through it.
	_ = d.Line(2, 0, 2, 1, wall)
	_ = d.Line(3, 2, 4, 2, wall)
	_ = d.SetPixel(0, 3, color.NRGBA{A: 0x10})
	d.Fill(0, 0, ink, 0)
	got := pattern(d)
	want := "###..\n###..\n#####\n#####\n"
	if got != want {
		t.Fatalf("tolerance 0 got\n%swant\n%s", got, want)
	}
	if d.PixelAt(0, 3) != (color.NRGBA{A: 0x10}) {
		t.Fatal("tolerance 0 must stop at a pixel whose alpha differs")
	}
	d.Undo()
	d.Fill(0, 0, ink, 0x10)
	if d.PixelAt(0, 3) != ink {
		t.Fatal("tolerance 16 must take a pixel 16 away in alpha")
	}
	if d.PixelAt(3, 0) != (color.NRGBA{}) {
		t.Fatal("the fill leaked through the diagonal gap")
	}
}

func TestFillWithOwnColorIsNoStep(t *testing.T) {
	d, err := New(3, 3)
	if err != nil {
		t.Fatal(err)
	}
	d.Fill(1, 1, color.NRGBA{}, 0)
	if d.CanUndo() {
		t.Fatal("a fill that changes nothing must not become an undo step")
	}
}

func TestRevertGroupKeepsGrouping(t *testing.T) {
	d, err := New(4, 1)
	if err != nil {
		t.Fatal(err)
	}
	d.BeginGroup()
	_ = d.Line(0, 0, 3, 0, ink)
	d.RevertGroup()
	if d.PixelAt(3, 0) != (color.NRGBA{}) {
		t.Fatal("revert must restore the pixels")
	}
	_ = d.SetPixel(1, 0, ink)
	d.EndGroup()
	if pattern(d) != ".#..\n" {
		t.Fatalf("got %q, want only the drawing after the revert", pattern(d))
	}
	d.Undo()
	if d.CanUndo() || pattern(d) != "....\n" {
		t.Fatal("the reverted preview must not leave an undo step of its own")
	}
}
