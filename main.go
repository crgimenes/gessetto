// Command gessetto is an opinionated drawing and pixel-art editor.
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"os/signal"
	"syscall"

	"github.com/crgimenes/gessetto/doc"
	"github.com/hajimehoshi/ebiten/v2"
)

var Version = "dev"

const (
	winW = 1280
	winH = 800
	minW = 390
	minH = 700
)

type options struct {
	showVersion bool
	debug       bool
	apply       string
	out         string
	input       string
}

func main() {
	opts, code, done := parseArgs(os.Args[1:], os.Stdout, os.Stderr)
	if done {
		os.Exit(code)
	}
	if opts.apply != "" {
		ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
		code = runApply(ctx, opts, os.Stdin, os.Stdout, os.Stderr)
		stop()
		os.Exit(code)
	}
	os.Exit(runWindow(opts, os.Stderr))
}

func usage(w io.Writer) {
	_, _ = fmt.Fprint(w, `usage: gessetto [flags] [image.png]
       gessetto -apply ops.filo -out out.png [in.png]

Opinionated drawing and pixel-art editor. Opens image.png, or a new 64x64
image. With -apply it opens no window:
it runs a Filo script of doc-* operations on in.png, or on the document the
script creates with (doc-new w h), and writes the flattened result as PNG.

  -apply file    Filo script to run headless; "-" reads standard input
  -out file      PNG to write with -apply; "-" writes standard output
  -debug         write events to standard error as key=value lines
  -version       print the version and exit
  -h, --help     print this help and exit

  echo '(doc-new 8 8) (doc-line 0 0 7 7 "#ff0000")' | gessetto -apply - -out x.png
`)
}

// parseArgs reports done when the process should exit with code without
// opening a window.
func parseArgs(args []string, stdout, stderr io.Writer) (opts options, code int, done bool) {
	fs := flag.NewFlagSet("gessetto", flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	fs.BoolVar(&opts.showVersion, "version", false, "")
	fs.BoolVar(&opts.debug, "debug", false, "")
	fs.StringVar(&opts.apply, "apply", "", "")
	fs.StringVar(&opts.out, "out", "", "")

	err := fs.Parse(args)
	if errors.Is(err, flag.ErrHelp) {
		usage(stdout)
		return opts, 0, true
	}
	if err != nil {
		_, _ = fmt.Fprintln(stderr, "gessetto:", err)
		usage(stderr)
		return opts, 2, true
	}
	if opts.showVersion {
		_, _ = fmt.Fprintln(stdout, Version)
		return opts, 0, true
	}
	problem := ""
	switch {
	case fs.NArg() > 1:
		problem = fmt.Sprintf("unexpected argument %q", fs.Arg(1))
	case opts.apply != "" && opts.out == "":
		problem = "-apply needs -out"
	case opts.apply == "" && opts.out != "":
		problem = "-out is only used with -apply"
	}
	if problem != "" {
		_, _ = fmt.Fprintln(stderr, "gessetto:", problem)
		usage(stderr)
		return opts, 2, true
	}
	opts.input = fs.Arg(0)
	return opts, 0, false
}

func runWindow(opts options, stderr io.Writer) int {
	ebiten.SetWindowSize(winW, winH)
	ebiten.SetWindowSizeLimits(minW, minH, -1, -1)
	ebiten.SetWindowTitle(baseTitle)
	ebiten.SetWindowResizingMode(ebiten.WindowResizingModeEnabled)
	err := setWindowIcon()
	if err != nil {
		_, _ = fmt.Fprintln(stderr, "gessetto: window icon:", err)
	}
	// Closing the window and Cmd+Q never pass through a menu; without this and
	// the check at the top of Update, unsaved work dies with the process.
	ebiten.SetWindowClosingHandled(true)

	d, err := doc.New(defaultNewPx, defaultNewPx)
	if opts.input != "" {
		d, err = readPNG(opts.input)
	}
	if err != nil {
		_, _ = fmt.Fprintln(stderr, "gessetto:", err)
		return 1
	}
	a := newApp(d, opts.input, opts.debug, stderr)
	a.event("start", "version="+Version)
	err = ebiten.RunGame(a)
	if err != nil {
		_, _ = fmt.Fprintln(stderr, "gessetto:", err)
		return 1
	}
	return 0
}
