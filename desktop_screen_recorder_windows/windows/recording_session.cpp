#include "recording_session.h"

#include "audio_capture.h"
#include "gif_encoder.h"
#include "recording_overlay.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;

class RecordingSession {
 public:
  explicit RecordingSession(RecordingConfig config)
      : config_(config), output_path_(RecordingOutputPath(config.format)) {}

  ~RecordingSession() {
    if (worker_.joinable()) {
      stop_requested_ = true;
      worker_.join();
    }
  }

  bool Start(std::string* error) {
    if (config_.format != "mp4" && config_.format != "gif") {
      if (error != nullptr) {
        *error = "Windows native recording supports MP4 and GIF output.";
      }
      return false;
    }

    if (!ResolveRecordingRect(config_, &capture_rect_, error)) {
      return false;
    }

    width_ = capture_rect_.right - capture_rect_.left;
    height_ = capture_rect_.bottom - capture_rect_.top;
    if (width_ < 2 || height_ < 2) {
      if (error != nullptr) {
        *error = "The recording area must be at least 2 physical pixels wide and high.";
      }
      return false;
    }
    width_ -= width_ % 2;
    height_ -= height_ % 2;
    if (width_ < 2 || height_ < 2) {
      if (error != nullptr) {
        *error = "The recording area is too small after encoder alignment.";
      }
      return false;
    }
    capture_rect_.right = capture_rect_.left + width_;
    capture_rect_.bottom = capture_rect_.top + height_;
    encoded_width_ = width_;
    encoded_height_ = height_;
    if (config_.format == "mp4" && config_.quality_max_height > 0 &&
        height_ > config_.quality_max_height) {
      const double scale =
          static_cast<double>(config_.quality_max_height) / height_;
      encoded_width_ = EvenDimension(width_ * scale);
      encoded_height_ = EvenDimension(config_.quality_max_height);
    }

    worker_ = std::thread(&RecordingSession::CaptureLoop, this);

    std::unique_lock<std::mutex> lock(state_mutex_);
    ready_cv_.wait(lock, [this] { return ready_; });
    if (FAILED(startup_hr_)) {
      if (error != nullptr) {
        *error = startup_error_;
      }
      lock.unlock();
      stop_requested_ = true;
      if (worker_.joinable()) {
        worker_.join();
      }
      return false;
    }
    return true;
  }

  void Pause() { paused_ = true; }

  void Resume() { paused_ = false; }

  EncodableValue Stop() {
    stop_requested_ = true;
    if (worker_.joinable()) {
      worker_.join();
    }

    if (FAILED(final_hr_)) {
      return MakeFailure("nativeFailure",
                         final_error_.empty()
                             ? "Windows recording failed."
                             : final_error_,
                         "Try recording a smaller area or lower frame rate.",
                         HResultToString(final_hr_));
    }

    EncodableMap payload;
    payload[EncodableValue("filePath")] =
        EncodableValue(WideToUtf8(output_path_));
    payload[EncodableValue("durationMs")] =
        EncodableValue(static_cast<int64_t>(duration_ms_));
    payload[EncodableValue("format")] = EncodableValue(config_.format);
    payload[EncodableValue("width")] = EncodableValue(encoded_width_);
    payload[EncodableValue("height")] = EncodableValue(encoded_height_);
    payload[EncodableValue("fileSizeBytes")] =
        EncodableValue(static_cast<int64_t>(file_size_));
    payload[EncodableValue("requested")] = EncodableValue(RequestedMetadata());
    payload[EncodableValue("actual")] = EncodableValue(ActualMetadata());
    payload[EncodableValue("effectiveCapabilities")] =
        EncodableValue(EffectiveCapabilities());
    payload[EncodableValue("completedEarly")] =
        EncodableValue(completed_early_);
    if (!completion_reason_.empty()) {
      payload[EncodableValue("completionReason")] =
          EncodableValue(completion_reason_);
    }
    if (!completion_suggestion_.empty()) {
      payload[EncodableValue("completionSuggestion")] =
          EncodableValue(completion_suggestion_);
    }
    if (!recovery_events_.empty()) {
      EncodableList events;
      for (const auto& event : recovery_events_) {
        events.emplace_back(EncodableValue(event));
      }
      payload[EncodableValue("recoveryEvents")] = EncodableValue(events);
    }
    return MakeSuccess(EncodableValue(payload));
  }

