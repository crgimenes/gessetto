import UniformTypeIdentifiers
import XCTest

@testable import GessettoKit

@MainActor
final class EditorTests: XCTestCase {
  private func editor() -> (Editor, UndoManager) {
    let e = Editor(sketch: Sketch(), isNew: false)
    let undo = UndoManager()
    undo.groupsByEvent = false
    e.undo = undo
    e.viewSize = CGSize(width: 400, height: 400)
    e.fit()
    return (e, undo)
  }

  /// The pixel at image (x, y), as rendered.
  private func pixel(_ e: Editor, _ x: Int, _ y: Int) -> UInt32 {
    let image = e.sketch.image(frame: e.sketch.frame, zoom: 8, onion: false)!
    let data = image.dataProvider!.data! as Data
    return data.withUnsafeBytes {
      $0.load(fromByteOffset: (y * image.width + x) * 4, as: UInt32.self)
    }
  }

  private func step(_ undo: UndoManager, _ body: () -> Void) {
    undo.beginUndoGrouping()
    body()
    undo.endUndoGrouping()
  }

  private func at(_ e: Editor, _ x: Int, _ y: Int) -> CGPoint {
    let z = CGFloat(e.zoom)
    return CGPoint(x: e.pan.x + (CGFloat(x) + 0.5) * z, y: e.pan.y + (CGFloat(y) + 0.5) * z)
  }

  func testStrokesUndoThroughTheUndoManager() {
    let (e, undo) = editor()
    let canvas = CanvasCore(editor: e)
    let red = e.sketch.color(9) | 0xff00_0000
    e.setFg(9)
    let blank = pixel(e, 3, 3)
    step(undo) {
      canvas.press(at(e, 0, 3), secondary: false, pan: false)
      canvas.drag(at(e, 5, 3), shift: false, alt: false)
      canvas.release(at(e, 5, 3), shift: false, alt: false)
    }
    XCTAssertEqual(pixel(e, 3, 3), red)
    XCTAssertTrue(undo.canUndo)
    undo.undo()
    XCTAssertEqual(pixel(e, 3, 3), blank)
    XCTAssertFalse(undo.canUndo)
    XCTAssertTrue(undo.canRedo)
    undo.redo()
    XCTAssertEqual(pixel(e, 3, 3), red)

    // A color edited live is one step, whatever the color panel sends.
    step(undo) {
      e.setColor(9, 0x112233)
      e.setColor(9, 0x445566)
    }
    XCTAssertEqual(e.sketch.color(9), 0xff44_5566)
    undo.undo()
    XCTAssertEqual(pixel(e, 3, 3), red)
    undo.undo()
    XCTAssertEqual(pixel(e, 3, 3), blank)
    XCTAssertFalse(undo.canUndo)
  }

  func testSelectionKeysAndPaste() {
    let (e, undo) = editor()
    let canvas = CanvasCore(editor: e)
    e.setFg(12)
    step(undo) {
      canvas.press(at(e, 1, 1), secondary: false, pan: false)
      canvas.release(at(e, 1, 1), shift: false, alt: false)
    }
    XCTAssertTrue(canvas.key(.char("m"), down: true, shift: false))
    XCTAssertEqual(e.tool, .select)
    step(undo) { e.selectAll() }
    let copied = e.copy()
    XCTAssertEqual(copied?.width, 32)
    step(undo) { e.paste(copied) }
    XCTAssertEqual(e.sketch.selection, .floating(CGRect(x: 0, y: 0, width: 32, height: 32)))
    // Moving and dropping a floating selection are the paste's step: they
    // register nothing (outside a group, a registration would raise).
    XCTAssertTrue(canvas.key(.right, down: true, shift: true))
    XCTAssertTrue(canvas.key(.escape, down: true, shift: false))
    XCTAssertEqual(e.sketch.selection, .none)
    XCTAssertEqual(pixel(e, 11, 1), e.sketch.color(12) | 0xff00_0000)
    undo.undo()
    XCTAssertNotEqual(pixel(e, 11, 1), e.sketch.color(12) | 0xff00_0000)
    XCTAssertTrue(canvas.key(.char("x"), down: true, shift: false))
    XCTAssertEqual(e.bg, 12)
    XCTAssertFalse(canvas.key(.char("q"), down: true, shift: false))
  }

  func testCanvasSizeAndImportAreSteps() throws {
    let (e, undo) = editor()
    step(undo) { e.resize(width: 64, height: 16) }
    XCTAssertEqual(e.sketch.width, 64)
    XCTAssertTrue(undo.canUndo)
    undo.undo()
    XCTAssertEqual(e.sketch.width, 32)

    let png = try Data(contentsOf: repo.appendingPathComponent("testdata/png/indexed-16.png"))
    step(undo) { e.importPNG(png) }
    XCTAssertNil(e.error)
    XCTAssertTrue(undo.canUndo)
    step(undo) { e.importPNG(Data("not a png".utf8)) }
    XCTAssertNotNil(e.error)
  }

  func testDocumentRoundTrip() throws {
    let doc = GessoDocument()
    let e = doc.editor
    e.setFg(8)
    e.addFrame()
    e.setTag("walk", first: 0, last: 1)
    let data = try doc.snapshot(contentType: .gesso)
    let back = try Sketch(gesso: data)
    XCTAssertEqual(back.frames, 2)
    XCTAssertEqual(back.tags, [Sketch.Tag(name: "walk", first: 0, last: 1)])
    XCTAssertThrowsError(try Sketch(gesso: Data("(gesso 1 0 0)".utf8)))
  }

  private var repo: URL {
    URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("../../..")
      .standardized
  }
}
