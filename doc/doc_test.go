package doc

import (
	"bytes"
	"encoding/binary"
	"errors"
	"hash/crc32"
	"image"
	"image/color"
	"image/png"
	"testing"
)

var red = color.NRGBA{R: 0xff, A: 0xff}

func TestHistoryBudgetDropsOldest(t *testing.T) {
	d, err := New(4, 4)
	if err != nil {
		t.Fatal(err)
	}
	// One pixel edit costs 8 bytes of pixels plus the overhead; room for two.
	d.hist.max = 2 * (8 + entryOverhead)
	for x := range 3 {
		err = d.SetPixel(x, 0, red)
		if err != nil {
			t.Fatal(err)
		}
	}
	if d.Dropped() != 1 {
		t.Fatalf("dropped %d, want 1", d.Dropped())
	}
	for range 2 {
		if !d.Undo() {
			t.Fatal("want two undos")
		}
	}
	if d.Undo() {
		t.Fatal("third undo must be gone")
	}
	got := d.Flatten().NRGBAAt(0, 0)
	if got != red {
		t.Fatalf("pixel (0,0) = %v, want the dropped edit kept", got)
	}
}

func TestRedoCountsAgainstBudgetUntilDiscarded(t *testing.T) {
	d, err := New(2, 1)
	if err != nil {
		t.Fatal(err)
	}
	_ = d.SetPixel(0, 0, red)
	_ = d.SetPixel(1, 0, red)
	d.Undo()
	two := 2 * (8 + entryOverhead)
	if d.hist.bytes != two {
		t.Fatalf("bytes %d, want %d while the redo entry is held", d.hist.bytes, two)
	}
	_ = d.SetPixel(0, 0, color.NRGBA{})
	if d.hist.bytes != two {
		t.Fatalf("bytes %d, want %d after the redo entry is discarded", d.hist.bytes, two)
	}
}

func TestCoordinateCeiling(t *testing.T) {
	d, err := New(2, 2)
	if err != nil {
		t.Fatal(err)
	}
	err = d.Line(0, 0, MaxCoord+1, 0, red)
	if !errors.Is(err, ErrCoord) {
		t.Fatalf("err %v, want ErrCoord", err)
	}
	err = d.SetPixel(-MaxCoord-1, 0, red)
	if !errors.Is(err, ErrCoord) {
		t.Fatalf("err %v, want ErrCoord", err)
	}
}

// Checked on the arithmetic, not by allocating the gigabyte it guards.
func TestLayerMemoryCeiling(t *testing.T) {
	fits := maxDocBytes / (MaxSide * MaxSide * 4)
	err := checkSize(MaxSide, MaxSide, fits)
	if err != nil {
		t.Fatalf("%d full-size layers: %v", fits, err)
	}
	err = checkSize(MaxSide, MaxSide, fits+1)
	if !errors.Is(err, ErrTooBig) {
		t.Fatalf("err %v, want ErrTooBig", err)
	}
}

// A PNG header claiming a canvas past MaxSide must be refused before any
// pixel buffer is allocated.
func TestDecodePNGRefusesHugeHeader(t *testing.T) {
	var buf bytes.Buffer
	buf.WriteString("\x89PNG\r\n\x1a\n")
	ihdr := make([]byte, 13)
	binary.BigEndian.PutUint32(ihdr[0:], 1<<20)
	binary.BigEndian.PutUint32(ihdr[4:], 1<<20)
	ihdr[8] = 8 // bit depth
	ihdr[9] = 6 // RGBA
	writeChunk(&buf, "IHDR", ihdr)
	_, err := DecodePNG(&buf)
	if !errors.Is(err, ErrSize) {
		t.Fatalf("err %v, want ErrSize", err)
	}
}

func TestPNGRoundTrip(t *testing.T) {
	src := image.NewNRGBA(image.Rect(0, 0, 3, 2))
	src.SetNRGBA(0, 0, color.NRGBA{R: 1, G: 2, B: 3, A: 4})
	src.SetNRGBA(2, 1, color.NRGBA{R: 200, G: 100, B: 50, A: 255})
	var in bytes.Buffer
	err := png.Encode(&in, src)
	if err != nil {
		t.Fatal(err)
	}
	d, err := DecodePNG(&in)
	if err != nil {
		t.Fatal(err)
	}
	var out bytes.Buffer
	err = d.EncodePNG(&out)
	if err != nil {
		t.Fatal(err)
	}
	back, err := png.Decode(&out)
	if err != nil {
		t.Fatal(err)
	}
	for y := range 2 {
		for x := range 3 {
			got := color.NRGBAModel.Convert(back.At(x, y))
			if got != src.NRGBAAt(x, y) {
				t.Fatalf("(%d,%d) = %v, want %v", x, y, got, src.NRGBAAt(x, y))
			}
		}
	}
}

