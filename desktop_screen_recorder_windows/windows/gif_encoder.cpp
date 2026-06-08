#include "gif_encoder.h"

#include <propidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

using Microsoft::WRL::ComPtr;

class AnimatedGifEncoderImpl {
 public:
  HRESULT Initialize(const std::wstring& path,
                     int width,
                     int height,
                     int frame_rate,
                     bool optimize_palette) {
    if (width <= 0 || height <= 0 || frame_rate <= 0) {
      return E_INVALIDARG;
    }
    width_ = width;
    height_ = height;
    optimize_palette_ = optimize_palette;
    delay_cs_ = static_cast<USHORT>(
        std::max(1, static_cast<int>(std::lround(100.0 / frame_rate))));

    HRESULT hr = CreateWicImagingFactory(&factory_);
    if (FAILED(hr)) {
      return hr;
    }

    hr = factory_->CreateStream(&stream_);
    if (FAILED(hr)) {
      return hr;
    }
    hr = stream_->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    if (FAILED(hr)) {
      return hr;
    }

    hr = factory_->CreateEncoder(GUID_ContainerFormatGif, nullptr, &encoder_);
    if (FAILED(hr)) {
      return hr;
    }
    hr = encoder_->Initialize(stream_.Get(), WICBitmapEncoderNoCache);
    if (FAILED(hr)) {
      return hr;
    }
    return S_OK;
  }

  HRESULT WriteFrame(const BitmapFrame& frame) {
    if (frame.width != width_ || frame.height != height_ ||
        frame.bgra.empty()) {
      return E_INVALIDARG;
    }

    ComPtr<IWICBitmap> bitmap;
    size_t safe_buffer_size = 0;
    if (!TryGetBgraBufferSize(frame.width, frame.height, &safe_buffer_size) ||
        safe_buffer_size > std::numeric_limits<UINT>::max() ||
        frame.bgra.size() < safe_buffer_size) {
      return E_INVALIDARG;
    }
    const UINT stride = static_cast<UINT>(frame.width * 4);
    const UINT buffer_size = static_cast<UINT>(safe_buffer_size);
    HRESULT hr = factory_->CreateBitmapFromMemory(
        static_cast<UINT>(frame.width), static_cast<UINT>(frame.height),
        GUID_WICPixelFormat32bppBGRA, stride, buffer_size,
        const_cast<BYTE*>(reinterpret_cast<const BYTE*>(frame.bgra.data())),
        &bitmap);
    if (FAILED(hr)) {
      return hr;
    }

    ComPtr<IWICPalette> palette;
    hr = factory_->CreatePalette(&palette);
    if (FAILED(hr)) {
      return hr;
    }
    if (optimize_palette_) {
      hr = palette->InitializeFromBitmap(bitmap.Get(), 256, TRUE);
    } else {
      hr = palette->InitializePredefined(WICBitmapPaletteTypeFixedHalftone256,
                                         FALSE);
    }
    if (FAILED(hr)) {
      return hr;
    }

    ComPtr<IWICBitmapFrameEncode> frame_encoder;
    ComPtr<IPropertyBag2> options;
    hr = encoder_->CreateNewFrame(&frame_encoder, &options);
    if (FAILED(hr)) {
      return hr;
    }
    hr = frame_encoder->Initialize(options.Get());
    if (FAILED(hr)) {
      return hr;
    }
    hr = frame_encoder->SetSize(static_cast<UINT>(frame.width),
                                static_cast<UINT>(frame.height));
    if (FAILED(hr)) {
      return hr;
    }
    hr = frame_encoder->SetPalette(palette.Get());
    if (FAILED(hr)) {
      return hr;
    }
    WICPixelFormatGUID pixel_format = GUID_WICPixelFormat8bppIndexed;
    hr = frame_encoder->SetPixelFormat(&pixel_format);
    if (FAILED(hr)) {
      return hr;
    }

    ComPtr<IWICMetadataQueryWriter> metadata;
    if (SUCCEEDED(frame_encoder->GetMetadataQueryWriter(&metadata)) &&
        metadata != nullptr) {
      PROPVARIANT delay;
      PropVariantInit(&delay);
      delay.vt = VT_UI2;
      delay.uiVal = delay_cs_;
      metadata->SetMetadataByName(L"/grctlext/Delay", &delay);
      PropVariantClear(&delay);

      PROPVARIANT disposal;
      PropVariantInit(&disposal);
      disposal.vt = VT_UI1;
      disposal.bVal = 2;
      metadata->SetMetadataByName(L"/grctlext/Disposal", &disposal);
      PropVariantClear(&disposal);
    }

    hr = frame_encoder->WriteSource(bitmap.Get(), nullptr);
    if (FAILED(hr)) {
      return hr;
    }
    return frame_encoder->Commit();
  }

  HRESULT Finalize() {
    if (encoder_ == nullptr) {
      return E_FAIL;
    }
    return encoder_->Commit();
  }

 private:
  ComPtr<IWICImagingFactory> factory_;
  ComPtr<IWICStream> stream_;
  ComPtr<IWICBitmapEncoder> encoder_;
  int width_ = 0;
  int height_ = 0;
  USHORT delay_cs_ = 8;
  bool optimize_palette_ = true;
};

AnimatedGifEncoder::AnimatedGifEncoder()
    : impl_(std::make_unique<AnimatedGifEncoderImpl>()) {}

AnimatedGifEncoder::~AnimatedGifEncoder() = default;

HRESULT AnimatedGifEncoder::Initialize(const std::wstring& path,
                                       int width,
                                       int height,
                                       int frame_rate,
                                       bool optimize_palette) {
  return impl_->Initialize(path, width, height, frame_rate, optimize_palette);
}

HRESULT AnimatedGifEncoder::WriteFrame(const BitmapFrame& frame) {
  return impl_->WriteFrame(frame);
}

HRESULT AnimatedGifEncoder::Finalize() {
  return impl_->Finalize();
}
