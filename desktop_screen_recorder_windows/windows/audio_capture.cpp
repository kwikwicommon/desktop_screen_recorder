#include "audio_capture.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <propidl.h>
#include <propsys.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

constexpr size_t kMaxPendingAudioFrames = kOutputAudioSampleRate * 2;

PROPERTYKEY DeviceFriendlyNameKey() {
  PROPERTYKEY key = {};
  key.fmtid = {0xa45c254e,
               0xdf1c,
               0x4efd,
               {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}};
  key.pid = 14;
  return key;
}

float ReadWasapiSample(const BYTE* frame,
                       int channel,
                       int channel_count,
                       WasapiSampleFormat sample_format,
                       int bytes_per_sample) {
  const BYTE* sample = frame + channel * bytes_per_sample;
  switch (sample_format) {
    case WasapiSampleFormat::float32: {
      float value = 0.0f;
      std::memcpy(&value, sample, sizeof(float));
      return std::clamp(value, -1.0f, 1.0f);
    }
    case WasapiSampleFormat::pcm16: {
      int16_t value = 0;
      std::memcpy(&value, sample, sizeof(int16_t));
      return static_cast<float>(value) / 32768.0f;
    }
    case WasapiSampleFormat::pcm24: {
      int32_t value = (static_cast<int32_t>(sample[0]) |
                       (static_cast<int32_t>(sample[1]) << 8) |
                       (static_cast<int32_t>(sample[2]) << 16));
      if ((value & 0x00800000) != 0) {
        value |= static_cast<int32_t>(0xFF000000);
      }
      return static_cast<float>(value) / 8388608.0f;
    }
    case WasapiSampleFormat::pcm32: {
      int32_t value = 0;
      std::memcpy(&value, sample, sizeof(int32_t));
      return static_cast<float>(value) / 2147483648.0f;
    }
    case WasapiSampleFormat::unsupported:
      break;
  }
  return 0.0f;
}

std::string AudioSourceLabel(bool loopback) {
  return loopback ? "system audio" : "microphone";
}

class WasapiCaptureSource {
 public:
  WasapiCaptureSource(EDataFlow data_flow, bool loopback)
      : data_flow_(data_flow), loopback_(loopback) {}

  ~WasapiCaptureSource() { Stop(); }

  bool Initialize(std::string* message) {
    Stop();
    last_retry_at_ = std::chrono::steady_clock::now();
    HRESULT hr = Activate();
    if (FAILED(hr)) {
      if (message != nullptr) {
        *message = AudioSourceLabel(loopback_) +
                   " source is unavailable: " + HResultToString(hr);
      }
      Stop();
      return false;
    }
    return true;
  }

  void Stop() {
    if (audio_client_ != nullptr && started_) {
      audio_client_->Stop();
    }
    started_ = false;
    capture_client_.Reset();
    audio_client_.Reset();
    device_.Reset();
    if (mix_format_ != nullptr) {
      CoTaskMemFree(mix_format_);
      mix_format_ = nullptr;
    }
    pending_.clear();
    read_offset_ = 0;
    active_ = false;
  }

  void DiscardPendingAudio(std::vector<std::string>* recovery_events) {
    if (!active_) {
      MaybeReconnect(recovery_events);
      return;
    }
    const HRESULT hr = DrainPackets(recovery_events);
    if (FAILED(hr)) {
      Stop();
      MaybeReconnect(recovery_events);
      return;
    }
    pending_.clear();
    read_offset_ = 0;
  }

  size_t ReadFrames(size_t requested_frames,
                    std::vector<float>* output,
                    std::vector<std::string>* recovery_events) {
    output->assign(requested_frames * kOutputAudioChannelCount, 0.0f);
    if (!active_) {
      MaybeReconnect(recovery_events);
      return 0;
    }

    HRESULT hr = DrainPackets(recovery_events);
    if (FAILED(hr)) {
      const bool can_recover = hr == AUDCLNT_E_DEVICE_INVALIDATED ||
                               hr == AUDCLNT_E_SERVICE_NOT_RUNNING;
      if (recovery_events != nullptr) {
        recovery_events->push_back(
            AudioSourceLabel(loopback_) +
            (can_recover ? " disconnected; writing silence until it returns."
                         : " capture failed; writing silence."));
      }
      Stop();
      MaybeReconnect(recovery_events);
      return 0;
    }

    const size_t available_frames =
        (pending_.size() / kOutputAudioChannelCount) - read_offset_;
    const size_t frames_to_copy = std::min(requested_frames, available_frames);
    if (frames_to_copy == 0) {
      return 0;
    }

    const size_t source_offset = read_offset_ * kOutputAudioChannelCount;
    std::copy_n(pending_.begin() + static_cast<ptrdiff_t>(source_offset),
                frames_to_copy * kOutputAudioChannelCount, output->begin());
    read_offset_ += frames_to_copy;
    CompactPendingBuffer();
    return frames_to_copy;
  }

