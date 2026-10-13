import CGessetto
import ImageIO
import SwiftUI
import UniformTypeIdentifiers

/// Bytes made for export, handed to the system's save panel.
struct ExportFile: FileDocument {
  static let readableContentTypes: [UTType] = [.png, .gif, .cHeader]
  let data: Data
  let type: UTType

  init(data: Data, type: UTType) {
    self.data = data
    self.type = type
  }

  init(configuration: ReadConfiguration) throws {
    throw CocoaError(.featureUnsupported)
  }

  func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
    FileWrapper(regularFileWithContents: data)
  }
}

enum ExportFormat: String, CaseIterable, Identifiable {
  case png, sheet, gif, header

  var id: String { rawValue }

  var title: String {
    switch self {
    case .png: "PNG (this frame)"
    case .sheet: "Sprite sheet (PNG)"
    case .gif: "Animated GIF"
    case .header: "C header (as gesso2c)"
    }
  }

  var type: UTType {
    switch self {
    case .png, .sheet: .png
    case .gif: .gif
    case .header: .cHeader
    }
  }

  /// Formats that take several frames: all of them, or a tag's.
  var animated: Bool { self == .sheet || self == .gif }
  var scaled: Bool { self != .header }
}

struct ExportOptions {
  var format = ExportFormat.png
  var scale = 1
  /// A tag's name, or nil for every frame.
  var tag: String?
  /// Frames per row of a sprite sheet; 0 puts them all in one row.
  var columns = 0
  /// The document's name, which names a header's identifiers.
  var name = "untitled"
}

enum Exporter {
  static func make(_ sketch: Sketch, _ o: ExportOptions) -> Data? {
    let frames = frameList(sketch, tag: o.tag)
    switch o.format {
    case .png: return png(sketch, frame: sketch.frame, scale: o.scale)
    case .sheet: return sheet(sketch, frames: frames, columns: o.columns, scale: o.scale)
    case .gif:
      let order = o.tag == nil ? frames : playOrder(sketch, tag: o.tag)
      return gif(sketch, frames: order, scale: o.scale)
    case .header: return try? sketch.header(fileName: o.name + ".gesso")
    }
  }

  static func frameList(_ sketch: Sketch, tag: String?) -> [Int] {
    guard let tag, let t = sketch.tags.first(where: { $0.name == tag }) else {
      return Array(0..<sketch.frames)
    }
    return Array(t.first...t.last)
  }

  /// How a tag plays once round: ping-pong comes back without repeating its
  /// ends, so it loops evenly.
  static func playOrder(_ sketch: Sketch, tag: String?) -> [Int] {
    let frames = frameList(sketch, tag: tag)
    guard let tag, let t = sketch.tags.first(where: { $0.name == tag }) else { return frames }
    switch t.direction {
    case .forward: return frames
    case .reverse: return frames.reversed()
    case .pingpong: return frames + frames.dropFirst().dropLast().reversed()
    }
  }

  /// A frame as a PNG, each image pixel scale x scale pixels.
  static func png(_ sketch: Sketch, frame: Int, scale: Int) -> Data? {
    let w = sketch.width
    let h = sketch.height
    let px = upscale(sketch.argb(frame: frame), width: w, height: h, by: scale)
    guard let image = Sketch.cgImage(px, width: w * scale, height: h * scale, opaque: false)
    else { return nil }
    return encode([image], as: .png)
  }

  /// The frames in reading order, columns to a row, as one PNG.
  static func sheet(_ sketch: Sketch, frames: [Int], columns: Int, scale: Int) -> Data? {
    guard !frames.isEmpty else { return nil }
    let w = sketch.width
    let h = sketch.height
    let cols = columns > 0 ? min(columns, frames.count) : frames.count
    let rows = (frames.count + cols - 1) / cols
    let sw = w * cols
    var px = [UInt32](repeating: 0, count: sw * h * rows)
    for (n, f) in frames.enumerated() {
      let src = sketch.argb(frame: f)
      let ox = n % cols * w
      let oy = n / cols * h
      for y in 0..<h {
        for x in 0..<w {
          px[(oy + y) * sw + ox + x] = src[y * w + x]
        }
      }
    }
    let big = upscale(px, width: sw, height: h * rows, by: scale)
    guard
      let image = Sketch.cgImage(
        big, width: sw * scale, height: h * rows * scale, opaque: false)
    else { return nil }
    return encode([image], as: .png)
  }

