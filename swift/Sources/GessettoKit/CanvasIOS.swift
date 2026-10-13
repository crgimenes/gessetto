#if os(iOS)
  import ImageIO
  import SwiftUI
  import UIKit
  import UniformTypeIdentifiers

  struct CanvasView: UIViewRepresentable {
    let editor: Editor

    func makeUIView(context: Context) -> CanvasUIView {
      CanvasUIView(editor: editor)
    }

    func updateUIView(_ view: CanvasUIView, context: Context) {
      view.setNeedsDisplay()
    }
  }

  /// One finger or the Pencil draws; two fingers pan and zoom, taking back
  /// whatever the first finger had begun.
  final class CanvasUIView: UIView, UIPencilInteractionDelegate, UIGestureRecognizerDelegate {
    private let core: CanvasCore
    private var editor: Editor { core.editor }
    private var touch: UITouch?
    private var antsTimer: Timer?
    private var pinchAnchor: CGFloat = 1
    private var panAnchor = CGPoint.zero

    init(editor: Editor) {
      core = CanvasCore(editor: editor)
      super.init(frame: .zero)
      contentMode = .redraw
      clipsToBounds = true
      isMultipleTouchEnabled = true
      editor.redraw = { [weak self] in self?.refresh() }

      let pinch = UIPinchGestureRecognizer(target: self, action: #selector(pinched(_:)))
      pinch.allowedTouchTypes = CanvasUIView.fingers
      pinch.delegate = self
      addGestureRecognizer(pinch)
      let pan = UIPanGestureRecognizer(target: self, action: #selector(panned(_:)))
      pan.minimumNumberOfTouches = 2
      pan.allowedTouchTypes = CanvasUIView.fingers
      pan.allowedScrollTypesMask = .continuous
      pan.delegate = self
      addGestureRecognizer(pan)
      let pencil = UIPencilInteraction()
      pencil.delegate = self
      addInteraction(pencil)
    }

    /// Fingers and a trackpad: never the Pencil, which only draws.
    private static let fingers = [UITouch.TouchType.direct, .indirectPointer].map {
      NSNumber(value: $0.rawValue)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { nil }

    override var canBecomeFirstResponder: Bool { true }

    override func didMoveToWindow() {
      super.didMoveToWindow()
      becomeFirstResponder()
    }

    override func layoutSubviews() {
      super.layoutSubviews()
      editor.resized(bounds.size)
    }

    private func refresh() {
      setNeedsDisplay()
      let selected = editor.sketch.selection != .none
      if selected && antsTimer == nil {
        antsTimer = Timer.scheduledTimer(withTimeInterval: 0.15, repeats: true) { [weak self] _ in
          MainActor.assumeIsolated {
            guard let self else { return }
            self.core.ants += 1
            self.setNeedsDisplay()
          }
        }
      } else if !selected {
        antsTimer?.invalidate()
        antsTimer = nil
      }
    }

    override func draw(_ rect: CGRect) {
      guard let ctx = UIGraphicsGetCurrentContext() else { return }
      core.draw(
        in: ctx, size: bounds.size, scale: window?.screen.scale ?? 2,
        background: UIColor.secondarySystemBackground.cgColor)
    }

    // MARK: touches

    private func shift(_ e: UIEvent?) -> Bool { e?.modifierFlags.contains(.shift) ?? false }
    private func alt(_ e: UIEvent?) -> Bool { e?.modifierFlags.contains(.alternate) ?? false }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
      becomeFirstResponder()
      guard touch == nil, let t = touches.first else { return }
      touch = t
      let secondary = event?.buttonMask.contains(.secondary) ?? false
      core.press(t.location(in: self), secondary: secondary, pan: false, alt: alt(event))
    }

    override func touchesMoved(_ touches: Set<UITouch>, with event: UIEvent?) {
      guard let t = touch, touches.contains(t) else { return }
      for c in event?.coalescedTouches(for: t) ?? [t] {
        core.drag(c.location(in: self), shift: shift(event), alt: alt(event))
      }
    }

    override func touchesEnded(_ touches: Set<UITouch>, with event: UIEvent?) {
      guard let t = touch, touches.contains(t) else { return }
      touch = nil
      core.release(t.location(in: self), shift: shift(event), alt: alt(event))
    }

    override func touchesCancelled(_ touches: Set<UITouch>, with event: UIEvent?) {
      guard let t = touch, touches.contains(t) else { return }
      touch = nil
      core.cancel()
    }

    private func takeOver() {
      if touch != nil {
        touch = nil
        core.cancel()
      }
    }

    @objc private func pinched(_ r: UIPinchGestureRecognizer) {
      switch r.state {
      case .began:
        takeOver()
        pinchAnchor = 1
      case .changed:
        core.magnify(by: r.scale / pinchAnchor, at: r.location(in: self))
        pinchAnchor = r.scale
      default:
        core.endMagnify()
      }
    }

    @objc private func panned(_ r: UIPanGestureRecognizer) {
      switch r.state {
      case .began:
        takeOver()
        panAnchor = .zero
      case .changed:
        let t = r.translation(in: self)
        core.scroll(dx: t.x - panAnchor.x, dy: t.y - panAnchor.y)
        panAnchor = t
      default:
        break
      }
    }

    func gestureRecognizer(
      _ g: UIGestureRecognizer, shouldRecognizeSimultaneouslyWith other: UIGestureRecognizer
    ) -> Bool {
      true
    }

    /// The Pencil's double tap: the eraser and back.
    func pencilInteractionDidTap(_ interaction: UIPencilInteraction) {
      editor.tool = editor.tool == .eraser ? .pencil : .eraser
    }

    // MARK: keys

    private func canvasKey(_ key: UIKey) -> CanvasKey? {
      switch key.keyCode {
      case .keyboardLeftArrow: return .left
      case .keyboardRightArrow: return .right
      case .keyboardUpArrow: return .up
      case .keyboardDownArrow: return .down
      case .keyboardDeleteOrBackspace, .keyboardDeleteForward: return .delete
      case .keyboardEscape: return .escape
      case .keyboardReturnOrEnter, .keypadEnter: return .enter
      case .keyboardSpacebar: return .space
      default:
        guard let c = key.charactersIgnoringModifiers.lowercased().first else { return nil }
        return .char(c)
      }
    }

    private func handle(_ presses: Set<UIPress>, down: Bool) -> Bool {
      guard let key = presses.first?.key,
        key.modifierFlags.intersection([.command, .control, .alternate]).isEmpty,
        let k = canvasKey(key)
      else { return false }
      return core.key(k, down: down, shift: key.modifierFlags.contains(.shift))
    }

    override func pressesBegan(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
      if !handle(presses, down: true) {
        super.pressesBegan(presses, with: event)
      }
    }

    override func pressesEnded(_ presses: Set<UIPress>, with event: UIPressesEvent?) {
      if !handle(presses, down: false) {
        super.pressesEnded(presses, with: event)
      }
    }

    // MARK: the Edit menu, through the responder chain

    override func copy(_ sender: Any?) {
      guard let image = editor.copy() else { return }
      Clipboard.put(image)
    }

    override func cut(_ sender: Any?) {
      guard let image = editor.cut() else { return }
      Clipboard.put(image)
    }

    override func paste(_ sender: Any?) {
      editor.paste(Clipboard.image())
    }

    override func delete(_ sender: Any?) {
      editor.deleteSelection()
    }

    override func selectAll(_ sender: Any?) {
      editor.selectAll()
    }

    override func canPerformAction(_ action: Selector, withSender sender: Any?) -> Bool {
      switch action {
      case #selector(copy(_:)), #selector(cut(_:)), #selector(delete(_:)):
        return editor.hasSelection
      case #selector(paste(_:)), #selector(selectAll(_:)):
        return true
      default:
        return super.canPerformAction(action, withSender: sender)
      }
    }
  }

  enum Clipboard {
    static func put(_ image: CGImage) {
      guard let png = UIImage(cgImage: image).pngData() else { return }
      UIPasteboard.general.setData(png, forPasteboardType: UTType.png.identifier)
    }

    static func image() -> CGImage? {
      let pb = UIPasteboard.general
      for type in [UTType.png, .tiff] {
        guard let data = pb.data(forPasteboardType: type.identifier),
          let source = CGImageSourceCreateWithData(data as CFData, nil),
          let image = CGImageSourceCreateImageAtIndex(source, 0, nil)
        else { continue }
        return image
      }
      return pb.image?.cgImage
    }
  }
#endif
