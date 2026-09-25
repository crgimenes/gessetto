package main

import (
	"bytes"
	"context"
	"image/color"
	"image/png"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestApplyStdinToStdout(t *testing.T) {
	var stdout, stderr bytes.Buffer
	opts := options{apply: "-", out: "-"}
	script := strings.NewReader(`(doc-new 2 1) (doc-pixel 1 0 "#ff0000")`)
	code := runApply(context.Background(), opts, script, &stdout, &stderr)
	if code != 0 {
		t.Fatalf("exit %d: %s", code, stderr.String())
	}
	img, err := png.Decode(&stdout)
	if err != nil {
		t.Fatal(err)
	}
	got := color.NRGBAModel.Convert(img.At(1, 0))
	if got != (color.NRGBA{R: 0xff, A: 0xff}) {
		t.Fatalf("pixel (1,0) = %v, want opaque red", got)
	}
}

func TestApplyErrorSaysWhere(t *testing.T) {
	var stdout, stderr bytes.Buffer
	opts := options{apply: "-", out: "-"}
	script := strings.NewReader("(doc-new 2 2)\n  (doc-pixel 0 0 \"red\")\n")
	code := runApply(context.Background(), opts, script, &stdout, &stderr)
	if code != 1 {
		t.Fatalf("exit %d, want 1", code)
	}
	msg := stderr.String()
	if !strings.HasPrefix(msg, "gessetto: -:2:3: ") || !strings.Contains(msg, `"doc-pixel": color:`) {
		t.Fatalf("stderr %q, want the script position and the failing argument", msg)
	}
	if stdout.Len() != 0 {
		t.Fatalf("stdout has %d bytes, want none on failure", stdout.Len())
	}
}

func TestApplyFailureKeepsExistingOutput(t *testing.T) {
	dir := t.TempDir()
	out := filepath.Join(dir, "out.png")
	err := os.WriteFile(out, []byte("previous"), 0o600)
	if err != nil {
		t.Fatal(err)
	}
	var stdout, stderr bytes.Buffer
	opts := options{apply: "-", out: out}
	code := runApply(context.Background(), opts, strings.NewReader("(doc-undo)"), &stdout, &stderr)
	if code != 1 {
		t.Fatalf("exit %d, want 1", code)
	}
	b, err := os.ReadFile(out) // #nosec G304 -- test temp dir
	if err != nil {
		t.Fatal(err)
	}
	if string(b) != "previous" {
		t.Fatalf("output overwritten on failure: %q", b)
	}
	entries, err := os.ReadDir(dir)
	if err != nil {
		t.Fatal(err)
	}
	if len(entries) != 1 {
		t.Fatalf("%d files in the output dir, want no temp file left", len(entries))
	}
}
