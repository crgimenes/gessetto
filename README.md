# gessetto

A small pixel-art editor for making game assets: indexed color (up to 256
colors, index 0 transparent), up to 256 x 256, frames with durations and
tags. macOS and iPadOS, with the system's own windows, menus, documents and
iCloud.

## Layout

- The C core, at the root: `gesso.c` (the image, its edits and undo),
  `gesso_filo.c` (the `.gesso` file, Filo read in a sealed context),
  `editor.c` (the tools), `png.c` and `gesso_png.c` (PNG import), and
  `gessetto.c`, the facade the app sees. No allocation; nothing from libc
  but the memory functions.
- `gesso2c` turns a `.gesso` into a C header; `gesso-apply` runs an ops
  script on an image (`testdata/corpus/README.md` lists the builtins).
- `swift/`: the app. SwiftUI for the window, AppKit and UIKit for the
  canvas only.

## Build

The Filo runtime comes from a `clang_filo` checkout beside this one.

```
make            # gesso2c, gesso-apply, corpus runner
make app        # build/gessetto.app
make ios-sim    # build/gessetto-sim.app for the iPad simulator
make qa         # every test and lint, C and Swift
```

## Keys on the canvas

B pencil, E eraser, G fill, I picker, N line, R rectangle, O ellipse,
M select; X swaps the colors; comma and period step frames; P plays; space
and drag pans; arrows move a selection (Shift: 10 pixels); Shift+H and
Shift+V flip it; Escape or Return drops it.

## License

MIT
