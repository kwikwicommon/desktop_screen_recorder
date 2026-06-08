#ifndef FLUTTER_PLUGIN_DESKTOP_SCREEN_RECORDER_GIF_ENCODER_H_
#define FLUTTER_PLUGIN_DESKTOP_SCREEN_RECORDER_GIF_ENCODER_H_

#include "windows_native_bridge_internal.h"

#include <windows.h>

#include <memory>
#include <string>

class AnimatedGifEncoderImpl;

class AnimatedGifEncoder {
 public:
  AnimatedGifEncoder();
  ~AnimatedGifEncoder();

  AnimatedGifEncoder(const AnimatedGifEncoder&) = delete;
  AnimatedGifEncoder& operator=(const AnimatedGifEncoder&) = delete;

  HRESULT Initialize(const std::wstring& path,
                     int width,
                     int height,
                     int frame_rate,
                     bool optimize_palette);
  HRESULT WriteFrame(const BitmapFrame& frame);
  HRESULT Finalize();

 private:
  std::unique_ptr<AnimatedGifEncoderImpl> impl_;
};

#endif  // FLUTTER_PLUGIN_DESKTOP_SCREEN_RECORDER_GIF_ENCODER_H_
