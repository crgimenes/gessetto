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
