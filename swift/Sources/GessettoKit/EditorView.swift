import CGessetto
import SwiftUI
import UniformTypeIdentifiers

/// A document's window: palette on the left, the canvas, frames below.
public struct EditorView: View {
  @Bindable var editor: Editor
  @Environment(\.undoManager) private var undoManager

  public init(document: GessoDocument) {
    editor = document.editor
  }

  public var body: some View {
    VStack(spacing: 0) {
      HStack(spacing: 0) {
        PalettePanel(editor: editor)
          .frame(width: 136)
        Divider()
        CanvasView(editor: editor)
      }
      Divider()
      FramesStrip(editor: editor)
      Divider()
      StatusBar(editor: editor)
    }
    .toolbar { EditorToolbar(editor: editor) }
    .focusedSceneValue(\.editor, editor)
    .onAppear {
      editor.undo = undoManager
      // Asked here, not at creation: a sheet set up before the window is
      // shown never appears.
      if editor.askSize {
        editor.askSize = false
        editor.sizing = true
      }
    }
    .onChange(of: undoManager) { editor.undo = $1 }
    .sheet(isPresented: $editor.sizing) { SizeSheet(editor: editor) }
    .sheet(isPresented: $editor.newTag) { TagSheet(editor: editor) }
    .fileImporter(isPresented: $editor.importing, allowedContentTypes: [.png]) { result in
      importPNG(result)
    }
    .alert(
      "gessetto",
      isPresented: Binding(get: { editor.error != nil }, set: { _ in editor.error = nil })
    ) {
      Button("OK") {}
    } message: {
      Text(editor.error ?? "")
    }
  }

  /// A PNG replaces the image, its colors the palette; undo brings it back.
  private func importPNG(_ result: Result<URL, Error>) {
    do {
      let url = try result.get()
      let scoped = url.startAccessingSecurityScopedResource()
      defer {
        if scoped {
          url.stopAccessingSecurityScopedResource()
        }
      }
      editor.importPNG(try Data(contentsOf: url))
    } catch {
      editor.error = "Cannot import the PNG. \(error.localizedDescription)"
    }
  }
}

struct EditorToolbar: ToolbarContent {
  @Bindable var editor: Editor
  @Environment(\.undoManager) private var undoManager

  var body: some ToolbarContent {
    ToolbarItem(placement: .principal) {
      Picker("Tool", selection: $editor.tool) {
        ForEach(Tool.allCases) { t in
          Label(t.name, systemImage: t.symbol)
            .help("\(t.name) (\(t.key.uppercased()))")
            .tag(t)
        }
      }
      .pickerStyle(.segmented)
    }
    ToolbarItemGroup(placement: .primaryAction) {
      switch editor.tool {
      case .pencil:
        toggle("Pixel Perfect", "square.grid.3x3.middle.filled", $editor.pixelPerfect)
      case .rect, .ellipse:
        toggle("Filled", "square.fill", $editor.filled)
      case .select:
        toggle("Transparent", "checkerboard.rectangle", $editor.transparentSelection)
        Button("Flip Horizontal", systemImage: "arrow.left.and.right") {
          editor.flip(vertical: false)
        }
        Button("Flip Vertical", systemImage: "arrow.up.and.down") { editor.flip(vertical: true) }
        Button("Rotate", systemImage: "rotate.right") { editor.rotate() }
      default:
        EmptyView()
      }
      if editor.tool != .select && editor.tool != .picker {
        toggle(
          "Mirror X", "arrow.left.and.right.righttriangle.left.righttriangle.right", $editor.mirrorX
        )
        toggle("Mirror Y", "arrow.up.and.down.righttriangle.up.righttriangle.down", $editor.mirrorY)
      }
      #if os(iOS)
        Button("Undo", systemImage: "arrow.uturn.backward") { undoManager?.undo() }
        Button("Redo", systemImage: "arrow.uturn.forward") { undoManager?.redo() }
      #endif
    }
  }

  private func toggle(_ title: String, _ symbol: String, _ on: Binding<Bool>) -> some View {
    Toggle(isOn: on) { Label(title, systemImage: symbol) }
      .toggleStyle(.button)
      .help(title)
  }
}

struct StatusBar: View {
  let editor: Editor

  var body: some View {
    let _ = editor.revision
    let sketch = editor.sketch
    HStack(spacing: 16) {
      Text("\(sketch.width) × \(sketch.height)")
      Text("Frame \(sketch.frame + 1) of \(sketch.frames)")
      Spacer()
      if let c = editor.cursor {
        Text("\(c.x), \(c.y)")
      }
      Text(verbatim: "\(editor.zoom * 100)%")
    }
    .font(.caption)
    .monospacedDigit()
    .foregroundStyle(.secondary)
    .padding(.horizontal, 10)
    .frame(height: 22)
  }
}

struct SizeSheet: View {
  let editor: Editor
  @State private var width = 32
  @State private var height = 32
  @Environment(\.dismiss) private var dismiss

  var body: some View {
    NavigationStack {
      Form {
        Section("Canvas Size") {
          LabeledContent("Width") { number($width) }
          LabeledContent("Height") { number($height) }
          Text("1 to 256 pixels on each side; the image stays at the top left.")
            .font(.caption)
            .foregroundStyle(.secondary)
        }
      }
      .formStyle(.grouped)
      .frame(minWidth: 280)
      .toolbar {
        ToolbarItem(placement: .cancellationAction) {
          Button("Cancel") { dismiss() }
        }
        ToolbarItem(placement: .confirmationAction) {
          Button("OK") {
            editor.resize(width: width, height: height)
            dismiss()
          }
        }
      }
    }
    .onAppear {
      width = editor.sketch.width
      height = editor.sketch.height
    }
  }
}

struct TagSheet: View {
  let editor: Editor
  @State private var name = ""
  @State private var first = 1
  @State private var last = 1
  @Environment(\.dismiss) private var dismiss

  var body: some View {
    NavigationStack {
      Form {
        Section("New Tag") {
          LabeledContent("Name") {
            TextField("Name", text: $name).labelsHidden().multilineTextAlignment(.trailing)
          }
          LabeledContent("First frame") { number($first) }
          LabeledContent("Last frame") { number($last) }
        }
      }
      .formStyle(.grouped)
      .frame(minWidth: 280)
      .toolbar {
        ToolbarItem(placement: .cancellationAction) {
          Button("Cancel") { dismiss() }
        }
        ToolbarItem(placement: .confirmationAction) {
          Button("Add") {
            editor.setTag(name, first: first - 1, last: last - 1)
            dismiss()
          }
          .disabled(name.isEmpty)
        }
      }
    }
    .onAppear {
      first = editor.sketch.frame + 1
      last = editor.sketch.frames
    }
  }
}

/// A whole number typed in a form row, aligned like the platform's.
func number(_ value: Binding<Int>) -> some View {
  TextField("", value: value, format: .number)
    .labelsHidden()
    .multilineTextAlignment(.trailing)
    .frame(maxWidth: 96)
    #if os(iOS)
      .keyboardType(.numberPad)
    #endif
}

struct EditorKey: FocusedValueKey {
  typealias Value = Editor
}

extension FocusedValues {
  var editor: Editor? {
    get { self[EditorKey.self] }
    set { self[EditorKey.self] = newValue }
  }
}