func writeChunk(buf *bytes.Buffer, typ string, data []byte) {
	var n [4]byte
	binary.BigEndian.PutUint32(n[:], uint32(len(data))) // #nosec G115 -- test chunk, 13 bytes
	buf.Write(n[:])
	body := append([]byte(typ), data...)
	buf.Write(body)
	binary.BigEndian.PutUint32(n[:], crc32.ChecksumIEEE(body))
	buf.Write(n[:])
}

func TestGroupIsOneUndoStep(t *testing.T) {
	d, err := New(4, 1)
	if err != nil {
		t.Fatal(err)
	}
	blue := color.NRGBA{B: 0xff, A: 0xff}
	_ = d.SetPixel(0, 0, blue)
	d.BeginGroup()
	_ = d.SetPixel(1, 0, red)
	_ = d.Line(1, 0, 3, 0, red)
	_ = d.SetPixel(0, 0, red)
	d.EndGroup()
	want := []color.NRGBA{red, red, red, red}
	assertRow(t, d, want)

	if !d.Undo() {
		t.Fatal("want the group undone")
	}
	assertRow(t, d, []color.NRGBA{blue, {}, {}, {}})
	if !d.Redo() {
		t.Fatal("want the group redone")
	}
	assertRow(t, d, want)
	d.Undo()
	d.Undo()
	if d.Undo() {
		t.Fatal("want exactly two steps: the blue pixel and the group")
	}
}

func TestEmptyGroupLeavesNoStep(t *testing.T) {
	d, err := New(2, 2)
	if err != nil {
		t.Fatal(err)
	}
	d.BeginGroup()
	_ = d.SetPixel(9, 9, red)
	d.EndGroup()
	if d.CanUndo() {
		t.Fatal("a group with nothing on the canvas must not become a step")
	}
}

func TestDirtyFollowsSavedState(t *testing.T) {
	d, err := New(2, 1)
	if err != nil {
		t.Fatal(err)
	}
	if d.Dirty() {
		t.Fatal("new document must start clean")
	}
	_ = d.SetPixel(0, 0, red)
	d.MarkSaved()
	_ = d.SetPixel(1, 0, red)
	if !d.Dirty() {
		t.Fatal("edit after save must be dirty")
	}
	d.Undo()
	if d.Dirty() {
		t.Fatal("undo back to the saved state must be clean")
	}
	d.Undo()
	_ = d.SetPixel(1, 0, red)
	if !d.Dirty() {
		t.Fatal("a branch away from the saved state must stay dirty")
	}
	d.Undo()
	d.Redo()
	if !d.Dirty() {
		t.Fatal("the saved state is gone for good once its branch is discarded")
	}
}

func TestFlattenIntoMatchesFlatten(t *testing.T) {
	d, err := New(5, 4)
	if err != nil {
		t.Fatal(err)
	}
	_ = d.Line(0, 0, 4, 3, color.NRGBA{B: 0xff, A: 0x80})
	_ = d.AddLayer("top")
	_ = d.Line(4, 0, 0, 3, color.NRGBA{R: 0xff, A: 0x60})
	full := d.Flatten()
	part := image.NewNRGBA(d.Bounds())
	d.FlattenInto(part, image.Rect(1, 1, 4, 3))
	for y := range 4 {
		for x := range 5 {
			want := color.NRGBA{}
			if (image.Point{x, y}).In(image.Rect(1, 1, 4, 3)) {
				want = full.NRGBAAt(x, y)
			}
			if part.NRGBAAt(x, y) != want {
				t.Fatalf("(%d,%d) = %v, want %v", x, y, part.NRGBAAt(x, y), want)
			}
			if d.PixelAt(x, y) != full.NRGBAAt(x, y) {
				t.Fatalf("PixelAt(%d,%d) = %v, want %v", x, y, d.PixelAt(x, y), full.NRGBAAt(x, y))
			}
		}
	}
}

func assertRow(t *testing.T, d *Document, want []color.NRGBA) {
	t.Helper()
	img := d.Flatten()
	for x, w := range want {
		if img.NRGBAAt(x, 0) != w {
			t.Fatalf("(%d,0) = %v, want %v", x, img.NRGBAAt(x, 0), w)
		}
	}
}
