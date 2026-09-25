package doc

import (
	"bytes"
	"fmt"
	"image/png"
	"io"
)

// DecodePNG checks the declared size before decoding, so a small file that
// claims a huge canvas is refused instead of exhausting memory.
func DecodePNG(r io.Reader) (*Document, error) {
	var buf bytes.Buffer
	cfg, err := png.DecodeConfig(io.TeeReader(r, &buf))
	if err != nil {
		return nil, err
	}
	err = checkSize(cfg.Width, cfg.Height, 1)
	if err != nil {
		return nil, err
	}
	img, err := png.Decode(io.MultiReader(&buf, r))
	if err != nil {
		return nil, err
	}
	return FromImage(img)
}

func (d *Document) EncodePNG(w io.Writer) error {
	err := png.Encode(w, d.Flatten())
	if err != nil {
		return fmt.Errorf("encode png: %w", err)
	}
	return nil
}
