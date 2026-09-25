package doc

import (
	"image"
	"image/color"
	"slices"
	"testing"
)

func TestRoundFootprints(t *testing.T) {
	tests := []struct {
		w    int
		want []image.Point
	}{
		{1, []image.Point{{0, 0}}},
		{2, []image.Point{{0, 0}, {1, 0}, {0, 1}, {1, 1}}},
		{3, []image.Point{{0, -1}, {-1, 0}, {0, 0}, {1, 0}, {0, 1}}},
	}
	for _, tt := range tests {
		got := Pen{Width: tt.w, Round: true}.footprint()
		if !slices.Equal(got, tt.want) {
			t.Errorf("round %d: %v, want %v", tt.w, got, tt.want)
		}
	}
	n := len(Pen{Width: 4, Round: true}.footprint())
	if n != 12 {
		t.Errorf("round 4 covers %d pixels, want 12", n)
	}
	n = len(Pen{Width: 4}.footprint())
	if n != 16 {
		t.Errorf("square 4 covers %d pixels, want 16", n)
	}
	if (Pen{Width: 999}).width() != MaxWidth || (Pen{}).width() != 1 {
		t.Error("width must clamp to 1..MaxWidth")
	}
}

func TestThickShapes(t *testing.T) {
	tests := []struct {
		name string
		w, h int
		draw func(d *Document) error
		want string
	}{
		{"square line runs past its ends", 10, 5, func(d *Document) error { return d.Line(1, 2, 8, 2, ink, Pen{Width: 3}) },
			"..........\n##########\n##########\n##########\n..........\n"},
		{"thick rect grows inward", 8, 6, func(d *Document) error { return d.Rect(0, 0, 7, 5, StyleOutline, ink, ink, Pen{Width: 2}) },
			"########\n########\n##....##\n##....##\n########\n########\n"},
		{"thick round ellipse stays in its box", 11, 9, func(d *Document) error {
			return d.Ellipse(0, 0, 10, 8, StyleOutline, ink, ink, Pen{Width: 3, Round: true})
		}, "...#####...\n..#######..\n.#########.\n###.....###\n###.....###\n###.....###\n.#########.\n..#######..\n...#####...\n"},
		{"filled shapes ignore the pen", 4, 3, func(d *Document) error { return d.Rect(1, 1, 2, 1, StyleFill, ink, ink, Pen{Width: 9}) },
			"....\n.##.\n....\n"},
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

func TestStyles(t *testing.T) {
	fill := ink
	fill.G = 0xff
	line := ink
	d, err := New(6, 5)
	if err != nil {
		t.Fatal(err)
	}
	err = d.Rect(0, 0, 5, 4, StyleBoth, line, fill, Pen{Width: 2})
	if err != nil {
		t.Fatal(err)
	}
	if d.PixelAt(1, 1) != line || d.PixelAt(2, 2) != fill || d.PixelAt(3, 2) != fill {
		t.Fatalf("want a 2px outline around a filled middle, got\\n%s", pattern(d))
	}
	d2, _ := New(5, 5)
	_ = d2.Ellipse(0, 0, 4, 4, StyleFill, line, fill, Pen{Width: 3})
	if d2.PixelAt(0, 2) != fill || d2.PixelAt(0, 0) != (color.NRGBA{}) {
		t.Fatal("fill only must paint the area in the fill color and nothing outside it")
	}
}
