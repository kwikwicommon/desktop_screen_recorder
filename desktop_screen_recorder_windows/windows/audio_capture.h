#ifndef FLUTTER_PLUGIN_DESKTOP_SCREEN_RECORDER_AUDIO_CAPTURE_H_
#define FLUTTER_PLUGIN_DESKTOP_SCREEN_RECORDER_AUDIO_CAPTURE_H_

#include "windows_native_bridge_internal.h"

#include <mfreadwrite.h>

#include <memory>
#include <string>
#include <vector>

class AudioMixerImpl;

class AudioMixer {
 public:
  AudioMixer();
  ~AudioMixer();

  AudioMixer(const AudioMixer&) = delete;
  AudioMixer& operator=(const AudioMixer&) = delete;

  bool Initialize(const RecordingConfig& config,
                  std::vector<std::string>* recovery_events);
  bool system_audio_active() const;
  bool microphone_active() const;
  HRESULT WriteUntil(IMFSinkWriter* writer,
                     DWORD stream_index,
                     LONGLONG target_time,
                     std::vector<std::string>* recovery_events);
  void DiscardPendingAudio(std::vector<std::string>* recovery_events);
  void Stop();

 private:
  std::unique_ptr<AudioMixerImpl> impl_;
};

#endif  // FLUTTER_PLUGIN_DESKTOP_SCREEN_RECORDER_AUDIO_CAPTURE_H_
