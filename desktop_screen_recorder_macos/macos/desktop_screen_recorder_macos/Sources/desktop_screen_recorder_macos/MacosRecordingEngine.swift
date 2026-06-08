import AVFoundation
import AVKit
import ApplicationServices
import Cocoa
import CoreAudio
import CoreGraphics
import FlutterMacOS
import ImageIO
import ScreenCaptureKit
import UniformTypeIdentifiers

enum DesktopNativeStatus: String {
  case success
  case cancelled
  case invalidInput
  case permissionDenied
  case unsupported
  case nativeFailure
  case ioFailure
  case notConfigured
}

private enum RecordingErrorKey {
  static let status = "DesktopScreenRecorderStatus"
  static let recoverySuggestion = "DesktopScreenRecorderRecoverySuggestion"
  static let nativeCode = "DesktopScreenRecorderNativeCode"
  static let recoverability = "DesktopScreenRecorderRecoverability"
  static let artifactPath = "DesktopScreenRecorderArtifactPath"
}

private enum RecordingLimits {
  static let maxFrameRate = 240
  static let minVideoBitrate = 500_000
  static let maxVideoBitrate = 100_000_000
  static let minGifWidth = 160
  static let maxGifWidth = 3840
}

struct RecordingConfiguration {
  let mode: String
  let format: String
  let qualityMaxHeight: Int
  let videoBitrate: Int
  let frameRate: Int
  let captureAudio: Bool
  let captureMicrophone: Bool
  let showCursor: Bool
  let highlightClicks: Bool
  let showKeystrokes: Bool
  let region: CGRect?
  let hasInvalidRegionInput: Bool
  let displayId: Int?
  let windowId: Int?
  let windowTitle: String?
  let gifMaxWidth: Int
  let gifFrameRate: Int
  let gifOptimizePalette: Bool
  let cameraOverlay: CameraOverlayConfiguration

  init(arguments: [String: Any]) {
    mode = arguments["mode"] as? String ?? "fullscreen"
    format = arguments["format"] as? String ?? "mp4"
    qualityMaxHeight = max(0, recordingIntValue(arguments["qualityMaxHeight"]) ?? 1440)
    videoBitrate = recordingClampedInt(
      recordingIntValue(arguments["videoBitrate"]) ?? 16000000,
      lowerBound: RecordingLimits.minVideoBitrate,
      upperBound: RecordingLimits.maxVideoBitrate
    )
    frameRate = recordingClampedInt(
      recordingIntValue(arguments["frameRate"]) ?? 30,
      lowerBound: 1,
      upperBound: RecordingLimits.maxFrameRate
    )
    captureAudio = arguments["captureAudio"] as? Bool ?? false
    captureMicrophone = arguments["captureMicrophone"] as? Bool ?? false
    showCursor = arguments["showCursor"] as? Bool ?? true
    highlightClicks = arguments["highlightClicks"] as? Bool ?? false
    showKeystrokes = arguments["showKeystrokes"] as? Bool ?? false
    displayId = recordingIntValue(arguments["displayId"])
    windowId = recordingIntValue(arguments["windowId"])
    windowTitle = arguments["windowTitle"] as? String

    if arguments.keys.contains("region") {
      if let regionMap = arguments["region"] as? [String: Any],
         let left = recordingDoubleValue(regionMap["left"]),
         let top = recordingDoubleValue(regionMap["top"]),
         let width = recordingDoubleValue(regionMap["width"]),
         let height = recordingDoubleValue(regionMap["height"]) {
        let candidate = CGRect(x: left, y: top, width: width, height: height)
        if recordingRectHasFiniteValues(candidate) {
          region = candidate
          hasInvalidRegionInput = false
        } else {
          region = nil
          hasInvalidRegionInput = true
        }
      } else {
        region = nil
        hasInvalidRegionInput = true
      }
    } else {
      region = nil
      hasInvalidRegionInput = false
    }

    let gifMap = arguments["gif"] as? [String: Any] ?? [:]
    gifMaxWidth = recordingClampedInt(
      recordingIntValue(gifMap["maxWidth"]) ?? 960,
      lowerBound: RecordingLimits.minGifWidth,
      upperBound: RecordingLimits.maxGifWidth
    )
    gifFrameRate = max(1, min(30, recordingIntValue(gifMap["frameRate"]) ?? 12))
    gifOptimizePalette = gifMap["optimizePalette"] as? Bool ?? true

    cameraOverlay = CameraOverlayConfiguration(
      arguments: arguments["cameraOverlay"] as? [String: Any] ?? [:]
    )
  }
}

struct CameraOverlayConfiguration {
  let enabled: Bool
  let shape: String
  let position: String
  let size: Int

  init(arguments: [String: Any]) {
    enabled = arguments["enabled"] as? Bool ?? false
    shape = arguments["shape"] as? String ?? "circle"
    position = arguments["position"] as? String ?? "bottomRight"
    size = max(96, min(480, recordingIntValue(arguments["size"]) ?? 220))
  }
}

func recordingIntValue(_ raw: Any?) -> Int? {
  if let value = raw as? Int { return value }
  if let value = raw as? Double {
    guard recordingDoubleFitsInt(value) else { return nil }
    return Int(value)
  }
  if let value = raw as? NSNumber {
    let doubleValue = value.doubleValue
    guard recordingDoubleFitsInt(doubleValue) else { return nil }
    return value.intValue
  }
  return nil
}

func recordingDoubleValue(_ raw: Any?) -> Double? {
  if let value = raw as? Double { return value }
  if let value = raw as? Int { return Double(value) }
  if let value = raw as? NSNumber { return value.doubleValue }
  return nil
}

func recordingDoubleFitsInt(_ value: Double) -> Bool {
  value.isFinite
    && value >= Double(Int.min)
    && value <= Double(Int.max)
}

func recordingClampedInt(
  _ value: Int,
  lowerBound: Int,
  upperBound: Int
) -> Int {
  min(max(value, lowerBound), upperBound)
}

func recordingRectHasFiniteValues(_ rect: CGRect) -> Bool {
  rect.origin.x.isFinite
    && rect.origin.y.isFinite
    && rect.size.width.isFinite
    && rect.size.height.isFinite
}

func recordingRectHasUsableSize(_ rect: CGRect?) -> Bool {
  guard let rect else { return false }
  return rect.width > 1 && rect.height > 1
}

final class NativeRecordingBridge: NSObject {
  private var recorder: Any?
  private var isStarting = false

  func startUnified(arguments: Any?, result: @escaping FlutterResult) {
    guard #available(macOS 12.3, *) else {
      result(unifiedFailure(
        status: .unsupported,
        message: "ScreenCaptureKit requires macOS 12.3 or later.",
        recoverySuggestion: "Run the app on macOS 12.3 or later."
      ))
      return
    }

