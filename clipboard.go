package main

import (
	"bytes"
	"image"
	"image/png"

	"github.com/crgimenes/native/clipboard"
)

// The menu takes Cmd+C before the window sees it. A text field with focus
// gets the command injected into the next frame's input; otherwise it acts
// on the picture through the system clipboard.

func (a *app) typing() bool {
	return a.top.HasFocus() || a.left.HasFocus() || a.right.HasFocus() || a.bottom.HasFocus() || a.dlg.HasFocus()
}

func (a *app) editCopy() {
	if a.typing() {
		a.inject.Copy = true
		return
	}
	a.copyImage()
}

func (a *app) editCut() {
	if a.typing() {
		a.inject.Cut = true
		return
	}
	if a.copyImage() {
		a.ed.deleteSelection()
	}
}

func (a *app) editPaste() {
	if a.typing() {
		a.inject.Paste = true
		return
	}
	a.pasteImage()
}

func (a *app) editSelectAll() {
	if a.typing() {
		a.inject.SelectAll = true
		return
	}
	a.ed.setTool(toolSelect)
	a.ed.drop()
	a.ed.d.Select(a.ed.d.Bounds())
	a.ed.touch(a.ed.d.Bounds())
}

func (a *app) copyImage() bool {
	img, ok := a.ed.d.SelectionImage()
	if !ok {
		a.status = "Select an area to copy."
		return false
	}
	var buf bytes.Buffer
	err := png.Encode(&buf, img)
	if err == nil {
		err = clipboard.WriteImage(buf.Bytes())
	}
	if err != nil {
		a.status = "Could not copy: " + err.Error()
		return false
	}
	a.status = ""
	return true
}

// pasteImage floats the clipboard image at the top left of what the canvas
// shows, so it lands in view, and switches to the select tool to move it.
func (a *app) pasteImage() {
	data, err := clipboard.ReadImage()
	if err != nil {
		a.status = "Could not paste: " + err.Error()
		return
	}
	if data == nil {
		a.status = "The clipboard holds no image."
		return
	}
	img, err := png.Decode(bytes.NewReader(data))
	if err != nil {
		a.status = "Could not paste: " + err.Error()
		return
	}
	at := a.v.toDoc(a.lay.canvas.Min)
	at = image.Pt(max(at.X, 0), max(at.Y, 0))
	a.ed.setTool(toolSelect)
	err = a.ed.d.Paste(img, at)
	if err != nil {
		a.status = "Could not paste: " + err.Error()
		return
	}
	r, _ := a.ed.d.Selection()
	a.ed.touch(r)
	a.status = ""
}
