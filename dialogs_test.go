package main

import "testing"

func TestNewFromSize(t *testing.T) {
	for _, tt := range []struct {
		in   string
		w, h int
	}{
		{"64 x 48", 64, 48}, {"64x48", 64, 48}, {" 32 16 ", 32, 16}, {"20", 20, 20}, {"8X4", 8, 4}, {"3,5", 3, 5},
	} {
		d, err := newFromSize(tt.in)
		if err != nil {
			t.Errorf("%q: %v", tt.in, err)
			continue
		}
		s := d.Bounds().Size()
		if s.X != tt.w || s.Y != tt.h {
			t.Errorf("%q: %v, want %dx%d", tt.in, s, tt.w, tt.h)
		}
	}
	for _, in := range []string{"", "a x b", "0 x 5", "1 2 3", "99999 x 2"} {
		_, err := newFromSize(in)
		if err == nil {
			t.Errorf("%q: want an error", in)
		}
	}
}
