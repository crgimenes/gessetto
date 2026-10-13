import SwiftUI
import UniformTypeIdentifiers

/// The index a drag of this kind carries ("frame:3", "color:5").
func dragged(_ items: [String], _ kind: String) -> Int? {
  guard let s = items.first, s.hasPrefix(kind) else { return nil }
  return Int(s.dropFirst(kind.count))
}

extension Color {
  init(rgb: UInt32) {
    self.init(
      .sRGB, red: Double(rgb >> 16 & 0xff) / 255, green: Double(rgb >> 8 & 0xff) / 255,
      blue: Double(rgb & 0xff) / 255)
  }

  /// 0xRRGGBB in sRGB, or nil for a color that has no fixed value.
  var rgb: UInt32? {
    guard let cg = cgColor,
      let srgb = cg.converted(
        to: CGColorSpace(name: CGColorSpace.sRGB)!, intent: .defaultIntent, options: nil),
      let c = srgb.components, c.count >= 3
    else { return nil }
    let byte = { (v: CGFloat) in UInt32((min(max(v, 0), 1) * 255).rounded()) }
    return byte(c[0]) << 16 | byte(c[1]) << 8 | byte(c[2])
  }
}

/// Index 0, the transparent one, drawn as what transparency looks like.
struct Checker: View {
  var body: some View {
    Canvas { ctx, size in
      let cell = size.width / 2
      ctx.fill(Path(CGRect(origin: .zero, size: size)), with: .color(Color(rgb: 0xe0e0e0)))
      for (x, y) in [(0, 0), (1, 1)] {
        let r = CGRect(x: CGFloat(x) * cell, y: CGFloat(y) * cell, width: cell, height: cell)
        ctx.fill(Path(r), with: .color(Color(rgb: 0xb0b0b0)))
      }
    }
  }
}

struct Swatch: View {
  let rgb: UInt32
  let index: Int

  var body: some View {
    if index == 0 {
      Checker()
    } else {
      Color(rgb: rgb)
    }
  }
}

struct PalettePanel: View {
  let editor: Editor
  private let side: CGFloat = 18

  var body: some View {
    let _ = editor.revision
    let sketch = editor.sketch
    VStack(alignment: .leading, spacing: 10) {
      wells
      ScrollView {
        LazyVGrid(
          columns: Array(repeating: GridItem(.fixed(side), spacing: 2), count: 6), spacing: 2
        ) {
          ForEach(0..<sketch.colors, id: \.self) { i in
            Swatch(rgb: sketch.color(i), index: i)
              .frame(width: side, height: side)
              .overlay {
                if i == editor.fg {
                  Rectangle().strokeBorder(Color.accentColor, lineWidth: 2)
                } else if i == editor.bg {
                  Rectangle().strokeBorder(.secondary, style: StrokeStyle(lineWidth: 1, dash: [2]))
                }
              }
              .contentShape(Rectangle())
              .onTapGesture { pick(i) }
              .contextMenu {
                Button("Use as Foreground") { editor.setFg(i) }
                Button("Use as Background") { editor.setBg(i) }
                Divider()
                Button("Delete Color") { editor.deleteColor(i) }
                  .disabled(i == 0)
              }
              .draggable("color:\(i)")
              .dropDestination(for: String.self) { items, _ in
                guard i > 0, let from = dragged(items, "color:"), from > 0, from != i else {
                  return false
                }
                editor.moveColor(from, to: i)
                return true
              }
              .help(i == 0 ? "Transparent" : String(format: "#%06x", sketch.color(i) & 0xff_ffff))
          }
        }
      }
      HStack {
        ColorPicker(
          "Color",
          selection: Binding(
            get: { Color(rgb: sketch.color(editor.fg)) },
            set: { c in
              if let rgb = c.rgb {
                editor.setColor(editor.fg, rgb)
              }
            }),
          supportsOpacity: false
        )
        .labelsHidden()
        .disabled(editor.fg == 0)
        .help("Edit the foreground color")
        Spacer()
        Button("Add Color", systemImage: "plus") { editor.addColor() }
          .labelStyle(.iconOnly)
          .help("Add a color like the foreground one")
        Button("Load Palette", systemImage: "swatchpalette") {
          editor.loadedPalette = nil
          editor.paletteSheet = true
        }
        .labelStyle(.iconOnly)
        .help("Load a palette")
      }
    }
    .padding(8)
  }

