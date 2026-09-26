package doc

import (
	"image"
	"image/color"
	"slices"
	"strings"
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

func TestRoundDiv(t *testing.T) {
	for _, tt := range []struct{ n, d, want int64 }{
		{7, 2, 4}, {5, 2, 3}, {4, 2, 2}, {-5, 2, -2}, {-7, 2, -3}, {-4, 2, -2}, {1, 3, 0}, {2, 3, 1}, {-1, 3, 0}, {-2, 3, -1},
	} {
		got := roundDiv(tt.n, tt.d)
		if got != tt.want {
			t.Errorf("roundDiv(%d, %d) = %d, want %d", tt.n, tt.d, got, tt.want)
		}
	}
}

func TestCurve(t *testing.T) {
	d, err := New(9, 5)
	if err != nil {
		t.Fatal(err)
	}
	// Straight controls on the line give the line itself.
	err = d.Curve(image.Pt(0, 2), image.Pt(3, 2), image.Pt(5, 2), image.Pt(8, 2), ink, Pen{})
	if err != nil {
		t.Fatal(err)
	}
	if pattern(d) != ".........\n.........\n#########\n.........\n.........\n" {
		t.Fatalf("a curve with controls on its line must be that line, got\n%s", pattern(d))
	}
	d2, _ := New(9, 5)
	_ = d2.Curve(image.Pt(0, 4), image.Pt(0, 0), image.Pt(8, 0), image.Pt(8, 4), ink, Pen{})
	got := pattern(d2)
	if got[0:9] != "........." || d2.PixelAt(4, 1) != ink || d2.PixelAt(0, 4) != ink || d2.PixelAt(8, 4) != ink {
		t.Fatalf("an arch from (0,4) to (8,4) peaking at y=1, got\n%s", got)
	}
}

func TestCoverHoldsEveryPoint(t *testing.T) {
	got := Cover(image.Pt(3, 3), image.Pt(0, 5), image.Pt(7, 1))
	if got != image.Rect(0, 1, 8, 6) {
		t.Fatalf("Cover = %v, want (0,1)-(8,6)", got)
	}
}

// A shape's bounds are what undo records; drawing outside them would leave
// pixels no revert can reach. The curve once did, through Union of empty
// rectangles.
func TestCurvePreviewRevertsCompletely(t *testing.T) {
	d, err := New(17, 7)
	if err != nil {
		t.Fatal(err)
	}
	d.BeginGroup()
	_ = d.Curve(image.Pt(0, 6), image.Pt(0, 0), image.Pt(16, 0), image.Pt(16, 6), ink, Pen{Width: 3})
	d.RevertGroup()
	d.EndGroup()
	if pattern(d) != strings.Repeat(".................\n", 7) {
		t.Fatalf("revert left pixels behind:\n%s", pattern(d))
	}
}
