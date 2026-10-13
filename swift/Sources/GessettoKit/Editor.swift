import CGessetto
import CoreGraphics
import Foundation
import ImageIO
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
  /// Points per image pixel: whole numbers at rest, anything between them
  /// during a pinch.
  public var zoom: CGFloat = 1
  /// Where the image's top left is on the canvas. At least half the image,
  /// or half the canvas when the image is larger, always stays in view.
  @ObservationIgnored var pan = CGPoint.zero {
    didSet {
      guard viewSize.width > 0 && viewSize.height > 0 else { return }
      let w = CGFloat(sketch.width) * zoom
      let h = CGFloat(sketch.height) * zoom
      let kx = min(w, viewSize.width) / 2
      let ky = min(h, viewSize.height) / 2
      let x = min(max(pan.x, kx - w), viewSize.width - kx)
      let y = min(max(pan.y, ky - h), viewSize.height - ky)
      if x != pan.x || y != pan.y {
        pan = CGPoint(x: x, y: y)
      }
    }
  }
  /// Fitting again whenever the view changes size, until the person zooms
  /// or pans.
  var autoFit = true
  public var grid = true {
    didSet { redraw() }
  }
  public var onion = false {
    didSet { redraw() }
  }
  public var playing = false
  /// The tag the preview plays, or nil for every frame.
  public var playTag: String?

  /// The frames the preview shows, in order, once round: a tag's direction
  /// applied.
  var playOrder: [Int] {
    _ = revision
    return Exporter.playOrder(sketch, tag: playTag)
  }
  /// The canvas size sheet; a new document opens with it, once.
  public var sizing = false
  @ObservationIgnored var askSize: Bool
  public enum Import {
    case png, sheet, palette
  }

  /// The palette sheet, with a palette read from a file when there is one.
  var paletteSheet = false
  var loadedPalette: Palette?

  /// What the file importer is open for.
  public var importing: Import?
  /// A sprite sheet read, waiting for its frame size.
  var sheet: Sheet?

  struct Sheet {
    let argb: [UInt32]
    let width: Int
    let height: Int
  }
  public var exporting = false
  /// Made by the export sheet, waiting for the save panel.
  var exportFile: ExportFile?
  /// The tag sheet: a new tag (empty name) or the one being edited.
  var tagSheet: Sketch.Tag?
  public var error: String?
  public var cursor: (x: Int, y: Int)?

  @ObservationIgnored weak var undo: UndoManager?
  @ObservationIgnored var redraw: () -> Void = {}
  @ObservationIgnored var viewSize = CGSize.zero

  static let zooms: [CGFloat] = [1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64]

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

  /// The color at an image pixel becomes the foreground, or the background.
  func pick(x: Int, y: Int, secondary: Bool) {
    let i = Int(gst_index_at(g, Int32(clamping: x), Int32(clamping: y)))
    if secondary {
      setBg(i)
    } else {
      setFg(i)
    }
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

  /// The pen, for the tools that stroke.
  public var brushSize: Int {
    get {
      _ = revision
      return Int(gst_brush_size(g))
    }
    set {
      gst_brush_set(g, UInt32(min(max(newValue, 1), 16)), gst_brush_round(g))
      changed()
    }
  }

  public var brushRound: Bool {
    get {
      _ = revision
      return gst_brush_round(g) != 0
    }
    set {
      gst_brush_set(g, gst_brush_size(g), newValue ? 1 : 0)
      changed()
    }
  }

  var usesBrush: Bool {
    [.pencil, .eraser, .line, .rect, .ellipse].contains(tool)
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

  /// Its pixels take the nearest color left.
  public func deleteColor(_ index: Int) { run { gst_color_delete($0, UInt32(index)) } }

  /// Pixels keep their colors; only the order changes.
  public func moveColor(_ from: Int, to: Int) {
    run { gst_color_move($0, UInt32(from), UInt32(to)) }
  }

  public func loadPalette(_ colors: [UInt32], match: Bool) {
    run { gst_palette_load($0, colors, UInt32(colors.count), match ? 1 : 0) }
  }

  func readPalette(_ data: Data, name: String) {
    guard let p = Palette.read(data, name: name) else {
      error = "No colors in that file: it takes .hex (one rrggbb a line), .gpl or .gesso."
      return
    }
    loadedPalette = p
    paletteSheet = true
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

  /// The other end of a frame selection made with Shift; nil when only the
  /// active frame is selected.
  public var frameAnchor: Int?

  /// The selected frames: from the anchor to the active one.
  public var selectedFrames: ClosedRange<Int> {
    _ = revision
    let f = sketch.frame
    guard let a = frameAnchor, a < sketch.frames else { return f...f }
    return min(a, f)...max(a, f)
  }

  /// extend keeps the anchor (Shift-click); otherwise f alone is selected.
  public func selectFrame(_ f: Int, extend: Bool = false) {
    guard f >= 0 && f < sketch.frames else { return }
    frameAnchor = extend ? (frameAnchor ?? sketch.frame) : nil
    run { gst_frame_select($0, UInt32(f)) }
  }

  public func step(frames by: Int) {
    let n = sketch.frames
    selectFrame(((sketch.frame + by) % n + n) % n)
  }

  public func addFrame() {
    frameAnchor = nil
    run { gst_frame_add($0) }
  }

  /// The copies come out selected.
  public func duplicateFrame() {
    let r = selectedFrames
    if gst_frames_duplicate(g, UInt32(r.lowerBound), UInt32(r.upperBound)) != 0 {
      frameAnchor = sketch.frame + r.count - 1
      changed()
    }
  }

  public func deleteFrame() {
    let r = selectedFrames
    frameAnchor = nil
    run { gst_frames_delete($0, UInt32(r.lowerBound), UInt32(r.upperBound)) }
  }

  /// A frame dropped on another: the selection moves with it when it is part
  /// of it, landing where the frame was dropped.
  public func moveFrame(_ from: Int, to target: Int) {
    let r = selectedFrames.contains(from) ? selectedFrames : from...from
    guard !r.contains(target) else { return }
    let to = target > r.upperBound ? target - r.count + 1 : target
    if gst_frames_move(g, UInt32(r.lowerBound), UInt32(r.upperBound), UInt32(to)) != 0 {
      frameAnchor = r.count > 1 ? to + r.count - 1 : nil
      changed()
    }
  }

  /// Frames copied as colors, for any document of the same size.
  struct FrameClip {
    let width: Int
    let height: Int
    let argb: [UInt32]
    let durations: [UInt32]
  }

  static var frameClip: FrameClip?

  public func copyFrames() {
    let r = selectedFrames
    Editor.frameClip = FrameClip(
      width: sketch.width, height: sketch.height, argb: r.flatMap { sketch.argb(frame: $0) },
      durations: r.map { UInt32(sketch.duration($0)) })
  }

  /// After the active frame, the pasted frames selected.
  public func pasteFrames() {
    guard let c = Editor.frameClip else { return }
    guard c.width == sketch.width && c.height == sketch.height else {
      error =
        "The frames copied are \(c.width) × \(c.height); this image is \(sketch.width) × \(sketch.height)."
      return
    }
    guard sketch.pasteFrames(c.argb, durations: c.durations) else {
      error = "Cannot paste: an image holds at most 256 frames."
      return
    }
    frameAnchor = sketch.frame + c.durations.count - 1
    changed()
  }

  /// Every selected frame.
  public func setDuration(_ ms: Int) {
    let r = selectedFrames
    run {
      gst_frames_duration($0, UInt32(r.lowerBound), UInt32(r.upperBound), UInt32(clamping: ms))
    }
  }

  @discardableResult
  public func setTag(
    _ name: String, first: Int, last: Int, direction: Sketch.Direction = .forward
  ) -> Bool {
    var err = [CChar](repeating: 0, count: Int(GST_ERROR_MAX))
    let ok = gst_tag_set(
      g, name, UInt32(clamping: first), UInt32(clamping: last), UInt32(direction.rawValue), &err)
    if ok == 0 {
      error = message(err)
      return false
    }
    changed()
    return true
  }

  public func deleteTag(_ name: String) { run { gst_tag_delete($0, name) } }

  /// A new tag over the selected frames, or from the active one to the last.
  public func newTag() {
    let r = selectedFrames
    let last = r.count > 1 ? r.upperBound : sketch.frames - 1
    tagSheet = Sketch.Tag(name: "", first: r.lowerBound, last: last)
  }

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

  func export(_ o: ExportOptions) {
    guard let made = Exporter.make(sketch, o) else {
      error = "Cannot make the \(o.format.title) file."
      return
    }
    exportFile = ExportFile(data: made, type: o.format.type)
  }

  /// Any image the system reads becomes a sheet to cut.
  func readSheet(_ data: Data) {
    guard let source = CGImageSourceCreateWithData(data as CFData, nil),
      let image = CGImageSourceCreateImageAtIndex(source, 0, nil),
      let px = Editor.argb(image)
    else {
      error = "Cannot read that image."
      return
    }
    sheet = Sheet(argb: px, width: image.width, height: image.height)
  }

  func importSheet(_ s: Sheet, frameWidth: Int, frameHeight: Int) {
    do {
      try sketch.importSheet(
        s.argb, width: s.width, height: s.height, frame: (frameWidth, frameHeight))
    } catch {
      self.error = "Cannot import the sprite sheet. \(error.localizedDescription)"
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
    (Int(((p.x - pan.x) / zoom).rounded(.down)), Int(((p.y - pan.y) / zoom).rounded(.down)))
  }

  /// The zoom changes keeping the image point under p where it is.
  func zoom(to z: CGFloat, at p: CGPoint) {
    let new = min(max(z, Editor.zooms[0]), Editor.zooms[Editor.zooms.count - 1])
    pan = CGPoint(x: p.x - (p.x - pan.x) * new / zoom, y: p.y - (p.y - pan.y) * new / zoom)
    zoom = new
    if new == new.rounded() {
      pan = CGPoint(x: pan.x.rounded(), y: pan.y.rounded())
    }
    autoFit = false
    redraw()
  }

  /// The next zoom of the list up or down from wherever the zoom is.
  func zoom(by step: Int, at p: CGPoint) {
    let next =
      step > 0
      ? Editor.zooms.first { $0 > zoom + 0.01 } : Editor.zooms.last { $0 < zoom - 0.01 }
    zoom(to: next ?? zoom, at: p)
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
        CGFloat(sketch.width) * $0 <= room.width && CGFloat(sketch.height) * $0 <= room.height
      } ?? 1
    pan = CGPoint(
      x: ((viewSize.width - CGFloat(sketch.width) * zoom) / 2).rounded(),
      y: ((viewSize.height - CGFloat(sketch.height) * zoom) / 2).rounded())
    autoFit = true
    redraw()
  }

  func resized(_ size: CGSize) {
    viewSize = size
    if autoFit {
      fit()
    }
  }
}