  /// The foreground over the background, as in Paint; tapping them swaps.
  private var wells: some View {
    let sketch = editor.sketch
    return ZStack(alignment: .topLeading) {
      Swatch(rgb: sketch.color(editor.bg), index: editor.bg)
        .frame(width: 28, height: 28)
        .border(.secondary)
        .offset(x: 14, y: 14)
      Swatch(rgb: sketch.color(editor.fg), index: editor.fg)
        .frame(width: 28, height: 28)
        .border(.primary)
    }
    .frame(width: 42, height: 42, alignment: .topLeading)
    .contentShape(Rectangle())
    .onTapGesture { editor.swapColors() }
    .help("Foreground and background (X swaps)")
  }

  private func pick(_ i: Int) {
    #if os(macOS)
      if NSEvent.modifierFlags.contains(.option) {
        editor.setBg(i)
        return
      }
    #endif
    editor.setFg(i)
  }
}

struct FramesStrip: View {
  let editor: Editor
  static let cell: CGFloat = 48
  static let gap: CGFloat = 4

  var body: some View {
    let _ = editor.revision
    let sketch = editor.sketch
    HStack(spacing: 12) {
      ScrollViewReader { proxy in
        ScrollView(.horizontal) {
          VStack(alignment: .leading, spacing: 3) {
            TagsRow(editor: editor)
            HStack(spacing: FramesStrip.gap) {
              ForEach(0..<sketch.frames, id: \.self) { f in
                FrameCell(editor: editor, frame: f).id(f)
              }
            }
          }
          .padding(8)
        }
        .onChange(of: editor.revision) { proxy.scrollTo(editor.sketch.frame) }
      }
      VStack(alignment: .leading, spacing: 6) {
        HStack {
          Button("New Frame", systemImage: "plus") { editor.addFrame() }
          Button("Duplicate Frames", systemImage: "plus.square.on.square") {
            editor.duplicateFrame()
          }
          Button("Delete Frames", systemImage: "trash") { editor.deleteFrame() }
            .disabled(sketch.frames == editor.selectedFrames.count)
        }
        .labelStyle(.iconOnly)
        HStack {
          Toggle(isOn: Binding(get: { editor.playing }, set: { editor.playing = $0 })) {
            Label(
              editor.playing ? "Stop" : "Play",
              systemImage: editor.playing ? "stop.fill" : "play.fill")
          }
          .toggleStyle(.button)
          .labelStyle(.iconOnly)
          .help("Play the animation (P)")
          if !sketch.tags.isEmpty {
            Picker(
              "Play",
              selection: Binding(get: { editor.playTag }, set: { editor.playTag = $0 })
            ) {
              Text("All").tag(String?.none)
              ForEach(sketch.tags, id: \.self) { t in
                Text(t.name).tag(String?.some(t.name))
              }
            }
            .labelsHidden()
            .fixedSize()
            .help("What the preview plays")
          }
          TextField(
            "ms",
            value: Binding(
              get: { sketch.duration(sketch.frame) }, set: { editor.setDuration($0) }),
            format: .number
          )
          .frame(width: 56)
          .help("The selected frames' duration in milliseconds")
          Text("ms").foregroundStyle(.secondary)
        }
      }
      PreviewBox(editor: editor)
        .frame(width: 72, height: 72)
        .padding(.trailing, 8)
    }
    .frame(height: 96)
  }
}

struct FrameCell: View {
  let editor: Editor
  let frame: Int

