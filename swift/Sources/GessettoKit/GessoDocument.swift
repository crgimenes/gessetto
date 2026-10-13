import SwiftUI
import UniformTypeIdentifiers

extension UTType {
  public static let gesso = UTType(exportedAs: "br.eti.crg.gesso")
}

/// A .gesso file. SwiftUI reads it on any thread; everything after that,
/// snapshot included, happens on the main thread.
public final class GessoDocument: ReferenceFileDocument, @unchecked Sendable {
  public static let readableContentTypes: [UTType] = [.gesso]

  let sketch: Sketch
  private let isNew: Bool
  @MainActor public private(set) lazy var editor = Editor(sketch: sketch, isNew: isNew)

  public init() {
    sketch = Sketch()
    isNew = true
  }

  public init(sketch: Sketch) {
    self.sketch = sketch
    isNew = false
  }

  public init(configuration: ReadConfiguration) throws {
    guard let data = configuration.file.regularFileContents else {
      throw CocoaError(.fileReadCorruptFile)
    }
    sketch = try Sketch(gesso: data)
    isNew = data == GessoDocument.blank
  }

  /// What a new document holds. The iPad's browser writes a new document
  /// and opens it again from the file, so a file still exactly this is
  /// treated as new: it asks its size.
  private static let blank = try? Sketch().gesso()

  public func snapshot(contentType: UTType) throws -> Data {
    try sketch.gesso()
  }

  public func fileWrapper(snapshot: Data, configuration: WriteConfiguration) throws -> FileWrapper {
    FileWrapper(regularFileWithContents: snapshot)
  }
}
