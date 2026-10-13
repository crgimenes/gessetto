import GessettoKit
import SwiftUI

@main
struct GessettoApp: App {
  init() {
    #if os(macOS)
      // Every sample of a stroke: coalesced moves skip pixels.
      NSEvent.isMouseCoalescingEnabled = false
    #endif
  }

  var body: some Scene {
    GessettoScene()
  }
}
