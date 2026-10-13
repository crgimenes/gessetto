#if os(iOS)
  import SwiftUI
  import UniformTypeIdentifiers

  /// The iPad's window holds one document, a blank one at launch, instead of
  /// the document browser. Files go through the system's panels: Save writes
  /// back where the document came from, Save As and Open pick a place.
  /// While there are unsaved changes the image is also kept in the app's own
  /// folder, so a closed app comes back with it.
  @MainActor
  @Observable
  final class Workspace {
    private(set) var document: GessoDocument
    /// Where Save writes: the file it was opened from or last saved as.
    private(set) var fileURL: URL?
    /// Brought back from the app's folder: unsaved, though the engine sees
    /// it as just read.
    private var restored = false
    var opening = false
    var exporting: GessoFile?
    /// What waits for the answer to "discard the changes?".
    var discarding: (() -> Void)?
    var error: String?

    private static let draft = URL.applicationSupportDirectory.appending(path: "draft.gesso")
    private static let draftFile = "draftFile"

    init() {
      document = GessoDocument(asksSize: false)
      guard let data = try? Data(contentsOf: Workspace.draft),
        let sketch = try? Sketch(gesso: data)
      else { return }
      document = GessoDocument(sketch: sketch)
      restored = true
      if let mark = UserDefaults.standard.data(forKey: Workspace.draftFile) {
        var stale = false
        fileURL = try? URL(resolvingBookmarkData: mark, bookmarkDataIsStale: &stale)
      }
    }

    var name: String { fileURL?.deletingPathExtension().lastPathComponent ?? "Untitled" }

    var dirty: Bool { restored || document.sketch.dirty }

    func new() {
      unlessDirty { [self] in
        replace(GessoDocument(), at: nil)
      }
    }

    func open(_ url: URL) {
      unlessDirty { [self] in
        do {
          let data = try coordinated(url) { try Data(contentsOf: $0) }
          replace(GessoDocument(sketch: try Sketch(gesso: data)), at: url)
        } catch {
          self.error = "Cannot open \(url.lastPathComponent). \(error.localizedDescription)"
        }
      }
    }

    func save() {
      guard let url = fileURL else {
        saveAs()
        return
      }
      do {
        let data = try document.sketch.gesso()
        try coordinated(url, writing: true) { try data.write(to: $0) }
        saved()
      } catch {
        // Some places give no lasting access: ask for one again.
        saveAs()
      }
    }

    func saveAs() {
      do {
        exporting = GessoFile(data: try document.sketch.gesso())
      } catch {
        self.error = "Cannot save. \(error.localizedDescription)"
      }
    }

    func exported(_ result: Result<URL, Error>) {
      switch result {
      case .success(let url):
        fileURL = url
        saved()
      case .failure(let e):
        error = "Cannot save. \(e.localizedDescription)"
      }
    }

    /// When the app leaves the screen: unsaved work goes to the app's folder,
    /// saved work leaves nothing there.
    func keep() {
      guard dirty, let data = try? document.sketch.gesso() else {
        forget()
        return
      }
      try? FileManager.default.createDirectory(
        at: URL.applicationSupportDirectory, withIntermediateDirectories: true)
      try? data.write(to: Workspace.draft, options: .atomic)
      let mark = try? fileURL?.bookmarkData()
      UserDefaults.standard.set(mark, forKey: Workspace.draftFile)
    }

    private func saved() {
      document.sketch.markSaved()
      restored = false
      forget()
    }

    private func forget() {
      try? FileManager.default.removeItem(at: Workspace.draft)
      UserDefaults.standard.removeObject(forKey: Workspace.draftFile)
    }

    private func replace(_ doc: GessoDocument, at url: URL?) {
      document = doc
      fileURL = url
      restored = false
      forget()
    }

    private func unlessDirty(_ action: @escaping () -> Void) {
      if dirty {
        discarding = action
        return
      }
      action()
    }

    private func coordinated<T>(_ url: URL, writing: Bool = false, _ body: (URL) throws -> T)
      throws -> T
    {
      let scoped = url.startAccessingSecurityScopedResource()
      defer {
        if scoped {
          url.stopAccessingSecurityScopedResource()
        }
      }
      var failure: NSError?
      var result: Result<T, Error>?
      let c = NSFileCoordinator()
      if writing {
        c.coordinate(writingItemAt: url, options: .forReplacing, error: &failure) { u in
          result = Result { try body(u) }
        }
      } else {
        c.coordinate(readingItemAt: url, options: [], error: &failure) { u in
          result = Result { try body(u) }
        }
      }
      if let failure {
        throw failure
      }
      guard let result else { throw CocoaError(.fileReadUnknown) }
      return try result.get()
    }
  }

  /// A .gesso image on its way to Save As.
  struct GessoFile: FileDocument {
    static let readableContentTypes: [UTType] = [.gesso]
    let data: Data

    init(data: Data) {
      self.data = data
    }

    init(configuration: ReadConfiguration) throws {
      data = configuration.file.regularFileContents ?? Data()
    }

    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper {
      FileWrapper(regularFileWithContents: data)
    }
  }

  public struct WorkspaceView: View {
    @State private var space = Workspace()
    @Environment(\.undoManager) private var undoManager
    @Environment(\.scenePhase) private var phase

    public init() {}

    public var body: some View {
      NavigationStack {
        EditorView(document: space.document, fileURL: space.fileURL)
          .id(ObjectIdentifier(space.document))
          .navigationTitle(space.name)
          .navigationBarTitleDisplayMode(.inline)
          .toolbar {
            ToolbarItem(placement: .topBarLeading) {
              Menu {
                Button("New", systemImage: "doc.badge.plus") { space.new() }
                  .keyboardShortcut("n")
                Button("Open…", systemImage: "folder") { space.opening = true }
                  .keyboardShortcut("o")
                Divider()
                Button("Save", systemImage: "square.and.arrow.down") { space.save() }
                  .keyboardShortcut("s")
                Button("Save As…", systemImage: "square.and.arrow.down.on.square") {
                  space.saveAs()
                }
                .keyboardShortcut("s", modifiers: [.command, .shift])
              } label: {
                Label("File", systemImage: "doc")
              }
            }
          }
      }
      .onChange(of: ObjectIdentifier(space.document)) { undoManager?.removeAllActions() }
      .onChange(of: phase) { if $1 != .active { space.keep() } }
      .onOpenURL { space.open($0) }
      .fileImporter(isPresented: $space.opening, allowedContentTypes: [.gesso]) { result in
        if case .success(let url) = result {
          space.open(url)
        }
      }
      .fileExporter(
        isPresented: Binding(
          get: { space.exporting != nil }, set: { if !$0 { space.exporting = nil } }),
        document: space.exporting, contentType: .gesso, defaultFilename: space.name
      ) { space.exported($0) }
      .confirmationDialog(
        "Discard the unsaved changes?",
        isPresented: Binding(
          get: { space.discarding != nil }, set: { if !$0 { space.discarding = nil } }),
        titleVisibility: .visible
      ) {
        Button("Discard", role: .destructive) {
          let action = space.discarding
          space.discarding = nil
          action?()
        }
        Button("Cancel", role: .cancel) {}
      }
      .alert(
        "gessetto",
        isPresented: Binding(get: { space.error != nil }, set: { _ in space.error = nil })
      ) {
        Button("OK") {}
      } message: {
        Text(space.error ?? "")
      }
    }
  }
#endif
