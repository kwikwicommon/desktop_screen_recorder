import AVFoundation
import Cocoa

final class RecordingEnhancementOverlayController {
  private var windows: [NSWindow] = []
  private var monitors: [Any] = []
  private weak var activeView: RecordingEnhancementView?

  func start(config: RecordingConfiguration, targetFrame: CGRect) {
    stop()

    let view = RecordingEnhancementView(frame: CGRect(origin: .zero, size: targetFrame.size))
    view.configureCamera(config.cameraOverlay)
    let panel = NSPanel(
        contentRect: targetFrame,
        styleMask: [.borderless, .nonactivatingPanel],
        backing: .buffered,
        defer: false
      )
    panel.level = .screenSaver
    panel.backgroundColor = .clear
    panel.isOpaque = false
    panel.ignoresMouseEvents = true
    panel.collectionBehavior = [.canJoinAllSpaces, .stationary, .ignoresCycle]
    panel.contentView = view
    panel.orderFrontRegardless()
    windows.append(panel)
    activeView = view

    if config.highlightClicks {
      let mouseMonitor = NSEvent.addGlobalMonitorForEvents(
        matching: [.leftMouseDown, .rightMouseDown]
      ) { [weak self] _ in
        let point = NSEvent.mouseLocation
        DispatchQueue.main.async {
          self?.addClick(at: point)
        }
      }
      if let mouseMonitor {
        monitors.append(mouseMonitor)
      }
    }

    if config.showKeystrokes {
      let keyMonitor = NSEvent.addGlobalMonitorForEvents(matching: [.keyDown]) { [weak self] event in
        let label = Self.keyLabel(for: event)
        DispatchQueue.main.async {
          self?.activeView?.addKey(label: label)
        }
      }
      if let keyMonitor {
        monitors.append(keyMonitor)
      }
    }
  }

  func stop() {
    for monitor in monitors {
      NSEvent.removeMonitor(monitor)
    }
    monitors.removeAll()

    for window in windows {
      (window.contentView as? RecordingEnhancementView)?.stopCamera()
      window.orderOut(nil)
    }
    windows.removeAll()
    activeView = nil
  }

  func setPaused(_ paused: Bool) {
    for window in windows {
      (window.contentView as? RecordingEnhancementView)?.setPaused(paused)
    }
  }

  private func addClick(at screenPoint: CGPoint) {
    for window in windows where window.frame.contains(screenPoint) {
      let localPoint = window.convertPoint(fromScreen: screenPoint)
      (window.contentView as? RecordingEnhancementView)?.addClick(at: localPoint)
    }
  }

  private static func keyLabel(for event: NSEvent) -> String {
    var parts: [String] = []
    let flags = event.modifierFlags
    if flags.contains(.command) { parts.append("Cmd") }
    if flags.contains(.shift) { parts.append("Shift") }
    if flags.contains(.option) { parts.append("Opt") }
    if flags.contains(.control) { parts.append("Ctrl") }

    if let characters = event.charactersIgnoringModifiers, !characters.isEmpty {
      parts.append(characters.uppercased())
    }

    return parts.isEmpty ? "Key" : parts.joined(separator: "+")
  }
}

private final class RecordingEnhancementView: NSView {
  private struct ClickRipple {
    let point: CGPoint
    let createdAt: Date
  }

  private struct KeyBubble {
    let label: String
    let createdAt: Date
  }

  private var ripples: [ClickRipple] = []
  private var keyBubbles: [KeyBubble] = []
  private var timer: Timer?
  private var isPaused = false
  private var cameraView: RecordingCameraPreviewView?

  override var isFlipped: Bool { true }

  override init(frame frameRect: NSRect) {
    super.init(frame: frameRect)
    wantsLayer = true
    layer?.backgroundColor = NSColor.clear.cgColor
    timer = Timer.scheduledTimer(withTimeInterval: 1.0 / 30.0, repeats: true) { [weak self] _ in
      self?.tick()
    }
  }

  required init?(coder: NSCoder) {
    super.init(coder: coder)
  }

  deinit {
    timer?.invalidate()
    stopCamera()
  }

  func addClick(at point: CGPoint) {
    guard !isPaused else { return }
    ripples.append(ClickRipple(point: point, createdAt: Date()))
    needsDisplay = true
  }

  func addKey(label: String) {
    guard !isPaused else { return }
    let visibleLabel = String(label.prefix(48))
    keyBubbles.append(KeyBubble(label: visibleLabel, createdAt: Date()))
    keyBubbles = Array(keyBubbles.suffix(6))
    needsDisplay = true
  }

  func configureCamera(_ config: CameraOverlayConfiguration) {
    guard config.enabled else { return }
    let margin: CGFloat = 28
    let availableSize = min(bounds.width, bounds.height) - margin * 2
    guard availableSize >= 48 else { return }
    let size = min(CGFloat(config.size), availableSize)
    let origin: CGPoint
    switch config.position {
    case "topLeft":
      origin = CGPoint(x: margin, y: margin)
    case "topRight":
      origin = CGPoint(x: bounds.width - size - margin, y: margin)
    case "bottomLeft":
      origin = CGPoint(x: margin, y: bounds.height - size - margin)
    default:
      origin = CGPoint(x: bounds.width - size - margin, y: bounds.height - size - margin)
    }

    let preview = RecordingCameraPreviewView(
      frame: CGRect(origin: origin, size: CGSize(width: size, height: size)),
      shape: config.shape
    )
    addSubview(preview)
    preview.start()
    cameraView = preview
  }

  func stopCamera() {
    cameraView?.stop()
    cameraView?.removeFromSuperview()
    cameraView = nil
  }

