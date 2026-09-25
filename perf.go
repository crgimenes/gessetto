package main

import (
	"fmt"
	"io"
	"runtime"
	"slices"
	"time"

	"github.com/hajimehoshi/ebiten/v2"
)

// perf writes one key=value line per second with -debug: percentiles and the
// maximum, because the occasional slow frame is what the eye catches.
type perf struct {
	enabled bool
	log     io.Writer

	upd, draw, sync []float64 // ms
	start           time.Time
}

func (p *perf) now() time.Time {
	if !p.enabled {
		return time.Time{}
	}
	return time.Now()
}

func (p *perf) add(series *[]float64, t0 time.Time) {
	if t0.IsZero() {
		return
	}
	*series = append(*series, float64(time.Since(t0))/1e6)
}

func (p *perf) tick() {
	if !p.enabled {
		return
	}
	if p.start.IsZero() {
		p.start = time.Now()
		return
	}
	if time.Since(p.start) < time.Second {
		return
	}
	var m runtime.MemStats
	runtime.ReadMemStats(&m)
	_, _ = fmt.Fprintf(p.log, "gessetto perf: fps=%.1f tps=%.1f upd_p50=%.2fms upd_p95=%.2fms upd_max=%.2fms draw_p50=%.2fms draw_p95=%.2fms draw_max=%.2fms sync_max=%.2fms heap=%dMB gc=%d\n",
		ebiten.ActualFPS(), ebiten.ActualTPS(),
		pct(p.upd, 50), pct(p.upd, 95), pct(p.upd, 100),
		pct(p.draw, 50), pct(p.draw, 95), pct(p.draw, 100),
		pct(p.sync, 100),
		m.HeapAlloc>>20, m.NumGC)
	p.upd, p.draw, p.sync = p.upd[:0], p.draw[:0], p.sync[:0]
	p.start = time.Now()
}

func pct(s []float64, q int) float64 {
	if len(s) == 0 {
		return 0
	}
	slices.Sort(s)
	return s[(len(s)-1)*q/100]
}
