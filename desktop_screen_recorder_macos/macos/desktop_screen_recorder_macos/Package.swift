// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "desktop_screen_recorder_macos",
    platforms: [
        .macOS("12.3")
    ],
    products: [
        .library(name: "desktop-screen-recorder-macos", targets: ["desktop_screen_recorder_macos"])
    ],
    dependencies: [
        .package(name: "FlutterFramework", path: "../FlutterFramework")
    ],
    targets: [
        .target(
            name: "desktop_screen_recorder_macos",
            dependencies: [
                .product(name: "FlutterFramework", package: "FlutterFramework")
            ]
        ),
        .testTarget(
            name: "desktop_screen_recorder_macosTests",
            dependencies: ["desktop_screen_recorder_macos"]
        )
    ]
)
