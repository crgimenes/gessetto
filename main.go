// Command gessetto is an opinionated drawing and pixel-art editor.
package main

import (
	"errors"
	"flag"
	"fmt"
	"io"
	"os"

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
}

func main() {
	opts, code, done := parseArgs(os.Args[1:], os.Stdout, os.Stderr)
	if done {
		os.Exit(code)
	}
	os.Exit(runWindow(opts, os.Stderr))
}

func usage(w io.Writer) {
	_, _ = fmt.Fprint(w, `usage: gessetto [flags]

Opinionated drawing and pixel-art editor.

  -debug         write events to standard error as key=value lines
  -version       print the version and exit
  -h, --help     print this help and exit

  gessetto -debug
`)
}

// parseArgs reports done when the process should exit with code without
// opening a window.
func parseArgs(args []string, stdout, stderr io.Writer) (opts options, code int, done bool) {
	fs := flag.NewFlagSet("gessetto", flag.ContinueOnError)
	fs.SetOutput(io.Discard)
	fs.BoolVar(&opts.showVersion, "version", false, "")
	fs.BoolVar(&opts.debug, "debug", false, "")

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
	if fs.NArg() > 0 {
		_, _ = fmt.Fprintf(stderr, "gessetto: unexpected argument %q\n", fs.Arg(0))
		usage(stderr)
		return opts, 2, true
	}
	if opts.showVersion {
		_, _ = fmt.Fprintln(stdout, Version)
		return opts, 0, true
	}
	return opts, 0, false
}

func runWindow(opts options, stderr io.Writer) int {
	ebiten.SetWindowSize(winW, winH)
	ebiten.SetWindowSizeLimits(minW, minH, -1, -1)
	ebiten.SetWindowTitle("gessetto")
	ebiten.SetWindowResizingMode(ebiten.WindowResizingModeEnabled)
	// Closing the window and Cmd+Q never pass through a menu; without this and
	// the check at the top of Update, unsaved work dies with the process.
	ebiten.SetWindowClosingHandled(true)

	a := &app{debug: opts.debug, log: stderr}
	a.event("start", "version="+Version)
	err := ebiten.RunGame(a)
	if err != nil {
		_, _ = fmt.Fprintln(stderr, "gessetto:", err)
		return 1
	}
	return 0
}
