#include "windows_native_runtime.h"

#include <cstdio>
#include <utility>
#include <variant>

using EncodableMap = flutter::EncodableMap;
using EncodableValue = flutter::EncodableValue;
using Microsoft::WRL::ComPtr;

std::string HResultToString(HRESULT hr) {
  char buffer[32] = {};
  std::snprintf(buffer, sizeof(buffer), "0x%08lX",
                static_cast<unsigned long>(hr));
  return std::string(buffer);
}

EncodableValue MakeSuccess(EncodableValue payload) {
  EncodableMap response;
  response[EncodableValue("status")] = EncodableValue("success");
  if (!std::holds_alternative<std::monostate>(payload)) {
    response[EncodableValue("payload")] = std::move(payload);
  }
  return EncodableValue(response);
}

EncodableValue MakeFailure(const std::string& status,
                           const std::string& message,
                           const std::string& recovery,
                           const std::string& native_code,
                           const std::string& domain,
                           const std::string& recoverability,
                           const std::string& artifact_path) {
  EncodableMap failure;
  failure[EncodableValue("message")] = EncodableValue(message);
  failure[EncodableValue("recoverySuggestion")] = EncodableValue(recovery);
  failure[EncodableValue("requiresRestart")] = EncodableValue(false);
  failure[EncodableValue("domain")] =
      EncodableValue(domain.empty() ? status : domain);
  failure[EncodableValue("nativeCode")] = EncodableValue(native_code);
  failure[EncodableValue("recoverability")] = EncodableValue(recoverability);
  failure[EncodableValue("artifactPath")] = EncodableValue(artifact_path);

  EncodableMap response;
  response[EncodableValue("status")] = EncodableValue(status);
  response[EncodableValue("failure")] = EncodableValue(failure);
  return EncodableValue(response);
}

EncodableValue MakePermissionPayload(const std::string& type,
                                     const std::string& state,
                                     const std::string& message,
                                     bool requires_restart) {
  EncodableMap payload;
  payload[EncodableValue("type")] = EncodableValue(type);
  payload[EncodableValue("state")] = EncodableValue(state);
  payload[EncodableValue("message")] = EncodableValue(message);
  payload[EncodableValue("requiresRestart")] = EncodableValue(requires_restart);
  return EncodableValue(payload);
}

EncodableValue MakeFeaturePayload(const std::string& feature,
                                  const std::string& status,
                                  const std::string& message,
                                  const std::string& permission_type) {
  EncodableMap payload;
  payload[EncodableValue("feature")] = EncodableValue(feature);
  payload[EncodableValue("status")] = EncodableValue(status);
  payload[EncodableValue("message")] = EncodableValue(message);
  if (!permission_type.empty()) {
    payload[EncodableValue("permissionType")] = EncodableValue(permission_type);
  }
  return EncodableValue(payload);
}

ScopedComInitialization::ScopedComInitialization(DWORD coinit) {
  hr_ = CoInitializeEx(nullptr, coinit);
  should_uninitialize_ = SUCCEEDED(hr_);
}

ScopedComInitialization::~ScopedComInitialization() {
  if (should_uninitialize_) {
    CoUninitialize();
  }
}

bool ScopedComInitialization::ok() const {
  return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE;
}

HRESULT ScopedComInitialization::hr() const {
  return hr_;
}

ScopedMediaFoundation::~ScopedMediaFoundation() {
  if (started_) {
    MFShutdown();
  }
}

HRESULT ScopedMediaFoundation::Startup() {
  if (started_) {
    return S_OK;
  }
  hr_ = MFStartup(MF_VERSION);
  started_ = SUCCEEDED(hr_);
  return hr_;
}

HRESULT ScopedMediaFoundation::hr() const {
  return hr_;
}

HRESULT CreateWicImagingFactory(ComPtr<IWICImagingFactory>* factory) {
  if (factory == nullptr) {
    return E_POINTER;
  }
  return CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                          CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(factory->ReleaseAndGetAddressOf()));
}

namespace {

bool IsWaveFormatExtensiblePcm(const WAVEFORMATEX* format,
                               const GUID& subtype) {
  if (format->wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
      format->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
    return false;
  }
  const auto* extensible =
      reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
  return extensible->SubFormat == subtype;
}

}  // namespace

WasapiSampleFormat ResolveWasapiSampleFormat(const WAVEFORMATEX* format) {
  if (format == nullptr) {
    return WasapiSampleFormat::unsupported;
  }
  if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
      IsWaveFormatExtensiblePcm(format, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
    return format->wBitsPerSample == 32 ? WasapiSampleFormat::float32
                                        : WasapiSampleFormat::unsupported;
  }
  if (format->wFormatTag == WAVE_FORMAT_PCM ||
      IsWaveFormatExtensiblePcm(format, KSDATAFORMAT_SUBTYPE_PCM)) {
    switch (format->wBitsPerSample) {
      case 16:
        return WasapiSampleFormat::pcm16;
      case 24:
        return WasapiSampleFormat::pcm24;
      case 32:
        return WasapiSampleFormat::pcm32;
      default:
        return WasapiSampleFormat::unsupported;
    }
  }
  return WasapiSampleFormat::unsupported;
}

bool IsPermissionDenied(HRESULT hr) {
  return hr == E_ACCESSDENIED || hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
}
