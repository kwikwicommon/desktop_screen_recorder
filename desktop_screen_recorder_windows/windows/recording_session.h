#ifndef FLUTTER_PLUGIN_DESKTOP_SCREEN_RECORDER_RECORDING_SESSION_H_
#define FLUTTER_PLUGIN_DESKTOP_SCREEN_RECORDER_RECORDING_SESSION_H_

#include "windows_native_bridge.h"
#include "windows_native_bridge_internal.h"

using RecordingSessionPtr =
    std::unique_ptr<RecordingSession, RecordingSessionDeleter>;

RecordingSessionPtr CreateRecordingSession(RecordingConfig config);
bool StartRecordingSession(RecordingSession* session, std::string* error);
void PauseRecordingSession(RecordingSession* session);
void ResumeRecordingSession(RecordingSession* session);
EncodableValue StopRecordingSession(RecordingSession* session);

#endif  // FLUTTER_PLUGIN_DESKTOP_SCREEN_RECORDER_RECORDING_SESSION_H_
