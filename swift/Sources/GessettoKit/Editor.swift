import CGessetto
import CoreGraphics
import Foundation
import Observation

public enum Tool: Int, CaseIterable, Identifiable, Sendable {
  case pencil, eraser, fill, picker, line, rect, ellipse, select

  public var id: Int { rawValue }

  var name: String {
    ["Pencil", "Eraser", "Fill", "Picker", "Line", "Rectangle", "Ellipse", "Select"][rawValue]
  }

  var key: Character { Array("beginrom")[rawValue] }

  var symbol: String {
    [
      "pencil", "eraser", "drop.fill", "eyedropper", "line.diagonal", "rectangle", "circle",
      "rectangle.dashed",
    ][rawValue]
  }
}

/// One window's editor: the document, how it is shown, and every action the
/// menus, the toolbar and the canvas take on it. `revision` moves whenever
/// SwiftUI has something new to show.
@MainActor
@Observable
public final class Editor {
  public let sketch: Sketch
  private var g: OpaquePointer { sketch.g }

  public private(set) var revision = 0
  public var zoom = 1
  var pan = CGPoint.zero
  /// Fitting again whenever the view changes size, until the person zooms
  /// or pans.
  var autoFit = true
  public var grid = true
  public var onion = false {
    didSet { redraw() }
  }
  public var playing = false
  /// The canvas size sheet; a new document opens with it, once.
  public var sizing = false
  @ObservationIgnored var askSize: Bool
  public var importing = false
  public var newTag = false
  public var error: String?
  public var cursor: (x: Int, y: Int)?

  @ObservationIgnored weak var undo: UndoManager?
  @ObservationIgnored var redraw: () -> Void = {}
  @ObservationIgnored var viewSize = CGSize.zero

  static let zooms = [1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64]

  public init(sketch: Sketch, isNew: Bool) {
    self.sketch = sketch
    self.askSize = isNew
  }

  // MARK: changes and the undo manager

  /// After anything that may have changed the document: a new step goes to
  /// the undo manager, then everything showing it catches up.
  func changed() {
    if gst_new_step(g) != 0 {
      undo?.registerUndo(withTarget: self) { $0.undoStep() }
    }
    revision += 1
    redraw()
  }

  private func undoStep() {
    _ = gst_undo(g)
    if gst_can_redo(g) != 0 {
      undo?.registerUndo(withTarget: self) { $0.redoStep() }
    }
    revision += 1
    redraw()
  }

  private func redoStep() {
    if gst_redo(g) != 0 {
      undo?.registerUndo(withTarget: self) { $0.undoStep() }
    }
    revision += 1
    redraw()
  }

  // MARK: tools and colors

  public var tool: Tool {
    get {
      _ = revision
      return Tool(rawValue: Int(gst_tool_get(g).rawValue)) ?? .pencil
    }
    set {
      gst_tool_set(g, gst_tool(UInt32(newValue.rawValue)))
      changed()
    }
  }

  public var fg: Int {
    _ = revision
    return Int(gst_fg(g))
  }

  public var bg: Int {
    _ = revision
    return Int(gst_bg(g))
  }

  public func setFg(_ index: Int) {
    gst_set_fg(g, UInt32(index))
    changed()
  }

  public func setBg(_ index: Int) {
    gst_set_bg(g, UInt32(index))
    changed()
  }

  public func swapColors() {
    gst_swap_colors(g)
    changed()
  }

  func option(_ o: gst_option) -> Bool {
    _ = revision
    return gst_option_get(g, o) != 0
  }

  func setOption(_ o: gst_option, _ on: Bool) {
    gst_option_set(g, o, on ? 1 : 0)
    changed()
  }

  public var pixelPerfect: Bool {
    get { option(GST_PIXEL_PERFECT) }
    set { setOption(GST_PIXEL_PERFECT, newValue) }
  }

  public var mirrorX: Bool {
    get { option(GST_MIRROR_X) }
    set { setOption(GST_MIRROR_X, newValue) }
  }

  public var mirrorY: Bool {
    get { option(GST_MIRROR_Y) }
    set { setOption(GST_MIRROR_Y, newValue) }
  }

