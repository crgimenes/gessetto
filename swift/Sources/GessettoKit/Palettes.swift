import CGessetto
import SwiftUI
import UniformTypeIdentifiers

/// A palette to load: 0xRRGGBB colors for indices 1 and up.
struct Palette: Hashable, Identifiable {
  let name: String
  let colors: [UInt32]

  var id: String { name }

  static let presets = [
    Palette(
      name: "PICO-8",
      colors: [
        0x000000, 0x1d2b53, 0x7e2553, 0x008751, 0xab5236, 0x5f574f, 0xc2c3c7, 0xfff1e8,
        0xff004d, 0xffa300, 0xffec27, 0x00e436, 0x29adff, 0x83769c, 0xff77a8, 0xffccaa,
      ]),
    Palette(
      name: "DawnBringer 16",
      colors: [
        0x140c1c, 0x442434, 0x30346d, 0x4e4a4e, 0x854c30, 0x346524, 0xd04648, 0x757161,
        0x597dce, 0xd27d2c, 0x8595a1, 0x6daa2c, 0xd2aa99, 0x6dc2ca, 0xdad45e, 0xdeeed6,
      ]),
    Palette(
      name: "DawnBringer 32",
      colors: [
        0x000000, 0x222034, 0x45283c, 0x663931, 0x8f563b, 0xdf7126, 0xd9a066, 0xeec39a,
        0xfbf236, 0x99e550, 0x6abe30, 0x37946e, 0x4b692f, 0x524b24, 0x323c39, 0x3f3f74,
        0x306082, 0x5b6ee1, 0x639bff, 0x5fcde4, 0xcbdbfc, 0xffffff, 0x9badb7, 0x847e87,
        0x696a6a, 0x595652, 0x76428a, 0xac3232, 0xd95763, 0xd77bba, 0x8f974a, 0x8a6f30,
      ]),
    Palette(name: "Game Boy", colors: [0x0f380f, 0x306230, 0x8bac0f, 0x9bbc0f]),
  ]

  /// A palette file: one "rrggbb" a line (Lospec .hex), a GIMP .gpl, or the
  /// palette of a .gesso image. Nil when nothing in it is a color.
  static func read(_ data: Data, name: String) -> Palette? {
    if let sketch = try? Sketch(gesso: data) {
      let colors = (1..<sketch.colors).map { sketch.color($0) & 0xff_ffff }
      return colors.isEmpty ? nil : Palette(name: name, colors: colors)
    }
    let text = String(decoding: data, as: UTF8.self)
    var colors: [UInt32] = []
    for raw in text.split(whereSeparator: \.isNewline) {
      let line = raw.trimmingCharacters(in: .whitespaces)
      if isHex(line), let v = UInt32(line.suffix(6), radix: 16) {
        colors.append(v)
        continue
      }
      let rgb = line.split(whereSeparator: { $0 == " " || $0 == "\t" }).prefix(3).compactMap {
        UInt32($0)
      }
      if rgb.count == 3 && rgb.allSatisfy({ $0 < 256 }) {
        colors.append(rgb[0] << 16 | rgb[1] << 8 | rgb[2])
      }
    }
    colors = Array(colors.prefix(Int(GST_COLORS_MAX) - 1))
    return colors.isEmpty ? nil : Palette(name: name, colors: colors)
  }

  private static func isHex(_ s: String) -> Bool {
    let digits = s.hasPrefix("#") ? s.dropFirst() : Substring(s)
    return digits.count == 6 && digits.allSatisfy(\.isHexDigit)
  }
}

/// Choose a palette and how the pixels follow it.
struct PaletteSheet: View {
  let editor: Editor
  /// A palette read from a file, offered first.
  let loaded: Palette?
  @State private var choice: Palette
  @State private var match = true
  @Environment(\.dismiss) private var dismiss

  init(editor: Editor, loaded: Palette?) {
    self.editor = editor
    self.loaded = loaded
    _choice = State(initialValue: loaded ?? Palette.presets[0])
  }

  var body: some View {
    NavigationStack {
      Form {
        Section("Load Palette") {
          Picker("Palette", selection: $choice) {
            if let loaded {
              Text(verbatim: "\(loaded.name) (\(loaded.colors.count))").tag(loaded)
            }
            ForEach(Palette.presets) { p in
              Text(verbatim: "\(p.name) (\(p.colors.count))").tag(p)
            }
          }
          HStack(spacing: 2) {
            ForEach(Array(choice.colors.prefix(32).enumerated()), id: \.offset) { _, c in
              Color(rgb: c).frame(width: 10, height: 14)
            }
          }
          Picker("Pixels", selection: $match) {
            Text("Take the nearest new color").tag(true)
            Text("Keep their index (recolor)").tag(false)
          }
          .pickerStyle(.inline)
          Button("From a File…") {
            dismiss()
            editor.importing = .palette
          }
        }
      }
      .formStyle(.grouped)
      .frame(minWidth: 360)
      .toolbar {
        ToolbarItem(placement: .cancellationAction) {
          Button("Cancel") { dismiss() }
        }
        ToolbarItem(placement: .confirmationAction) {
          Button("Load") {
            editor.loadPalette(choice.colors, match: match)
            dismiss()
          }
        }
      }
    }
  }
}
