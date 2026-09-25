package ops

import (
	"bytes"
	"context"
	"flag"
	"fmt"
	"image"
	"image/png"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/crgimenes/gessetto/doc"
)

var update = flag.Bool("update", false, "write want.png for corpus cases that have none")

const corpusDir = "../testdata/corpus"

func TestCorpus(t *testing.T) {
	dirs, err := os.ReadDir(corpusDir)
	if err != nil {
		t.Fatal(err)
	}
	n := 0
	for _, e := range dirs {
		if !e.IsDir() {
			continue
		}
		n++
		dir := filepath.Join(corpusDir, e.Name())
		t.Run(e.Name(), func(t *testing.T) { runCase(t, dir) })
	}
	if n == 0 {
		t.Fatal("corpus is empty")
	}
}

func runCase(t *testing.T, dir string) {
	src, err := os.ReadFile(filepath.Join(dir, "ops.filo"))
	if err != nil {
		t.Fatal(err)
	}
	var in *doc.Document
	f, err := os.Open(filepath.Join(dir, "in.png"))
	switch {
	case err == nil:
		in, err = doc.DecodePNG(f)
		_ = f.Close()
		if err != nil {
			t.Fatalf("in.png: %v", err)
		}
	case !os.IsNotExist(err):
		t.Fatal(err)
	}

	d, runErr := Apply(context.Background(), string(src), in)

	wantErr, err := os.ReadFile(filepath.Join(dir, "error.txt"))
	if err == nil {
		want := strings.TrimSpace(string(wantErr))
		if runErr == nil || !strings.Contains(runErr.Error(), want) {
			t.Fatalf("error %v, want one containing %q", runErr, want)
		}
		return
	}
	if runErr != nil {
		t.Fatal(runErr)
	}

	got := d.Flatten()
	wantPath := filepath.Join(dir, "want.png")
	wf, err := os.Open(wantPath)
	if os.IsNotExist(err) && *update {
		writePNG(t, wantPath, got)
		t.Logf("wrote %s", wantPath)
		return
	}
	if err != nil {
		t.Fatal(err)
	}
	wantDoc, err := doc.DecodePNG(wf)
	_ = wf.Close()
	if err != nil {
		t.Fatalf("want.png: %v", err)
	}
	diff := firstDiff(got, wantDoc.Flatten())
	if diff != "" {
		gotPath := filepath.Join(t.TempDir(), "got.png")
		writePNG(t, gotPath, got)
		t.Fatalf("%s (got written to %s)", diff, gotPath)
	}
}

func firstDiff(got, want *image.NRGBA) string {
	if got.Rect != want.Rect {
		return fmt.Sprintf("size %v, want %v", got.Rect.Size(), want.Rect.Size())
	}
	for y := range got.Rect.Dy() {
		for x := range got.Rect.Dx() {
			g, w := got.NRGBAAt(x, y), want.NRGBAAt(x, y)
			if g != w {
				return fmt.Sprintf("pixel (%d,%d) = %v, want %v", x, y, g, w)
			}
		}
	}
	return ""
}

func writePNG(t *testing.T, path string, img image.Image) {
	t.Helper()
	var buf bytes.Buffer
	err := png.Encode(&buf, img)
	if err != nil {
		t.Fatal(err)
	}
	err = os.WriteFile(path, buf.Bytes(), 0o600)
	if err != nil {
		t.Fatal(err)
	}
}