  public var filled: Bool {
    get { option(GST_FILLED) }
    set { setOption(GST_FILLED, newValue) }
  }

  public var transparentSelection: Bool {
    get { option(GST_TRANSPARENT) }
    set { setOption(GST_TRANSPARENT, newValue) }
  }

  /// 0xRRGGBB; a color past the last is appended.
  public func setColor(_ index: Int, _ rgb: UInt32) {
    if gst_color_set(g, UInt32(index), rgb) != 0 {
      changed()
    }
  }

  public func addColor() {
    let n = sketch.colors
    guard n < Int(GST_COLORS_MAX) else { return }
    let base = fg != 0 ? sketch.color(fg) : 0xff00_0000
    setColor(n, base & 0xff_ffff)
    setFg(n)
  }

  // MARK: edits

  private func run(_ edit: (OpaquePointer) -> Int32) {
    if edit(g) != 0 {
      changed()
    }
  }

  public var hasSelection: Bool {
    _ = revision
    return sketch.selection != .none
  }

  public func selectAll() { run { gst_select_all($0) } }
  public func deselect() { run { gst_deselect($0) } }
  public func deleteSelection() { run { gst_delete($0) } }
  public func flip(vertical: Bool) { run { gst_flip($0, vertical ? 1 : 0) } }
  public func rotate() { run { gst_rotate($0) } }

  func moveSelection(dx: Int, dy: Int) {
    run { gst_move_selection($0, Int32(dx), Int32(dy)) }
  }

  /// The clipboard's image, or nil with nothing selected.
  public func copy() -> CGImage? {
    guard gst_copy(g) != 0 else { return nil }
    return clipboardImage()
  }

  public func cut() -> CGImage? {
    guard gst_cut(g) != 0 else { return nil }
    let image = clipboardImage()
    changed()
    return image
  }

  private func clipboardImage() -> CGImage? {
    var px = [UInt32](repeating: 0, count: Int(GST_SIDE_MAX * GST_SIDE_MAX))
    var w: Int32 = 0
    var h: Int32 = 0
    gst_clip_argb(g, &px, &w, &h)
    guard w > 0 && h > 0 else { return nil }
    return Sketch.cgImage(
      Array(px[0..<Int(w * h)]), width: Int(w), height: Int(h), opaque: false)
  }

  /// An image from the system's clipboard, its colors matched to the palette,
  /// floats at the top left of what is in view; without one, the last copy.
  public func paste(_ image: CGImage?) {
    if let image {
      guard image.width <= Int(GST_SIDE_MAX) && image.height <= Int(GST_SIDE_MAX) else {
        error = "The image on the clipboard is larger than 256 × 256."
        return
      }
      guard let px = Editor.argb(image) else { return }
      _ = gst_clip_from_argb(g, px, Int32(image.width), Int32(image.height))
    }
    let corner = imagePoint(.zero)
    run { gst_paste($0, Int32(max(corner.x, 0)), Int32(max(corner.y, 0))) }
  }

  static func argb(_ image: CGImage) -> [UInt32]? {
    let w = image.width
    let h = image.height
    var px = [UInt32](repeating: 0, count: w * h)
    let drawn = px.withUnsafeMutableBytes { raw -> Bool in
      guard
        let ctx = CGContext(
          data: raw.baseAddress, width: w, height: h, bitsPerComponent: 8,
          bytesPerRow: w * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
          bitmapInfo: CGImageAlphaInfo.premultipliedFirst.rawValue
            | CGBitmapInfo.byteOrder32Little.rawValue)
      else { return false }
      ctx.draw(image, in: CGRect(x: 0, y: 0, width: w, height: h))
      return true
    }
    guard drawn else { return nil }
    // Straight alpha again: matching to the palette wants the real colors.
    return px.map { p in
      let a = p >> 24
      guard a != 0 && a != 255 else { return p }
      let un = { (c: UInt32) in min(255, (c * 255 + a / 2) / a) }
      return a << 24 | un(p >> 16 & 0xff) << 16 | un(p >> 8 & 0xff) << 8 | un(p & 0xff)
    }
  }