    guard recorder == nil, !isStarting else {
      result(unifiedFailure(
        status: .invalidInput,
        message: "A recording is already in progress.",
        recoverySuggestion: "Stop the active recording before starting a new one."
      ))
      return
    }

    guard let arguments = arguments as? [String: Any] else {
      result(unifiedFailure(
        status: .invalidInput,
        message: "Recording arguments are missing.",
        recoverySuggestion: "Build a record request before invoking the native recorder."
      ))
      return
    }

    let config = RecordingConfiguration(arguments: arguments)
    let screenRecorder = NativeScreenRecorder()
    recorder = screenRecorder
    isStarting = true
    screenRecorder.start(config: config) { [weak self] outcome in
      DispatchQueue.main.async {
        self?.isStarting = false
        switch outcome {
        case .success:
          result(self?.unifiedSuccess() ?? ["status": DesktopNativeStatus.success.rawValue])
        case .failure(let error):
          self?.recorder = nil
          result(self?.unifiedFailure(
            error: error,
            defaultStatus: .nativeFailure,
            defaultRecoverySuggestion: "Check screen recording permission and try again."
          ) ?? [
            "status": DesktopNativeStatus.nativeFailure.rawValue,
            "failure": [
              "message": error.localizedDescription,
              "recoverySuggestion": "Check screen recording permission and try again.",
            ],
          ])
        }
      }
    }
  }

  func pauseUnified(result: @escaping FlutterResult) {
    if isStarting {
      result(recordingTransitionFailure(action: "pausing"))
      return
    }

    guard #available(macOS 12.3, *),
          let screenRecorder = recorder as? NativeScreenRecorder else {
      result(unifiedFailure(
        status: .invalidInput,
        message: "No recording is active.",
        recoverySuggestion: "Start a recording before pausing."
      ))
      return
    }

    screenRecorder.pause { [weak self] paused in
      DispatchQueue.main.async {
        if paused {
          result(self?.unifiedSuccess() ?? ["status": DesktopNativeStatus.success.rawValue])
        } else {
          result(self?.unifiedFailure(
            status: .nativeFailure,
            message: "Native recorder did not pause.",
            recoverySuggestion: "Try pausing the recording again."
          ) ?? [
            "status": DesktopNativeStatus.nativeFailure.rawValue,
            "failure": [
              "message": "Native recorder did not pause.",
              "recoverySuggestion": "Try pausing the recording again.",
            ],
          ])
        }
      }
    }
  }

  func resumeUnified(result: @escaping FlutterResult) {
    if isStarting {
      result(recordingTransitionFailure(action: "resuming"))
      return
    }

    guard #available(macOS 12.3, *),
          let screenRecorder = recorder as? NativeScreenRecorder else {
      result(unifiedFailure(
        status: .invalidInput,
        message: "No recording is active.",
        recoverySuggestion: "Start a recording before resuming."
      ))
      return
    }

    screenRecorder.resume { [weak self] resumed in
      DispatchQueue.main.async {
        if resumed {
          result(self?.unifiedSuccess() ?? ["status": DesktopNativeStatus.success.rawValue])
        } else {
          result(self?.unifiedFailure(
            status: .nativeFailure,
            message: "Native recorder did not resume.",
            recoverySuggestion: "Try resuming the recording again."
          ) ?? [
            "status": DesktopNativeStatus.nativeFailure.rawValue,
            "failure": [
              "message": "Native recorder did not resume.",
              "recoverySuggestion": "Try resuming the recording again.",
            ],
          ])
        }
      }
    }
  }

  func stopUnified(result: @escaping FlutterResult) {
    if isStarting {
      result(recordingTransitionFailure(action: "stopping"))
      return
    }

    guard #available(macOS 12.3, *),
          let screenRecorder = recorder as? NativeScreenRecorder else {
      result(unifiedFailure(
        status: .invalidInput,
        message: "No recording is active.",
        recoverySuggestion: "Start a recording before stopping."
      ))
      return
    }

    screenRecorder.stop { [weak self] outcome in
      DispatchQueue.main.async {
        self?.recorder = nil
        switch outcome {
        case .success(let value):
          result(self?.unifiedSuccess(payload: value) ?? [
            "status": DesktopNativeStatus.success.rawValue,
            "payload": value,
          ])
        case .failure(let error):
          result(self?.unifiedFailure(
            error: error,
            defaultStatus: .nativeFailure,
            defaultRecoverySuggestion: "Try the recording again."
          ) ?? [
            "status": DesktopNativeStatus.nativeFailure.rawValue,
            "failure": [
              "message": error.localizedDescription,
              "recoverySuggestion": "Try the recording again.",
            ],
          ])
        }
      }
    }
  }

  private func recordingTransitionFailure(action: String) -> [String: Any] {
    unifiedFailure(
      status: .invalidInput,
      message: "Recording is still starting.",
      recoverySuggestion: "Wait until recording starts before \(action) it.",
      nativeCode: "recordingStarting"
    )
  }

  private func unifiedSuccess(payload: Any? = nil) -> [String: Any] {
    var response: [String: Any] = [
      "status": DesktopNativeStatus.success.rawValue
    ]
    if let payload {
      response["payload"] = payload
    }
    return response
  }

  private func unifiedFailure(
    status: DesktopNativeStatus,
    message: String,
    recoverySuggestion: String,
    domain: String? = nil,
    nativeCode: String = "",
    recoverability: String = "unknown",
    artifactPath: String = ""
  ) -> [String: Any] {
    return [
      "status": status.rawValue,
      "failure": [
        "message": message,
        "recoverySuggestion": recoverySuggestion,
        "requiresRestart": false,
        "domain": domain ?? status.rawValue,
        "nativeCode": nativeCode,
        "recoverability": recoverability,
        "artifactPath": artifactPath,
      ],
    ]
  }

  private func unifiedFailure(
    error: Error,
    defaultStatus: DesktopNativeStatus,
    defaultRecoverySuggestion: String
  ) -> [String: Any] {
    let nsError = error as NSError
    let statusName = nsError.userInfo[RecordingErrorKey.status] as? String
    let status = DesktopNativeStatus(rawValue: statusName ?? "") ?? defaultStatus
    return unifiedFailure(
      status: status,
      message: nsError.localizedDescription,
      recoverySuggestion: nsError.userInfo[RecordingErrorKey.recoverySuggestion] as? String
        ?? defaultRecoverySuggestion,
      domain: nsError.domain,
      nativeCode: nsError.userInfo[RecordingErrorKey.nativeCode] as? String ?? "\(nsError.code)",
      recoverability: nsError.userInfo[RecordingErrorKey.recoverability] as? String ?? "unknown",
      artifactPath: nsError.userInfo[RecordingErrorKey.artifactPath] as? String ?? ""
    )
  }
}