 private:
  HRESULT Activate() {
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                  CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr)) {
      return hr;
    }

    hr = enumerator->GetDefaultAudioEndpoint(data_flow_, eConsole, &device_);
    if (FAILED(hr)) {
      return hr;
    }

    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                           reinterpret_cast<void**>(
                               audio_client_.GetAddressOf()));
    if (FAILED(hr)) {
      return hr;
    }

    hr = audio_client_->GetMixFormat(&mix_format_);
    if (FAILED(hr)) {
      return hr;
    }
    sample_format_ = ResolveWasapiSampleFormat(mix_format_);
    if (sample_format_ == WasapiSampleFormat::unsupported ||
        mix_format_->nChannels == 0 || mix_format_->nSamplesPerSec == 0 ||
        mix_format_->nBlockAlign == 0) {
      return MF_E_INVALIDMEDIATYPE;
    }

    const REFERENCE_TIME buffer_duration = kMediaFoundationSecond / 2;
    DWORD stream_flags = 0;
    if (loopback_) {
      stream_flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
    }
    hr = audio_client_->Initialize(AUDCLNT_SHAREMODE_SHARED, stream_flags,
                                   buffer_duration, 0, mix_format_, nullptr);
    if (FAILED(hr)) {
      return hr;
    }

    hr = audio_client_->GetService(
        __uuidof(IAudioCaptureClient),
        reinterpret_cast<void**>(capture_client_.GetAddressOf()));
    if (FAILED(hr)) {
      return hr;
    }

    hr = audio_client_->Start();
    if (FAILED(hr)) {
      return hr;
    }
    started_ = true;
    active_ = true;
    return S_OK;
  }

  void MaybeReconnect(std::vector<std::string>* recovery_events) {
    const auto now = std::chrono::steady_clock::now();
    if (now - last_retry_at_ < std::chrono::seconds(2)) {
      return;
    }
    last_retry_at_ = now;
    HRESULT hr = Activate();
    if (SUCCEEDED(hr)) {
      if (recovery_events != nullptr) {
        recovery_events->push_back(AudioSourceLabel(loopback_) +
                                   " source reconnected.");
      }
      return;
    }
    Stop();
  }

  HRESULT DrainPackets(std::vector<std::string>* recovery_events) {
    if (capture_client_ == nullptr) {
      return AUDCLNT_E_DEVICE_INVALIDATED;
    }

    UINT32 packet_frames = 0;
    HRESULT hr = capture_client_->GetNextPacketSize(&packet_frames);
    if (FAILED(hr)) {
      return hr;
    }

    while (packet_frames > 0) {
      BYTE* data = nullptr;
      UINT32 frame_count = 0;
      DWORD flags = 0;
      hr = capture_client_->GetBuffer(&data, &frame_count, &flags, nullptr,
                                      nullptr);
      if (FAILED(hr)) {
        return hr;
      }

      AppendPacket(data, frame_count, (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0);
      hr = capture_client_->ReleaseBuffer(frame_count);
      if (FAILED(hr)) {
        return hr;
      }

      hr = capture_client_->GetNextPacketSize(&packet_frames);
      if (FAILED(hr)) {
        return hr;
      }
    }

    if (recovery_events != nullptr && pending_.empty() && !reported_ready_) {
      reported_ready_ = true;
      recovery_events->push_back(AudioSourceLabel(loopback_) +
                                 " source started.");
    }
    return S_OK;
  }

  void AppendPacket(const BYTE* data, UINT32 frame_count, bool silent) {
    if (frame_count == 0 || mix_format_ == nullptr) {
      return;
    }

    const int channel_count = static_cast<int>(mix_format_->nChannels);
    const int bytes_per_sample = mix_format_->wBitsPerSample / 8;
    const int block_align = static_cast<int>(mix_format_->nBlockAlign);
    const double step = static_cast<double>(mix_format_->nSamplesPerSec) /
                        static_cast<double>(kOutputAudioSampleRate);
    double position = resample_position_;
    while (position < frame_count) {
      const int source_frame =
          std::min(static_cast<int>(std::floor(position)),
                   static_cast<int>(frame_count) - 1);
      float left = 0.0f;
      float right = 0.0f;
      if (!silent && data != nullptr) {
        const BYTE* frame = data + source_frame * block_align;
        left = ReadWasapiSample(frame, 0, channel_count, sample_format_,
                                bytes_per_sample);
        right = channel_count == 1
                    ? left
                    : ReadWasapiSample(frame, 1, channel_count, sample_format_,
                                       bytes_per_sample);
      }
      pending_.push_back(left);
      pending_.push_back(right);
      position += step;
    }
    resample_position_ = position - frame_count;
    TrimPendingBuffer();
  }

  void CompactPendingBuffer() {
    if (read_offset_ < 4096) {
      return;
    }
    const size_t erase_values = read_offset_ * kOutputAudioChannelCount;
    pending_.erase(pending_.begin(),
                   pending_.begin() + static_cast<ptrdiff_t>(erase_values));
    read_offset_ = 0;
  }

  void TrimPendingBuffer() {
    const size_t available_frames =
        (pending_.size() / kOutputAudioChannelCount) - read_offset_;
    if (available_frames <= kMaxPendingAudioFrames) {
      return;
    }
    const size_t frames_to_drop = available_frames - kMaxPendingAudioFrames;
    read_offset_ += frames_to_drop;
    CompactPendingBuffer();
  }

  EDataFlow data_flow_;
  bool loopback_ = false;
  ComPtr<IMMDevice> device_;
  ComPtr<IAudioClient> audio_client_;
  ComPtr<IAudioCaptureClient> capture_client_;
  WAVEFORMATEX* mix_format_ = nullptr;
  WasapiSampleFormat sample_format_ = WasapiSampleFormat::unsupported;
  std::vector<float> pending_;
  size_t read_offset_ = 0;
  double resample_position_ = 0.0;
  bool active_ = false;
  bool started_ = false;
  bool reported_ready_ = false;
  std::chrono::steady_clock::time_point last_retry_at_ =
      std::chrono::steady_clock::now();
};

}  // namespace

