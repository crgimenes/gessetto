#if os(macOS)
  import AppKit
  import ImageIO
  import SwiftUI

  struct CanvasView: NSViewRepresentable {
    let editor: Editor

    func makeNSView(context: Context) -> CanvasNSView {
      CanvasNSView(editor: editor)
    }

    func updateNSView(_ view: CanvasNSView, context: Context) {
      view.needsDisplay = true
    }
  }

  final class CanvasNSView: NSView, NSMenuItemValidation {
    private let core: CanvasCore
    private var editor: Editor { core.editor }
    private var antsTimer: Timer?

    init(editor: Editor) {
      core = CanvasCore(editor: editor)
      super.init(frame: .zero)
      editor.redraw = { [weak self] in self?.refresh() }
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { nil }

    override var isFlipped: Bool { true }
    override var acceptsFirstResponder: Bool { true }

    override func viewDidMoveToWindow() {
      super.viewDidMoveToWindow()
      window?.acceptsMouseMovedEvents = true
      window?.makeFirstResponder(self)
    }

    override func setFrameSize(_ size: NSSize) {
      super.setFrameSize(size)
      editor.resized(size)
    }

    override func updateTrackingAreas() {
      super.updateTrackingAreas()
      trackingAreas.forEach(removeTrackingArea)
      addTrackingArea(
        NSTrackingArea(
          rect: .zero,
          options: [.mouseMoved, .mouseEnteredAndExited, .activeInKeyWindow, .inVisibleRect],
          owner: self))
    }

    private func refresh() {
      needsDisplay = true
      window?.invalidateCursorRects(for: self)
      let selected = editor.sketch.selection != .none
      if selected && antsTimer == nil {
        antsTimer = Timer.scheduledTimer(withTimeInterval: 0.15, repeats: true) { [weak self] _ in
          MainActor.assumeIsolated {
            guard let self else { return }
            self.core.ants += 1
            self.needsDisplay = true
          }
        }
      } else if !selected {
        antsTimer?.invalidate()
        antsTimer = nil
      }
    }

    override func draw(_ dirtyRect: NSRect) {
      guard let ctx = NSGraphicsContext.current?.cgContext else { return }
      core.draw(
        in: ctx, size: bounds.size, scale: window?.backingScaleFactor ?? 2,
        background: NSColor.underPageBackgroundColor.cgColor)
    }

    override func resetCursorRects() {
      addCursorRect(bounds, cursor: core.spaceHeld || core.panning ? .openHand : .crosshair)
    }

    // MARK: pointer

    private func point(_ e: NSEvent) -> CGPoint {
      convert(e.locationInWindow, from: nil)
    }

    override func mouseDown(with e: NSEvent) {
      window?.makeFirstResponder(self)
      core.press(point(e), secondary: false, pan: false)
    }

    override func mouseDragged(with e: NSEvent) {
      core.drag(
        point(e), shift: e.modifierFlags.contains(.shift), alt: e.modifierFlags.contains(.option))
    }

    override func mouseUp(with e: NSEvent) {
      core.release(
        point(e), shift: e.modifierFlags.contains(.shift), alt: e.modifierFlags.contains(.option))
    }

    override func rightMouseDown(with e: NSEvent) {
      window?.makeFirstResponder(self)
      core.press(point(e), secondary: true, pan: false)
    }

    override func rightMouseDragged(with e: NSEvent) { mouseDragged(with: e) }
    override func rightMouseUp(with e: NSEvent) { mouseUp(with: e) }

    override func otherMouseDown(with e: NSEvent) {
      core.press(point(e), secondary: false, pan: true)
    }

    override func otherMouseDragged(with e: NSEvent) { mouseDragged(with: e) }
    override func otherMouseUp(with e: NSEvent) { mouseUp(with: e) }

    override func mouseMoved(with e: NSEvent) { core.hover(point(e)) }
    override func mouseExited(with e: NSEvent) { core.hover(nil) }

    override func scrollWheel(with e: NSEvent) {
      if e.modifierFlags.contains(.command) {
        core.magnify(e.scrollingDeltaY / (e.hasPreciseScrollingDeltas ? 100 : 4), at: point(e))
        return
      }
      let k: CGFloat = e.hasPreciseScrollingDeltas ? 1 : 10
      core.scroll(dx: e.scrollingDeltaX * k, dy: e.scrollingDeltaY * k)
    }

    override func magnify(with e: NSEvent) {
      core.magnify(e.magnification, at: point(e))
      if e.phase == .ended || e.phase == .cancelled {
        core.endMagnify()
      }
    }

    // MARK: keys

    private func canvasKey(_ e: NSEvent) -> CanvasKey? {
      switch e.keyCode {
      case 123: return .left
      case 124: return .right
      case 125: return .down
      case 126: return .up
      case 51, 117: return .delete
      case 53: return .escape
      case 36, 76: return .enter
      case 49: return .space
      default:
        guard let c = e.charactersIgnoringModifiers?.lowercased().first else { return nil }
        return .char(c)
      }
    }

    override func keyDown(with e: NSEvent) {
      let blocked = e.modifierFlags.intersection([.command, .control, .option])
      guard blocked.isEmpty, let k = canvasKey(e),
        core.key(k, down: true, shift: e.modifierFlags.contains(.shift))
      else {
        super.keyDown(with: e)
        return
      }
      if case .space = k {
        window?.invalidateCursorRects(for: self)
      }
    }

    override func keyUp(with e: NSEvent) {
      guard let k = canvasKey(e), core.key(k, down: false, shift: false) else {
        super.keyUp(with: e)
        return
      }
      window?.invalidateCursorRects(for: self)
    }

    // MARK: the Edit menu, through the responder chain

    @objc func copy(_ sender: Any?) {
      guard let image = editor.copy() else { return }
      Clipboard.put(image)
    }

    @objc func cut(_ sender: Any?) {
      guard let image = editor.cut() else { return }
      Clipboard.put(image)
    }

    @objc func paste(_ sender: Any?) {
      editor.paste(Clipboard.image())
    }

    @objc func delete(_ sender: Any?) {
      editor.deleteSelection()
    }

    override func selectAll(_ sender: Any?) {
      editor.selectAll()
    }

    func validateMenuItem(_ item: NSMenuItem) -> Bool {
      switch item.action {
      case #selector(copy(_:)), #selector(cut(_:)), #selector(delete(_:)):
        return editor.hasSelection
      default:
        return true
      }
    }
  }

  enum Clipboard {
    static func put(_ image: CGImage) {
      let pb = NSPasteboard.general
      pb.clearContents()
      let rep = NSBitmapImageRep(cgImage: image)
      if let png = rep.representation(using: .png, properties: [:]) {
        pb.setData(png, forType: .png)
      }
      if let tiff = rep.tiffRepresentation {
        pb.setData(tiff, forType: .tiff)
      }
    }

    /// Read as data: an NSImage would size it in points, not pixels.
    static func image() -> CGImage? {
      let pb = NSPasteboard.general
      for type in [NSPasteboard.PasteboardType.png, .tiff] {
        guard let data = pb.data(forType: type),
          let source = CGImageSourceCreateWithData(data as CFData, nil),
          let image = CGImageSourceCreateImageAtIndex(source, 0, nil)
        else { continue }
        return image
      }
      return nil
    }
  }
#endif
