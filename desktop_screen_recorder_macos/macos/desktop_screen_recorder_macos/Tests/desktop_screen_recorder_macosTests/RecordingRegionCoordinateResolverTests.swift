import CoreGraphics
import XCTest
@testable import desktop_screen_recorder_macos

final class RecordingRegionCoordinateResolverTests: XCTestCase {
  func testDisplayLocalRegionOnNegativeSecondaryDisplay() throws {
    let display = RecordingDisplayGeometry(
      frame: CGRect(x: -1920, y: 0, width: 1920, height: 1080),
      logicalWidth: 1920,
      logicalHeight: 1080
    )

    let resolution = try RecordingRegionCoordinateResolver.resolve(
      display: display,
      region: CGRect(x: 120, y: 80, width: 320, height: 200),
      hasDisplayId: true
    )

    XCTAssertEqual(resolution.sourceRect, CGRect(x: 120, y: 80, width: 320, height: 200))
    XCTAssertEqual(resolution.overlayFrame, CGRect(x: -1800, y: 80, width: 320, height: 200))
  }

  func testDisplayLocalRegionOnHighDpiSecondaryDisplay() throws {
    let display = RecordingDisplayGeometry(
      frame: CGRect(x: 1440, y: 0, width: 1440, height: 900),
      logicalWidth: 1440,
      logicalHeight: 900
    )

    let resolution = try RecordingRegionCoordinateResolver.resolve(
      display: display,
      region: CGRect(x: 10, y: 20, width: 100, height: 80),
      hasDisplayId: true
    )

    XCTAssertEqual(resolution.sourceRect, CGRect(x: 10, y: 20, width: 100, height: 80))
    XCTAssertEqual(resolution.overlayFrame, CGRect(x: 1450, y: 20, width: 100, height: 80))
  }

  func testFullDisplaySourceRectUsesLogicalDisplaySizeOnHighDpiDisplay() throws {
    let display = RecordingDisplayGeometry(
      frame: CGRect(x: 1440, y: 0, width: 1440, height: 900),
      logicalWidth: 1440,
      logicalHeight: 900
    )

    let resolution = try RecordingRegionCoordinateResolver.resolve(
      display: display,
      region: nil,
      hasDisplayId: true
    )

    XCTAssertEqual(resolution.sourceRect, CGRect(x: 0, y: 0, width: 1440, height: 900))
    XCTAssertEqual(resolution.overlayFrame, CGRect(x: 1440, y: 0, width: 1440, height: 900))
  }

  func testVirtualDesktopRegionWithoutDisplayIdKeepsGlobalContract() throws {
    let display = RecordingDisplayGeometry(
      frame: CGRect(x: -1920, y: 0, width: 1920, height: 1080),
      logicalWidth: 1920,
      logicalHeight: 1080
    )

    let resolution = try RecordingRegionCoordinateResolver.resolve(
      display: display,
      region: CGRect(x: -1800, y: 100, width: 500, height: 300),
      hasDisplayId: false
    )

    XCTAssertEqual(resolution.sourceRect, CGRect(x: 120, y: 100, width: 500, height: 300))
    XCTAssertEqual(resolution.overlayFrame, CGRect(x: -1800, y: 100, width: 500, height: 300))
  }

  func testDisplayLocalRegionOutsideTargetDisplayFails() {
    let display = RecordingDisplayGeometry(
      frame: CGRect(x: -1920, y: 0, width: 1920, height: 1080),
      logicalWidth: 1920,
      logicalHeight: 1080
    )

    XCTAssertThrowsError(
      try RecordingRegionCoordinateResolver.resolve(
        display: display,
        region: CGRect(x: 2000, y: 100, width: 300, height: 200),
        hasDisplayId: true
      )
    ) { error in
      XCTAssertEqual((error as NSError).domain, "DesktopScreenRecorderMacos")
    }
  }
}
