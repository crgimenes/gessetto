package main

import (
	"runtime"
	"strings"
	"sync"

	"github.com/crgimenes/glaze/menu"
	"github.com/hajimehoshi/ebiten/v2"
	"github.com/hajimehoshi/ebiten/v2/inpututil"
)

// command is one menu entry. The same table feeds the native menu bar and,
// where the menu bar does not own the shortcuts, the keyboard handler.
type command struct {
	title    string
	shortcut string // "cmd+shift+z"; cmd is Ctrl outside macOS
	run      func()
	disabled bool
	checked  bool
	sep      bool
}

type menuSig struct {
	dirty, canUndo, canRedo, grid bool
}

// menuState marshals menu clicks, which arrive on the main thread, onto the
// Update goroutine.
type menuState struct {
	mu      sync.Mutex
	pending []func()

	sig       menuSig
	installed bool
	failed    bool
}

func (m *menuState) enqueue(f func()) {
	m.mu.Lock()
	m.pending = append(m.pending, f)
	m.mu.Unlock()
}

func (m *menuState) drain() {
	m.mu.Lock()
	p := m.pending
	m.pending = nil
	m.mu.Unlock()
	for _, f := range p {
		f()
	}
}

func (a *app) menus() []struct {
	title string
	items []command
} {
	d := a.ed.d
	return []struct {
		title string
		items []command
	}{
		{"gessetto", []command{
			{title: "Quit gessetto", shortcut: "cmd+q", run: a.requestQuit},
		}},
		{"File", []command{
			{title: "New...", shortcut: "cmd+n", run: func() { a.guard(a.openNewDialog) }},
			{title: "Open...", shortcut: "cmd+o", run: func() { a.guard(a.openFile) }},
			{sep: true},
			{title: "Save", shortcut: "cmd+s", run: func() { a.save(false) }},
			{title: "Save As...", shortcut: "cmd+shift+s", run: func() { a.save(true) }},
		}},
		{"Edit", []command{
			{title: "Undo", shortcut: "cmd+z", run: a.ed.undo, disabled: !d.CanUndo()},
			{title: "Redo", shortcut: "cmd+shift+z", run: a.ed.redo, disabled: !d.CanRedo()},
			{sep: true},
			// The menu takes Cmd+C before the window sees it, so text fields get
			// these by injection into the next frame's input.
			{title: "Cut", shortcut: "cmd+x", run: func() { a.inject.Cut = true }},
			{title: "Copy", shortcut: "cmd+c", run: func() { a.inject.Copy = true }},
			{title: "Paste", shortcut: "cmd+v", run: func() { a.inject.Paste = true }},
			{title: "Select All", shortcut: "cmd+a", run: func() { a.inject.SelectAll = true }},
		}},
		{"View", []command{
			{title: "Zoom In", shortcut: "cmd+=", run: func() { a.zoomBy(1) }},
			{title: "Zoom Out", shortcut: "cmd+-", run: func() { a.zoomBy(-1) }},
			{title: "Fit in Window", shortcut: "cmd+0", run: a.fitView},
			{title: "Actual Pixels", shortcut: "cmd+1", run: a.actualPixels},
			{sep: true},
			{title: "Pixel Grid", shortcut: "cmd+'", run: func() { a.grid = !a.grid }, checked: a.grid},
		}},
		{"Layer", []command{
			{title: "New Layer", shortcut: "cmd+shift+n", run: a.addLayer},
		}},
	}
}

func (a *app) menuSignature() menuSig {
	return menuSig{
		dirty:   a.ed.d.Dirty(),
		canUndo: a.ed.d.CanUndo(),
		canRedo: a.ed.d.CanRedo(),
		grid:    a.grid,
	}
}

// syncMenu rebuilds the native menu when what it shows changed. Windows needs
// the window handle, which exists only after the first frames.
func (a *app) syncMenu() {
	sig := a.menuSignature()
	if a.menu.failed || a.menu.installed && sig == a.menu.sig {
		return
	}
	var items []menu.Item
	for _, m := range a.menus() {
		sub := make([]menu.Item, 0, len(m.items))
		for _, c := range m.items {
			if c.sep {
				sub = append(sub, menu.Item{Separator: true})
				continue
			}
			title := c.title
			if c.checked {
				title = "✓ " + title
			}
			run := c.run
			sub = append(sub, menu.Item{
				Title:    title,
				Shortcut: c.shortcut,
				OnClick:  func() { a.menu.enqueue(run) },
				Disabled: c.disabled,
			})
		}
		items = append(items, menu.Item{Title: m.title, Submenu: sub})
	}
	hwnd := mainWindowHandle()
	var err error
	ebiten.RunOnMainThread(func() {
		_, err = menu.Set(items, menu.Options{Window: hwnd})
	})
	if err != nil && hwnd == nil && runtime.GOOS == "windows" {
		return
	}
	if err != nil {
		a.menu.failed = true
		a.event("menu", "installed=false err="+strings.ReplaceAll(err.Error(), " ", "_"))
		return
	}
	a.menu.installed = true
	a.menu.sig = sig
}

// runShortcuts handles the menu shortcuts wherever the menu bar does not own
// them: Windows and Linux, or any system where the menu failed to install.
func (a *app) runShortcuts() {
	if menuOwnsShortcuts && !a.menu.failed {
		return
	}
	for _, m := range a.menus() {
		for _, c := range m.items {
			if c.sep || c.disabled || c.shortcut == "" {
				continue
			}
			if shortcutPressed(c.shortcut) {
				c.run()
				return
			}
		}
	}
}

var shortcutKeys = map[string]ebiten.Key{
	"=": ebiten.KeyEqual, "-": ebiten.KeyMinus, "'": ebiten.KeyQuote,
	"0": ebiten.KeyDigit0, "1": ebiten.KeyDigit1,
}

func shortcutPressed(s string) bool {
	parts := strings.Split(s, "+")
	name := parts[len(parts)-1]
	key, ok := shortcutKeys[name]
	if !ok && len(name) == 1 && name[0] >= 'a' && name[0] <= 'z' {
		key, ok = ebiten.KeyA+ebiten.Key(name[0]-'a'), true
	}
	if !ok || !inpututil.IsKeyJustPressed(key) {
		return false
	}
	wantShift := false
	for _, m := range parts[:len(parts)-1] {
		wantShift = wantShift || m == "shift"
	}
	return ebiten.IsKeyPressed(ebiten.KeyControl) && ebiten.IsKeyPressed(ebiten.KeyShift) == wantShift
}
