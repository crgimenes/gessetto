// swift-tools-version: 6.0
import PackageDescription

// The C core comes prebuilt from the repository root (make xcframework):
// one binary per platform, the same header.
let package = Package(
  name: "Gessetto",
  platforms: [.macOS(.v14), .iOS(.v17)],
  products: [
    .executable(name: "gessetto", targets: ["gessetto"])
  ],
  targets: [
    .binaryTarget(name: "CGessetto", path: "Frameworks/CGessetto.xcframework"),
    .target(
      name: "GessettoKit",
      dependencies: ["CGessetto"],
      swiftSettings: [.swiftLanguageMode(.v6)]
    ),
    .executableTarget(
      name: "gessetto",
      dependencies: ["GessettoKit"],
      swiftSettings: [.swiftLanguageMode(.v6)]
    ),
    .testTarget(
      name: "GessettoKitTests",
      dependencies: ["GessettoKit"],
      swiftSettings: [.swiftLanguageMode(.v6)]
    ),
  ]
)
