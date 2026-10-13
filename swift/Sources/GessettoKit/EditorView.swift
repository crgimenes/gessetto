import CGessetto
import SwiftUI
import UniformTypeIdentifiers

/// A document's window: palette on the left, the canvas, frames below.
public struct EditorView: View {
  @Bindable var editor: Editor
  @Environment(\.undoManager) private var undoManager

  /// The document's name without extension, for exported files.
  let name: String

  public init(document: GessoDocument, fileURL: URL?) {
    editor = document.editor
    name = fileURL?.deletingPathExtension().lastPathComponent ?? "untitled"
  }

  public var body: some View {
    VStack(spacing: 0) {
      #if os(iOS)
        // Out of the navigation bar, where the title's rename menu takes the
        // taps.
        ToolPicker(editor: editor)
          .padding(.horizontal, 8)
          .padding(.vertical, 6)
        Divider()
      #endif
      HStack(spacing: 0) {
        PalettePanel(editor: editor)
          .frame(width: PalettePanel.side * 6 + 10 + 16)
        Divider()
        CanvasView(editor: editor)
          .clipped()
          .overlay(alignment: .topLeading) {
            if let at = editor.quickPalette {
              QuickPalette(editor: editor)
                .offset(x: max(8, at.x - 120), y: max(8, at.y - 150))
            }
          }
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
    .sheet(
      isPresented: Binding(
        get: { editor.tagSheet != nil }, set: { if !$0 { editor.tagSheet = nil } })
    ) {
      if let t = editor.tagSheet {
        TagSheet(editor: editor, tag: t)
      }
    }
    .sheet(isPresented: $editor.paletteSheet) {
      PaletteSheet(editor: editor, loaded: editor.loadedPalette)
    }
    .sheet(isPresented: $editor.exporting) { ExportSheet(editor: editor, name: name) }
    .fileExporter(
      isPresented: Binding(
        get: { editor.exportFile != nil }, set: { if !$0 { editor.exportFile = nil } }),
      document: editor.exportFile, contentType: editor.exportFile?.type ?? .png,
      defaultFilename: name
    ) { result in
      if case .failure(let e) = result {
        editor.error = "Cannot export. \(e.localizedDescription)"
      }
    }
    .fileImporter(
      isPresented: Binding(
        get: { editor.importing != nil }, set: { if !$0 { editor.importing = nil } }),
      allowedContentTypes: editor.importing == .sheet
        ? [.image] : editor.importing == .palette ? [.data] : [.png]
    ) { result in
      importFile(result, as: editor.importing ?? .png)
    }
    .sheet(
      isPresented: Binding(get: { editor.sheet != nil }, set: { if !$0 { editor.sheet = nil } })
    ) {
      if let s = editor.sheet {
        SheetImport(editor: editor, sheet: s)
      }
    }
    .alert(
      "Text",
      isPresented: Binding(
        get: { editor.textAt != nil }, set: { if !$0 { editor.textAt = nil } })
    ) {
      TextField("Text", text: $editor.textEntry)
      Button("Place") { editor.placeText() }
      Button("Cancel", role: .cancel) {}
    } message: {
      Text("In the built-in 5×9 font, in the foreground color.")
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

  /// A PNG replaces the image, its colors the palette; a sprite sheet asks
  /// its frame size first. Undo brings the old image back.
  private func importFile(_ result: Result<URL, Error>, as kind: Editor.Import) {
    do {
      let url = try result.get()
      let scoped = url.startAccessingSecurityScopedResource()
      defer {
        if scoped {
          url.stopAccessingSecurityScopedResource()
        }
      }
      let data = try Data(contentsOf: url)
      switch kind {
      case .png: editor.importPNG(data)
      case .sheet: editor.readSheet(data)
      case .palette: editor.readPalette(data, name: url.deletingPathExtension().lastPathComponent)
      }
    } catch {
      editor.error = "Cannot import. \(error.localizedDescription)"
    }
  }
}

/// The frame size for a sprite sheet, with how many frames it makes.
struct SheetImport: View {
  let editor: Editor
  let sheet: Editor.Sheet
  @State private var width = 16
  @State private var height = 16
  @Environment(\.dismiss) private var dismiss

  var body: some View {
    let frames = width > 0 && height > 0 ? (sheet.width / width) * (sheet.height / height) : 0
    NavigationStack {
      Form {
        Section("Import Sprite Sheet") {
          LabeledContent("Sheet") { Text(verbatim: "\(sheet.width) × \(sheet.height)") }
          LabeledContent("Frame width") { number($width) }
          LabeledContent("Frame height") { number($height) }
          LabeledContent("Frames") { Text(verbatim: "\(frames)") }
          Text(
            "Frames are read left to right, top to bottom; the image is replaced (undo brings it back)."
          )
          .font(.caption)
          .foregroundStyle(.secondary)
        }
      }
      .formStyle(.grouped)
      .frame(minWidth: 320)
      .toolbar {
        ToolbarItem(placement: .cancellationAction) {
          Button("Cancel") { dismiss() }
        }
        ToolbarItem(placement: .confirmationAction) {
          Button("Import") {
            editor.importSheet(sheet, frameWidth: width, frameHeight: height)
            dismiss()
          }
          .disabled(frames < 1 || frames > Int(GST_FRAMES_MAX))
        }
      }
    }
    .onAppear {
      let side = min(sheet.width, sheet.height, Int(GST_SIDE_MAX))
      width = side
      height = side
    }
  }
}

struct EditorToolbar: ToolbarContent {
  @Bindable var editor: Editor
  @Environment(\.undoManager) private var undoManager

  var body: some ToolbarContent {
    #if os(macOS)
      ToolbarItem(placement: .principal) { ToolPicker(editor: editor) }
    #endif
    ToolbarItemGroup(placement: .primaryAction) {
      switch editor.tool {
      case .pencil:
        toggle("Pixel Perfect", "square.grid.3x3.middle.filled", $editor.pixelPerfect)
      case .rect:
        toggle("Filled", "square.fill", $editor.filled)
        Menu {
          Picker("Corner Radius", selection: $editor.cornerRadius) {
            ForEach(0...16, id: \.self) { Text(verbatim: $0 == 0 ? "Square" : "\($0) px").tag($0) }
          }
        } label: {
          Label("Corners \(editor.cornerRadius)", systemImage: "app")
        }
        .help("Corner radius")
      case .ellipse, .polygon:
        toggle("Filled", "square.fill", $editor.filled)
      case .spray:
        Menu {
          Picker("Spray Radius", selection: $editor.sprayRadius) {
            ForEach([1, 2, 3, 4, 6, 8, 12, 16, 24, 32], id: \.self) {
              Text(verbatim: "\($0) px").tag($0)
            }
          }
        } label: {
          Label("Spray \(editor.sprayRadius) px", systemImage: "circle.dotted")
        }
        .help("Spray radius")
      case .select, .lasso, .text:
        toggle("Transparent", "checkerboard.rectangle", $editor.transparentSelection)
        Button("Flip Horizontal", systemImage: "arrow.left.and.right") {
          editor.flip(vertical: false)
        }
        Button("Flip Vertical", systemImage: "arrow.up.and.down") { editor.flip(vertical: true) }
        Button("Rotate", systemImage: "rotate.right") { editor.rotate() }
      default:
        EmptyView()
      }
      if editor.usesBrush {
        Menu {
          Picker("Size", selection: $editor.brushSize) {
            ForEach(1...16, id: \.self) { Text(verbatim: "\($0) px").tag($0) }
          }
          Picker("Shape", selection: $editor.brushRound) {
            Label("Square", systemImage: "square.fill").tag(false)
            Label("Round", systemImage: "circle.fill").tag(true)
          }
        } label: {
          Label(
            "Pen \(editor.brushSize) px",
            systemImage: editor.brushRound ? "circle.fill" : "square.fill")
        }
        .help("Pen size and shape (- and = change the size)")
      }
      if ![.select, .lasso, .text, .picker].contains(editor.tool) {
        toggle(
          "Mirror X", "arrow.left.and.right.righttriangle.left.righttriangle.right", $editor.mirrorX
        )
        toggle("Mirror Y", "arrow.up.and.down.righttriangle.up.righttriangle.down", $editor.mirrorY)
      }
      toggle("Grid", "grid", $editor.grid)
      toggle("Onion Skin", "square.on.square.dashed", $editor.onion)
      #if os(iOS)
        toggle("Draw with Finger", "hand.draw", $editor.fingerDraws)
        Button("Undo", systemImage: "arrow.uturn.backward") { undoManager?.undo() }
        Button("Redo", systemImage: "arrow.uturn.forward") { undoManager?.redo() }
        // What the Mac keeps in its menu bar, which the iPad shows only on
        // request.
        Menu {
          Button("Export…", systemImage: "square.and.arrow.up") { editor.exporting = true }
          Button("Import PNG…", systemImage: "photo") { editor.importing = .png }
          Button("Import Sprite Sheet…", systemImage: "square.grid.3x3") {
            editor.importing = .sheet
          }
          Divider()
          Button("Canvas Size…", systemImage: "crop") { editor.sizing = true }
          Button("Load Palette…", systemImage: "swatchpalette") {
            editor.loadedPalette = nil
            editor.paletteSheet = true
          }
          Button("New Tag…", systemImage: "tag") { editor.newTag() }
        } label: {
          Label("More", systemImage: "ellipsis.circle")
        }
      #endif
    }
  }

  private func toggle(_ title: String, _ symbol: String, _ on: Binding<Bool>) -> some View {
    Toggle(isOn: on) { Label(title, systemImage: symbol) }
      .toggleStyle(.button)
      .help(title)
  }
}

struct ToolPicker: View {
  @Bindable var editor: Editor

  var body: some View {
    Picker("Tool", selection: $editor.tool) {
      ForEach(Tool.allCases) { t in
        // A segment on the iPad shows a Label's title only; the icon fits.
        Image(systemName: t.symbol)
          .accessibilityLabel(t.name)
          .help("\(t.name) (\(t.key.uppercased()))")
          .tag(t)
      }
    }
    .pickerStyle(.segmented)
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
        let i = Int(gst_index_at(editor.sketch.g, Int32(c.x), Int32(c.y)))
        Text(
          verbatim: i == 0
            ? "\(c.x), \(c.y)  0 clear"
            : "\(c.x), \(c.y)  \(i) " + String(format: "#%06x", editor.sketch.color(i) & 0xff_ffff))
      }
      Text(verbatim: "\(Int((editor.zoom * 100).rounded()))%")
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

/// A new tag, or one edited: renaming replaces the old one.
struct TagSheet: View {
  let editor: Editor
  let tag: Sketch.Tag
  @State private var name = ""
  @State private var first = 1
  @State private var last = 1
  @State private var direction = Sketch.Direction.forward
  @Environment(\.dismiss) private var dismiss

  var body: some View {
    let isNew = tag.name.isEmpty
    NavigationStack {
      Form {
        Section(isNew ? "New Tag" : "Edit Tag") {
          LabeledContent("Name") {
            TextField("Name", text: $name).labelsHidden().multilineTextAlignment(.trailing)
          }
          LabeledContent("First frame") { number($first) }
          LabeledContent("Last frame") { number($last) }
          Picker("Direction", selection: $direction) {
            ForEach(Sketch.Direction.allCases) { Text($0.title).tag($0) }
          }
        }
      }
      .formStyle(.grouped)
      .frame(minWidth: 300)
      .toolbar {
        ToolbarItem(placement: .cancellationAction) {
          Button("Cancel") { dismiss() }
        }
        ToolbarItem(placement: .confirmationAction) {
          Button(isNew ? "Add" : "OK") {
            if editor.setTag(name, first: first - 1, last: last - 1, direction: direction),
              !isNew, name != tag.name
            {
              editor.deleteTag(tag.name)
            }
            dismiss()
          }
          .disabled(name.isEmpty)
        }
      }
    }
    .onAppear {
      name = tag.name
      first = tag.first + 1
      last = tag.last + 1
      direction = tag.direction
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
