import CGessetto
import CoreGraphics
import Foundation

/// A NUL-ended message the C side wrote.
func message(_ err: [CChar]) -> String {
  String(decoding: err.prefix { $0 != 0 }.map { UInt8(bitPattern: $0) }, as: UTF8.self)
}

public struct GessoError: LocalizedError {
  public let message: String
  public var errorDescription: String? { message }
}

/// The C core's document. Reading, importing and writing share one scratch
/// area in C among every document, and documents open on background
/// threads: those three go through `scratch`.
public final class Sketch: @unchecked Sendable {
  let g: OpaquePointer
  private static let scratch = NSLock()

  public init() {
    guard let mem = calloc(1, gst_size()) else {
      fatalError("gessetto: no memory for a document")
    }
    g = OpaquePointer(mem)
    gst_init(g)
  }

  deinit {
    free(UnsafeMutableRawPointer(g))
  }

  public convenience init(gesso data: Data) throws {
    self.init()
    try load(data, gst_read)
  }

  /// The PNG's image replaces this one, as one undo step.
  public func importPNG(_ data: Data) throws {
    try load(data, gst_import_png)
  }

  private func load(
    _ data: Data,
    _ reader: (OpaquePointer, UnsafePointer<UInt8>, Int, UnsafeMutablePointer<CChar>) -> Int32
  ) throws {
    guard !data.isEmpty else {
      throw GessoError(message: "The file is empty.")
    }
    var err = [CChar](repeating: 0, count: Int(GST_ERROR_MAX))
    let ok = Sketch.scratch.withLock {
      data.withUnsafeBytes { raw in
        reader(g, raw.bindMemory(to: UInt8.self).baseAddress!, data.count, &err)
      }
    }
    if ok == 0 {
      throw GessoError(message: message(err))
    }
  }

  public func gesso() throws -> Data {
    try Sketch.scratch.withLock {
      var n = 0
      guard let p = gst_write(g, &n) else {
        throw GessoError(message: "The image is too large to save.")
      }
      return Data(bytes: p, count: n)
    }
  }

  public var width: Int { Int(gst_width(g)) }
  public var height: Int { Int(gst_height(g)) }
  public var frames: Int { Int(gst_frames(g)) }
  public var frame: Int { Int(gst_frame(g)) }
  public var colors: Int { Int(gst_colors(g)) }

  public func duration(_ frame: Int) -> Int { Int(gst_duration(g, UInt32(frame))) }
  public func color(_ index: Int) -> UInt32 { gst_color(g, UInt32(index)) }

  public struct Tag: Hashable {
    public let name: String
    public let first: Int
    public let last: Int
  }

  public var tags: [Tag] {
    (0..<gst_tags(g)).map {
      Tag(
        name: String(
          decoding: UnsafeRawBufferPointer(
            start: gst_tag_name(g, $0), count: strlen(gst_tag_name(g, $0))), as: UTF8.self),
        first: Int(gst_tag_first(g, $0)),
        last: Int(gst_tag_last(g, $0)))
    }
  }

  public enum Selection: Equatable {
    case none
    case marked(CGRect)
    case floating(CGRect)
  }

  public var selection: Selection {
    var r: [Int32] = [0, 0, 0, 0]
    let kind = gst_selection(g, &r)
    let rect = CGRect(x: Int(r[0]), y: Int(r[1]), width: Int(r[2]), height: Int(r[3]))
    switch kind {
    case 1: return .marked(rect)
    case 2: return .floating(rect)
    default: return .none
    }
  }

  /// A frame as opaque pixels, transparency drawn as the checkerboard for
  /// that zoom; nil for a frame that is not there.
  public func image(frame: Int, zoom: Int, onion: Bool) -> CGImage? {
    guard frame >= 0 && frame < frames else { return nil }
    let w = width
    let h = height
    var px = [UInt32](repeating: 0, count: w * h)
    gst_render(g, &px, UInt32(frame), Int32(clamping: zoom), onion ? 1 : 0)
    return Sketch.cgImage(px, width: w, height: h, opaque: true)
  }

  /// 0xAARRGGBB pixels as an image, alpha straight.
  static func cgImage(_ px: [UInt32], width: Int, height: Int, opaque: Bool) -> CGImage? {
    let data = px.withUnsafeBufferPointer { Data(buffer: $0) }
    guard let provider = CGDataProvider(data: data as CFData) else { return nil }
    let alpha: CGImageAlphaInfo = opaque ? .noneSkipFirst : .first
    return CGImage(
      width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
      bytesPerRow: width * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
      bitmapInfo: CGBitmapInfo(
        rawValue: alpha.rawValue | CGBitmapInfo.byteOrder32Little.rawValue),
      provider: provider, decode: nil, shouldInterpolate: false, intent: .defaultIntent)
  }
}
