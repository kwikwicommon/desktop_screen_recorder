#ifndef FLUTTER_PLUGIN_DESKTOP_SCREEN_RECORDER_WINDOWS_NATIVE_RUNTIME_H_
#define FLUTTER_PLUGIN_DESKTOP_SCREEN_RECORDER_WINDOWS_NATIVE_RUNTIME_H_

#include <audioclient.h>
#include <flutter/encodable_value.h>
#include <ks.h>
#include <ksmedia.h>
#include <mfapi.h>
#include <mmreg.h>
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

#include <string>

std::string HResultToString(HRESULT hr);

flutter::EncodableValue MakeSuccess(
    flutter::EncodableValue payload = flutter::EncodableValue());

flutter::EncodableValue MakeFailure(
    const std::string& status,
    const std::string& message,
    const std::string& recovery,
    const std::string& native_code = "",
    const std::string& domain = "",
    const std::string& recoverability = "unknown",
    const std::string& artifact_path = "");

flutter::EncodableValue MakePermissionPayload(
    const std::string& type,
    const std::string& state,
    const std::string& message = "",
    bool requires_restart = false);

flutter::EncodableValue MakeFeaturePayload(
    const std::string& feature,
    const std::string& status,
    const std::string& message = "",
    const std::string& permission_type = "");

class ScopedComInitialization {
 public:
  explicit ScopedComInitialization(DWORD coinit = COINIT_MULTITHREADED);
  ~ScopedComInitialization();

  ScopedComInitialization(const ScopedComInitialization&) = delete;
  ScopedComInitialization& operator=(const ScopedComInitialization&) = delete;

  bool ok() const;
  HRESULT hr() const;

 private:
  HRESULT hr_ = E_FAIL;
  bool should_uninitialize_ = false;
};

class ScopedMediaFoundation {
 public:
  ScopedMediaFoundation() = default;
  ~ScopedMediaFoundation();

  ScopedMediaFoundation(const ScopedMediaFoundation&) = delete;
  ScopedMediaFoundation& operator=(const ScopedMediaFoundation&) = delete;

  HRESULT Startup();
  HRESULT hr() const;

 private:
  HRESULT hr_ = E_FAIL;
  bool started_ = false;
};

HRESULT CreateWicImagingFactory(
    Microsoft::WRL::ComPtr<IWICImagingFactory>* factory);

enum class WasapiSampleFormat {
  float32,
  pcm16,
  pcm24,
  pcm32,
  unsupported,
};

WasapiSampleFormat ResolveWasapiSampleFormat(const WAVEFORMATEX* format);
bool IsPermissionDenied(HRESULT hr);

#endif  // FLUTTER_PLUGIN_DESKTOP_SCREEN_RECORDER_WINDOWS_NATIVE_RUNTIME_H_
