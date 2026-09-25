package main

import (
	"image"
	"testing"

	"github.com/crgimenes/gessetto/doc"
)

// One frame of a stroke without the GPU upload: the segment, the undo
// capture and the CPU side of refreshing the view.
func benchStroke(b *testing.B, side int) {
	d, err := doc.New(side, side)
	if err != nil {
		b.Fatal(err)
	}
	e := newEditor(d)
	var cv canvasView
	cv.flat = image.NewNRGBA(d.Bounds())
	e.takeChanged()
	e.press(image.Pt(0, 0), false)
	b.ResetTimer()
	for i := range b.N {
		p := image.Pt((i*7)%side, (i*3)%side)
		e.drag(p)
		r, _ := e.takeChanged()
		d.FlattenInto(cv.flat, r)
		cv.buf = premultiply(cv.buf[:0], cv.flat, r)
	}
	b.StopTimer()
	e.release()
}

func BenchmarkStroke256(b *testing.B)  { benchStroke(b, 256) }
func BenchmarkStroke2048(b *testing.B) { benchStroke(b, 2048) }