 private:
  void SignalReady(HRESULT hr, const std::string& error) {
    {
      std::lock_guard<std::mutex> lock(state_mutex_);
      startup_hr_ = hr;
      startup_error_ = error;
      ready_ = true;
    }
    ready_cv_.notify_one();
  }

  EncodableMap RequestedMetadata() const {
    EncodableMap requested;
    requested[EncodableValue("format")] = EncodableValue(config_.format);
    requested[EncodableValue("mode")] = EncodableValue(config_.mode);
    requested[EncodableValue("systemAudio")] =
        EncodableValue(config_.capture_audio);
    requested[EncodableValue("microphone")] =
        EncodableValue(config_.capture_microphone);
    requested[EncodableValue("cameraOverlay")] =
        EncodableValue(config_.camera_overlay.enabled);
    requested[EncodableValue("windowTarget")] =
        EncodableValue(config_.mode == "window" && config_.window_id > 0);
    requested[EncodableValue("gifAudio")] = EncodableValue(
        config_.format == "gif" &&
        (config_.capture_audio || config_.capture_microphone));
    return requested;
  }

  EncodableMap ActualMetadata() const {
    EncodableMap actual;
    actual[EncodableValue("format")] = EncodableValue(config_.format);
    actual[EncodableValue("mode")] = EncodableValue(config_.mode);
    actual[EncodableValue("systemAudio")] =
        EncodableValue(actual_system_audio_);
    actual[EncodableValue("microphone")] =
        EncodableValue(actual_microphone_);
    actual[EncodableValue("cameraOverlay")] =
        EncodableValue(actual_camera_overlay_);
    actual[EncodableValue("windowTarget")] = EncodableValue(
        config_.mode == "window" && !completed_early_);
    actual[EncodableValue("gifAudio")] = EncodableValue(false);
    actual[EncodableValue("gifAudioIgnored")] =
        EncodableValue(gif_audio_ignored_);
    return actual;
  }

  EncodableMap EffectiveCapabilities() const {
    EncodableMap capabilities;
    capabilities[EncodableValue("systemAudio")] =
        EncodableValue(actual_system_audio_);
    capabilities[EncodableValue("microphone")] =
        EncodableValue(actual_microphone_);
    capabilities[EncodableValue("cursor")] =
        EncodableValue(config_.show_cursor);
    capabilities[EncodableValue("clickHighlight")] =
        EncodableValue(config_.highlight_clicks);
    capabilities[EncodableValue("keystrokes")] =
        EncodableValue(config_.show_keystrokes);
    capabilities[EncodableValue("cameraOverlay")] =
        EncodableValue(actual_camera_overlay_);
    return capabilities;
  }

  void MarkCompletedEarly(const std::string& reason,
                          const std::string& suggestion) {
    completed_early_ = true;
    if (completion_reason_.empty()) {
      completion_reason_ = reason;
      completion_suggestion_ = suggestion;
    }
    recovery_events_.push_back(reason + " " + suggestion);
  }