  func setPaused(_ paused: Bool) {
    isPaused = paused
    if paused {
      ripples.removeAll()
      keyBubbles.removeAll()
      needsDisplay = true
    }
  }

  private func tick() {
    guard !isPaused else { return }
    let now = Date()
    ripples.removeAll { now.timeIntervalSince($0.createdAt) > 0.65 }
    keyBubbles.removeAll { now.timeIntervalSince($0.createdAt) > 2.5 }
    needsDisplay = true
  }

  override func draw(_ dirtyRect: NSRect) {
    super.draw(dirtyRect)
    drawRipples()
    drawKeys()
  }

  private func drawRipples() {
    let now = Date()
    for ripple in ripples {
      let age = now.timeIntervalSince(ripple.createdAt)
      let progress = min(1.0, age / 0.65)
      let radius = CGFloat(12.0 + 36.0 * progress)
      let alpha = CGFloat(1.0 - progress)
      let rect = CGRect(
        x: ripple.point.x - radius,
        y: ripple.point.y - radius,
        width: radius * 2,
        height: radius * 2
      )

      NSColor.systemRed.withAlphaComponent(alpha * 0.45).setStroke()
      let path = NSBezierPath(ovalIn: rect)
      path.lineWidth = 3
      path.stroke()

      NSColor.systemRed.withAlphaComponent(alpha * 0.18).setFill()
      NSBezierPath(ovalIn: rect.insetBy(dx: radius * 0.25, dy: radius * 0.25)).fill()
    }
  }

  private func drawKeys() {
    guard !keyBubbles.isEmpty else { return }
    guard bounds.width >= 120, bounds.height >= 80 else { return }

    let labels = keyBubbles.map(\.label).joined(separator: "  ")
    let paragraph = NSMutableParagraphStyle()
    paragraph.lineBreakMode = .byTruncatingMiddle
    paragraph.alignment = .center
    let attributes: [NSAttributedString.Key: Any] = [
      .font: NSFont.systemFont(ofSize: 22, weight: .semibold),
      .foregroundColor: NSColor.white,
      .paragraphStyle: paragraph
    ]
    let attributed = NSAttributedString(string: labels, attributes: attributes)
    let textSize = attributed.size()
    let maxWidth = max(80, bounds.width - 56)
    let bubbleWidth = min(textSize.width + 36, maxWidth)
    let bubbleHeight = textSize.height + 20
    let rect = CGRect(
      x: max(28, (bounds.width - bubbleWidth) / 2),
      y: max(20, bounds.height - bubbleHeight - 68),
      width: bubbleWidth,
      height: bubbleHeight
    )

    NSColor.black.withAlphaComponent(0.62).setFill()
    NSBezierPath(roundedRect: rect, xRadius: 10, yRadius: 10).fill()
    attributed.draw(
      with: rect.insetBy(dx: 18, dy: 10),
      options: [.usesLineFragmentOrigin, .truncatesLastVisibleLine]
    )
  }
}

private final class RecordingCameraPreviewView: NSView {
  private let session = AVCaptureSession()
  private let sessionQueue = DispatchQueue(label: "desktop_screen_recorder.camera.session")
  private let stateLock = NSLock()
  private let shape: String
  private var previewLayer: AVCaptureVideoPreviewLayer?
  private var isRunningRequested = false
  private var isSessionConfigured = false
  private var hasVideoInput = false

  init(frame frameRect: NSRect, shape: String) {
    self.shape = shape
    super.init(frame: frameRect)
    wantsLayer = true
    layer?.backgroundColor = NSColor.black.cgColor
    layer?.borderWidth = 2
    layer?.borderColor = NSColor.white.withAlphaComponent(0.85).cgColor
    layer?.masksToBounds = true
    configureLayerShape()
  }

  required init?(coder: NSCoder) {
    self.shape = "circle"
    super.init(coder: coder)
  }

  override func layout() {
    super.layout()
    previewLayer?.frame = bounds
    configureLayerShape()
  }

  func start() {
    setRunningRequested(true)
    if previewLayer == nil {
      let layer = AVCaptureVideoPreviewLayer(session: session)
      layer.videoGravity = .resizeAspectFill
      layer.frame = bounds
      self.layer?.insertSublayer(layer, at: 0)
      previewLayer = layer
    }

    sessionQueue.async { [weak self] in
      guard let self else { return }
      self.configureSessionIfNeeded()
      guard self.runningRequested(), self.hasVideoInput else { return }
      if !self.session.isRunning {
        self.session.startRunning()
      }
    }
  }

  func stop() {
    setRunningRequested(false)
    sessionQueue.async { [weak self] in
      guard let self else { return }
      if self.session.isRunning {
        self.session.stopRunning()
      }
    }
    DispatchQueue.main.async { [weak self] in
      self?.previewLayer?.removeFromSuperlayer()
      self?.previewLayer = nil
    }
  }

  private func configureLayerShape() {
    if shape == "circle" {
      layer?.cornerRadius = min(bounds.width, bounds.height) / 2
    } else {
      layer?.cornerRadius = 12
    }
  }

  private func configureSessionIfNeeded() {
    guard !isSessionConfigured else { return }
    session.beginConfiguration()
    session.sessionPreset = .medium
    if let device = AVCaptureDevice.default(for: .video),
       let input = try? AVCaptureDeviceInput(device: device),
       session.canAddInput(input) {
      session.addInput(input)
      hasVideoInput = true
    }
    session.commitConfiguration()
    isSessionConfigured = true
  }

  private func runningRequested() -> Bool {
    stateLock.lock()
    defer { stateLock.unlock() }
    return isRunningRequested
  }

  private func setRunningRequested(_ value: Bool) {
    stateLock.lock()
    isRunningRequested = value
    stateLock.unlock()
  }
}
