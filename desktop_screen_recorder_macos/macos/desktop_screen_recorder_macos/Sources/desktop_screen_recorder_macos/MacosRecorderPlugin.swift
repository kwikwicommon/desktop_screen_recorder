import AVFoundation
import ApplicationServices
import Cocoa
import CoreGraphics
import FlutterMacOS

public final class MacosRecorderPlugin: NSObject, FlutterPlugin {
  private let recordingBridge = NativeRecordingBridge()

  public static func register(with registrar: FlutterPluginRegistrar) {
    let channel = FlutterMethodChannel(
      name: "desktop_screen_recorder/native",
      binaryMessenger: registrar.messenger
    )
    let instance = MacosRecorderPlugin()
    registrar.addMethodCallDelegate(instance, channel: channel)
  }

  public func handle(
    _ call: FlutterMethodCall,
    result: @escaping FlutterResult
  ) {
    switch call.method {
    case "record.start":
      recordingBridge.startUnified(arguments: call.arguments, result: result)
    case "record.pause":
      recordingBridge.pauseUnified(result: result)
    case "record.resume":
      recordingBridge.resumeUnified(result: result)
    case "record.stop":
      recordingBridge.stopUnified(result: result)
    case "record.listAudioInputs":
      result(success(payload: AudioDeviceReader.inputDevices()))
    case "record.listAudioOutputs":
      result(success(payload: AudioDeviceReader.outputDevices()))
    case "capability.queryPermissions", "capability.queryCapabilities", "capability.queryFeatureAvailability":
      result(success(payload: capabilitySnapshot()))
    case "capability.requestPermission":
      requestPermission(arguments: call.arguments, result: result)
    default:
      result(FlutterMethodNotImplemented)
    }
  }

  private func requestPermission(
    arguments: Any?,
    result: @escaping FlutterResult
  ) {
    let args = arguments as? [String: Any]
    let type = args?["type"] as? String ?? ""
    switch type {
    case "screenRecording":
      let granted = CGRequestScreenCaptureAccess()
      result(success(payload: permissionInfo(
        type: "screenRecording",
        state: granted ? "granted" : "denied",
        message: granted ? "" : "Screen recording permission is required."
      )))
    case "microphone":
      AVCaptureDevice.requestAccess(for: .audio) { granted in
        DispatchQueue.main.async {
          result(self.success(payload: self.permissionInfo(
            type: "microphone",
            state: granted ? "granted" : "denied",
            message: granted ? "" : "Microphone permission is required."
          )))
        }
      }
    case "camera":
      AVCaptureDevice.requestAccess(for: .video) { granted in
        DispatchQueue.main.async {
          result(self.success(payload: self.permissionInfo(
            type: "camera",
            state: granted ? "granted" : "denied",
            message: granted ? "" : "Camera permission is required."
          )))
        }
      }
    case "systemAudio":
      result(success(payload: permissionInfo(
        type: "systemAudio",
        state: isSystemAudioSupported ? "granted" : "unsupported",
        message: isSystemAudioSupported
          ? ""
          : "System audio capture requires macOS 13 or later."
      )))
    case "accessibility":
      let granted = AXIsProcessTrusted()
      if !granted,
         let url = URL(
           string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"
         ) {
        NSWorkspace.shared.open(url)
      }
      result(success(payload: permissionInfo(
        type: "accessibility",
        state: granted ? "granted" : "notDetermined",
        message: granted ? "" : "Accessibility permission is required."
      )))
    default:
      result(failure(
        status: .unsupported,
        message: "The requested permission is not supported.",
        recoverySuggestion: "Request a supported desktop recorder permission."
      ))
    }
  }

  private var isSystemAudioSupported: Bool {
    ProcessInfo.processInfo.isOperatingSystemAtLeast(
      OperatingSystemVersion(majorVersion: 13, minorVersion: 0, patchVersion: 0)
    )
  }

  private var isMicrophoneCaptureSupported: Bool {
    ProcessInfo.processInfo.isOperatingSystemAtLeast(
      OperatingSystemVersion(majorVersion: 15, minorVersion: 0, patchVersion: 0)
    )
  }