  var body: some View {
    let _ = editor.revision
    let sketch = editor.sketch
    let selected = editor.selectedFrames.contains(frame)
    let zoom = max(1, Int(FramesStrip.cell) / max(sketch.width, sketch.height))
    VStack(spacing: 1) {
      Group {
        if let image = sketch.image(frame: frame, zoom: zoom, onion: false) {
          Image(decorative: image, scale: 1)
            .interpolation(.none)
            .resizable()
            .scaledToFit()
        }
      }
      .frame(width: FramesStrip.cell, height: FramesStrip.cell)
      .border(frame == sketch.frame ? Color.accentColor : .secondary.opacity(0.4), width: 2)
      Text("\(frame + 1)")
        .font(.caption2)
        .monospacedDigit()
        .foregroundStyle(selected ? .primary : .secondary)
    }
    .padding(2)
    .background(
      selected ? Color.accentColor.opacity(0.2) : .clear, in: RoundedRectangle(cornerRadius: 4)
    )
    .contentShape(Rectangle())
    .onTapGesture { editor.selectFrame(frame, extend: shiftHeld) }
    .contextMenu {
      Button("Select Through Here") { editor.selectFrame(frame, extend: true) }
      Divider()
      Button(selected && editor.selectedFrames.count > 1 ? "Duplicate Frames" : "Duplicate Frame") {
        keep()
        editor.duplicateFrame()
      }
      Button(selected && editor.selectedFrames.count > 1 ? "Delete Frames" : "Delete Frame") {
        keep()
        editor.deleteFrame()
      }
      .disabled(sketch.frames == (selected ? editor.selectedFrames.count : 1))
      Button("Copy Frames") {
        keep()
        editor.copyFrames()
      }
      Button("Paste Frames After") {
        editor.selectFrame(frame)
        editor.pasteFrames()
      }
      Divider()
      Button("New Tag…") {
        keep()
        editor.newTag()
      }
    }
    .draggable("frame:\(frame)")
    .dropDestination(for: String.self) { items, _ in
      guard let from = dragged(items, "frame:"), from != frame else { return false }
      editor.moveFrame(from, to: frame)
      return true
    }
  }

  /// A menu on a frame outside the selection acts on that frame alone.
  private func keep() {
    if !editor.selectedFrames.contains(frame) {
      editor.selectFrame(frame)
    }
  }

  private var shiftHeld: Bool {
    #if os(macOS)
      NSEvent.modifierFlags.contains(.shift)
    #else
      false
    #endif
  }
}

struct TagsRow: View {
  let editor: Editor

  var body: some View {
    let step = FramesStrip.cell + FramesStrip.gap
    ZStack(alignment: .topLeading) {
      Color.clear.frame(height: 14)
      ForEach(editor.sketch.tags, id: \.self) { t in
        Text(t.name)
          .font(.caption2)
          .lineLimit(1)
          .padding(.horizontal, 4)
          .frame(
            width: CGFloat(t.last - t.first + 1) * step - FramesStrip.gap, height: 14,
            alignment: .leading
          )
          .background(Color.accentColor.opacity(0.25), in: RoundedRectangle(cornerRadius: 3))
          .offset(x: CGFloat(t.first) * step)
          .contextMenu {
            Button("Edit Tag “\(t.name)”…") { editor.tagSheet = t }
            Button("Delete Tag “\(t.name)”") { editor.deleteTag(t.name) }
          }
      }
    }
  }
}

/// The animation at its speed while playing; the active frame otherwise.
struct PreviewBox: View {
  let editor: Editor
  @State private var step = 0

  var body: some View {
    let _ = editor.revision
    let sketch = editor.sketch
    let order = editor.playOrder
    let frame = editor.playing && !order.isEmpty ? order[step % order.count] : sketch.frame
    let zoom = max(1, 72 / max(sketch.width, sketch.height))
    Group {
      if let image = sketch.image(frame: frame, zoom: zoom, onion: false) {
        Image(decorative: image, scale: 1)
          .interpolation(.none)
          .resizable()
          .scaledToFit()
      }
    }
    .border(.secondary.opacity(0.4))
    .task(id: "\(editor.playing) \(editor.playTag ?? "")") {
      guard editor.playing else { return }
      step = 0
      while !Task.isCancelled {
        let order = editor.playOrder
        guard !order.isEmpty else { return }
        let f = order[step % order.count]
        try? await Task.sleep(for: .milliseconds(max(editor.sketch.duration(f), 1)))
        step += 1
      }
    }
  }
}