@available(macOS 12.3, *)
final class NativeScreenRecorder: NSObject, SCStreamOutput, SCStreamDelegate, @unchecked Sendable {
  private let sampleQueue = DispatchQueue(label: "desktop_screen_recorder.recording.samples")
  private var stream: SCStream?
  private var writer: AVAssetWriter?
  private var videoInput: AVAssetWriterInput?
  private var systemAudioInput: AVAssetWriterInput?
  private var microphoneInput: AVAssetWriterInput?
  private var outputURL: URL?
  private var outputSize = CGSize(width: 0, height: 0)
  private var config: RecordingConfiguration?
  private var startedAt: Date?
  private var pausedDuration: TimeInterval = 0
  private var pauseStartedAtDate: Date?
  private var pendingPauseOffset = CMTime.zero
  private var sampleTimeOffset = CMTime.zero
  private var didStartSession = false
  private var didWriteVideoSample = false
  private var isRecording = false
  private var isPaused = false
  private var isStopping = false
  private var terminalError: Error?
  private var overlayController: RecordingEnhancementOverlayController?

  func start(
    config: RecordingConfiguration,
    completion: @escaping (Result<Void, Error>) -> Void
  ) {
    guard config.format == "mp4" || config.format == "gif" else {
      completion(.failure(nativeError(
        "The requested recording format is not supported.",
        status: .invalidInput,
        recoverySuggestion: "Use mp4 or gif for macOS recording.",
        nativeCode: "unsupportedFormat"
      )))
      return
    }

    guard config.mode == "fullscreen" || config.mode == "region" || config.mode == "window" else {
      completion(.failure(nativeError(
        "The requested recording mode is not supported.",
        status: .invalidInput,
        recoverySuggestion: "Use fullscreen, region, or window mode.",
        nativeCode: "unsupportedMode"
      )))
      return
    }

    if config.mode == "region",
       (config.hasInvalidRegionInput || !recordingRectHasUsableSize(config.region)) {
      completion(.failure(nativeError(
        "A valid target region is required for region recording.",
        status: .invalidInput,
        recoverySuggestion: "Select a visible recording region and try again.",
        nativeCode: "invalidRegion"
      )))
      return
    }

    if config.mode == "window", (config.windowId ?? 0) <= 0 {
      completion(.failure(nativeError(
        "A target window is required for window recording.",
        status: .invalidInput,
        recoverySuggestion: "Select a window before starting window recording.",
        nativeCode: "missingWindow"
      )))
      return
    }

    guard CGPreflightScreenCaptureAccess() else {
      completion(.failure(nativeError(
        "Screen recording permission is required.",
        status: .permissionDenied,
        recoverySuggestion: "Grant screen recording permission in System Settings and try again.",
        nativeCode: "screenPermission",
        recoverability: "userActionRequired"
      )))
      return
    }

    if shouldCaptureSystemAudio(config), !ProcessInfo.processInfo.isOperatingSystemAtLeast(
      OperatingSystemVersion(majorVersion: 13, minorVersion: 0, patchVersion: 0)
    ) {
      completion(.failure(nativeError(
        "System audio capture requires macOS 13 or later.",
        status: .unsupported,
        recoverySuggestion: "Run the app on macOS 13 or later, or disable system audio.",
        nativeCode: "systemAudioUnsupported"
      )))
      return
    }

    if shouldCaptureMicrophone(config) {
      let authorization = AVCaptureDevice.authorizationStatus(for: .audio)
      if authorization != .authorized {
        completion(.failure(nativeError(
          "Microphone permission is required.",
          status: .permissionDenied,
          recoverySuggestion: "Grant microphone permission and try again.",
          nativeCode: "microphonePermission",
          recoverability: "userActionRequired"
        )))
        return
      }

      if !ProcessInfo.processInfo.isOperatingSystemAtLeast(
        OperatingSystemVersion(majorVersion: 15, minorVersion: 0, patchVersion: 0)
      ) {
        completion(.failure(nativeError(
          "Microphone capture requires macOS 15 or later.",
          status: .unsupported,
          recoverySuggestion: "Run the app on macOS 15 or later, or disable microphone capture.",
          nativeCode: "microphoneUnsupported"
        )))
        return
      }
    }

    if shouldUseCameraOverlay(config) {
      let authorization = AVCaptureDevice.authorizationStatus(for: .video)
      if authorization == .denied || authorization == .restricted || authorization == .notDetermined {
        completion(.failure(nativeError(
          "Camera permission is required for camera overlay.",
          status: .permissionDenied,
          recoverySuggestion: "Grant camera permission and try again.",
          nativeCode: "cameraPermission",
          recoverability: "userActionRequired"
        )))
        return
      }

      if AVCaptureDevice.default(for: .video) == nil {
        completion(.failure(nativeError(
          "No camera device is available for camera overlay.",
          status: .notConfigured,
          recoverySuggestion: "Connect a camera or disable camera overlay.",
          nativeCode: "cameraUnavailable",
          recoverability: "userActionRequired"
        )))
        return
      }
    }

    if config.showKeystrokes, config.mode != "window", !AXIsProcessTrusted() {
      completion(.failure(nativeError(
        "Accessibility permission is required for keystroke overlay.",
        status: .permissionDenied,
        recoverySuggestion: "Grant accessibility permission and try again.",
        nativeCode: "accessibilityPermission",
        recoverability: "userActionRequired"
      )))
      return
    }

    sampleQueue.sync {
      resetRuntimeState(removeOutputFile: false)
      self.config = config
    }

    Task { [weak self] in
      guard let self else { return }
      do {
        let content = try await SCShareableContent.excludingDesktopWindows(
          false,
          onScreenWindowsOnly: true
        )
        guard !content.displays.isEmpty else {
          completion(.failure(nativeError(
            "No capturable displays were found.",
            status: .notConfigured,
            recoverySuggestion: "Connect or enable a display and try again.",
            nativeCode: "noDisplays"
          )))
          return
        }

        let target = try self.makeRecordingTarget(content: content, config: config)
        let streamConfiguration = self.makeStreamConfiguration(target: target, config: config)
        let filter = target.makeFilter()

        let outputURL = try self.makeOutputURL()
        try self.sampleQueue.sync {
          self.outputURL = outputURL
          try self.configureWriter(url: outputURL, size: self.outputSize, config: config)
        }

        let stream = SCStream(filter: filter, configuration: streamConfiguration, delegate: self)
        try stream.addStreamOutput(self, type: .screen, sampleHandlerQueue: self.sampleQueue)

        if self.shouldCaptureSystemAudio(config), #available(macOS 13.0, *) {
          try stream.addStreamOutput(self, type: .audio, sampleHandlerQueue: self.sampleQueue)
        }

        if self.shouldCaptureMicrophone(config), #available(macOS 15.0, *) {
          try stream.addStreamOutput(self, type: .microphone, sampleHandlerQueue: self.sampleQueue)
        }

        try await stream.startCapture()

        self.sampleQueue.sync {
          self.stream = stream
          self.startedAt = Date()
          self.isRecording = true
          self.isPaused = false
          self.isStopping = false
        }

        if target.supportsOverlay && self.shouldStartOverlay(config) {
          DispatchQueue.main.async {
            let overlay = RecordingEnhancementOverlayController()
            overlay.start(
              config: config,
              targetFrame: target.frame
            )
            self.overlayController = overlay
          }
        }

        completion(.success(()))
      } catch {
        self.sampleQueue.async {
          self.cancelWriter(removeOutputFile: true)
          self.resetRuntimeState(removeOutputFile: true)
        }
        completion(.failure(error))
      }
    }
  }

  func pause(completion: @escaping (Bool) -> Void) {
    sampleQueue.async {
      guard self.isRecording, !self.isPaused else {
        completion(false)
        return
      }

      self.isPaused = true
      self.pauseStartedAtDate = Date()
      DispatchQueue.main.async {
        self.overlayController?.setPaused(true)
      }
      completion(true)
    }
  }

  func resume(completion: @escaping (Bool) -> Void) {
    sampleQueue.async {
      guard self.isRecording, self.isPaused else {
        completion(false)
        return
      }

      self.isPaused = false
      if let pauseStartedAtDate = self.pauseStartedAtDate {
        let pausedInterval = Date().timeIntervalSince(pauseStartedAtDate)
        self.pausedDuration += pausedInterval
        self.pendingPauseOffset = CMTimeAdd(
          self.pendingPauseOffset,
          CMTime(seconds: pausedInterval, preferredTimescale: 1_000_000)
        )
      }
      self.pauseStartedAtDate = nil
      DispatchQueue.main.async {
        self.overlayController?.setPaused(false)
      }
      completion(true)
    }
  }

  func stop(completion: @escaping (Result<[String: Any], Error>) -> Void) {
    sampleQueue.async {
      if let terminalError = self.terminalError {
        self.cancelWriter(removeOutputFile: true)
        self.resetRuntimeState(removeOutputFile: true)
        completion(.failure(terminalError))
        return
      }

      guard self.isRecording else {
        completion(.failure(nativeError(
          "No recording is active.",
          status: .invalidInput,
          recoverySuggestion: "Start a recording before stopping.",
          nativeCode: "noActiveRecording"
        )))
        return
      }

      self.finishPausedDurationIfNeeded()
      self.isRecording = false
      self.isStopping = true
      DispatchQueue.main.async {
        self.overlayController?.stop()
        self.overlayController = nil
      }

      guard let stream = self.stream else {
        self.cancelWriter(removeOutputFile: true)
        self.resetRuntimeState(removeOutputFile: true)
        completion(.failure(nativeError(
          "Recording stream was not created.",
          status: .nativeFailure,
          recoverySuggestion: "Try the recording again.",
          nativeCode: "missingStream"
        )))
        return
      }

      stream.stopCapture { stopError in
        self.sampleQueue.async {
          if let stopError {
            self.cancelWriter(removeOutputFile: true)
            self.resetRuntimeState(removeOutputFile: true)
            completion(.failure(stopError))
            return
          }

          guard let writer = self.writer else {
            self.resetRuntimeState(removeOutputFile: true)
            completion(.failure(nativeError(
              "Recording writer was not created.",
              status: .nativeFailure,
              recoverySuggestion: "Try the recording again.",
              nativeCode: "missingWriter"
            )))
            return
          }

          self.videoInput?.markAsFinished()
          self.systemAudioInput?.markAsFinished()
          self.microphoneInput?.markAsFinished()

          if writer.status == .unknown || !self.didWriteVideoSample {
            self.cancelWriter(removeOutputFile: true)
            self.resetRuntimeState(removeOutputFile: true)
            completion(.failure(nativeError(
              "No frames were written.",
              status: .nativeFailure,
              recoverySuggestion: "Keep the target visible and try recording again.",
              nativeCode: "noFrames"
            )))
            return
          }

          if writer.status == .failed || writer.status == .cancelled {
            let error = writer.error ?? nativeError(
              "Recording writer stopped before finalizing.",
              status: .ioFailure,
              recoverySuggestion: "Try the recording again.",
              nativeCode: "writerStopped"
            )
            self.cancelWriter(removeOutputFile: true)
            self.resetRuntimeState(removeOutputFile: true)
            completion(.failure(error))
            return
          }

          writer.finishWriting {
            self.sampleQueue.async {
              if let error = writer.error {
                self.cancelWriter(removeOutputFile: true)
                self.resetRuntimeState(removeOutputFile: true)
                completion(.failure(error))
                return
              }

              let thumbnail = self.thumbnailData(url: self.outputURL)
              if let config = self.config, config.format == "gif" {
                do {
                  let originalOutputURL = self.outputURL
                  let transcode = try RecordingGIFTranscoder.transcode(
                    inputURL: originalOutputURL,
                    config: config
                  )
                  if let originalOutputURL {
                    try? FileManager.default.removeItem(at: originalOutputURL)
                  }
                  self.outputURL = transcode.url
                  self.outputSize = transcode.size
                  guard self.fileSizeBytes(url: self.outputURL) > 0 else {
                    self.resetRuntimeState(removeOutputFile: true)
                    completion(.failure(nativeError(
                      "The GIF output file is empty.",
                      status: .ioFailure,
                      recoverySuggestion: "Try the recording again.",
                      nativeCode: "emptyGif"
                    )))
                    return
                  }
                  let result = self.makeResult(thumbnail: thumbnail)
                  self.resetRuntimeState(removeOutputFile: false)
                  completion(.success(result))
                } catch {
                  self.resetRuntimeState(removeOutputFile: true)
                  completion(.failure(error))
                }
                return
              }

              guard self.fileSizeBytes(url: self.outputURL) > 0 else {
                self.resetRuntimeState(removeOutputFile: true)
                completion(.failure(nativeError(
                  "The recording output file is empty.",
                  status: .ioFailure,
                  recoverySuggestion: "Try the recording again.",
                  nativeCode: "emptyOutput"
                )))
                return
              }
              let result = self.makeResult(thumbnail: thumbnail)
              self.resetRuntimeState(removeOutputFile: false)
              completion(.success(result))
            }
          }
        }
      }
    }
  }

  func stream(_ stream: SCStream, didStopWithError error: Error) {
    sampleQueue.async {
      guard !self.isStopping else { return }
      self.cancelWriter(removeOutputFile: true)
      self.isRecording = false
      self.terminalError = error
      DispatchQueue.main.async {
        self.overlayController?.stop()
        self.overlayController = nil
      }
    }
  }

  func stream(
    _ stream: SCStream,
    didOutputSampleBuffer sampleBuffer: CMSampleBuffer,
    of type: SCStreamOutputType
  ) {
    guard isRecording, CMSampleBufferIsValid(sampleBuffer) else { return }
    if type == .screen, CMSampleBufferGetImageBuffer(sampleBuffer) == nil { return }

    if isPaused {
      return
    }

    if pendingPauseOffset != .zero {
      sampleTimeOffset = CMTimeAdd(
        sampleTimeOffset,
        pendingPauseOffset
      )
      pendingPauseOffset = .zero
    }

    guard let adjustedSample = copySample(sampleBuffer, subtracting: sampleTimeOffset) else {
      return
    }

    if type == .screen {
      if !didStartSession {
        let adjustedPresentationTime = CMSampleBufferGetPresentationTimeStamp(adjustedSample)
        guard startWritingSession(at: adjustedPresentationTime) else { return }
      }
      if append(adjustedSample, to: videoInput) {
        didWriteVideoSample = true
      }
    } else if #available(macOS 13.0, *), type == .audio {
      guard didStartSession else { return }
      append(adjustedSample, to: systemAudioInput)
    } else if #available(macOS 15.0, *), type == .microphone {
      guard didStartSession else { return }
      append(adjustedSample, to: microphoneInput)
    }
  }

  private func startWritingSession(at sourceTime: CMTime) -> Bool {
    guard let writer else {
      failRecording(nativeError(
        "Recording writer was not created.",
        status: .nativeFailure,
        recoverySuggestion: "Try the recording again.",
        nativeCode: "missingWriter"
      ))
      return false
    }
    guard sourceTime.isValid else { return false }
    guard writer.startWriting() else {
      failRecording(writer.error ?? nativeError(
        "Recording writer could not start.",
        status: .ioFailure,
        recoverySuggestion: "Try the recording again.",
        nativeCode: "writerStartFailed"
      ))
      return false
    }
    writer.startSession(atSourceTime: sourceTime)
    didStartSession = true
    return true
  }

  @discardableResult
  private func append(_ sampleBuffer: CMSampleBuffer, to input: AVAssetWriterInput?) -> Bool {
    guard let writer else { return false }
    guard writer.status == .writing else { return false }
    guard let input, input.isReadyForMoreMediaData else { return false }
    if !input.append(sampleBuffer) {
      failRecording(writer.error ?? nativeError(
        "A recording sample could not be written.",
        status: .ioFailure,
        recoverySuggestion: "Try the recording again.",
        nativeCode: "sampleAppendFailed"
      ))
      return false
    }
    return true
  }

  private func shouldCaptureSystemAudio(_ config: RecordingConfiguration) -> Bool {
    config.captureAudio && config.format != "gif"
  }

  private func shouldCaptureMicrophone(_ config: RecordingConfiguration) -> Bool {
    config.captureMicrophone && config.format != "gif"
  }

  private func shouldUseCameraOverlay(_ config: RecordingConfiguration) -> Bool {
    config.cameraOverlay.enabled && config.mode != "window"
  }

  private func shouldStartOverlay(_ config: RecordingConfiguration) -> Bool {
    config.mode != "window"
      && (config.highlightClicks || config.showKeystrokes || shouldUseCameraOverlay(config))
  }

  private func finishPausedDurationIfNeeded() {
    if let pauseStartedAtDate {
      pausedDuration += Date().timeIntervalSince(pauseStartedAtDate)
    }
    pauseStartedAtDate = nil
    pendingPauseOffset = .zero
    isPaused = false
  }

  private func failRecording(_ error: Error) {
    guard terminalError == nil else { return }
    terminalError = error
    isRecording = false
    isPaused = false
    if !isStopping {
      isStopping = true
      stream?.stopCapture { _ in }
    }
    cancelWriter(removeOutputFile: true)
    DispatchQueue.main.async {
      self.overlayController?.stop()
      self.overlayController = nil
    }
  }

  private func cancelWriter(removeOutputFile: Bool) {
    writer?.cancelWriting()
    if removeOutputFile, let outputURL {
      try? FileManager.default.removeItem(at: outputURL)
    }
  }

  private func resetRuntimeState(removeOutputFile: Bool) {
    if removeOutputFile, let outputURL {
      try? FileManager.default.removeItem(at: outputURL)
    }
    stream = nil
    writer = nil
    videoInput = nil
    systemAudioInput = nil
    microphoneInput = nil
    outputURL = nil
    outputSize = CGSize(width: 0, height: 0)
    config = nil
    startedAt = nil
    pausedDuration = 0
    pauseStartedAtDate = nil
    pendingPauseOffset = .zero
    sampleTimeOffset = .zero
    didStartSession = false
    didWriteVideoSample = false
    isRecording = false
    isPaused = false
    isStopping = false
    terminalError = nil
  }

  private func copySample(
    _ sampleBuffer: CMSampleBuffer,
    subtracting offset: CMTime
  ) -> CMSampleBuffer? {
    if offset == .zero || !offset.isValid { return sampleBuffer }

    var timingCount: CMItemCount = 0
    let countStatus = CMSampleBufferGetSampleTimingInfoArray(
      sampleBuffer,
      entryCount: 0,
      arrayToFill: nil,
      entriesNeededOut: &timingCount
    )
    guard countStatus == noErr, timingCount > 0 else { return nil }

    var timing = Array(
      repeating: CMSampleTimingInfo(
        duration: .invalid,
        presentationTimeStamp: .invalid,
        decodeTimeStamp: .invalid
      ),
      count: timingCount
    )

    let timingStatus = timing.withUnsafeMutableBufferPointer { buffer in
      CMSampleBufferGetSampleTimingInfoArray(
        sampleBuffer,
        entryCount: timingCount,
        arrayToFill: buffer.baseAddress,
        entriesNeededOut: nil
      )
    }
    guard timingStatus == noErr else { return nil }

    for index in timing.indices {
      guard timing[index].presentationTimeStamp.isValid else { return nil }
      timing[index].presentationTimeStamp = CMTimeSubtract(
        timing[index].presentationTimeStamp,
        offset
      )
      if timing[index].decodeTimeStamp.isValid {
        timing[index].decodeTimeStamp = CMTimeSubtract(
          timing[index].decodeTimeStamp,
          offset
        )
      }
    }

    var adjustedSample: CMSampleBuffer?
    let createStatus = timing.withUnsafeMutableBufferPointer { buffer in
      CMSampleBufferCreateCopyWithNewTiming(
        allocator: kCFAllocatorDefault,
        sampleBuffer: sampleBuffer,
        sampleTimingEntryCount: timingCount,
        sampleTimingArray: buffer.baseAddress,
        sampleBufferOut: &adjustedSample
      )
    }
    guard createStatus == noErr else { return nil }
    return adjustedSample
  }

  private enum RecordingTarget {
    case display(display: SCDisplay, sourceRect: CGRect, overlayFrame: CGRect)
    case window(window: SCWindow)

    var captureRect: CGRect {
      switch self {
      case .display(_, let sourceRect, _):
        return sourceRect
      case .window(let window):
        return CGRect(
          x: 0,
          y: 0,
          width: max(2, window.frame.width),
          height: max(2, window.frame.height)
        )
      }
    }

    var frame: CGRect {
      switch self {
      case .display(_, _, let overlayFrame):
        return overlayFrame
      case .window(let window):
        return window.frame
      }
    }

    var displaySourceRect: CGRect? {
      switch self {
      case .display(_, let sourceRect, _):
        return sourceRect
      case .window:
        return nil
      }
    }

    var supportsOverlay: Bool {
      switch self {
      case .display:
        return true
      case .window:
        return false
      }
    }

    func makeFilter() -> SCContentFilter {
      switch self {
      case .display(let display, _, _):
        return SCContentFilter(display: display, excludingWindows: [])
      case .window(let window):
        return SCContentFilter(desktopIndependentWindow: window)
      }
    }
  }

  private func makeRecordingTarget(
    content: SCShareableContent,
    config: RecordingConfiguration
  ) throws -> RecordingTarget {
    if config.mode == "window" {
      guard let windowId = config.windowId else {
        throw nativeError(
          "A target window is required for window recording.",
          status: .invalidInput,
          recoverySuggestion: "Select a window before starting window recording.",
          nativeCode: "missingWindow"
        )
      }
      guard let window = content.windows.first(where: { Int($0.windowID) == windowId }) else {
        throw nativeError(
          "The selected window is no longer available.",
          status: .invalidInput,
          recoverySuggestion: "Select an open window and try again.",
          nativeCode: "windowUnavailable"
        )
      }
      guard window.frame.width > 1, window.frame.height > 1 else {
        throw nativeError(
          "The selected window has an invalid size.",
          status: .invalidInput,
          recoverySuggestion: "Select a visible window and try again.",
          nativeCode: "invalidWindowSize"
        )
      }
      return .window(window: window)
    }

    let display = selectDisplay(
      from: content.displays,
      displayId: config.displayId,
      region: config.region
    )
    let sourceRect = try resolvedSourceRect(display: display, region: config.region)
    let overlayFrame: CGRect
    if let region = config.region, region.width > 0, region.height > 0 {
      let clippedRegion = region.intersection(display.frame)
      guard !clippedRegion.isNull, clippedRegion.width > 1, clippedRegion.height > 1 else {
        throw nativeError(
          "The selected recording region is outside the target display.",
          status: .invalidInput,
          recoverySuggestion: "Select a region inside a visible display and try again.",
          nativeCode: "regionOutsideDisplay"
        )
      }
      overlayFrame = clippedRegion
    } else {
      overlayFrame = display.frame
    }
    return .display(
      display: display,
      sourceRect: sourceRect,
      overlayFrame: overlayFrame
    )
  }

  private func selectDisplay(
    from displays: [SCDisplay],
    displayId: Int?,
    region: CGRect?
  ) -> SCDisplay {
    if let displayId,
       let display = displays.first(where: { Int($0.displayID) == displayId }) {
      return display
    }

    if let region {
      let center = CGPoint(x: region.midX, y: region.midY)
      if let display = displays.first(where: { $0.frame.contains(center) }) {
        return display
      }
    }

    let mainDisplayId = CGMainDisplayID()
    return displays.first(where: { $0.displayID == mainDisplayId }) ?? displays[0]
  }

  private func makeStreamConfiguration(
    target: RecordingTarget,
    config: RecordingConfiguration
  ) -> SCStreamConfiguration {
    let streamConfiguration = SCStreamConfiguration()
    let outputSize = scaledOutputSize(
      sourceRect: target.captureRect,
      maxHeight: config.qualityMaxHeight
    )
    self.outputSize = outputSize

    streamConfiguration.width = Int(outputSize.width)
    streamConfiguration.height = Int(outputSize.height)
    streamConfiguration.minimumFrameInterval = CMTime(
      value: 1,
      timescale: CMTimeScale(config.frameRate)
    )
    streamConfiguration.pixelFormat = kCVPixelFormatType_32BGRA
    streamConfiguration.showsCursor = config.showCursor
    if let sourceRect = target.displaySourceRect {
      streamConfiguration.sourceRect = sourceRect
    }
    streamConfiguration.queueDepth = 6

    if #available(macOS 13.0, *) {
      streamConfiguration.capturesAudio = shouldCaptureSystemAudio(config)
      streamConfiguration.sampleRate = 48000
      streamConfiguration.channelCount = 2
      streamConfiguration.excludesCurrentProcessAudio = false
    }

    if #available(macOS 15.0, *) {
      streamConfiguration.showMouseClicks = false
      streamConfiguration.captureMicrophone = shouldCaptureMicrophone(config)
    }

    return streamConfiguration
  }

  private func resolvedSourceRect(display: SCDisplay, region: CGRect?) throws -> CGRect {
    guard let region, region.width > 0, region.height > 0 else {
      return CGRect(x: 0, y: 0, width: display.width, height: display.height)
    }

    let displayFrame = display.frame
    let clippedRegion = region.intersection(displayFrame)
    guard !clippedRegion.isNull, clippedRegion.width > 1, clippedRegion.height > 1 else {
      throw nativeError(
        "The selected recording region is outside the target display.",
        status: .invalidInput,
        recoverySuggestion: "Select a region inside a visible display and try again.",
        nativeCode: "regionOutsideDisplay"
      )
    }
    let normalized = CGRect(
      x: clippedRegion.minX - displayFrame.minX,
      y: clippedRegion.minY - displayFrame.minY,
      width: clippedRegion.width,
      height: clippedRegion.height
    )
    return normalized.integral
  }

  private func scaledOutputSize(sourceRect: CGRect, maxHeight: Int) -> CGSize {
    let width = max(2, sourceRect.width)
    let height = max(2, sourceRect.height)
    guard maxHeight > 0, height > CGFloat(maxHeight) else {
      return CGSize(width: even(width), height: even(height))
    }

    let scale = CGFloat(maxHeight) / height
    return CGSize(width: even(width * scale), height: even(height * scale))
  }

  private func even(_ value: CGFloat) -> CGFloat {
    let rounded = Int(value.rounded())
    return CGFloat(rounded.isMultiple(of: 2) ? rounded : rounded + 1)
  }

  private func configureWriter(
    url: URL,
    size: CGSize,
    config: RecordingConfiguration
  ) throws {
    let writer = try AVAssetWriter(outputURL: url, fileType: .mp4)
    writer.shouldOptimizeForNetworkUse = true

    let videoSettings: [String: Any] = [
      AVVideoCodecKey: AVVideoCodecType.h264,
      AVVideoWidthKey: Int(size.width),
      AVVideoHeightKey: Int(size.height),
      AVVideoCompressionPropertiesKey: [
        AVVideoAverageBitRateKey: config.videoBitrate,
        AVVideoExpectedSourceFrameRateKey: config.frameRate,
        AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel
      ]
    ]

    let videoInput = AVAssetWriterInput(mediaType: .video, outputSettings: videoSettings)
    videoInput.expectsMediaDataInRealTime = true
    guard writer.canAdd(videoInput) else {
      throw nativeError(
        "Video writer input could not be added.",
        status: .nativeFailure,
        recoverySuggestion: "Try a lower resolution or frame rate.",
        nativeCode: "videoWriterInput"
      )
    }
    writer.add(videoInput)

    let audioSettings: [String: Any] = [
      AVFormatIDKey: kAudioFormatMPEG4AAC,
      AVSampleRateKey: 48000,
      AVNumberOfChannelsKey: 2,
      AVEncoderBitRateKey: 128000
    ]

    if shouldCaptureSystemAudio(config) {
      let input = AVAssetWriterInput(mediaType: .audio, outputSettings: audioSettings)
      input.expectsMediaDataInRealTime = true
      guard writer.canAdd(input) else {
        throw nativeError(
          "System audio writer input could not be added.",
          status: .nativeFailure,
          recoverySuggestion: "Disable system audio or try the recording again.",
          nativeCode: "systemAudioWriterInput"
        )
      }
      writer.add(input)
      systemAudioInput = input
    }

    if shouldCaptureMicrophone(config) {
      let input = AVAssetWriterInput(mediaType: .audio, outputSettings: audioSettings)
      input.expectsMediaDataInRealTime = true
      guard writer.canAdd(input) else {
        throw nativeError(
          "Microphone writer input could not be added.",
          status: .nativeFailure,
          recoverySuggestion: "Disable microphone capture or try the recording again.",
          nativeCode: "microphoneWriterInput"
        )
      }
      writer.add(input)
      microphoneInput = input
    }

    self.writer = writer
    self.videoInput = videoInput
  }

  private func makeOutputURL() throws -> URL {
    let directory = FileManager.default.urls(for: .moviesDirectory, in: .userDomainMask).first
      ?? FileManager.default.temporaryDirectory
    do {
      try FileManager.default.createDirectory(
        at: directory,
        withIntermediateDirectories: true
      )
    } catch {
      throw nativeError(
        "Recording output directory could not be prepared.",
        status: .ioFailure,
        recoverySuggestion: "Choose a writable Movies directory and try again.",
        nativeCode: "outputDirectory"
      )
    }
    let formatter = DateFormatter()
    formatter.dateFormat = "yyyyMMdd_HHmmss_SSS"
    let suffix = String(UUID().uuidString.prefix(8))
    let fileName = "GoodCleanShot_\(formatter.string(from: Date()))_\(suffix).mp4"
    let url = directory.appendingPathComponent(fileName)
    if FileManager.default.fileExists(atPath: url.path) {
      try FileManager.default.removeItem(at: url)
    }
    return url
  }

  private func makeResult(thumbnail: FlutterStandardTypedData?) -> [String: Any] {
    let url = outputURL
    let fileSize = fileSizeBytes(url: url)
    let elapsed = max(0, (startedAt.map { Date().timeIntervalSince($0) } ?? 0) - pausedDuration)
    let format = config?.format ?? "mp4"
    return [
      "filePath": url?.path ?? "",
      "durationMs": Int(elapsed * 1000),
      "format": format,
      "width": Int(outputSize.width),
      "height": Int(outputSize.height),
      "fileSizeBytes": fileSize,
      "thumbnail": thumbnail as Any,
      "effectiveCapabilities": effectiveCapabilities(),
      "recoveryEvents": recoveryEvents()
    ]
  }

  private func effectiveCapabilities() -> [String: Any] {
    guard let config else {
      return [
        "systemAudio": false,
        "microphone": false,
        "cursor": false,
        "clickHighlight": false,
        "keystrokes": false,
        "cameraOverlay": false
      ]
    }
    let isGif = config.format == "gif"
    let systemAudioEnabled: Bool
    if #available(macOS 13.0, *) {
      systemAudioEnabled = config.captureAudio && !isGif
    } else {
      systemAudioEnabled = false
    }
    let microphoneEnabled: Bool
    if #available(macOS 15.0, *) {
      microphoneEnabled = config.captureMicrophone && !isGif
    } else {
      microphoneEnabled = false
    }
    return [
      "systemAudio": systemAudioEnabled,
      "microphone": microphoneEnabled,
      "cursor": config.showCursor,
      "clickHighlight": config.mode != "window" && config.highlightClicks,
      "keystrokes": config.mode != "window" && config.showKeystrokes,
      "cameraOverlay": shouldUseCameraOverlay(config)
    ]
  }

  private func recoveryEvents() -> [String] {
    guard let config, config.mode == "window" else { return [] }
    var events: [String] = []
    if config.highlightClicks || config.showKeystrokes || config.cameraOverlay.enabled {
      events.append("Enhancement overlay is disabled for window capture.")
    }
    return events
  }

  private func fileSizeBytes(url: URL?) -> Int {
    guard let url,
          let attributes = try? FileManager.default.attributesOfItem(atPath: url.path),
          let size = attributes[.size] as? NSNumber else {
      return 0
    }
    return size.intValue
  }

  private func thumbnailData(url: URL?) -> FlutterStandardTypedData? {
    guard let url else { return nil }
    let asset = AVAsset(url: url)
    let generator = AVAssetImageGenerator(asset: asset)
    generator.appliesPreferredTrackTransform = true

    guard let cgImage = try? generator.copyCGImage(at: .zero, actualTime: nil) else {
      return nil
    }

    let image = NSImage(cgImage: cgImage, size: .zero)
    guard let tiff = image.tiffRepresentation,
          let bitmap = NSBitmapImageRep(data: tiff),
          let png = bitmap.representation(using: .png, properties: [:]) else {
      return nil
    }
    return FlutterStandardTypedData(bytes: png)
  }
}