  bool RefreshCaptureTarget() {
    if (config_.mode == "window") {
      HWND window = reinterpret_cast<HWND>(
          static_cast<intptr_t>(config_.window_id));
      if (window == nullptr || !IsWindow(window)) {
        MarkCompletedEarly(
            "The target window closed during recording.",
            "The saved file contains the frames captured before the window closed; select the window again or use region recording.");
        return false;
      }

      RECT window_rect = {};
      if (!WindowVisibleCaptureBounds(window, &window_rect)) {
        MarkCompletedEarly(
            "The target window became unavailable during recording.",
            "The saved file contains the frames captured before the target changed; select the window again or use region recording.");
        return false;
      }
      const int window_width = window_rect.right - window_rect.left;
      const int window_height = window_rect.bottom - window_rect.top;
      if (window_width < 2 || window_height < 2) {
        MarkCompletedEarly(
            "The target window became too small to record.",
            "The saved file contains the frames captured before the resize; restore the window and record again.");
        return false;
      }
      if ((window_width != width_ || window_height != height_) &&
          !window_resize_reported_) {
        window_resize_reported_ = true;
        recovery_events_.push_back(
            "The target window size changed; captured window frames are scaled to the original encoder frame.");
      }
      capture_rect_ = window_rect;
      return true;
    }

    RECT visible = {};
    if (IntersectRects(capture_rect_, VirtualScreenBounds(), &visible) &&
        visible.right - visible.left >= width_ &&
        visible.bottom - visible.top >= height_) {
      return true;
    }

    RECT refreshed = {};
    std::string error;
    if (!ResolveRecordingRect(config_, &refreshed, &error)) {
      MarkCompletedEarly(
          "The recording display became unavailable.",
          "The saved file contains the frames captured before the display changed; choose an available display and record again.");
      return false;
    }
    const int refreshed_width = refreshed.right - refreshed.left;
    const int refreshed_height = refreshed.bottom - refreshed.top;
    if (refreshed_width < width_ || refreshed_height < height_) {
      MarkCompletedEarly(
          "The recording display changed to a smaller frame.",
          "The saved file contains the frames captured before the display changed; choose a smaller region and record again.");
      return false;
    }
    capture_rect_.left = refreshed.left;
    capture_rect_.top = refreshed.top;
    capture_rect_.right = capture_rect_.left + width_;
    capture_rect_.bottom = capture_rect_.top + height_;
    recovery_events_.push_back(
        "The recording display changed; capture continued on the refreshed desktop target.");
    return true;
  }

  bool CaptureFrame(BitmapFrame* frame, std::string* error) {
    if (config_.mode == "window") {
      HWND window = reinterpret_cast<HWND>(
          static_cast<intptr_t>(config_.window_id));
      if (window == nullptr || !IsWindow(window)) {
        if (error != nullptr) {
          *error = "The target window is no longer available.";
        }
        return false;
      }
      RECT window_rect = {};
      if (!WindowVisibleCaptureBounds(window, &window_rect)) {
        if (error != nullptr) {
          *error = "The target window is not currently capturable.";
        }
        return false;
      }
      capture_rect_ = window_rect;
      return CaptureWindowBgra(window, frame, error);
    }
    return CaptureRectBgra(capture_rect_, frame, error);
  }

  void CaptureGifFrames() {
    if (config_.capture_audio || config_.capture_microphone) {
      gif_audio_ignored_ = true;
      recovery_events_.push_back(
          "GIF output does not carry audio; requested audio sources were ignored.");
    }

    encoded_width_ = width_;
    encoded_height_ = height_;
    if (config_.gif.max_width > 0 && width_ > config_.gif.max_width) {
      encoded_width_ = config_.gif.max_width;
      encoded_height_ = std::max(
          1, static_cast<int>(std::lround(
                 static_cast<double>(height_) * encoded_width_ / width_)));
    }

    AnimatedGifEncoder encoder;
    HRESULT hr = encoder.Initialize(output_path_, encoded_width_,
                                    encoded_height_, config_.gif.frame_rate,
                                    config_.gif.optimize_palette);
    if (FAILED(hr)) {
      final_hr_ = hr;
      final_error_ = "The Windows GIF encoder could not be opened.";
      SignalReady(hr, final_error_);
      return;
    }

    RecordingOverlayRenderer overlay_renderer;
    overlay_renderer.Initialize(config_, &recovery_events_);
    actual_camera_overlay_ = overlay_renderer.camera_active();

    SignalReady(S_OK, "");

    const LONGLONG sample_duration =
        kMediaFoundationSecond / config_.gif.frame_rate;
    const auto frame_interval =
        std::chrono::microseconds(1000000 / config_.gif.frame_rate);
    auto next_frame_time = std::chrono::steady_clock::now();
    LONGLONG sample_time = 0;
    int64_t frame_count = 0;

    while (!stop_requested_) {
      if (paused_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        next_frame_time = std::chrono::steady_clock::now() + frame_interval;
        continue;
      }

      if (!RefreshCaptureTarget()) {
        break;
      }

      BitmapFrame frame;
      std::string capture_error;
      if (!CaptureFrame(&frame, &capture_error)) {
        recovery_events_.push_back(
            "Frame capture failed; finalizing the GIF captured so far.");
        if (frame_count == 0) {
          final_hr_ = E_FAIL;
          final_error_ = capture_error;
        }
        break;
      }

      overlay_renderer.Apply(&frame, capture_rect_);
      BitmapFrame output_frame = frame;
      if (output_frame.width != encoded_width_ ||
          output_frame.height != encoded_height_) {
        output_frame =
            ResizeFrameToDimensions(frame, encoded_width_, encoded_height_);
      }
      hr = encoder.WriteFrame(output_frame);
      if (FAILED(hr)) {
        final_hr_ = hr;
        final_error_ = "A captured frame could not be encoded into GIF.";
        recovery_events_.push_back("GIF encoding failed while writing a frame.");
        break;
      }

      sample_time += sample_duration;
      ++frame_count;
      next_frame_time += frame_interval;
      std::this_thread::sleep_until(next_frame_time);
    }

    if (SUCCEEDED(final_hr_)) {
      hr = encoder.Finalize();
      if (FAILED(hr)) {
        final_hr_ = hr;
        final_error_ = "The GIF file could not be finalized.";
        recovery_events_.push_back(
            "Finalizing the GIF failed; the partial file may be unavailable.");
      }
    }

    duration_ms_ = sample_time / 10000;
    file_size_ = FileSizeForPath(output_path_);
    if (frame_count == 0 && SUCCEEDED(final_hr_)) {
      final_hr_ = E_FAIL;
      final_error_ = "Recording stopped before any frame was captured.";
    }
  }