  private func capabilitySnapshot() -> [String: Any] {
    let screenState = CGPreflightScreenCaptureAccess() ? "granted" : "denied"
    let microphoneState = permissionState(
      AVCaptureDevice.authorizationStatus(for: .audio)
    )
    let cameraState = permissionState(
      AVCaptureDevice.authorizationStatus(for: .video)
    )
    let accessibilityState = AXIsProcessTrusted() ? "granted" : "notDetermined"

    let displayStatus = screenState == "granted" ? "available" : "permissionRequired"
    let microphoneStatus: String
    if !isMicrophoneCaptureSupported {
      microphoneStatus = "unsupported"
    } else if microphoneState == "granted" {
      microphoneStatus = "available"
    } else {
      microphoneStatus = "permissionRequired"
    }
    let cameraStatus = cameraState == "granted" ? "available" : "permissionRequired"

    return [
      "permissions": [
        permissionInfo(
          type: "screenRecording",
          state: screenState,
          message: screenState == "granted"
            ? ""
            : "Screen recording permission is required."
        ),
        permissionInfo(
          type: "microphone",
          state: microphoneState,
          message: microphoneState == "granted"
            ? ""
            : "Microphone permission is required."
        ),
        permissionInfo(
          type: "camera",
          state: cameraState,
          message: cameraState == "granted"
            ? ""
            : "Camera permission is required."
        ),
        permissionInfo(
          type: "systemAudio",
          state: isSystemAudioSupported ? "granted" : "unsupported",
          message: isSystemAudioSupported
            ? ""
            : "System audio capture requires macOS 13 or later."
        ),
        permissionInfo(
          type: "accessibility",
          state: accessibilityState,
          message: accessibilityState == "granted"
            ? ""
            : "Accessibility permission is required for keystroke overlay."
        ),
      ],
      "features": [
        featureInfo(
          feature: "recordDisplay",
          status: displayStatus,
          message: displayStatus == "available"
            ? ""
            : "Screen recording permission is required.",
          permissionType: "screenRecording"
        ),
        featureInfo(
          feature: "recordRegion",
          status: displayStatus,
          message: displayStatus == "available"
            ? ""
            : "Screen recording permission is required.",
          permissionType: "screenRecording"
        ),
        featureInfo(
          feature: "recordWindow",
          status: displayStatus,
          message: displayStatus == "available"
            ? ""
            : "Screen recording permission is required.",
          permissionType: "screenRecording"
        ),
        featureInfo(
          feature: "recordSystemAudio",
          status: isSystemAudioSupported ? "available" : "unsupported",
          message: isSystemAudioSupported
            ? ""
            : "System audio capture requires macOS 13 or later.",
          permissionType: "systemAudio"
        ),
        featureInfo(
          feature: "recordMicrophone",
          status: microphoneStatus,
          message: microphoneStatus == "available"
            ? ""
            : "Microphone capture requires permission and macOS 15 or later.",
          permissionType: "microphone"
        ),
        featureInfo(
          feature: "recordCameraOverlay",
          status: cameraStatus,
          message: cameraStatus == "available"
            ? ""
            : "Camera permission is required for camera overlay.",
          permissionType: "camera"
        ),
      ],
    ]
  }

  private func permissionState(_ status: AVAuthorizationStatus) -> String {
    switch status {
    case .authorized:
      return "granted"
    case .denied:
      return "denied"
    case .restricted:
      return "restricted"
    case .notDetermined:
      return "notDetermined"
    @unknown default:
      return "unsupported"
    }
  }

  private func permissionInfo(
    type: String,
    state: String,
    message: String
  ) -> [String: Any] {
    [
      "type": type,
      "state": state,
      "message": message,
      "requiresRestart": false,
    ]
  }

  private func featureInfo(
    feature: String,
    status: String,
    message: String,
    permissionType: String?
  ) -> [String: Any] {
    var value: [String: Any] = [
      "feature": feature,
      "status": status,
      "message": message,
    ]
    if let permissionType {
      value["permissionType"] = permissionType
    }
    return value
  }

  private func success(payload: Any? = nil) -> [String: Any] {
    var response: [String: Any] = ["status": DesktopNativeStatus.success.rawValue]
    if let payload {
      response["payload"] = payload
    }
    return response
  }

  private func failure(
    status: DesktopNativeStatus,
    message: String,
    recoverySuggestion: String,
    nativeCode: String = ""
  ) -> [String: Any] {
    [
      "status": status.rawValue,
      "failure": [
        "message": message,
        "recoverySuggestion": recoverySuggestion,
        "requiresRestart": false,
        "domain": status.rawValue,
        "nativeCode": nativeCode,
        "recoverability": "unknown",
        "artifactPath": "",
      ],
    ]
  }
}