  // MARK: frames and tags

  public func selectFrame(_ f: Int) {
    guard f >= 0 && f < sketch.frames else { return }
    run { gst_frame_select($0, UInt32(f)) }
  }

  public func step(frames by: Int) {
    let n = sketch.frames
    selectFrame(((sketch.frame + by) % n + n) % n)
  }

  public func addFrame() { run { gst_frame_add($0) } }
  public func duplicateFrame() { run { gst_frame_duplicate($0) } }
  public func deleteFrame() { run { gst_frame_delete($0) } }

  public func moveFrame(_ from: Int, to: Int) {
    run { gst_frame_move($0, UInt32(from), UInt32(to)) }
  }

  public func setDuration(_ ms: Int) {
    run { gst_frame_duration($0, UInt32(gst_frame($0)), UInt32(clamping: ms)) }
  }

  @discardableResult
  public func setTag(_ name: String, first: Int, last: Int) -> Bool {
    var err = [CChar](repeating: 0, count: Int(GST_ERROR_MAX))
    let ok = gst_tag_set(g, name, UInt32(clamping: first), UInt32(clamping: last), &err)
    if ok == 0 {
      error = message(err)
      return false
    }
    changed()
    return true
  }

  public func deleteTag(_ name: String) { run { gst_tag_delete($0, name) } }

  public func importPNG(_ data: Data) {
    do {
      try sketch.importPNG(data)
    } catch {
      self.error = "Cannot import the PNG. \(error.localizedDescription)"
      return
    }
    changed()
    fit()
  }

  /// One undo step, so a new document's size counts as a change to save.
  public func resize(width: Int, height: Int) {
    var err = [CChar](repeating: 0, count: Int(GST_ERROR_MAX))
    if gst_resize(g, Int32(clamping: width), Int32(clamping: height), &err) == 0 {
      error = message(err)
      return
    }
    changed()
    fit()
  }

  // MARK: the view

  /// The image pixel under a point of the canvas.
  func imagePoint(_ p: CGPoint) -> (x: Int, y: Int) {
    let z = CGFloat(zoom)
    return (Int(((p.x - pan.x) / z).rounded(.down)), Int(((p.y - pan.y) / z).rounded(.down)))
  }

  /// The zoom changes keeping the image pixel under p where it is.
  func zoom(to z: Int, at p: CGPoint) {
    let old = CGFloat(zoom)
    let new = CGFloat(z)
    pan = CGPoint(x: p.x - (p.x - pan.x) * new / old, y: p.y - (p.y - pan.y) * new / old)
    pan = CGPoint(x: pan.x.rounded(), y: pan.y.rounded())
    zoom = z
    autoFit = false
    redraw()
  }

  func zoom(by step: Int, at p: CGPoint) {
    let i = Editor.zooms.lastIndex { $0 <= zoom } ?? 0
    let j = min(max(i + step, 0), Editor.zooms.count - 1)
    zoom(to: Editor.zooms[j], at: p)
  }

  public func zoom(by step: Int) {
    zoom(by: step, at: CGPoint(x: viewSize.width / 2, y: viewSize.height / 2))
  }

  /// The largest zoom that leaves a margin around the image, centered.
  public func fit() {
    guard viewSize.width > 0 && viewSize.height > 0 else { return }
    let room = CGSize(width: viewSize.width - 32, height: viewSize.height - 32)
    zoom =
      Editor.zooms.last {
        CGFloat(sketch.width * $0) <= room.width && CGFloat(sketch.height * $0) <= room.height
      } ?? 1
    let z = CGFloat(zoom)
    pan = CGPoint(
      x: ((viewSize.width - CGFloat(sketch.width) * z) / 2).rounded(),
      y: ((viewSize.height - CGFloat(sketch.height) * z) / 2).rounded())
    autoFit = true
    redraw()
  }

  func resized(_ size: CGSize) {
    viewSize = size
    if autoFit {
      fit()
    }
  }

  public func toggleGrid() {
    grid.toggle()
    redraw()
  }
}
