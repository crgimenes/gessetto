import SwiftUI

/// The menus beyond what DocumentGroup gives. Cut, Copy, Paste, Delete and
/// Select All are the system's, answered by the canvas through the responder
/// chain, so a text field keeps its own. Single keys (tools, X, comma,
/// period, P) belong to the canvas for the same reason: here they would
/// fire while typing in a field. No shortcut uses L.
struct GessettoCommands: Commands {
  @FocusedValue(\.editor) private var editor

  var body: some Commands {
    CommandGroup(after: .importExport) {
      Button("Import PNG…") { editor?.importing = true }
        .keyboardShortcut("i", modifiers: [.command, .shift])
        .disabled(editor == nil)
    }
    CommandGroup(after: .pasteboard) {
      Button("Deselect") { editor?.deselect() }
        .keyboardShortcut("d")
        .disabled(editor?.hasSelection != true)
    }
    CommandMenu("Image") {
      Group {
        Button("Canvas Size…") { editor?.sizing = true }
        Divider()
        Button("Flip Horizontal") { editor?.flip(vertical: false) }
        Button("Flip Vertical") { editor?.flip(vertical: true) }
        Button("Rotate 90°") { editor?.rotate() }
          .keyboardShortcut("r")
        Divider()
        Toggle(
          "Transparent Selection",
          isOn: Binding(
            get: { editor?.transparentSelection ?? false },
            set: { editor?.transparentSelection = $0 }))
      }
      .disabled(editor == nil)
    }
    CommandMenu("Frame") {
      Group {
        Button("New Frame") { editor?.addFrame() }
          .keyboardShortcut("n", modifiers: .option)
        Button("Duplicate Frame") { editor?.duplicateFrame() }
          .keyboardShortcut("d", modifiers: .option)
        Button("Delete Frame") { editor?.deleteFrame() }
        Divider()
        Button("Previous Frame") { editor?.step(frames: -1) }
        Button("Next Frame") { editor?.step(frames: 1) }
        Divider()
        Button("New Tag…") { editor?.newTag = true }
        Divider()
        Button(editor?.playing == true ? "Stop" : "Play") { editor?.playing.toggle() }
      }
      .disabled(editor == nil)
    }
    CommandGroup(after: .toolbar) {
      Button("Zoom In") { editor?.zoom(by: 1) }
        .keyboardShortcut("=")
      Button("Zoom Out") { editor?.zoom(by: -1) }
        .keyboardShortcut("-")
      Button("Zoom to Fit") { editor?.fit() }
        .keyboardShortcut("0")
      Divider()
      Toggle(
        "Grid",
        isOn: Binding(get: { editor?.grid ?? false }, set: { _ in editor?.toggleGrid() })
      )
      .keyboardShortcut("'")
      Toggle(
        "Onion Skin",
        isOn: Binding(get: { editor?.onion ?? false }, set: { editor?.onion = $0 }))
      Divider()
    }
  }
}

public struct GessettoScene: Scene {
  public init() {}

  public var body: some Scene {
    DocumentGroup(newDocument: { GessoDocument() }) { file in
      EditorView(document: file.document)
    }
    .commands { GessettoCommands() }
    #if os(macOS)
      .defaultSize(width: 1000, height: 720)
    #endif
  }
}