EncodableValue ListAudioDevices(bool input) {
  ScopedComInitialization com;
  if (!com.ok()) {
    return MakeFailure("nativeFailure",
                       "COM could not be initialized for Windows audio devices.",
                       "Restart the app and try again.",
                       HResultToString(com.hr()));
  }

  ComPtr<IMMDeviceEnumerator> enumerator;
  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
  if (FAILED(hr)) {
    return MakeFailure("nativeFailure",
                       "Windows audio device enumeration failed.",
                       "Check the audio service and try again.",
                       HResultToString(hr));
  }

  ComPtr<IMMDeviceCollection> collection;
  hr = enumerator->EnumAudioEndpoints(
      input ? eCapture : eRender,
      DEVICE_STATE_ACTIVE | DEVICE_STATE_DISABLED | DEVICE_STATE_UNPLUGGED |
          DEVICE_STATE_NOTPRESENT,
      &collection);
  if (FAILED(hr)) {
    return MakeFailure("nativeFailure",
                       "Windows audio devices could not be listed.",
                       "Check the audio service and try again.",
                       HResultToString(hr));
  }

  UINT count = 0;
  collection->GetCount(&count);
  EncodableList devices;
  const PROPERTYKEY friendly_name_key = DeviceFriendlyNameKey();

  for (UINT index = 0; index < count; ++index) {
    ComPtr<IMMDevice> device;
    if (FAILED(collection->Item(index, &device)) || device == nullptr) {
      continue;
    }

    LPWSTR raw_id = nullptr;
    std::string id;
    if (SUCCEEDED(device->GetId(&raw_id)) && raw_id != nullptr) {
      id = WidePtrToUtf8(raw_id);
      CoTaskMemFree(raw_id);
    }

    std::string name = id;
    ComPtr<IPropertyStore> store;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) &&
        store != nullptr) {
      PROPVARIANT value;
      PropVariantInit(&value);
      if (SUCCEEDED(store->GetValue(friendly_name_key, &value)) &&
          value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
        name = WidePtrToUtf8(value.pwszVal);
      }
      PropVariantClear(&value);
    }

    DWORD state = DEVICE_STATE_NOTPRESENT;
    device->GetState(&state);
    std::string unavailable_reason;
    switch (state) {
      case DEVICE_STATE_ACTIVE:
        unavailable_reason = "";
        break;
      case DEVICE_STATE_DISABLED:
        unavailable_reason = "The audio endpoint is disabled.";
        break;
      case DEVICE_STATE_UNPLUGGED:
        unavailable_reason = "The audio endpoint is unplugged.";
        break;
      default:
        unavailable_reason = "The audio endpoint is not present.";
        break;
    }

    EncodableMap payload;
    payload[EncodableValue("id")] = EncodableValue(id);
    payload[EncodableValue("name")] = EncodableValue(name);
    payload[EncodableValue("isInput")] = EncodableValue(input);
    payload[EncodableValue("isAvailable")] =
        EncodableValue(state == DEVICE_STATE_ACTIVE);
    payload[EncodableValue("unavailableReason")] =
        EncodableValue(unavailable_reason);
    devices.emplace_back(EncodableValue(payload));
  }

  return MakeSuccess(EncodableValue(devices));
}

