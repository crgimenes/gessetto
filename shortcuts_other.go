//go:build !darwin

package main

// glaze/menu wires no accelerators outside macOS, so the app reads the keys.
const menuOwnsShortcuts = false