private struct RecordingGIFTranscodeResult {
  let url: URL
  let size: CGSize
}

private enum RecordingGIFTranscoder {
  static func transcode(
    inputURL: URL?,
    config: RecordingConfiguration
  ) throws -> RecordingGIFTranscodeResult {
    guard let inputURL else {
      throw nativeError(
        "The GIF source video was not created.",
        status: .ioFailure,
        recoverySuggestion: "Try the recording again.",
        nativeCode: "missingGifSource"
      )
    }

    let asset = AVAsset(url: inputURL)
    let durationSeconds = asset.duration.seconds
    guard durationSeconds.isFinite, durationSeconds > 0 else {
      throw nativeError(
        "The GIF source video has no duration.",
        status: .nativeFailure,
        recoverySuggestion: "Record for longer and try again.",
        nativeCode: "emptyGifSource"
      )
    }

    let generator = AVAssetImageGenerator(asset: asset)
    generator.appliesPreferredTrackTransform = true
    generator.requestedTimeToleranceBefore = .zero
    generator.requestedTimeToleranceAfter = .zero

    let frameRate = max(1, config.gifFrameRate)
    let frameCount = max(1, min(3600, Int(ceil(durationSeconds * Double(frameRate)))))
    let firstImage = try generator.copyCGImage(at: .zero, actualTime: nil)
    let outputSize = scaledSize(
      source: CGSize(width: firstImage.width, height: firstImage.height),
      maxWidth: config.gifMaxWidth
    )

    let outputURL = try makeOutputURL()
    var didFinalizeOutput = false
    defer {
      if !didFinalizeOutput {
        try? FileManager.default.removeItem(at: outputURL)
      }
    }
    guard let destination = CGImageDestinationCreateWithURL(
      outputURL as CFURL,
      "com.compuserve.gif" as CFString,
      frameCount,
      nil
    ) else {
      throw nativeError(
        "The GIF destination could not be created.",
        status: .ioFailure,
        recoverySuggestion: "Choose a writable output directory and try again.",
        nativeCode: "gifDestination"
      )
    }

    let fileProperties: [String: Any] = [
      kCGImagePropertyGIFDictionary as String: [
        kCGImagePropertyGIFLoopCount as String: 0,
        kCGImagePropertyGIFHasGlobalColorMap as String: config.gifOptimizePalette,
      ]
    ]
    CGImageDestinationSetProperties(destination, fileProperties as CFDictionary)

    let frameDelay = 1.0 / Double(frameRate)
    let frameProperties: [String: Any] = [
      kCGImagePropertyGIFDictionary as String: [
        kCGImagePropertyGIFDelayTime as String: frameDelay,
        kCGImagePropertyGIFUnclampedDelayTime as String: frameDelay,
      ]
    ]

    for index in 0..<frameCount {
      let seconds = min(durationSeconds, Double(index) / Double(frameRate))
      let time = CMTime(seconds: seconds, preferredTimescale: 600)
      let image = (try? generator.copyCGImage(at: time, actualTime: nil)) ?? firstImage
      let scaled = scaledImage(image, size: outputSize) ?? image
      CGImageDestinationAddImage(destination, scaled, frameProperties as CFDictionary)
    }

    guard CGImageDestinationFinalize(destination) else {
      throw nativeError(
        "The GIF transcode could not be finalized.",
        status: .ioFailure,
        recoverySuggestion: "Try the recording again.",
        nativeCode: "gifFinalize"
      )
    }

    didFinalizeOutput = true
    return RecordingGIFTranscodeResult(url: outputURL, size: outputSize)
  }