CapabilityProbeResult AudioEndpointProbe(EDataFlow data_flow, bool loopback) {
  const char* label = loopback ? "system audio output" : "microphone input";
  ScopedComInitialization com;
  if (!com.ok()) {
    return {false, "notConfigured",
            std::string("Windows ") + label +
                " probe could not initialize COM."};
  }

  ComPtr<IMMDeviceEnumerator> enumerator;
  HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
  if (FAILED(hr) || enumerator == nullptr) {
    return {false, "notConfigured",
            std::string("Windows ") + label +
                " probe could not start: " + HResultToString(hr)};
  }

  ComPtr<IMMDevice> device;
  hr = enumerator->GetDefaultAudioEndpoint(data_flow, eConsole, &device);
  if (FAILED(hr) || device == nullptr) {
    return {false, "notConfigured",
            std::string("No default Windows ") + label +
                " endpoint is available: " + HResultToString(hr)};
  }

  DWORD state = DEVICE_STATE_NOTPRESENT;
  hr = device->GetState(&state);
  if (FAILED(hr) || state != DEVICE_STATE_ACTIVE) {
    return {false, "notConfigured",
            std::string("The default Windows ") + label +
                " endpoint is not active."};
  }

  ComPtr<IAudioClient> audio_client;
  hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                        reinterpret_cast<void**>(audio_client.GetAddressOf()));
  if (FAILED(hr) || audio_client == nullptr) {
    return {false, IsPermissionDenied(hr) ? "requiresPermission"
                                          : "notConfigured",
            std::string("Windows ") + label +
                " endpoint could not be opened: " + HResultToString(hr)};
  }

  WAVEFORMATEX* mix_format = nullptr;
  hr = audio_client->GetMixFormat(&mix_format);
  if (FAILED(hr) || mix_format == nullptr) {
    if (mix_format != nullptr) {
      CoTaskMemFree(mix_format);
    }
    return {false, "notConfigured",
            std::string("Windows ") + label +
                " mix format is unavailable: " + HResultToString(hr)};
  }

  const WasapiSampleFormat sample_format =
      ResolveWasapiSampleFormat(mix_format);
  if (sample_format == WasapiSampleFormat::unsupported ||
      mix_format->nChannels == 0 || mix_format->nSamplesPerSec == 0 ||
      mix_format->nBlockAlign == 0) {
    CoTaskMemFree(mix_format);
    return {false, "notConfigured",
            std::string("Windows ") + label +
                " uses an unsupported audio format."};
  }

  const REFERENCE_TIME buffer_duration = kMediaFoundationSecond / 2;
  const DWORD stream_flags = loopback ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0;
  hr = audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED, stream_flags,
                                buffer_duration, 0, mix_format, nullptr);
  CoTaskMemFree(mix_format);
  if (FAILED(hr)) {
    return {false, IsPermissionDenied(hr) ? "requiresPermission"
                                          : "notConfigured",
            std::string("Windows ") + label +
                " capture could not initialize: " + HResultToString(hr)};
  }

  ComPtr<IAudioCaptureClient> capture_client;
  hr = audio_client->GetService(
      __uuidof(IAudioCaptureClient),
      reinterpret_cast<void**>(capture_client.GetAddressOf()));
  if (FAILED(hr) || capture_client == nullptr) {
    return {false, "notConfigured",
            std::string("Windows ") + label +
                " capture service is unavailable: " + HResultToString(hr)};
  }

  return {true, "available",
          std::string("Windows ") + label + " endpoint is ready."};
}

