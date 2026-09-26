package doc

import (
	"image"
	"image/color"
	"testing"
)

// seeded is a 6x1 row "RG...." with R and G opaque pixels at x 0 and 1.
func seeded(t *testing.T) *Document {
	t.Helper()
	d, err := New(6, 1)
	if err != nil {
		t.Fatal(err)
	}
	_ = d.SetPixel(0, 0, color.NRGBA{R: 0xff, A: 0xff})
	_ = d.SetPixel(1, 0, color.NRGBA{G: 0xff, A: 0xff})
	return d
}

func row(d *Document) string {
	out := ""
	for x := range d.width {
		c := d.PixelAt(x, 0)
		switch {
		case c.A == 0:
			out += "."
		case c.R == 0xff:
			out += "R"
		case c.G == 0xff:
			out += "G"
		default:
			out += "?"
		}
	}
	return out
}

func TestMoveSelectionIsOneUndoStep(t *testing.T) {
	d := seeded(t)
	d.Select(image.Rect(0, 0, 2, 1))
	_ = d.MoveSelection(1, 0, false)
	_ = d.MoveSelection(2, 0, false)
	if !d.Floating() || row(d) != "...RG." {
		t.Fatalf("while floating got %q, want ...RG.", row(d))
	}
	d.Drop()
	if row(d) != "...RG." {
		t.Fatalf("after drop got %q", row(d))
	}
	if _, ok := d.Selection(); ok {
		t.Fatal("drop must deselect")
	}
	d.Undo()
	if row(d) != "RG...." {
		t.Fatalf("one undo must restore the whole move, got %q", row(d))
	}
}

func TestDuplicateLeavesTheSource(t *testing.T) {
	d := seeded(t)
	d.Select(image.Rect(0, 0, 2, 1))
	_ = d.MoveSelection(3, 0, true)
	d.Drop()
	if row(d) != "RG.RG." {
		t.Fatalf("got %q, want RG.RG.", row(d))
	}
}

func TestUndoWhileFloatingPutsItBack(t *testing.T) {
	d := seeded(t)
	d.Select(image.Rect(0, 0, 2, 1))
	_ = d.MoveSelection(4, 0, false)
	if !d.Undo() {
		t.Fatal("undo must act on the floating selection")
	}
	if row(d) != "RG...." || d.Floating() {
		t.Fatalf("got %q floating=%v, want the pixels back and no selection", row(d), d.Floating())
	}
	if !d.Undo() || row(d) != "R....." {
		t.Fatalf("the next undo must reach the edit before, got %q", row(d))
	}
}

func TestDropClipsAndDeleteClears(t *testing.T) {
	d := seeded(t)
	d.Select(image.Rect(0, 0, 2, 1))
	_ = d.MoveSelection(5, 0, false)
	d.Drop()
	if row(d) != ".....R" {
		t.Fatalf("got %q, want the part past the edge lost", row(d))
	}
	d = seeded(t)
	d.Select(image.Rect(1, 0, 9, 1))
	d.DeleteSelection()
	if row(d) != "R....." {
		t.Fatalf("got %q, want the selected pixels cleared", row(d))
	}
	d.Undo()
	if row(d) != "RG...." {
		t.Fatalf("delete must be undoable, got %q", row(d))
	}
}

func TestSelectionFollowsItsLayer(t *testing.T) {
	d := seeded(t)
	d.Select(image.Rect(0, 0, 1, 1))
	_ = d.MoveSelection(2, 0, false)
	_ = d.AddLayer("top")
	if d.Floating() {
		t.Fatal("adding a layer must drop the selection first")
	}
	if d.layers[0].Pix.NRGBAAt(2, 0).R != 0xff {
		t.Fatal("the drop must land on the layer the pixels came from")
	}
}

func TestTransparentSelectionLetsTheLayerShow(t *testing.T) {
	white := color.NRGBA{R: 0xff, G: 0xff, B: 0xff, A: 0xff}
	red := color.NRGBA{R: 0xff, A: 0xff}
	green := color.NRGBA{G: 0xff, A: 0xff}
	d, err := New(4, 1)
	if err != nil {
		t.Fatal(err)
	}
	// Source "RW.." over target "..GG": a red pixel, a white one, then space.
	_ = d.SetPixel(0, 0, red)
	_ = d.SetPixel(1, 0, white)
	_ = d.Line(2, 0, 3, 0, green, Pen{})
	d.SetSelectionKey(&white)
	d.Select(image.Rect(0, 0, 2, 1))
	_ = d.MoveSelection(2, 0, false)
	if d.PixelAt(3, 0) != green {
		t.Fatal("while floating, the key color must show what is below")
	}
	d.Drop()
	if d.PixelAt(2, 0) != red || d.PixelAt(3, 0) != green || d.PixelAt(1, 0) != (color.NRGBA{}) {
		t.Fatalf("got %v %v %v, want red over, green kept, source cleared", d.PixelAt(2, 0), d.PixelAt(3, 0), d.PixelAt(1, 0))
	}
	d.SetSelectionKey(nil)
	d.Select(image.Rect(1, 0, 3, 1))
	_ = d.MoveSelection(1, 0, false)
	d.Drop()
	if d.PixelAt(2, 0) != (color.NRGBA{}) {
		t.Fatal("an opaque selection must cover with its transparent pixels too")
	}
}

func TestCopyPasteIsOneStep(t *testing.T) {
	d := seeded(t)
	d.Select(image.Rect(0, 0, 2, 1))
	img, ok := d.SelectionImage()
	if !ok || img.Rect != image.Rect(0, 0, 2, 1) {
		t.Fatalf("copied %v %v, want a 2x1 image at the origin", img.Rect, ok)
	}
	err := d.Paste(img, image.Pt(3, 0))
	if err != nil {
		t.Fatal(err)
	}
	if !d.Floating() || row(d) != "RG.RG." {
		t.Fatalf("pasted %q, want it floating at 3", row(d))
	}
	_ = d.MoveSelection(1, 0, false)
	d.Drop()
	if row(d) != "RG..RG" {
		t.Fatalf("got %q after moving the paste one right", row(d))
	}
	d.Undo()
	if row(d) != "RG...." {
		t.Fatalf("one undo must remove the paste, got %q", row(d))
	}
}

func TestPasteRefusesHugeImages(t *testing.T) {
	d := seeded(t)
	err := d.Paste(image.NewNRGBA(image.Rect(0, 0, MaxSide+1, 1)), image.Point{})
	if err == nil {
		t.Fatal("an image past MaxSide must be refused")
	}
}