  private static func makeOutputURL() throws -> URL {
    let directory = FileManager.default.urls(for: .moviesDirectory, in: .userDomainMask).first
      ?? FileManager.default.temporaryDirectory
    do {
      try FileManager.default.createDirectory(
        at: directory,
        withIntermediateDirectories: true
      )
    } catch {
      throw nativeError(
        "GIF output directory could not be prepared.",
        status: .ioFailure,
        recoverySuggestion: "Choose a writable Movies directory and try again.",
        nativeCode: "gifOutputDirectory"
      )
    }
    let formatter = DateFormatter()
    formatter.dateFormat = "yyyyMMdd_HHmmss_SSS"
    let suffix = String(UUID().uuidString.prefix(8))
    let url = directory.appendingPathComponent(
      "GoodCleanShot_\(formatter.string(from: Date()))_\(suffix).gif"
    )
    if FileManager.default.fileExists(atPath: url.path) {
      try FileManager.default.removeItem(at: url)
    }
    return url
  }

  private static func scaledSize(source: CGSize, maxWidth: Int) -> CGSize {
    let width = max(1, source.width)
    let height = max(1, source.height)
    guard width > CGFloat(maxWidth) else {
      return CGSize(width: even(width), height: even(height))
    }

    let scale = CGFloat(maxWidth) / width
    return CGSize(width: even(width * scale), height: even(height * scale))
  }