  /// The frames with their durations, looping forever. GIF counts time in
  /// hundredths of a second, so durations round to 10 ms.
  static func gif(_ sketch: Sketch, frames: [Int], scale: Int) -> Data? {
    let w = sketch.width
    let h = sketch.height
    let data = NSMutableData()
    guard !frames.isEmpty,
      let dest = CGImageDestinationCreateWithData(
        data as CFMutableData, UTType.gif.identifier as CFString, frames.count, nil)
    else { return nil }
    let loop = [kCGImagePropertyGIFDictionary: [kCGImagePropertyGIFLoopCount: 0]]
    CGImageDestinationSetProperties(dest, loop as CFDictionary)
    for f in frames {
      let px = upscale(sketch.argb(frame: f), width: w, height: h, by: scale)
      guard let image = Sketch.cgImage(px, width: w * scale, height: h * scale, opaque: false)
      else { return nil }
      let delay = Double(sketch.duration(f)) / 1000
      let props = [
        kCGImagePropertyGIFDictionary: [
          kCGImagePropertyGIFDelayTime: delay, kCGImagePropertyGIFUnclampedDelayTime: delay,
        ]
      ]
      CGImageDestinationAddImage(dest, image, props as CFDictionary)
    }
    guard CGImageDestinationFinalize(dest) else { return nil }
    return data as Data
  }

  static func upscale(_ px: [UInt32], width: Int, height: Int, by s: Int) -> [UInt32] {
    guard s > 1 else { return px }
    var out = [UInt32](repeating: 0, count: width * s * height * s)
    for y in 0..<height * s {
      for x in 0..<width * s {
        out[y * width * s + x] = px[(y / s) * width + x / s]
      }
    }
    return out
  }

  static func encode(_ images: [CGImage], as type: UTType) -> Data? {
    let data = NSMutableData()
    guard
      let dest = CGImageDestinationCreateWithData(
        data as CFMutableData, type.identifier as CFString, images.count, nil)
    else { return nil }
    for image in images {
      CGImageDestinationAddImage(dest, image, nil)
    }
    guard CGImageDestinationFinalize(dest) else { return nil }
    return data as Data
  }
}

struct ExportSheet: View {
  let editor: Editor
  @State private var o: ExportOptions

  init(editor: Editor, name: String) {
    self.editor = editor
    _o = State(initialValue: ExportOptions(name: name))
  }
  @Environment(\.dismiss) private var dismiss

  var body: some View {
    let tags = editor.sketch.tags
    NavigationStack {
      Form {
        Section("Export") {
          Picker("Format", selection: $o.format) {
            ForEach(ExportFormat.allCases) { Text($0.title).tag($0) }
          }
          if o.format.animated {
            Picker("Frames", selection: $o.tag) {
              Text("All frames").tag(String?.none)
              ForEach(tags, id: \.self) { t in
                Text(verbatim: "\(t.name) (\(t.first + 1)–\(t.last + 1))").tag(String?.some(t.name))
              }
            }
          }
          if o.format == .sheet {
            Stepper(value: $o.columns, in: 0...256) {
              LabeledContent("Columns") {
                Text(o.columns == 0 ? "One row" : "\(o.columns)")
              }
            }
          }
          if o.format.scaled {
            Stepper(value: $o.scale, in: 1...16) {
              LabeledContent("Scale") { Text(verbatim: "\(o.scale)×") }
            }
          }
        }
      }
      .formStyle(.grouped)
      .frame(minWidth: 340)
      .toolbar {
        ToolbarItem(placement: .cancellationAction) {
          Button("Cancel") { dismiss() }
        }
        ToolbarItem(placement: .confirmationAction) {
          Button("Export…") {
            editor.export(o)
            dismiss()
          }
        }
      }
    }
  }
}
