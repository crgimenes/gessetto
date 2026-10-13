import CGessetto
import CoreGraphics
import Foundation

/// What a key means on the canvas, whatever platform it came from.
enum CanvasKey {
  case char(Character)
  case left, right, up, down
  case delete, escape, enter, space
}

/// The canvas's input and drawing, the same on the Mac and the iPad; the
/// platform views feed it points in view coordinates (top-left origin).
@MainActor
final class CanvasCore {
  let editor: Editor
  private var g: OpaquePointer { editor.sketch.g }
  private(set) var panning = false
  private var panFrom = CGPoint.zero
  private var panStart = CGPoint.zero
  private var drawing = false
  var spaceHeld = false
  var ants = 0
  /// The lasso's path while it is drawn, in image pixels.
  private var trail: [CGPoint] = []

  init(editor: Editor) {
    self.editor = editor
  }

  // MARK: pointer

  /// Option (alt) with a drawing tool picks the color instead, as the
  /// picker would, without leaving the tool.
  func press(_ p: CGPoint, secondary: Bool, pan: Bool, alt: Bool = false) {
    if pan || spaceHeld {
      panning = true
      panFrom = p
      panStart = editor.pan
      return
    }
    let q = editor.imagePoint(p)
    if alt && ![.select, .lasso, .text, .picker].contains(editor.tool) {
      editor.pick(x: q.x, y: q.y, secondary: secondary)
      return
    }
    drawing = true
    gst_press(g, Int32(clamping: q.x), Int32(clamping: q.y), secondary ? 1 : 0)
    if editor.tool == .lasso && editor.sketch.selection == .none {
      trail = [CGPoint(x: q.x, y: q.y)]
    }
    if editor.tool == .text && editor.sketch.selection == .none {
      editor.textAt = q
    }
    editor.redraw()
  }

  func drag(_ p: CGPoint, shift: Bool, alt: Bool) {
    if panning {
      editor.pan = CGPoint(x: panStart.x + p.x - panFrom.x, y: panStart.y + p.y - panFrom.y)
      editor.autoFit = false
      editor.redraw()
      return
    }
    hover(p)
    guard drawing else { return }
    let q = editor.imagePoint(p)
    gst_drag(g, Int32(clamping: q.x), Int32(clamping: q.y), mods(shift, alt))
    if !trail.isEmpty {
      trail.append(CGPoint(x: q.x, y: q.y))
    }
    editor.redraw()
  }

  func release(_ p: CGPoint, shift: Bool, alt: Bool) {
    if panning {
      panning = false
      return
    }
    guard drawing else { return }
    drawing = false
    trail = []
    let q = editor.imagePoint(p)
    gst_release(g, Int32(clamping: q.x), Int32(clamping: q.y), mods(shift, alt))
    editor.changed()
  }

  /// A press taken over by a gesture (two fingers): what it drew goes back.
  func cancel() {
    panning = false
    guard drawing else { return }
    drawing = false
    trail = []
    gst_cancel(g)
    editor.redraw()
  }

  func hover(_ p: CGPoint?) {
    guard let p else {
      editor.cursor = nil
      editor.redraw()
      return
    }
    let q = editor.imagePoint(p)
    let inside = q.x >= 0 && q.y >= 0 && q.x < editor.sketch.width && q.y < editor.sketch.height
    let shown = inside ? q : nil
    if editor.cursor?.x != shown?.x || editor.cursor?.y != shown?.y {
      editor.cursor = shown
      editor.redraw()
    }
  }

  private func mods(_ shift: Bool, _ alt: Bool) -> UInt32 {
    (shift ? GST_SHIFT : 0) | (alt ? GST_ALT : 0)
  }

  /// Trackpad and wheel: the view moves under the fingers.
  func scroll(dx: CGFloat, dy: CGFloat) {
    editor.pan = CGPoint(x: (editor.pan.x + dx).rounded(), y: (editor.pan.y + dy).rounded())
    editor.autoFit = false
    editor.redraw()
  }

  private var pinchAt = CGPoint.zero

  /// A pinch step: the zoom times factor, following the fingers.
  func magnify(by factor: CGFloat, at p: CGPoint) {
    pinchAt = p
    editor.zoom(to: editor.zoom * factor, at: p)
  }

  /// At the end of a pinch the zoom settles on a whole number, where every
  /// image pixel has the same size on screen.
  func endMagnify() {
    editor.zoom(to: editor.zoom.rounded(), at: pinchAt)
  }

  // MARK: keys

  /// Single keys while the canvas has the focus; false for any it leaves to
  /// the system.
  func key(_ k: CanvasKey, down: Bool, shift: Bool) -> Bool {
    if case .space = k {
      spaceHeld = down
      return true
    }
    guard down else { return false }
    let step = shift ? 10 : 1
    switch k {
    case .left: editor.moveSelection(dx: -step, dy: 0)
    case .right: editor.moveSelection(dx: step, dy: 0)
    case .up: editor.moveSelection(dx: 0, dy: -step)
    case .down: editor.moveSelection(dx: 0, dy: step)
    case .delete: editor.deleteSelection()
    case .enter where editor.shapeOpen: editor.finishShape()
    case .escape where editor.shapeOpen: editor.cancelShape()
    case .escape, .enter: editor.deselect()
    case .space: break
    case .char(let c): return char(c, shift: shift)
    }
    return true
  }