  private static func scaledImage(_ image: CGImage, size: CGSize) -> CGImage? {
    let width = Int(size.width)
    let height = Int(size.height)
    if image.width == width, image.height == height {
      return image
    }

    let colorSpace = CGColorSpaceCreateDeviceRGB()
    guard let context = CGContext(
      data: nil,
      width: width,
      height: height,
      bitsPerComponent: 8,
      bytesPerRow: 0,
      space: colorSpace,
      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue
    ) else {
      return nil
    }

    context.interpolationQuality = .medium
    context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
    return context.makeImage()
  }

  private static func even(_ value: CGFloat) -> CGFloat {
    let rounded = Int(value.rounded())
    return CGFloat(rounded.isMultiple(of: 2) ? rounded : rounded + 1)
  }
}


func nativeError(
  _ message: String,
  status: DesktopNativeStatus = .nativeFailure,
  recoverySuggestion: String = "Try the operation again.",
  nativeCode: String = "",
  recoverability: String = "unknown",
  artifactPath: String = ""
) -> NSError {
  NSError(
    domain: "DesktopScreenRecorderMacos",
    code: 1,
    userInfo: [
      NSLocalizedDescriptionKey: message,
      RecordingErrorKey.status: status.rawValue,
      RecordingErrorKey.recoverySuggestion: recoverySuggestion,
      RecordingErrorKey.nativeCode: nativeCode,
      RecordingErrorKey.recoverability: recoverability,
      RecordingErrorKey.artifactPath: artifactPath,
    ]
  )
}