  void CaptureLoop() {
    ScopedComInitialization com;
    if (!com.ok()) {
      final_hr_ = com.hr();
      final_error_ = "COM could not be initialized for Windows recording.";
      SignalReady(final_hr_, final_error_);
      return;
    }

    ScopedMediaFoundation media_foundation;
    HRESULT hr = media_foundation.Startup();
    if (FAILED(hr)) {
      final_hr_ = hr;
      final_error_ = "Media Foundation could not be initialized.";
      SignalReady(hr, final_error_);
      return;
    }

    if (config_.format == "gif") {
      CaptureGifFrames();
      return;
    }

    AudioMixer audio_mixer;
    bool include_audio = audio_mixer.Initialize(config_, &recovery_events_);
    actual_system_audio_ = audio_mixer.system_audio_active();
    actual_microphone_ = audio_mixer.microphone_active();

    ComPtr<IMFSinkWriter> writer;
    DWORD video_stream_index = 0;
    DWORD audio_stream_index = MF_SINK_WRITER_INVALID_STREAM_INDEX;
    hr = CreateRecordingSinkWriter(output_path_, encoded_width_, encoded_height_,
                                   config_.frame_rate, config_.video_bitrate,
                                   include_audio, writer.GetAddressOf(),
                                   &video_stream_index, &audio_stream_index);
    if (FAILED(hr) && include_audio) {
      recovery_events_.push_back(
          "Audio encoder setup failed; recording continues with video only.");
      audio_mixer.Stop();
      include_audio = false;
      actual_system_audio_ = false;
      actual_microphone_ = false;
      writer.Reset();
      video_stream_index = 0;
      audio_stream_index = MF_SINK_WRITER_INVALID_STREAM_INDEX;
      hr = CreateRecordingSinkWriter(output_path_, encoded_width_,
                                     encoded_height_, config_.frame_rate,
                                     config_.video_bitrate, false,
                                     writer.GetAddressOf(),
                                     &video_stream_index, &audio_stream_index);
    }
    if (FAILED(hr)) {
      final_hr_ = hr;
      final_error_ = "The Windows MP4 encoder could not be opened.";
      SignalReady(hr, final_error_);
      audio_mixer.Stop();
      return;
    }

    RecordingOverlayRenderer overlay_renderer;
    overlay_renderer.Initialize(config_, &recovery_events_);
    actual_camera_overlay_ = overlay_renderer.camera_active();

    SignalReady(S_OK, "");

    const LONGLONG sample_duration =
        kMediaFoundationSecond / config_.frame_rate;
    const auto frame_interval =
        std::chrono::microseconds(1000000 / config_.frame_rate);
    auto next_frame_time = std::chrono::steady_clock::now();
    LONGLONG sample_time = 0;
    int64_t frame_count = 0;

    while (!stop_requested_) {
      if (paused_) {
        audio_mixer.DiscardPendingAudio(&recovery_events_);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        next_frame_time = std::chrono::steady_clock::now() + frame_interval;
        continue;
      }

      if (!RefreshCaptureTarget()) {
        break;
      }

      BitmapFrame frame;
      std::string capture_error;
      if (!CaptureFrame(&frame, &capture_error)) {
        recovery_events_.push_back(
            "Frame capture failed; finalizing the recording captured so far.");
        if (frame_count == 0) {
          final_hr_ = E_FAIL;
          final_error_ = capture_error;
        }
        break;
      }

      overlay_renderer.Apply(&frame, capture_rect_);
      BitmapFrame encoded_frame;
      const BitmapFrame* frame_to_write = &frame;
      if (encoded_width_ != frame.width || encoded_height_ != frame.height) {
        encoded_frame =
            ResizeFrameToDimensions(frame, encoded_width_, encoded_height_);
        frame_to_write = &encoded_frame;
      }
      hr = WriteVideoFrame(writer.Get(), video_stream_index, *frame_to_write,
                           sample_time, sample_duration);
      if (FAILED(hr)) {
        recovery_events_.push_back(
            "Video encoding or disk output failed while writing a frame.");
        final_hr_ = hr;
        final_error_ = "A captured video frame could not be encoded.";
        break;
      }

      hr = audio_mixer.WriteUntil(writer.Get(), audio_stream_index,
                                  sample_time + sample_duration,
                                  &recovery_events_);
      if (FAILED(hr)) {
        recovery_events_.push_back(
            "Audio encoding or disk output failed while writing a sample.");
        final_hr_ = hr;
        final_error_ = "Captured audio could not be encoded into the MP4 file.";
        break;
      }

      sample_time += sample_duration;
      ++frame_count;
      next_frame_time += frame_interval;
      std::this_thread::sleep_until(next_frame_time);
    }

    if (SUCCEEDED(final_hr_)) {
      hr = writer->Finalize();
      if (FAILED(hr)) {
        recovery_events_.push_back(
            "Finalizing the MP4 file failed; the partial file may be unavailable.");
        final_hr_ = hr;
        final_error_ = "The MP4 file could not be finalized.";
      }
    }

    duration_ms_ = sample_time / 10000;
    writer.Reset();
    overlay_renderer.Shutdown();
    audio_mixer.Stop();
    file_size_ = FileSizeForPath(output_path_);
    if (frame_count == 0 && SUCCEEDED(final_hr_)) {
      final_hr_ = E_FAIL;
      final_error_ = "Recording stopped before any frame was captured.";
    }
  }