class AudioMixerImpl {
 public:
  bool Initialize(const RecordingConfig& config,
                  std::vector<std::string>* recovery_events) {
    sources_.clear();
    frames_written_ = 0;
    system_audio_active_ = false;
    microphone_active_ = false;

    if (config.capture_audio) {
      auto source = std::make_unique<WasapiCaptureSource>(eRender, true);
      std::string message;
      if (source->Initialize(&message)) {
        system_audio_active_ = true;
        sources_.push_back(std::move(source));
      } else if (recovery_events != nullptr) {
        recovery_events->push_back(message);
      }
    }

    if (config.capture_microphone) {
      auto source = std::make_unique<WasapiCaptureSource>(eCapture, false);
      std::string message;
      if (source->Initialize(&message)) {
        microphone_active_ = true;
        sources_.push_back(std::move(source));
      } else if (recovery_events != nullptr) {
        recovery_events->push_back(message);
      }
    }

    if ((config.capture_audio || config.capture_microphone) &&
        sources_.empty() && recovery_events != nullptr) {
      recovery_events->push_back(
          "No requested Windows audio device is available; recording continues with video only.");
    }
    return !sources_.empty();
  }

  bool system_audio_active() const { return system_audio_active_; }

  bool microphone_active() const { return microphone_active_; }

  HRESULT WriteUntil(IMFSinkWriter* writer,
                     DWORD stream_index,
                     LONGLONG target_time,
                     std::vector<std::string>* recovery_events) {
    if (sources_.empty() ||
        stream_index == MF_SINK_WRITER_INVALID_STREAM_INDEX) {
      return S_OK;
    }

    const int64_t target_frames =
        (target_time * kOutputAudioSampleRate) / kMediaFoundationSecond;
    while (frames_written_ < target_frames) {
      const int64_t remaining = target_frames - frames_written_;
      const size_t chunk_frames =
          static_cast<size_t>(std::min<int64_t>(remaining, 1024));
      std::vector<float> mixed(chunk_frames * kOutputAudioChannelCount, 0.0f);

      for (auto& source : sources_) {
        std::vector<float> source_frames;
        source->ReadFrames(chunk_frames, &source_frames, recovery_events);
        for (size_t index = 0; index < mixed.size(); ++index) {
          mixed[index] += source_frames[index];
        }
      }

      std::vector<int16_t> pcm(mixed.size());
      const float gain = sources_.size() > 1 ? 0.75f : 1.0f;
      for (size_t index = 0; index < mixed.size(); ++index) {
        const float clamped = std::clamp(mixed[index] * gain, -1.0f, 1.0f);
        pcm[index] = static_cast<int16_t>(std::lrint(clamped * 32767.0f));
      }

      HRESULT hr = WriteAudioSample(writer, stream_index, pcm, frames_written_);
      if (FAILED(hr)) {
        return hr;
      }
      frames_written_ += static_cast<int64_t>(chunk_frames);
    }
    return S_OK;
  }

  void Stop() {
    for (auto& source : sources_) {
      source->Stop();
    }
    sources_.clear();
  }

  void DiscardPendingAudio(std::vector<std::string>* recovery_events) {
    for (auto& source : sources_) {
      source->DiscardPendingAudio(recovery_events);
    }
  }

 private:
  std::vector<std::unique_ptr<WasapiCaptureSource>> sources_;
  int64_t frames_written_ = 0;
  bool system_audio_active_ = false;
  bool microphone_active_ = false;
};

AudioMixer::AudioMixer() : impl_(std::make_unique<AudioMixerImpl>()) {}

AudioMixer::~AudioMixer() = default;

bool AudioMixer::Initialize(const RecordingConfig& config,
                            std::vector<std::string>* recovery_events) {
  return impl_->Initialize(config, recovery_events);
}

bool AudioMixer::system_audio_active() const {
  return impl_->system_audio_active();
}

bool AudioMixer::microphone_active() const {
  return impl_->microphone_active();
}

HRESULT AudioMixer::WriteUntil(IMFSinkWriter* writer,
                               DWORD stream_index,
                               LONGLONG target_time,
                               std::vector<std::string>* recovery_events) {
  return impl_->WriteUntil(writer, stream_index, target_time, recovery_events);
}

void AudioMixer::Stop() {
  impl_->Stop();
}

void AudioMixer::DiscardPendingAudio(
    std::vector<std::string>* recovery_events) {
  impl_->DiscardPendingAudio(recovery_events);
}
