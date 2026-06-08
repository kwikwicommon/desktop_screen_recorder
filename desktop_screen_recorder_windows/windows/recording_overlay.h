#ifndef FLUTTER_PLUGIN_DESKTOP_SCREEN_RECORDER_RECORDING_OVERLAY_H_
#define FLUTTER_PLUGIN_DESKTOP_SCREEN_RECORDER_RECORDING_OVERLAY_H_

#include "windows_native_bridge_internal.h"

#include <memory>
#include <string>
#include <vector>

class RecordingOverlayRendererImpl;

class RecordingOverlayRenderer {
 public:
  RecordingOverlayRenderer();
  ~RecordingOverlayRenderer();

  RecordingOverlayRenderer(const RecordingOverlayRenderer&) = delete;
  RecordingOverlayRenderer& operator=(const RecordingOverlayRenderer&) = delete;

  void Initialize(const RecordingConfig& config,
                  std::vector<std::string>* recovery_events);
  bool camera_active() const;
  void Shutdown();
  void Apply(BitmapFrame* frame, const RECT& capture_rect);

 private:
  std::unique_ptr<RecordingOverlayRendererImpl> impl_;
};

#endif  // FLUTTER_PLUGIN_DESKTOP_SCREEN_RECORDER_RECORDING_OVERLAY_H_
