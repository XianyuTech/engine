// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_LIB_UI_EXTERNAL_ADAPTER_IMAGE_H_
#define FLUTTER_LIB_UI_EXTERNAL_ADAPTER_IMAGE_H_

#include <functional>
#include <map>
#include <string>

#define EXTERNAL_ADAPTER_IMAGE_EXPORT __attribute__((visibility("default")))

namespace flutter {

class EXTERNAL_ADAPTER_IMAGE_EXPORT ExternalAdapterImageProvider {
 public:
  virtual ~ExternalAdapterImageProvider() = default;

  struct RequestInfo {
    std::string url;       // request url
    int targetWidth = 0;   // target width in flutter point
    int targetHeight = 0;  // target height in flutter point
    std::map<std::string, std::string>
        parameters;  // parameters affecting final pixel data.
    std::map<std::string, std::string>
        extraInfo;  // extra info not affecting pixel data.
  };

  using PlatformHandle = uintptr_t;
  static constexpr int InfiniteLoop = -1;
  struct PlatformImage {
    PlatformHandle handle = 0;
    int width = 0;       // width in pixel
    int height = 0;      // height in pixel
    int frameCount = 1;  // for multiframe image such as GIF
    int repetitionCount = InfiniteLoop;
    int durationInMs = 0;  // in milliseconds
    void* userData = nullptr;
  };

  enum class AlphaType { Opaque, Premul, Unpremul };
  enum class ColorType { RGBA8888, BGRA8888, RGB565, ARGB4444, Alpha8 };

  struct Bitmap {
    void* pixels =
        nullptr;  // If pixels are copied, you should use malloc function.
    bool pixelsCopied =
        false;  // If true, the pixel data has no dependence on platform image.
    int width = 0;
    int height = 0;
    AlphaType alphaType;
    ColorType colorType;
    size_t bytesPerRow = 0;
    void* userData = nullptr;
  };

  using RequestId = uint32_t;
  using ReleaseImageCallback = std::function<void(PlatformImage image)>;
  using RequestCallback =
      std::function<void(PlatformImage image, ReleaseImageCallback&& release)>;
  using ReleaseBitmapCallback = std::function<void(Bitmap bitmap)>;
  using DecodeResult = std::pair<Bitmap, ReleaseBitmapCallback>;

  /// Request for a image. This function returns the platform image
  /// such as UIImage on iOS in callback. The platform image instance
  /// should be retained before being returned in callback.
  /// In callback, a release function should also be provided, and it
  /// is flutter engine's work to decide when to release the platform
  /// image instance.
  virtual void request(RequestId rid,
                       const RequestInfo& requestInfo,
                       RequestCallback&& callback) = 0;

  /// To cancel a image request. Think a case that images in a list
  /// is being fast scrolled. Before the provider finishes downloading
  /// the image and call back, the image widget is disposed and the
  /// request is no longer needed. Implement this cancel logic can
  /// provide better user experience in fast scrolling and speed up
  /// image display.
  virtual void cancel(RequestId rid) = 0;

  /// Flutter engine requests to decode a platform image to bitmap data.
  /// This function is synchronous and will be invoked in engine's worker
  /// thread so it is safe to do decoding synchronously. No need to switch
  /// thread. In result, a bitmap pixel release function should also be
  /// provided.
  virtual DecodeResult decode(PlatformImage image, int frameIndex = 0) = 0;

  /// While decoding images, massive temporary memory will be used and some
  /// memory peak may occur which might cause OOM. When requesting each image,
  /// engine will ask if we should reevaluate current device status.
  /// On implementing this method, you can listen to system's memory warning
  /// notification and return true flag when memory warning occurs.
  /// After engine calls evaluateDeviceStatus to reevaluate, set the flag to
  /// false.
  virtual bool shouldEvaluateDeviceStatus() = 0;
  virtual void evaluateDeviceStatus(uint32_t& cpuCoreCount,
                                    uint64_t& maxMemoryUsing) = 0;

  /// Implement this log function to log key information while requesting
  /// decoding and uploading GPU texture.
  enum class LogLevel { Debug, Info, Warn, Error };
  virtual void log(LogLevel level, const char* string) {}
};

/// Set subclassed ExternalAdapterImageProvider provider instance.
EXTERNAL_ADAPTER_IMAGE_EXPORT extern void SetExternalAdapterImageProvider(
    ExternalAdapterImageProvider* provider);

}  // namespace flutter

#endif  // FLUTTER_LIB_UI_EXTERNAL_ADAPTER_IMAGE_H_
