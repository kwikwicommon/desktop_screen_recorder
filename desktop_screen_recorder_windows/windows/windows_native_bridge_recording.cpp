#include "recording_session.h"
#include "windows_native_bridge_internal.h"

EncodableValue WindowsNativeBridge::HandleRecordingMethod(
    const std::string& method,
    const EncodableMap* arguments) {
  if (method == "record.listAudioInputs") {
    return ListAudioDevices(true);
  }
  if (method == "record.listAudioOutputs") {
    return ListAudioDevices(false);
  }

  if (method == "record.start") {
    std::lock_guard<std::mutex> lock(recording_mutex_);
    if (recording_session_ != nullptr) {
      return MakeFailure("invalidInput", "A recording is already active.",
                         "Stop the current recording before starting another.");
    }
    if (recording_stopping_) {
      return MakeFailure("invalidInput",
                         "The previous recording is still stopping.",
                         "Wait for recording cleanup to finish before starting another.");
    }
    const RecordingConfig config = ReadRecordingConfig(arguments);
    if (config.format == "gif" &&
        (config.capture_audio || config.capture_microphone)) {
      return MakeFailure(
          "invalidInput",
          "GIF recording does not support system audio or microphone capture.",
          "Disable audio capture or switch the output format to MP4.");
    }
    auto session = CreateRecordingSession(config);
    std::string error;
    if (!StartRecordingSession(session.get(), &error)) {
      session.reset();
      return MakeFailure("nativeFailure",
                         error.empty() ? "Windows recording could not start."
                                       : error,
                         "Try recording a smaller area or lower frame rate.");
    }
    recording_session_ = std::move(session);
    return MakeSuccess();
  }
  if (method == "record.pause") {
    std::lock_guard<std::mutex> lock(recording_mutex_);
    if (recording_stopping_) {
      return MakeFailure("invalidInput",
                         "The recording is already stopping.",
                         "Wait for the stop operation to complete.");
    }
    if (recording_session_ == nullptr) {
      return MakeFailure("invalidInput", "No recording is active.",
                         "Start recording before pausing.");
    }
    PauseRecordingSession(recording_session_.get());
    return MakeSuccess();
  }
  if (method == "record.resume") {
    std::lock_guard<std::mutex> lock(recording_mutex_);
    if (recording_stopping_) {
      return MakeFailure("invalidInput",
                         "The recording is already stopping.",
                         "Wait for the stop operation to complete.");
    }
    if (recording_session_ == nullptr) {
      return MakeFailure("invalidInput", "No recording is active.",
                         "Start recording before resuming.");
    }
    ResumeRecordingSession(recording_session_.get());
    return MakeSuccess();
  }
  if (method == "record.stop") {
    RecordingSessionPtr session;
    {
      std::lock_guard<std::mutex> lock(recording_mutex_);
      if (recording_stopping_) {
        return MakeFailure("invalidInput",
                           "The recording is already stopping.",
                           "Wait for the stop operation to complete.");
      }
      if (recording_session_ == nullptr) {
        return MakeFailure("invalidInput", "No recording is active.",
                           "Start recording before stopping.");
      }
      recording_stopping_ = true;
      session = std::move(recording_session_);
    }

    try {
      EncodableValue result = StopRecordingSession(session.get());
      session.reset();
      {
        std::lock_guard<std::mutex> lock(recording_mutex_);
        recording_stopping_ = false;
      }
      return result;
    } catch (...) {
      {
        std::lock_guard<std::mutex> lock(recording_mutex_);
        recording_stopping_ = false;
      }
      throw;
    }
  }
  return MakeFailure("unsupported",
                     "This recording method is not implemented on Windows.",
                     "Use the shared Dart contract and handle unsupported responses.");
}
