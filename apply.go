package main

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"

	"github.com/crgimenes/filo"
	"github.com/crgimenes/gessetto/doc"
	"github.com/crgimenes/gessetto/ops"
)

const maxScriptBytes = 16 << 20

func runApply(ctx context.Context, opts options, stdin io.Reader, stdout, stderr io.Writer) int {
	err := apply(ctx, opts, stdin, stdout)
	if err != nil {
		_, _ = fmt.Fprintln(stderr, "gessetto:", err)
		return 1
	}
	return 0
}

func apply(ctx context.Context, opts options, stdin io.Reader, stdout io.Writer) error {
	src, err := readScript(opts.apply, stdin)
	if err != nil {
		return err
	}
	var d *doc.Document
	if opts.input != "" {
		d, err = readPNG(opts.input)
		if err != nil {
			return err
		}
	}
	d, err = ops.Apply(ctx, src, d)
	pe, ok := errors.AsType[*filo.PositionError](err)
	if ok {
		return fmt.Errorf("%s:%d:%d: %w", opts.apply, pe.Line, pe.Col, pe.Err)
	}
	if err != nil {
		return fmt.Errorf("%s: %w", opts.apply, err)
	}
	var buf bytes.Buffer
	err = d.EncodePNG(&buf)
	if err != nil {
		return err
	}
	if opts.out == "-" {
		_, err = stdout.Write(buf.Bytes())
		return err
	}
	return writeFileAtomic(opts.out, buf.Bytes())
}

func readScript(path string, stdin io.Reader) (string, error) {
	r := stdin
	if path != "-" {
		f, err := os.Open(path) // #nosec G304 -- the user names the script on the command line
		if err != nil {
			return "", err
		}
		defer func() { _ = f.Close() }()
		r = f
	}
	b, err := io.ReadAll(io.LimitReader(r, maxScriptBytes+1))
	if err != nil {
		return "", err
	}
	if len(b) > maxScriptBytes {
		return "", fmt.Errorf("%s: script larger than %d bytes", path, maxScriptBytes)
	}
	return string(b), nil
}

func readPNG(path string) (*doc.Document, error) {
	f, err := os.Open(path) // #nosec G304 -- the user names the image on the command line
	if err != nil {
		return nil, err
	}
	defer func() { _ = f.Close() }()
	d, err := doc.DecodePNG(f)
	if err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	return d, nil
}

// writeFileAtomic never leaves a half-written image where the old one was.
func writeFileAtomic(path string, data []byte) error {
	tmp, err := os.CreateTemp(filepath.Dir(path), ".gessetto-*.png")
	if err != nil {
		return err
	}
	_, err = tmp.Write(data)
	cerr := tmp.Close()
	if err == nil {
		err = cerr
	}
	if err == nil {
		err = os.Rename(tmp.Name(), path)
	}
	if err != nil {
		_ = os.Remove(tmp.Name())
	}
	return err
}