  RecordingConfig config_;
  RECT capture_rect_ = {};
  std::wstring output_path_;
  std::thread worker_;
  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> paused_{false};
  std::mutex state_mutex_;
  std::condition_variable ready_cv_;
  bool ready_ = false;
  HRESULT startup_hr_ = E_FAIL;
  HRESULT final_hr_ = S_OK;
  std::string startup_error_;
  std::string final_error_;
  std::vector<std::string> recovery_events_;
  int width_ = 0;
  int height_ = 0;
  int encoded_width_ = 0;
  int encoded_height_ = 0;
  int64_t duration_ms_ = 0;
  int64_t file_size_ = 0;
  bool actual_system_audio_ = false;
  bool actual_microphone_ = false;
  bool actual_camera_overlay_ = false;
  bool gif_audio_ignored_ = false;
  bool completed_early_ = false;
  bool window_resize_reported_ = false;
  std::string completion_reason_;
  std::string completion_suggestion_;
};

void RecordingSessionDeleter::operator()(RecordingSession* session) const {
  delete session;
}

RecordingSessionPtr CreateRecordingSession(RecordingConfig config) {
  return RecordingSessionPtr(new RecordingSession(std::move(config)));
}

bool StartRecordingSession(RecordingSession* session, std::string* error) {
  return session != nullptr && session->Start(error);
}

void PauseRecordingSession(RecordingSession* session) {
  if (session != nullptr) {
    session->Pause();
  }
}

void ResumeRecordingSession(RecordingSession* session) {
  if (session != nullptr) {
    session->Resume();
  }
}

EncodableValue StopRecordingSession(RecordingSession* session) {
  return session == nullptr
             ? MakeFailure("invalidInput", "No recording is active.",
                           "Start recording before stopping.")
             : session->Stop();
}
