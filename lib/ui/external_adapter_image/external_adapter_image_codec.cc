// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "external_adapter_image_codec.h"
#include "external_adapter_image_decode_coordinator.h"
#include "flutter/fml/make_copyable.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkImageInfo.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "third_party/tonic/dart_binding_macros.h"
#include "third_party/tonic/dart_library_natives.h"
#include "third_party/tonic/logging/dart_invoke.h"

#define EXTERNAL_ADAPTER_IMAGE_LOG_TAG "[AdapterImage] "
#define EXTERNAL_ADAPTER_IMAGE_LOG(level, format, ...) \
  flutter::ExternalAdapterImagePrintLog((level), (format), ##__VA_ARGS__)

#define EXTERNAL_ADAPTER_IMAGE_LOGE(format, ...)                            \
  EXTERNAL_ADAPTER_IMAGE_LOG(ExternalAdapterImageProvider::LogLevel::Error, \
                             format, ##__VA_ARGS__)
#define EXTERNAL_ADAPTER_IMAGE_LOGW(format, ...)                           \
  EXTERNAL_ADAPTER_IMAGE_LOG(ExternalAdapterImageProvider::LogLevel::Warn, \
                             format, ##__VA_ARGS__)
#define EXTERNAL_ADAPTER_IMAGE_LOGI(format, ...)                           \
  EXTERNAL_ADAPTER_IMAGE_LOG(ExternalAdapterImageProvider::LogLevel::Info, \
                             format, ##__VA_ARGS__)

#ifdef DEBUG

#define EXTERNAL_ADAPTER_IMAGE_LOGD(format, ...)                            \
  EXTERNAL_ADAPTER_IMAGE_LOG(ExternalAdapterImageProvider::LogLevel::Debug, \
                             format, ##__VA_ARGS__)

#else

#define EXTERNAL_ADAPTER_IMAGE_LOGD(format, ...) ((void)0)

#endif

namespace flutter {

class ExternalAdapterImageProvider;
class ExternalAdapterImageManager;

static ExternalAdapterImageProvider* external_adapter_image_provider;
static ExternalAdapterImageManager* external_adapter_image_manager;

struct ExternalAdapterLogFlattenHelper {
  ExternalAdapterLogFlattenHelper() : mLargeBuf() {}
  ExternalAdapterLogFlattenHelper(const char* fmt, va_list args)
      : ExternalAdapterLogFlattenHelper() {
    set(fmt, args);
  }
  ~ExternalAdapterLogFlattenHelper() {
    if (mLargeBuf) {
      free(mLargeBuf);
    }
  }

  const char* str() const { return mLargeBuf ? mLargeBuf : mSmallBuf.data(); }
  ExternalAdapterLogFlattenHelper& set(const char* fmt, va_list args);

 private:
  ExternalAdapterLogFlattenHelper(const ExternalAdapterLogFlattenHelper&) =
      delete;
  void operator=(const ExternalAdapterLogFlattenHelper&) = delete;

  std::array<char, 4096> mSmallBuf;
  char* mLargeBuf;
};

ExternalAdapterLogFlattenHelper& ExternalAdapterLogFlattenHelper::set(
    const char* fmt,
    va_list args) {
  va_list argsCopy;
  va_copy(argsCopy, args);
  int len = 1 + vsnprintf(nullptr, 0, fmt, argsCopy);
  va_end(argsCopy);
  if (len <= 1) {
    mSmallBuf[0] = 0;
    return *this;
  }
  int tagLen = strlen(EXTERNAL_ADAPTER_IMAGE_LOG_TAG);
  len += tagLen;
  if (len > (int)mSmallBuf.size()) {
    mLargeBuf = static_cast<char*>(malloc(len));
  }
  int rv;
  if (mLargeBuf) {
    strcpy(mLargeBuf, EXTERNAL_ADAPTER_IMAGE_LOG_TAG);
    rv = vsnprintf(mLargeBuf + tagLen, len, fmt, args);
  } else {
    strcpy(mSmallBuf.data(), EXTERNAL_ADAPTER_IMAGE_LOG_TAG);
    rv = vsnprintf(mSmallBuf.data() + tagLen, mSmallBuf.size() - tagLen, fmt,
                   args);
  }
  (void)rv;
  return *this;
}

static void ExternalAdapterImagePrintLog(
    ExternalAdapterImageProvider::LogLevel level,
    const char* format,
    ...) {
  if (external_adapter_image_provider == nullptr) {
    return;
  }
  va_list args;
  va_start(args, format);
  ExternalAdapterLogFlattenHelper log(format, args);
  va_end(args);
  external_adapter_image_provider->log(level, log.str());
}

static SkColorType ConvertColorType(
    ExternalAdapterImageProvider::ColorType source) {
  switch (source) {
    case ExternalAdapterImageProvider::ColorType::RGBA8888:
      return kRGBA_8888_SkColorType;
    case ExternalAdapterImageProvider::ColorType::BGRA8888:
      return kBGRA_8888_SkColorType;
    case ExternalAdapterImageProvider::ColorType::RGB565:
      return kRGB_565_SkColorType;
    case ExternalAdapterImageProvider::ColorType::ARGB4444:
      return kARGB_4444_SkColorType;
    case ExternalAdapterImageProvider::ColorType::Alpha8:
      return kAlpha_8_SkColorType;
    default:
      return kUnknown_SkColorType;
  }
}

static SkAlphaType ConvertAlphaType(
    ExternalAdapterImageProvider::AlphaType source) {
  switch (source) {
    case ExternalAdapterImageProvider::AlphaType::Opaque:
      return kOpaque_SkAlphaType;
    case ExternalAdapterImageProvider::AlphaType::Premul:
      return kPremul_SkAlphaType;
    case ExternalAdapterImageProvider::AlphaType::Unpremul:
      return kUnpremul_SkAlphaType;
    default:
      return kUnknown_SkAlphaType;
  }
}

class ExternalAdapterImageManager {
 public:
  ExternalAdapterImageManager(
      const TaskRunners& runners,
      std::shared_ptr<fml::ConcurrentTaskRunner> concurrentRunner,
      fml::WeakPtr<IOManager> io_manager)
      : concurrentCoordinator_(concurrentRunner),
        runners_(std::move(runners)),
        ioManager_(std::move(io_manager)) {}

  const TaskRunners& runners() const { return runners_; }
  fml::WeakPtr<IOManager> ioManager() const { return ioManager_; }

  ExternalAdapterImageProvider::RequestId nextRequestId() {
    return requestId++;
  }

  ExternalAdapterImageDecodeCoordinator& concurrentCoordinator() {
    return concurrentCoordinator_;
  }
  void evaluateDeviceStatus() {
    bool shouldEvaluate =
        !initialDeviceStatusEvaluated_ ||
        external_adapter_image_provider->shouldEvaluateDeviceStatus();

    if (shouldEvaluate) {
      uint32_t cpu;
      uint64_t memory;
      external_adapter_image_provider->evaluateDeviceStatus(cpu, memory);
      EXTERNAL_ADAPTER_IMAGE_LOGI("Reevaluate device. Core: %u, Memory: %lu.",
                                  cpu, memory);
      concurrentCoordinator_.updateCapacity(cpu, memory);
      initialDeviceStatusEvaluated_ = true;
    }
  }

  void retainCodec(ExternalAdapterImageProvider::RequestId reqId,
                   ExternalAdapterImageFrameCodec* codec) {
    if (codec) {
      std::lock_guard<std::mutex> guard(pendingCodecLock_);
      pendingCodec_[reqId] =
          new fml::RefPtr<ExternalAdapterImageFrameCodec>(codec);
    }
  }

  fml::RefPtr<ExternalAdapterImageFrameCodec>* retrieveCodec(
      ExternalAdapterImageProvider::RequestId reqId) {
    fml::RefPtr<ExternalAdapterImageFrameCodec>* result = nullptr;
    std::lock_guard<std::mutex> guard(pendingCodecLock_);
    auto findCodec = pendingCodec_.find(reqId);
    if (findCodec != pendingCodec_.end()) {
      result = findCodec->second;
      pendingCodec_.erase(findCodec);
    }
    return result;
  }

 private:
  ExternalAdapterImageProvider::RequestId requestId = 0;

  bool initialDeviceStatusEvaluated_ = false;
  ExternalAdapterImageDecodeCoordinator concurrentCoordinator_;

  TaskRunners runners_;
  std::shared_ptr<fml::ConcurrentTaskRunner> concurrentTaskRunner_;
  fml::WeakPtr<IOManager> ioManager_;

  // Codecs are retained here before image library callback.
  std::mutex pendingCodecLock_;
  std::map<ExternalAdapterImageProvider::RequestId,
           fml::RefPtr<ExternalAdapterImageFrameCodec>*>
      pendingCodec_;
};

void SetExternalAdapterImageProvider(ExternalAdapterImageProvider* provider) {
  if (external_adapter_image_provider == nullptr) {
    external_adapter_image_provider = provider;
  }
}

ExternalAdapterImageProvider* GetExternalAdapterImageProvider() {
  return external_adapter_image_provider;
}

void InitializeExternalAdapterImageManager(
    const TaskRunners& runners,
    std::shared_ptr<fml::ConcurrentTaskRunner> concurrent_task_runner,
    fml::WeakPtr<IOManager> io_manager) {
  if (external_adapter_image_manager == nullptr) {
    external_adapter_image_manager = new ExternalAdapterImageManager(
        runners, concurrent_task_runner, io_manager);
  }
}

static SkiaGPUObject<SkImage> UploadTexture(
    ExternalAdapterImageProvider::Bitmap& bitmap) {
  SkImageInfo imageInfo = SkImageInfo::Make(bitmap.width, bitmap.height,
                                            ConvertColorType(bitmap.colorType),
                                            ConvertAlphaType(bitmap.alphaType));
  SkiaGPUObject<SkImage> result;
  external_adapter_image_manager->ioManager()
      ->GetIsGpuDisabledSyncSwitch()
      ->Execute(
          fml::SyncSwitch::Handlers()
              .SetIfTrue([&result, &bitmap, &imageInfo] {
                /* Here we create a CPU based image because App is in background
                 state. But the pixels must remain alive until the created
                 SkImage is deallocated. So if Bitmap's pixels are copied, we
                 can transfer its ownership to us. If Bitmap's pixels are not
                 copied, we have to copy the pixels.
                 */
                SkPixmap pixmap;
                if (bitmap.pixelsCopied) {
                  pixmap =
                      SkPixmap(imageInfo, bitmap.pixels, bitmap.bytesPerRow);
                  bitmap.pixels = nullptr;
                } else {
                  size_t bufferSize = bitmap.bytesPerRow * bitmap.height;
                  void* copiedPixels = malloc(bufferSize);
                  if (copiedPixels == nullptr) {
                    result = {};
                    return;
                  }
                  memcpy(copiedPixels, (const void*)bitmap.pixels, bufferSize);
                  pixmap =
                      SkPixmap(imageInfo, copiedPixels, bitmap.bytesPerRow);
                }
                sk_sp<SkImage> texture = SkImage::MakeFromRaster(
                    pixmap,
                    [](const void* pixels, SkImage::ReleaseContext context) {
                      free((void*)pixels);
                    },
                    nullptr);
                result = {texture, nullptr};
              })
              .SetIfFalse([&result,
                           context = external_adapter_image_manager->ioManager()
                                         ->GetResourceContext(),
                           queue = external_adapter_image_manager->ioManager()
                                       ->GetSkiaUnrefQueue(),
                           &bitmap, &imageInfo] {
                SkPixmap pixmap(imageInfo, bitmap.pixels, bitmap.bytesPerRow);
                sk_sp<SkImage> texture = SkImage::MakeCrossContextFromPixmap(
                    context.get(),  // context
                    pixmap,         // pixmap
                    false,          // buildMips,
                    true            // limitToMaxTextureSize
                );
                if (!texture) {
                  result = {};
                } else {
                  result = {texture, queue};
                }
              }));
  return result;
}

ExternalAdapterImageFrameCodec::ExternalAdapterImageFrameCodec(
    std::unique_ptr<ExternalAdapterImageProvider::RequestInfo> descriptor)
    : descriptor_(std::move(descriptor)) {}

ExternalAdapterImageFrameCodec::~ExternalAdapterImageFrameCodec() {
  destroyed_ = true;
  cancel();
}

int ExternalAdapterImageFrameCodec::frameCount() const {
  return platformImage_.frameCount;
}

int ExternalAdapterImageFrameCodec::repetitionCount() const {
  return platformImage_.repetitionCount;
}

static void _ReleaseOnUIThread(
    fml::RefPtr<ExternalAdapterImageFrameCodec>* ptr) {
  if (ptr) {
    external_adapter_image_manager->runners().GetUITaskRunner()->PostTask(
        [ptr] { delete ptr; });
  }
}

void ExternalAdapterImageFrameCodec::logError(const char* message) const {
  std::string log(message);
  log += " RequestId: " + std::to_string(requestId_) +
         ", URL: " + descriptor_->url;
  EXTERNAL_ADAPTER_IMAGE_LOGE("%s", log.c_str());
}

Dart_Handle ExternalAdapterImageFrameCodec::getNextFrame(Dart_Handle callback) {
  if (!Dart_IsClosure(callback)) {
    EXTERNAL_ADAPTER_IMAGE_LOGE("Invalid callback for getNextFrame. %s",
                                descriptor_->url.c_str());
    return tonic::ToDart("Callback must be a function");
  }

  if (isCanceled()) {
    tonic::DartInvoke(callback, {Dart_Null()});
    return Dart_Null();
  }

  if (status_ == Status::Complete) {
    if (platformImage_.frameCount > 1) {
      // Multiframe images. We do not cache frame, always do decoding
      // progressively.
      if (platformImage_.handle == 0) {
        // Fail to get platform GIF image instance.
        EXTERNAL_ADAPTER_IMAGE_LOGE("No platform image retained. %s",
                                    descriptor_->url.c_str());
        tonic::DartInvoke(callback, {Dart_Null()});
      } else if (callback) {
        getNextMultiframe(callback);
      }
      return Dart_Null();
    } else if (cachedFrame_) {
      tonic::DartInvoke(callback, {tonic::ToDart(cachedFrame_)});
      return Dart_Null();
    } else {
      // This should never happen. But if did happen, we change status to New
      // and restart downloading image.
      status_ = Status::New;
    }
  }

  // Keep the callback as persistent value and record VM state.
  getFrameCallbacks_.emplace_back(UIDartState::Current(), callback);

  if (status_ == Status::Downloading) {
    // We are downloading images from platform.
    return Dart_Null();
  }

  status_ = Status::Downloading;
  requestId_ = external_adapter_image_manager->nextRequestId();

  // Make sure that self is only deallocated on UI thread.
  external_adapter_image_manager->retainCodec(requestId_, this);

  external_adapter_image_provider->request(
      requestId_, *descriptor_,
      [reqId = requestId_](
          ExternalAdapterImageProvider::PlatformImage image,
          ExternalAdapterImageProvider::ReleaseImageCallback&& release) {
        fml::RefPtr<ExternalAdapterImageFrameCodec>* codecRef =
            external_adapter_image_manager->retrieveCodec(reqId);
        if (codecRef == nullptr) {
          // Already canceled.
          if (release) {
            release(image);
          }
          return;
        }

        ExternalAdapterImageFrameCodec* codec = codecRef->get();

        if (image.handle == 0) {
          codec->logError("Fail to get platform image.");
          if (codec->isCanceled()) {
            _ReleaseOnUIThread(codecRef);
            return;
          }

          // Fail to get image from platform.
          external_adapter_image_manager->runners().GetUITaskRunner()->PostTask(
              [codecRef] {
                // Keep ref of codec instance until UI task finishes.
                std::unique_ptr<fml::RefPtr<ExternalAdapterImageFrameCodec>>
                    innerCodecRef(codecRef);
                fml::RefPtr<ExternalAdapterImageFrameCodec> codec(
                    std::move(*innerCodecRef));
                codec->status_ = Status::Complete;  // Complete but failed.
                if (codec->getFrameCallbacks_.empty()) {
                  return;
                }
                auto state =
                    codec->getFrameCallbacks_.front().dart_state().lock();
                if (!state) {
                  codec->logError("Invalid dart state.");
                  return;
                }
                tonic::DartState::Scope scope(state.get());
                Dart_Handle nullFrame = Dart_Null();
                for (const DartPersistentValue& callback :
                     codec->getFrameCallbacks_) {
                  tonic::DartInvoke(callback.value(), {nullFrame});
                }
                codec->getFrameCallbacks_.clear();
              });
        } else {
          bool platformImageAssigned = false;
          {
            // Keep platform image info.
            std::lock_guard<std::recursive_mutex> lock(
                codec->platformImageLock_);
            if (!codec->isCanceled()) {
              codec->assignPlatformImage(
                  image, release);  // For GIF image, we must keep the platform
                                    // image instance.
              platformImageAssigned = true;
            } else if (release) {
              release(image);
            }
          }

          if (!platformImageAssigned) {
            _ReleaseOnUIThread(codecRef);
            return;
          }

          if (image.frameCount > 1) {
            EXTERNAL_ADAPTER_IMAGE_LOGI("Request %u is a GIF.",
                                        codec->requestId_);

            // For GIF we trigger decoding the first frame on Dart thread.
            external_adapter_image_manager->runners()
                .GetUITaskRunner()
                ->PostTask([codecRef] {
                  std::unique_ptr<fml::RefPtr<ExternalAdapterImageFrameCodec>>
                      innerCodecRef(codecRef);
                  fml::RefPtr<ExternalAdapterImageFrameCodec> codec(
                      std::move(*innerCodecRef));
                  codec->getNextMultiframe(nullptr);
                });
            return;
          }

          // Decode single image asynchronously on worker thread.
          uint64_t imageDecodingCost =
              image.width * image.height *
              4;  // approximate memory used for decoding
          external_adapter_image_manager->concurrentCoordinator().postTask(
              imageDecodingCost, [imageDecodingCost, codecRef]() {
                ExternalAdapterImageFrameCodec* codec = codecRef->get();

                ExternalAdapterImageProvider::DecodeResult decodeResult;

                // Check platform image because it might be released by
                // cancelling.
                bool quit = false;
                {
                  std::lock_guard<std::recursive_mutex> lock(
                      codec->platformImageLock_);
                  if (codec->isCanceled()) {
                    quit = true;
                  } else if (codec->platformImage_.handle != 0) {
                    decodeResult = external_adapter_image_provider->decode(
                        codec->platformImage_);  // Synchronous decode in lock.
                  } else {
                    codec->logError("No platform image retained.");
                    quit = true;
                  }
                }

                // Exit lock, check.
                if (quit) {
                  // Tell concurrent runner that previous task finished.
                  external_adapter_image_manager->concurrentCoordinator()
                      .finishTask(imageDecodingCost);
                  _ReleaseOnUIThread(codecRef);
                  return;
                }

                ExternalAdapterImageProvider::Bitmap& bitmap =
                    decodeResult.first;
                ExternalAdapterImageProvider::ReleaseBitmapCallback&
                    releaseBitmap = decodeResult.second;

                if (bitmap.pixels == nullptr) {
                  codec->logError("Fail to decode bitmap.");

                  // Tell concurrent runner that previous task finished.
                  external_adapter_image_manager->concurrentCoordinator()
                      .finishTask(imageDecodingCost);

                  // Decoding failed, release platform image such as UIImage.
                  codec->releasePlatformImage();

                  external_adapter_image_manager->runners()
                      .GetUITaskRunner()
                      ->PostTask([codecRef] {
                        // Keep ref of codec instance until UI task finishes.
                        std::unique_ptr<
                            fml::RefPtr<ExternalAdapterImageFrameCodec>>
                            innerCodecRef(codecRef);
                        fml::RefPtr<ExternalAdapterImageFrameCodec> codec(
                            std::move(*innerCodecRef));
                        codec->status_ =
                            Status::Complete;  // Complete but failed.
                        if (codec->getFrameCallbacks_.empty()) {
                          return;
                        }
                        auto state = codec->getFrameCallbacks_.front()
                                         .dart_state()
                                         .lock();
                        if (!state) {
                          codec->logError("Invalid dart state.");
                          return;
                        }
                        tonic::DartState::Scope scope(state.get());
                        Dart_Handle nullFrame = Dart_Null();
                        for (const DartPersistentValue& callback :
                             codec->getFrameCallbacks_) {
                          tonic::DartInvoke(callback.value(), {nullFrame});
                        }
                        codec->getFrameCallbacks_.clear();
                      });
                } else {
                  if (bitmap.pixelsCopied) {
                    // If the bitmap data is copied, we can actually release the
                    // platform image here for better memory performance.
                    codec->releasePlatformImage();
                  }

                  // Upload the bitmap to GPU on IO thread.
                  external_adapter_image_manager->runners()
                      .GetIOTaskRunner()
                      ->PostTask([imageDecodingCost, bitmap, releaseBitmap,
                                  codecRef]() {
                        ExternalAdapterImageFrameCodec* codec = codecRef->get();

                        auto ioManager =
                            external_adapter_image_manager->ioManager();
                        bool ioStatusValid = ioManager &&
                                             ioManager->GetResourceContext() &&
                                             ioManager->GetSkiaUnrefQueue();
                        SkiaGPUObject<SkImage> uploaded;
                        bool quit = false;

                        {
                          /* If bitmap data is held by platform image, 
                          we must ensure the valid status of bitmap data 
                          until the texture is safely uploaded to GPU. */
                          std::lock_guard<std::recursive_mutex> lock(
                            codec->platformImageLock_);

                          // Check again before we really do uploading to GPU.
                          if (codec->isCanceled()) {
                            quit = true;
                          }
                          else {
                            if (ioStatusValid) {
                              // This line do upload texture to GPU memory.
                              uploaded = UploadTexture(
                                *(const_cast<ExternalAdapterImageProvider::Bitmap*>
                                (&bitmap)));
                            }
                          }
                        }

                        // Release pixels, because pixels might be copied
                        // besides platform image instance.
                        if (releaseBitmap) {
                          releaseBitmap(bitmap);
                        }

                        // Tell concurrent runner that previous task finished.
                        external_adapter_image_manager->concurrentCoordinator()
                            .finishTask(imageDecodingCost);

                        if (quit) {
                          _ReleaseOnUIThread(codecRef);
                          return;
                        }

                        // All done, release platform image instance such as
                        // UIImage.
                        if (!bitmap.pixelsCopied) {
                          codec->releasePlatformImage();
                        }

                        // Go back to UI thread and notify dart widgets.
                        external_adapter_image_manager->runners()
                            .GetUITaskRunner()
                            ->PostTask(fml::MakeCopyable(
                                [codecRef,
                                 textureImage = std::move(uploaded)]() mutable {
                                  // Keep ref of codec instance until UI task
                                  // finishes.
                                  std::unique_ptr<fml::RefPtr<
                                      ExternalAdapterImageFrameCodec>>
                                      innerCodecRef(codecRef);
                                  fml::RefPtr<ExternalAdapterImageFrameCodec>
                                      codec(std::move(*innerCodecRef));
                                  if (codec->isCanceled()) {
                                    return;
                                  }

                                  codec->status_ =
                                      Status::Complete;  // Complete
                                  if (codec->getFrameCallbacks_.empty()) {
                                    return;
                                  }
                                  auto state = codec->getFrameCallbacks_.front()
                                                   .dart_state()
                                                   .lock();
                                  if (!state) {
                                    codec->logError("Invalid dart state.");
                                    return;
                                  }
                                  tonic::DartState::Scope scope(state.get());

                                  // Convert to cached frame.
                                  if (textureImage.get()) {
                                    auto canvasImage =
                                        fml::MakeRefCounted<CanvasImage>();
                                    canvasImage->set_image(
                                        std::move(textureImage));
                                    codec->cachedFrame_ =
                                        fml::MakeRefCounted<FrameInfo>(
                                            std::move(canvasImage),
                                            0 /* duration */);
                                  } else {
                                    codec->logError("Fail to upload GPU.");
                                  }

                                  Dart_Handle frame =
                                      tonic::ToDart(codec->cachedFrame_);
                                  for (const DartPersistentValue& callback :
                                       codec->getFrameCallbacks_) {
                                    tonic::DartInvoke(callback.value(),
                                                      {frame});
                                  }
                                  codec->getFrameCallbacks_.clear();
                                }));
                      });
                }
              });
        }
      });

  return Dart_Null();
}

size_t ExternalAdapterImageFrameCodec::GetAllocationSize() {
  return sizeof(ExternalAdapterImageFrameCodec);
}

bool ExternalAdapterImageFrameCodec::isCanceled() {
  return canceled_.load();
}

void ExternalAdapterImageFrameCodec::cancel() {
  fml::RefPtr<ExternalAdapterImageFrameCodec>* codecRef =
      external_adapter_image_manager->retrieveCodec(requestId_);
  if (codecRef) {
    // Means that cancelling happens before request callback of getNextFrame.
    delete codecRef;
  }

  canceled_.store(true);

  if (status_ == Status::Downloading) {
    external_adapter_image_provider->cancel(requestId_);
  }

  releasePlatformImage();  // For GIF, we also need to release platform image
                           // instance.

  cachedFrame_ = nullptr;
  status_ = Status::Complete;
  getFrameCallbacks_.clear();
  getInfoCallbacks_.clear();
}

void ExternalAdapterImageFrameCodec::assignPlatformImage(
    const ExternalAdapterImageProvider::PlatformImage& image,
    const ExternalAdapterImageProvider::ReleaseImageCallback& release) {
  platformImage_ = image;
  releasePlatformImageCallback_ = release;
  assignedPlatformImage_ = true;
}

void ExternalAdapterImageFrameCodec::releasePlatformImage() {
  std::lock_guard<std::recursive_mutex> lock(platformImageLock_);
  if (platformImage_.handle != 0 && releasePlatformImageCallback_) {
    releasePlatformImageCallback_(platformImage_);
  }
  platformImage_.handle = 0;
  releasePlatformImageCallback_ = nullptr;
}

void ExternalAdapterImageFrameCodec::getNextMultiframe(Dart_Handle callback) {
  if (isCanceled()) {
    return;
  }

  // Keep the callback as persistent value and record VM state.
  if (callback != nullptr) {
    if (Dart_IsClosure(callback)) {
      getFrameCallbacks_.emplace_back(UIDartState::Current(), callback);
    }
  }

  // Make sure that self is only deallocated on UI thread.
  fml::RefPtr<ExternalAdapterImageFrameCodec>* codecRef =
      new fml::RefPtr<ExternalAdapterImageFrameCodec>(this);
  uint64_t imageDecodingCost = platformImage_.width * platformImage_.height *
                               4;  // approximate memory used for decoding
  external_adapter_image_manager->concurrentCoordinator().postTask(
      imageDecodingCost,
      [codecRef, imageDecodingCost, frameIndex = nextFrameIndex_]() {
        ExternalAdapterImageFrameCodec* codec = codecRef->get();

        ExternalAdapterImageProvider::DecodeResult decodeResult;

        // Check platform image because it might be released by cancelling.
        bool quit = false;
        {
          std::lock_guard<std::recursive_mutex> lock(codec->platformImageLock_);
          if (codec->isCanceled()) {
            quit = true;
          } else if (codec->platformImage_.handle != 0) {
            decodeResult = external_adapter_image_provider->decode(
                codec->platformImage_,
                frameIndex);  // Synchronous decode in lock.
          } else {
            codec->logError("No platform image retained.");
            quit = true;
          }
        }

        if (quit) {
          // Tell concurrent runner that previous task finished.
          external_adapter_image_manager->concurrentCoordinator().finishTask(
              imageDecodingCost);
          _ReleaseOnUIThread(codecRef);
          return;
        }

        ExternalAdapterImageProvider::Bitmap& bitmap = decodeResult.first;
        ExternalAdapterImageProvider::ReleaseBitmapCallback& releaseBitmap =
            decodeResult.second;

        if (bitmap.pixels == nullptr) {
          codec->logError("Fail to decode GIF frame.");

          // Tell concurrent runner that previous task finished.
          external_adapter_image_manager->concurrentCoordinator().finishTask(
              imageDecodingCost);

          // Any frame decoding failed, release platform image such as UIImage.
          codec->releasePlatformImage();

          external_adapter_image_manager->runners().GetUITaskRunner()->PostTask(
              [codecRef] {
                // Keep ref of codec instance until UI task finishes.
                std::unique_ptr<fml::RefPtr<ExternalAdapterImageFrameCodec>>
                    innerCodecRef(codecRef);
                fml::RefPtr<ExternalAdapterImageFrameCodec> codec(
                    std::move(*innerCodecRef));
                codec->status_ = Status::Complete;  // Complete but failed.
                if (codec->getFrameCallbacks_.empty()) {
                  return;
                }
                auto state =
                    codec->getFrameCallbacks_.front().dart_state().lock();
                if (!state) {
                  codec->logError("Invalid dart state.");
                  return;
                }
                tonic::DartState::Scope scope(state.get());
                Dart_Handle nullFrame = Dart_Null();
                for (const DartPersistentValue& callback :
                     codec->getFrameCallbacks_) {
                  tonic::DartInvoke(callback.value(), {nullFrame});
                }
                codec->getFrameCallbacks_.clear();
              });
        } else {
          // Upload the bitmap to GPU on IO thread.
          external_adapter_image_manager->runners().GetIOTaskRunner()->PostTask(
              [imageDecodingCost, bitmap, releaseBitmap, codecRef]() {
                ExternalAdapterImageFrameCodec* codec = codecRef->get();

                // Check again before we really do uploading to GPU.
                if (codec->isCanceled()) {
                  // Release pixels, because pixels might be copied besides
                  // platform image instance.
                  if (releaseBitmap) {
                    releaseBitmap(bitmap);
                  }

                  // Tell concurrent runner that previous task finished.
                  external_adapter_image_manager->concurrentCoordinator()
                      .finishTask(imageDecodingCost);

                  _ReleaseOnUIThread(codecRef);
                  return;
                }

                auto ioManager = external_adapter_image_manager->ioManager();
                bool ioStatusValid = ioManager &&
                                     ioManager->GetResourceContext() &&
                                     ioManager->GetSkiaUnrefQueue();

                SkiaGPUObject<SkImage> uploaded;
                if (ioStatusValid) {
                  uploaded = UploadTexture(
                      *(const_cast<ExternalAdapterImageProvider::Bitmap*>(
                          &bitmap)));
                }

                // Release pixels, because pixels might be copied besides
                // platform image instance.
                if (releaseBitmap) {
                  releaseBitmap(bitmap);
                }

                // Tell concurrent runner that previous task finished.
                external_adapter_image_manager->concurrentCoordinator()
                    .finishTask(imageDecodingCost);

                // Go back to UI thread and notify dart widgets.
                external_adapter_image_manager->runners()
                    .GetUITaskRunner()
                    ->PostTask(fml::MakeCopyable([codecRef,
                                                  textureImage = std::move(
                                                      uploaded)]() mutable {
                      // Keep ref of codec instance until UI task finishes.
                      std::unique_ptr<
                          fml::RefPtr<ExternalAdapterImageFrameCodec>>
                          innerCodecRef(codecRef);
                      fml::RefPtr<ExternalAdapterImageFrameCodec> codec(
                          std::move(*innerCodecRef));
                      if (codec->isCanceled()) {
                        return;
                      }

                      codec->status_ = Status::Complete;  // Complete
                      if (codec->getFrameCallbacks_.empty()) {
                        return;
                      }
                      auto state =
                          codec->getFrameCallbacks_.front().dart_state().lock();
                      if (!state) {
                        codec->logError("Invalid dart state.");
                        return;
                      }
                      tonic::DartState::Scope scope(state.get());

                      // Convert to frame with duration info.
                      fml::RefPtr<FrameInfo> frameInfo = NULL;
                      if (textureImage.get()) {
                        auto canvasImage = fml::MakeRefCounted<CanvasImage>();
                        canvasImage->set_image(std::move(textureImage));
                        frameInfo = fml::MakeRefCounted<FrameInfo>(
                            std::move(canvasImage),
                            codec->platformImage_.durationInMs /
                                codec->platformImage_.frameCount);
                      } else {
                        // Any frame failed to be uploaded to GPU, we release
                        // the platform image and stop animation.
                        codec->releasePlatformImage();
                        codec->logError("Fail to upload GPU.");
                      }

                      Dart_Handle frame = tonic::ToDart(frameInfo);
                      for (const DartPersistentValue& callback :
                           codec->getFrameCallbacks_) {
                        tonic::DartInvoke(callback.value(), {frame});
                      }
                      codec->getFrameCallbacks_.clear();
                    }));
              });
        }
      });

  nextFrameIndex_ = (nextFrameIndex_ + 1) % platformImage_.frameCount;
}

static Dart_Handle _DartListOfImageInfo(
    int width,
    int height,
    int frameCount = 1,
    int durationInMs = 0,
    int repetitionCount = ExternalAdapterImageProvider::InfiniteLoop) {
  Dart_Handle result = Dart_NewListOf(Dart_CoreType_Int, 5);
  Dart_ListSetAt(result, 0, tonic::ToDart(width));
  Dart_ListSetAt(result, 1, tonic::ToDart(height));
  Dart_ListSetAt(result, 2, tonic::ToDart(frameCount));
  Dart_ListSetAt(result, 3, tonic::ToDart(durationInMs));
  Dart_ListSetAt(result, 4, tonic::ToDart(repetitionCount));
  return result;
}

static Dart_Handle _DartListOfImageInfo(
    const ExternalAdapterImageProvider::PlatformImage& platformImage) {
  return _DartListOfImageInfo(
      platformImage.width, platformImage.height, platformImage.frameCount,
      platformImage.durationInMs, platformImage.repetitionCount);
}

Dart_Handle ExternalAdapterImageFrameCodec::getImageInfo(Dart_Handle callback) {
  if (!Dart_IsClosure(callback)) {
    EXTERNAL_ADAPTER_IMAGE_LOGE("Invalid callback for getImageSize. %s",
                                descriptor_->url.c_str());
    return tonic::ToDart("Callback must be a function");
  }

  if (isCanceled()) {
    tonic::DartInvoke(callback, {_DartListOfImageInfo(0, 0)});
    return Dart_Null();
  }

  if (assignedPlatformImage_) {
    tonic::DartInvoke(callback, {_DartListOfImageInfo(platformImage_)});
    return Dart_Null();
  }

  // Keep the callback as persistent value and record VM state.
  getInfoCallbacks_.emplace_back(UIDartState::Current(), callback);

  if (requestingImageInfo) {
    // We are downloading images from platform.
    return Dart_Null();
  }

  requestingImageInfo = true;
  ExternalAdapterImageProvider::RequestId reqId =
      external_adapter_image_manager->nextRequestId();

  // Make sure that self is only deallocated on UI thread.
  external_adapter_image_manager->retainCodec(reqId, this);

  external_adapter_image_provider->request(
      reqId, *descriptor_,
      [reqId](ExternalAdapterImageProvider::PlatformImage image,
              ExternalAdapterImageProvider::ReleaseImageCallback&& release) {
        fml::RefPtr<ExternalAdapterImageFrameCodec>* codecRef =
            external_adapter_image_manager->retrieveCodec(reqId);
        if (codecRef == nullptr) {
          if (release) {
            release(image);
          }
          return;
        }

        external_adapter_image_manager->runners().GetUITaskRunner()->PostTask(
            [codecRef, image, release] {
              // Keep ref of codec instance until UI task finishes.
              std::unique_ptr<fml::RefPtr<ExternalAdapterImageFrameCodec>>
                  innerCodecRef(codecRef);
              fml::RefPtr<ExternalAdapterImageFrameCodec> codec(
                  std::move(*innerCodecRef));
              if (image.handle == 0) {
                codec->logError("Fail to get platform image for image info.");
              }

              do {
                if (codec->isCanceled()) {
                  break;
                }
                if (codec->getInfoCallbacks_.empty()) {
                  break;
                }

                auto state =
                    codec->getInfoCallbacks_.front().dart_state().lock();
                if (!state) {
                  codec->logError("Invalid dart state.");
                  break;
                }
                tonic::DartState::Scope scope(state.get());
                Dart_Handle imageInfo = _DartListOfImageInfo(image);
                for (const DartPersistentValue& callback :
                     codec->getInfoCallbacks_) {
                  tonic::DartInvoke(callback.value(), {imageInfo});
                }
              } while (false);

              codec->assignPlatformImage(image, release);
              codec->releasePlatformImage();  // Only record basic image info
                                              // such as width.
              codec->getInfoCallbacks_.clear();
              codec->requestingImageInfo = false;
            });
      });

  return Dart_Null();
}

static bool DecodeDartStringMap(Dart_Handle map,
                                std::map<std::string, std::string>& dest) {
  if (!Dart_IsNull(map)) {
    bool isValidMap = false;
    do {
      if (!Dart_IsMap(map)) {
        break;
      }

      Dart_Handle keys = Dart_MapKeys(map);
      if (!Dart_IsList(keys)) {
        break;
      }

      intptr_t length = 0;
      Dart_ListLength(keys, &length);
      if (length > 0) {
        bool isValidMap = true;
        for (intptr_t i = 0; i < length; i++) {
          Dart_Handle k = Dart_ListGetAt(keys, i);
          if (!Dart_IsString(k)) {
            isValidMap = false;
            break;
          }

          Dart_Handle v = Dart_MapGetAt(map, k);
          if (!Dart_IsString(v)) {
            isValidMap = false;
            break;
          }

          const char* ks = nullptr;
          const char* vs = nullptr;
          Dart_StringToCString(k, &ks);
          Dart_StringToCString(v, &vs);
          if (ks == nullptr || vs == nullptr) {
            isValidMap = false;
            break;
          }
          dest.insert({ks, vs});
        }

        if (!isValidMap) {
          break;
        }
      }

      isValidMap = true;
    } while (false);

    if (!isValidMap) {
      return false;
    }
  }
  return true;
}

/*
 Arguments:
    String url,
    int targetWidth,
    int targetHeight,
    Map<String, String> parameters,
    Map<String, String> extraInfo
 */
static fml::RefPtr<Codec> _ExternalAdapterInstantiateImageCodec(
    Dart_NativeArguments args) {
  if (external_adapter_image_provider == nullptr ||
      external_adapter_image_manager == nullptr) {
    return nullptr;
  }

  // Reevaluate device status for balanced memory usage.
  external_adapter_image_manager->evaluateDeviceStatus();

  std::unique_ptr<ExternalAdapterImageProvider::RequestInfo> descriptor =
      std::make_unique<ExternalAdapterImageProvider::RequestInfo>();

  Dart_Handle exception = nullptr;
  descriptor->url =
      tonic::DartConverter<std::string>::FromArguments(args, 0, exception);
  if (exception) {
    EXTERNAL_ADAPTER_IMAGE_LOGE("Invalid url.");
    return nullptr;
  }

  descriptor->targetWidth =
      tonic::DartConverter<int>::FromArguments(args, 1, exception);
  descriptor->targetHeight =
      tonic::DartConverter<int>::FromArguments(args, 2, exception);
  if (exception) {
    EXTERNAL_ADAPTER_IMAGE_LOGE("Invalid arguments. %s",
                                descriptor->url.c_str());
    return nullptr;
  }

  Dart_Handle parameters = Dart_GetNativeArgument(args, 3);
  if (!DecodeDartStringMap(parameters, descriptor->parameters)) {
    EXTERNAL_ADAPTER_IMAGE_LOGE("Invalid parameters.");
    return nullptr;
  }

  Dart_Handle extraInfo = Dart_GetNativeArgument(args, 4);
  if (!DecodeDartStringMap(extraInfo, descriptor->extraInfo)) {
    EXTERNAL_ADAPTER_IMAGE_LOGE("Invalid extraInfo.");
    return nullptr;
  }

  // Return cdn codec.
  return fml::MakeRefCounted<ExternalAdapterImageFrameCodec>(
      std::move(descriptor));
}

static void ExternalAdapterInstantiateImageCodec(Dart_NativeArguments args) {
  fml::RefPtr<Codec> result = _ExternalAdapterInstantiateImageCodec(args);
  Dart_SetReturnValue(args, tonic::ToDart(result));
}

IMPLEMENT_WRAPPERTYPEINFO(ui, ExternalAdapterImageFrameCodec);

#define FOR_EACH_BINDING(V)                 \
  V(ExternalAdapterImageFrameCodec, cancel) \
  V(ExternalAdapterImageFrameCodec, getImageInfo)

FOR_EACH_BINDING(DART_NATIVE_CALLBACK)

void ExternalAdapterImageFrameCodec::RegisterNatives(
    tonic::DartLibraryNatives* natives) {
  natives->Register({
      {"ExternalAdapterInstantiateImageCodec",
       ExternalAdapterInstantiateImageCodec, 5, true},
  });
  natives->Register({FOR_EACH_BINDING(DART_REGISTER_NATIVE)});
}

}  // namespace flutter
