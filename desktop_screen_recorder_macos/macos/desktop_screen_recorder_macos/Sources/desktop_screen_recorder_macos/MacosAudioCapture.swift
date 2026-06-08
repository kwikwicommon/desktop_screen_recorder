import AVFoundation
import CoreAudio
import Foundation

enum AudioDeviceReader {
  static func inputDevices() -> [[String: Any]] {
    let authorization = AVCaptureDevice.authorizationStatus(for: .audio)
    if authorization == .denied || authorization == .restricted {
      return [[
        "id": "microphone_unavailable",
        "name": "Microphone unavailable",
        "isInput": true,
        "isAvailable": false,
        "unavailableReason": "Microphone permission is denied."
      ]]
    }

    let devices = coreAudioInputDevices()
    if devices.isEmpty {
      return [[
        "id": "microphone_missing",
        "name": "No microphone devices",
        "isInput": true,
        "isAvailable": false,
        "unavailableReason": "No microphone input device was found."
      ]]
    }

    return devices
  }

  static func outputDevices() -> [[String: Any]] {
    guard ProcessInfo.processInfo.isOperatingSystemAtLeast(
      OperatingSystemVersion(majorVersion: 13, minorVersion: 0, patchVersion: 0)
    ) else {
      return [[
        "id": "system_audio_unsupported",
        "name": "System audio",
        "isInput": false,
        "isAvailable": false,
        "unavailableReason": "System audio capture requires macOS 13 or later."
      ]]
    }

    let devices = coreAudioOutputDevices()
    if devices.isEmpty {
      return [[
        "id": "system_audio_default",
        "name": "System audio",
        "isInput": false,
        "isAvailable": true
      ]]
    }
    return devices
  }

  private static func coreAudioInputDevices() -> [[String: Any]] {
    coreAudioDevices(
      scope: kAudioDevicePropertyScopeInput,
      fallbackName: "Audio input",
      fallbackPrefix: "audio_input",
      isInput: true
    )
  }

  private static func coreAudioOutputDevices() -> [[String: Any]] {
    coreAudioDevices(
      scope: kAudioDevicePropertyScopeOutput,
      fallbackName: "Audio output",
      fallbackPrefix: "audio_output",
      isInput: false
    )
  }

  private static func coreAudioDevices(
    scope: AudioObjectPropertyScope,
    fallbackName: String,
    fallbackPrefix: String,
    isInput: Bool
  ) -> [[String: Any]] {
    var address = AudioObjectPropertyAddress(
      mSelector: kAudioHardwarePropertyDevices,
      mScope: kAudioObjectPropertyScopeGlobal,
      mElement: kAudioObjectPropertyElementMain
    )
    var dataSize: UInt32 = 0
    let systemObject = AudioObjectID(kAudioObjectSystemObject)
    guard AudioObjectGetPropertyDataSize(systemObject, &address, 0, nil, &dataSize) == noErr else {
      return []
    }

    let deviceIdSize = UInt32(MemoryLayout<AudioDeviceID>.size)
    guard dataSize >= deviceIdSize,
          dataSize % deviceIdSize == 0 else {
      return []
    }

    let count = Int(dataSize / deviceIdSize)
    var deviceIds = [AudioDeviceID](repeating: 0, count: count)
    let readStatus = deviceIds.withUnsafeMutableBytes { buffer in
      guard let baseAddress = buffer.baseAddress else {
        return OSStatus(kAudioHardwareBadObjectError)
      }
      return AudioObjectGetPropertyData(systemObject, &address, 0, nil, &dataSize, baseAddress)
    }
    guard readStatus == noErr else {
      return []
    }

    return deviceIds.compactMap { deviceId in
      guard hasStreams(deviceId, scope: scope) else { return nil }
      return [
        "id": deviceUID(deviceId, fallbackPrefix: fallbackPrefix),
        "name": deviceName(deviceId, fallback: fallbackName),
        "isInput": isInput,
        "isAvailable": true
      ]
    }
  }

  private static func hasStreams(
    _ deviceId: AudioDeviceID,
    scope: AudioObjectPropertyScope
  ) -> Bool {
    var address = AudioObjectPropertyAddress(
      mSelector: kAudioDevicePropertyStreams,
      mScope: scope,
      mElement: kAudioObjectPropertyElementMain
    )
    var dataSize: UInt32 = 0
    return AudioObjectGetPropertyDataSize(deviceId, &address, 0, nil, &dataSize) == noErr
      && dataSize > 0
  }

  private static func deviceName(
    _ deviceId: AudioDeviceID,
    fallback: String
  ) -> String {
    stringProperty(
      deviceId,
      selector: kAudioObjectPropertyName,
      fallback: fallback
    )
  }

  private static func deviceUID(
    _ deviceId: AudioDeviceID,
    fallbackPrefix: String
  ) -> String {
    stringProperty(
      deviceId,
      selector: kAudioDevicePropertyDeviceUID,
      fallback: "\(fallbackPrefix)_\(deviceId)"
    )
  }

  private static func stringProperty(
    _ deviceId: AudioDeviceID,
    selector: AudioObjectPropertySelector,
    fallback: String
  ) -> String {
    var address = AudioObjectPropertyAddress(
      mSelector: selector,
      mScope: kAudioObjectPropertyScopeGlobal,
      mElement: kAudioObjectPropertyElementMain
    )
    var value: Unmanaged<CFString>?
    var dataSize = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
    let status = withUnsafeMutablePointer(to: &value) { pointer in
      AudioObjectGetPropertyData(deviceId, &address, 0, nil, &dataSize, pointer)
    }
    guard status == noErr, let value else {
      return fallback
    }
    return value.takeRetainedValue() as String
  }
}

final class MacosMicrophoneCapture {
  private let engine = AVAudioEngine()
  private let stateLock = NSLock()
  private var isRunning = false

  func start(
    bufferSize: AVAudioFrameCount = 1024,
    onBuffer: @escaping (AVAudioPCMBuffer, AVAudioTime) -> Void
  ) throws {
    try withLockedState {
      guard !isRunning else { return }
      try installTapAndStartEngine(bufferSize: bufferSize, onBuffer: onBuffer)
      isRunning = true
    }
  }

  func stop() {
    withLockedState {
      guard isRunning else { return }
      engine.inputNode.removeTap(onBus: 0)
      engine.stop()
      isRunning = false
    }
  }

  private func installTapAndStartEngine(
    bufferSize: AVAudioFrameCount,
    onBuffer: @escaping (AVAudioPCMBuffer, AVAudioTime) -> Void
  ) throws {
    let input = engine.inputNode
    let format = input.outputFormat(forBus: 0)
    guard format.sampleRate > 0, format.channelCount > 0 else {
      throw nativeError(
        "Microphone input format is not available.",
        status: .notConfigured,
        recoverySuggestion: "Select a valid microphone input and try again.",
        nativeCode: "microphoneFormat",
        recoverability: "userActionRequired"
      )
    }
    input.installTap(
      onBus: 0,
      bufferSize: bufferSize,
      format: format
    ) { buffer, time in
      onBuffer(buffer, time)
    }
    do {
      try engine.start()
    } catch {
      input.removeTap(onBus: 0)
      throw error
    }
  }

  private func withLockedState<T>(_ work: () throws -> T) rethrows -> T {
    stateLock.lock()
    defer { stateLock.unlock() }
    return try work()
  }
}
