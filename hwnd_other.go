//go:build !windows

package main

import "unsafe"

// Only Windows attaches the menu bar to a window handle.
func mainWindowHandle() unsafe.Pointer { return nil }