  private func char(_ c: Character, shift: Bool) -> Bool {
    if shift {
      switch c {
      case "h": editor.flip(vertical: false)
      case "v": editor.flip(vertical: true)
      default: return false
      }
      return true
    }
    switch c {
    case "x": editor.swapColors()
    case "-": editor.brushSize -= 1
    case "=": editor.brushSize += 1
    case "[": editor.setFg((editor.fg + editor.sketch.colors - 1) % editor.sketch.colors)
    case "]": editor.setFg((editor.fg + 1) % editor.sketch.colors)
    case ",": editor.step(frames: -1)
    case ".": editor.step(frames: 1)
    case "p": editor.playing.toggle()
    default:
      guard let t = Tool.allCases.first(where: { $0.key == c }) else { return false }
      editor.tool = t
    }
    return true
  }

  // MARK: drawing

  /// Draws into a context whose origin is the top left, y down.
  func draw(in ctx: CGContext, size: CGSize, scale: CGFloat, background: CGColor) {
    ctx.setFillColor(background)
    ctx.fill(CGRect(origin: .zero, size: size))
    let sketch = editor.sketch
    let z = editor.zoom
    let rect = CGRect(
      x: editor.pan.x, y: editor.pan.y, width: CGFloat(sketch.width) * z,
      height: CGFloat(sketch.height) * z)
    if let image = sketch.image(
      frame: sketch.frame, zoom: Int(editor.zoom.rounded()), onion: editor.onion)
    {
      ctx.saveGState()
      ctx.interpolationQuality = .none
      ctx.translateBy(x: 0, y: rect.minY + rect.maxY)
      ctx.scaleBy(x: 1, y: -1)
      ctx.draw(image, in: rect)
      ctx.restoreGState()
    }
    let hair = 1 / scale
    if editor.grid && editor.zoom >= 4 {
      ctx.saveGState()
      ctx.clip(to: rect)
      ctx.setStrokeColor(CGColor(gray: 0, alpha: 48.0 / 255))
      ctx.setLineWidth(hair)
      for i in 1..<sketch.width {
        let x = rect.minX + CGFloat(i) * z + hair / 2
        ctx.move(to: CGPoint(x: x, y: rect.minY))
        ctx.addLine(to: CGPoint(x: x, y: rect.maxY))
      }
      for i in 1..<sketch.height {
        let y = rect.minY + CGFloat(i) * z + hair / 2
        ctx.move(to: CGPoint(x: rect.minX, y: y))
        ctx.addLine(to: CGPoint(x: rect.maxX, y: y))
      }
      ctx.strokePath()
      ctx.restoreGState()
    }
    if let outline = selectionOutline(origin: rect.origin, z: z, hair: hair) {
      ctx.setLineWidth(hair)
      ctx.setStrokeColor(CGColor(gray: 1, alpha: 1))
      ctx.addPath(outline)
      ctx.strokePath()
      ctx.setStrokeColor(CGColor(gray: 0, alpha: 1))
      ctx.setLineDash(
        phase: CGFloat(ants) * 2 * hair, lengths: [4 * hair * scale, 4 * hair * scale])
      ctx.addPath(outline)
      ctx.strokePath()
      ctx.setLineDash(phase: 0, lengths: [])
    }
    // The pixel under the pointer, outlined to show where the tool lands.
    if let c = editor.cursor, !panning, !spaceHeld {
      let n = editor.usesBrush ? editor.brushSize : 1
      let lo = CGFloat((n - 1) / 2)
      let cell = CGRect(
        x: rect.minX + (CGFloat(c.x) - lo) * z, y: rect.minY + (CGFloat(c.y) - lo) * z,
        width: z * CGFloat(n), height: z * CGFloat(n))
      ctx.setLineWidth(hair)
      ctx.setStrokeColor(CGColor(gray: 0, alpha: 0.8))
      ctx.stroke(cell.insetBy(dx: hair / 2, dy: hair / 2))
      ctx.setStrokeColor(CGColor(gray: 1, alpha: 0.8))
      ctx.stroke(cell.insetBy(dx: hair * 1.5, dy: hair * 1.5))
    }
  }

  /// Where the marching ants run: the lasso's path while it is drawn, then
  /// the selection's edge, cell by cell for a lasso's.
  private func selectionOutline(origin o: CGPoint, z: CGFloat, hair: CGFloat) -> CGPath? {
    let path = CGMutablePath()
    if trail.count > 1 {
      path.addLines(
        between: trail.map { CGPoint(x: o.x + ($0.x + 0.5) * z, y: o.y + ($0.y + 0.5) * z) })
      path.closeSubpath()
      return path
    }
    let r: CGRect
    switch editor.sketch.selection {
    case .none: return nil
    case .marked(let s), .floating(let s): r = s
    }
    guard let mask = editor.sketch.mask else {
      path.addRect(
        CGRect(x: o.x + r.minX * z, y: o.y + r.minY * z, width: r.width * z, height: r.height * z)
          .insetBy(dx: hair / 2, dy: hair / 2))
      return path
    }
    let w = Int(r.width)
    let h = Int(r.height)
    func on(_ x: Int, _ y: Int) -> Bool {
      x >= 0 && y >= 0 && x < w && y < h && mask[y * w + x] != 0
    }
    func at(_ x: Int, _ y: Int) -> CGPoint {
      CGPoint(x: o.x + (r.minX + CGFloat(x)) * z, y: o.y + (r.minY + CGFloat(y)) * z)
    }
    func edge(_ x0: Int, _ y0: Int, _ x1: Int, _ y1: Int) {
      path.move(to: at(x0, y0))
      path.addLine(to: at(x1, y1))
    }
    for y in 0..<h {
      for x in 0..<w where on(x, y) {
        if !on(x, y - 1) { edge(x, y, x + 1, y) }
        if !on(x, y + 1) { edge(x, y + 1, x + 1, y + 1) }
        if !on(x - 1, y) { edge(x, y, x, y + 1) }
        if !on(x + 1, y) { edge(x + 1, y, x + 1, y + 1) }
      }
    }
    return path
  }
}
