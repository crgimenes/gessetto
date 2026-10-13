import ImageIO
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

  func testPinchIsSmoothAndSettlesOnAWholeZoom() {
    let (e, _) = editor()
    let canvas = CanvasCore(editor: e)
    let p = CGPoint(x: 123, y: 77)
    let under = { (e.pan.x - p.x) / e.zoom }
    let before = under()
    for _ in 0..<5 {
      canvas.magnify(by: 1.07, at: p)
    }
    XCTAssertNotEqual(e.zoom, e.zoom.rounded())
    XCTAssertEqual(under(), before, accuracy: 0.001)
    canvas.endMagnify()
    XCTAssertEqual(e.zoom, e.zoom.rounded())
    XCTAssertEqual(under(), before, accuracy: 1 / e.zoom)

    e.zoom = 5.3
    e.zoom(by: 1)
    XCTAssertEqual(e.zoom, 6)
    e.zoom = 5.3
    e.zoom(by: -1)
    XCTAssertEqual(e.zoom, 4)
    canvas.magnify(by: 1000, at: p)
    XCTAssertEqual(e.zoom, 64)
  }

  func testPanKeepsHalfTheImageInView() {
    let (e, _) = editor()
    let canvas = CanvasCore(editor: e)
    canvas.scroll(dx: -5000, dy: 5000)
    let w = CGFloat(e.sketch.width) * e.zoom
    XCTAssertEqual(e.pan.x, -w / 2)
    XCTAssertEqual(e.pan.y, e.viewSize.height - w / 2)
    canvas.scroll(dx: 20, dy: -20)
    XCTAssertEqual(e.pan.x, -w / 2 + 20)
  }

  func testExportedPNGReadsBack() throws {
    let (e, undo) = editor()
    let canvas = CanvasCore(editor: e)
    e.setFg(9)
    step(undo) {
      canvas.press(at(e, 0, 0), secondary: false, pan: false)
      canvas.drag(at(e, 4, 0), shift: false, alt: false)
      canvas.release(at(e, 4, 0), shift: false, alt: false)
    }
    let png = try XCTUnwrap(Exporter.png(e.sketch, frame: 0, scale: 3))
    let back = Sketch()
    try back.importPNG(png)
    XCTAssertEqual(back.width, 96)
    XCTAssertEqual(back.height, 96)
    let px = back.argb(frame: 0)
    XCTAssertEqual(px[0], 0xffff_004d)
    XCTAssertEqual(px[14], 0xffff_004d)
    XCTAssertEqual(px[15], 0)
    XCTAssertEqual(px[3 * 96], 0)
  }

  func testOptionPicksAndBracketsWalkThePalette() {
    let (e, undo) = editor()
    let canvas = CanvasCore(editor: e)
    e.setFg(12)
    step(undo) {
      canvas.press(at(e, 3, 3), secondary: false, pan: false)
      canvas.release(at(e, 3, 3), shift: false, alt: false)
    }
    e.setFg(1)
    canvas.press(at(e, 3, 3), secondary: false, pan: false, alt: true)
    XCTAssertEqual(e.fg, 12)
    XCTAssertEqual(e.tool, .pencil)
    canvas.press(at(e, 3, 3), secondary: true, pan: false, alt: true)
    XCTAssertEqual(e.bg, 12)
    XCTAssertFalse(undo.canRedo)
    XCTAssertTrue(canvas.key(.char("]"), down: true, shift: false))
    XCTAssertEqual(e.fg, 13)
    e.setFg(0)
    XCTAssertTrue(canvas.key(.char("["), down: true, shift: false))
    XCTAssertEqual(e.fg, e.sketch.colors - 1)
  }

  /// Three frames, each with one pixel of its own color at (f, 0).
  private func threeFrames() -> (Editor, UndoManager) {
    let (e, undo) = editor()
    let canvas = CanvasCore(editor: e)
    for f in 0..<3 {
      if f > 0 {
        step(undo) { e.addFrame() }
      }
      e.setFg(8 + f)
      step(undo) { e.setDuration(100 + 50 * f) }
      step(undo) {
        canvas.press(at(e, f, 0), secondary: false, pan: false)
        canvas.release(at(e, f, 0), shift: false, alt: false)
      }
    }
    step(undo) { e.setTag("tail", first: 1, last: 2) }
    return (e, undo)
  }

  func testSpriteSheetPlacesFramesInRows() throws {
    let (e, _) = threeFrames()
    let png = try XCTUnwrap(
      Exporter.make(e.sketch, ExportOptions(format: .sheet, scale: 1, tag: nil, columns: 2)))
    let back = Sketch()
    try back.importPNG(png)
    XCTAssertEqual(back.width, 64)
    XCTAssertEqual(back.height, 64)
    let px = back.argb(frame: 0)
    XCTAssertEqual(px[0], e.sketch.color(8))
    XCTAssertEqual(px[32 + 1], e.sketch.color(9))
    XCTAssertEqual(px[32 * 64 + 2], e.sketch.color(10))
    let tail = try XCTUnwrap(
      Exporter.make(e.sketch, ExportOptions(format: .sheet, scale: 2, tag: "tail", columns: 0)))
    try back.importPNG(tail)
    XCTAssertEqual(back.width, 128)
    XCTAssertEqual(back.height, 64)
  }

  func testGIFKeepsFramesAndDurations() throws {
    let (e, _) = threeFrames()
    let gif = try XCTUnwrap(Exporter.make(e.sketch, ExportOptions(format: .gif, scale: 2)))
    let src = try XCTUnwrap(CGImageSourceCreateWithData(gif as CFData, nil))
    XCTAssertEqual(CGImageSourceGetCount(src), 3)
    let delays = (0..<3).map { i -> Double in
      let p = CGImageSourceCopyPropertiesAtIndex(src, i, nil) as? [CFString: Any]
      let g = p?[kCGImagePropertyGIFDictionary] as? [CFString: Any]
      return g?[kCGImagePropertyGIFUnclampedDelayTime] as? Double ?? -1
    }
    XCTAssertEqual(delays, [0.1, 0.15, 0.2])
    let file = CGImageSourceCopyProperties(src, nil) as? [CFString: Any]
    let loop =
      (file?[kCGImagePropertyGIFDictionary] as? [CFString: Any])?[
        kCGImagePropertyGIFLoopCount] as? Int
    XCTAssertEqual(loop, 0)
    XCTAssertEqual(CGImageSourceCreateImageAtIndex(src, 0, nil)?.width, 64)
  }

  func testHeaderExportIsWhatGesso2cWrites() throws {
    let (e, _) = threeFrames()
    let data = try XCTUnwrap(
      Exporter.make(e.sketch, ExportOptions(format: .header, name: "Hero Walk")))
    let text = String(decoding: data, as: UTF8.self)
    XCTAssertTrue(text.hasPrefix("/* Hero Walk.gesso, by gesso2c."))
    XCTAssertTrue(text.contains("    HERO_WALK_FRAMES = 3,\n"))
    XCTAssertTrue(
      text.contains(
        "static const uint16_t hero_walk_duration[HERO_WALK_FRAMES] = {\n    100, 150, 200,\n};"))
    XCTAssertTrue(text.contains("    {\"tail\", 1, 2, 0},\n"))
  }

  func testSpriteSheetComesBackAsFrames() throws {
    let (e, _) = threeFrames()
    let png = try XCTUnwrap(
      Exporter.make(e.sketch, ExportOptions(format: .sheet, scale: 1, tag: nil, columns: 0)))
    let (f, undo) = editor()
    f.readSheet(png)
    let sheet = try XCTUnwrap(f.sheet)
    XCTAssertEqual(sheet.width, 96)
    step(undo) { f.importSheet(sheet, frameWidth: 32, frameHeight: 32) }
    XCTAssertNil(f.error)
    XCTAssertEqual(f.sketch.frames, 3)
    for n in 0..<3 {
      XCTAssertEqual(f.sketch.argb(frame: n), e.sketch.argb(frame: n))
    }
    XCTAssertTrue(undo.canUndo)
    step(undo) { f.importSheet(sheet, frameWidth: 0, frameHeight: 32) }
    XCTAssertNotNil(f.error)
  }

  func testTagDirectionsSetThePlayOrder() throws {
    let (e, undo) = threeFrames()
    step(undo) { e.addFrame() }
    XCTAssertEqual(e.playOrder, [0, 1, 2, 3])
    step(undo) { e.setTag("all", first: 0, last: 3, direction: .pingpong) }
    e.playTag = "all"
    XCTAssertEqual(e.playOrder, [0, 1, 2, 3, 2, 1])
    step(undo) { e.setTag("all", first: 1, last: 3, direction: .reverse) }
    XCTAssertEqual(e.playOrder, [3, 2, 1])
    XCTAssertEqual(e.sketch.tags.first { $0.name == "all" }?.direction, .reverse)
    step(undo) { e.setTag("all", first: 0, last: 3, direction: .pingpong) }
    let gif = try XCTUnwrap(Exporter.make(e.sketch, ExportOptions(format: .gif, tag: "all")))
    let src = try XCTUnwrap(CGImageSourceCreateWithData(gif as CFData, nil))
    XCTAssertEqual(CGImageSourceGetCount(src), 6)
    let back = try Sketch(gesso: try e.sketch.gesso())
    XCTAssertEqual(back.tags.first { $0.name == "all" }?.direction, .pingpong)
  }

  func testFrameSelectionActsAsOne() {
    let (e, undo) = threeFrames()
    let color = { (f: Int) in e.sketch.argb(frame: f)[f % 3] }
    let a = color(0)
    let b = color(1)
    e.selectFrame(0)
    e.selectFrame(1, extend: true)
    XCTAssertEqual(e.selectedFrames, 0...1)
    step(undo) { e.duplicateFrame() }
    XCTAssertEqual(e.sketch.frames, 5)
    XCTAssertEqual(e.selectedFrames, 2...3)
    XCTAssertEqual(e.sketch.argb(frame: 2)[0], a)
    XCTAssertEqual(e.sketch.argb(frame: 3)[1], b)
    step(undo) { e.setDuration(40) }
    XCTAssertEqual((0..<5).map { e.sketch.duration($0) }, [100, 150, 40, 40, 200])
    step(undo) { e.moveFrame(2, to: 4) }
    XCTAssertEqual((0..<5).map { e.sketch.duration($0) }, [100, 150, 200, 40, 40])
    XCTAssertEqual(e.selectedFrames, 3...4)
    step(undo) { e.deleteFrame() }
    XCTAssertEqual(e.sketch.frames, 3)
    XCTAssertEqual(e.selectedFrames, 2...2)
    undo.undo()
    undo.undo()
    XCTAssertEqual((0..<5).map { e.sketch.duration($0) }, [100, 150, 40, 40, 200])
  }

  func testFramesCopyIntoAnotherDocument() {
    let (e, _) = threeFrames()
    e.selectFrame(1)
    e.selectFrame(2, extend: true)
    e.copyFrames()
    let (f, undo) = editor()
    step(undo) { f.pasteFrames() }
    XCTAssertNil(f.error)
    XCTAssertEqual(f.sketch.frames, 3)
    XCTAssertEqual(f.selectedFrames, 1...2)
    XCTAssertEqual(f.sketch.argb(frame: 1), e.sketch.argb(frame: 1))
    XCTAssertEqual(f.sketch.argb(frame: 2), e.sketch.argb(frame: 2))
    XCTAssertEqual(f.sketch.duration(2), 200)
    step(undo) { f.resize(width: 8, height: 8) }
    f.pasteFrames()
    XCTAssertNotNil(f.error)
  }

  func testPaletteFilesAndPresets() throws {
    let hex = Palette.read(Data("ff0000\n#00ff00\n\nnot a color\n0000FF\n".utf8), name: "x")
    XCTAssertEqual(hex?.colors, [0xff0000, 0x00ff00, 0x0000ff])
    let gpl = """
      GIMP Palette
      Name: test
      Columns: 2
      # comment
        0   0   0\tBlack
      255 255 255\tWhite
      """
    XCTAssertEqual(Palette.read(Data(gpl.utf8), name: "g")?.colors, [0x000000, 0xffffff])
    XCTAssertNil(Palette.read(Data("nothing here".utf8), name: "n"))
    let (e, undo) = editor()
    let gesso = try e.sketch.gesso()
    XCTAssertEqual(Palette.read(gesso, name: "self")?.colors.count, 16)

    let gameBoy = try XCTUnwrap(Palette.presets.first { $0.name == "Game Boy" })
    step(undo) { e.loadPalette(gameBoy.colors, match: true) }
    XCTAssertEqual(e.sketch.colors, 5)
    XCTAssertEqual(e.sketch.color(4), 0xff9b_bc0f)
    undo.undo()
    XCTAssertEqual(e.sketch.colors, 17)
  }

  func testPenSizeFromKeysAndStamp() {
    let (e, undo) = editor()
    let canvas = CanvasCore(editor: e)
    XCTAssertTrue(canvas.key(.char("="), down: true, shift: false))
    XCTAssertTrue(canvas.key(.char("="), down: true, shift: false))
    XCTAssertEqual(e.brushSize, 3)
    e.setFg(8)
    step(undo) {
      canvas.press(at(e, 5, 5), secondary: false, pan: false)
      canvas.release(at(e, 5, 5), shift: false, alt: false)
    }
    let red = e.sketch.color(8)
    let px = e.sketch.argb(frame: 0)
    XCTAssertEqual(px[4 * 32 + 4], red)
    XCTAssertEqual(px[6 * 32 + 6], red)
    XCTAssertEqual(px[7 * 32 + 5], 0)
    for _ in 0..<20 {
      _ = canvas.key(.char("-"), down: true, shift: false)
    }
    XCTAssertEqual(e.brushSize, 1)
  }

  func testDocumentRoundTrip() throws {
    let doc = GessoDocument()
    let e = doc.editor
    e.setFg(8)
    e.addFrame()
    e.setTag("walk", first: 0, last: 1)
    let data = try doc.snapshot(contentType: .gesso)
    let back = try Sketch(gesso: data)
    XCTAssertEqual(try back.gesso(), data)
    XCTAssertEqual(back.frames, 2)
    XCTAssertEqual(back.tags, [Sketch.Tag(name: "walk", first: 0, last: 1)])
    XCTAssertThrowsError(try Sketch(gesso: Data("(gesso 1 0 0)".utf8)))
  }

  private var repo: URL {
    URL(fileURLWithPath: #filePath).deletingLastPathComponent().appendingPathComponent("../../..")
      .standardized
  }
}
