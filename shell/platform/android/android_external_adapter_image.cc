// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_external_adapter_image.h"
#include "android_external_adapter_image_jni.h"
#include "flutter/fml/platform/android/jni_util.h"
#include "flutter/lib/ui/external_adapter_image/ExternalAdapterImage.h"

#include <map>
#include <mutex>
#include <string>

namespace flutter {

class ExternalAdapterImageAndroidAdapter
    : public ExternalAdapterImageProvider,
      public ExternalAdapterImageJNIBridge::ImageAdapter {
 private:
  std::mutex _imageRequestLock;
  std::map<RequestId, RequestCallback> _imageRequests;

 public:
  virtual void request(RequestId rid,
                       const RequestInfo& requestInfo,
                       RequestCallback&& callback) override {
    {
      std::lock_guard<std::mutex> guard(_imageRequestLock);
      _imageRequests[rid] = callback;
    }

    bool result = ExternalAdapterImageJNIBridge::Request(
        rid, requestInfo.url, requestInfo.targetWidth, requestInfo.targetHeight,
        requestInfo.parameters, requestInfo.extraInfo);
    if (!result) {
      std::lock_guard<std::mutex> guard(_imageRequestLock);
      _imageRequests.erase(rid);
    }
  }

  virtual void cancel(RequestId rid) override {
    ExternalAdapterImageJNIBridge::Cancel(rid);
    std::lock_guard<std::mutex> guard(_imageRequestLock);
    _imageRequests.erase(rid);
  }

  virtual DecodeResult decode(PlatformImage image,
                              int frameIndex = 0) override {
    do {
      if (image.handle == 0 || image.userData == nullptr) {
        break;
      }

      AndroidPlatformImageInfo* imageInfo =
          static_cast<AndroidPlatformImageInfo*>(image.userData);

      Bitmap bitmap;
      bitmap.width = (int)imageInfo->bitmapInfo.width;
      bitmap.height = (int)imageInfo->bitmapInfo.height;
      bitmap.alphaType =
          imageInfo->isPremul ? AlphaType::Premul : AlphaType::Unpremul;
      switch (imageInfo->bitmapInfo.format) {
        case ANDROID_BITMAP_FORMAT_RGBA_8888:
          bitmap.colorType = ColorType::RGBA8888;
          break;
        case ANDROID_BITMAP_FORMAT_RGB_565:
          bitmap.colorType = ColorType::RGB565;
          break;
        case ANDROID_BITMAP_FORMAT_RGBA_4444:
          bitmap.colorType = ColorType::ARGB4444;
          break;
        case ANDROID_BITMAP_FORMAT_A_8:
          bitmap.colorType = ColorType::Alpha8;
          break;
        default:
          break;
      }
      bitmap.bytesPerRow = imageInfo->bitmapInfo.stride;

      if (imageInfo->isSingleBitmapAnimated) {
        // Get pixels from imageInfo->bitmap.
        JNIEnv* env = fml::jni::AttachCurrentThread();
        if (env == nullptr) {
          break;
        }

        void* jbitmapPixels = nullptr;
        int pixelsResult = AndroidBitmap_lockPixels(env, 
            imageInfo->bitmap, &jbitmapPixels);
        if (pixelsResult != ANDROID_BITMAP_RESULT_SUCCESS ||
            jbitmapPixels == nullptr) {
          break;
        }

        void* copyPixels = (void*)malloc(
           imageInfo->bitmapInfo.stride * 
           imageInfo->bitmapInfo.height);
        if (copyPixels == nullptr) {
          AndroidBitmap_unlockPixels(env, imageInfo->bitmap);
          break;
        }

        memcpy(copyPixels, jbitmapPixels,
            imageInfo->bitmapInfo.stride * 
            imageInfo->bitmapInfo.height);
        AndroidBitmap_unlockPixels(env, imageInfo->bitmap);        

        bitmap.pixels = copyPixels;
        bitmap.pixelsCopied = true;
      } else {
        if (imageInfo->frames.empty()) {
          break;
        }
        
        if (frameIndex < (int)(imageInfo->frames.size())) {
          bitmap.pixels = imageInfo->frames[frameIndex];
        } else {
          bitmap.pixels = imageInfo->frames[0];
        }
        bitmap.pixelsCopied = false;
      }

      return std::make_pair(bitmap, [](Bitmap bitmap) {
        if (bitmap.pixelsCopied && bitmap.pixels != nullptr) {
          free(bitmap.pixels);
        }
      });
    } while (false);

    return std::make_pair(Bitmap(), [](Bitmap bitmap) {
      // Nothing
    });
  }

  virtual bool shouldEvaluateDeviceStatus() override { return false; }

  virtual void evaluateDeviceStatus(uint32_t& cpuCoreCount,
                                    uint64_t& maxMemoryUsing) override {
    const uint64_t MegaBytes = 1024 * 1024;
    cpuCoreCount = 4;
    maxMemoryUsing = 40 * MegaBytes;
  }

  virtual void log(LogLevel level, const char* string) override {
    printf("%s\n", string);
  }

  virtual void onImageFinished(JNIEnv* env,
                               uint32_t id,
                               jobject image,
                               const std::vector<jobject>& bitmaps,
                               int frameCount,
                               double duration,
                               bool isSingleBitmapAnimated,
                               bool isPremul) override {
    RequestCallback callback = nullptr;
    {
      std::lock_guard<std::mutex> guard(_imageRequestLock);
      auto it = _imageRequests.find(id);
      if (it != _imageRequests.end()) {
        callback = it->second;
        _imageRequests.erase(it);
      }
    }

    if (callback) {
      PlatformImage platformImage;
      if (image == nullptr || bitmaps.empty()) {
        callback(platformImage, nullptr);
        return;
      }

      // Get first frame bitmap.
      AndroidBitmapInfo bitmapInfo;
      if (AndroidBitmap_getInfo(env, bitmaps[0], &bitmapInfo) !=
          ANDROID_BITMAP_RESULT_SUCCESS) {
        callback(platformImage, nullptr);
        return;
      }

      AndroidPlatformImageInfo* imageInfo = new AndroidPlatformImageInfo();
      imageInfo->isSingleBitmapAnimated = isSingleBitmapAnimated;

      if (isSingleBitmapAnimated) {
        // We retain the Java bitmap instance.
        if (bitmaps.size() == 1) {
          imageInfo->bitmap = env->NewGlobalRef(bitmaps.front());
          if (imageInfo->bitmap == nullptr) {
            delete imageInfo;
            callback(platformImage, nullptr);
            return;
          }

          // Retain the image instance.
          platformImage.handle = (PlatformHandle)(env->NewGlobalRef(image));
          if (platformImage.handle == 0) {
            env->DeleteGlobalRef(imageInfo->bitmap);  
            delete imageInfo;
            callback(platformImage, nullptr);
            return;
          }

          // Notify to start animation.
          ExternalAdapterImageJNIBridge::StartAnimation(env, image);
        } else {
          delete imageInfo;
          callback(platformImage, nullptr);
          return;
        }
      } else {
        // Copy bitmap pixels.
        bool failed = false;
        for (jobject b : bitmaps) {
          if (b == nullptr) {
            failed = true;
            break;
          }

          // Get bitmap pixels.
          void* jbitmapPixels = nullptr;
          int pixelsResult = AndroidBitmap_lockPixels(env, b, &jbitmapPixels);
          if (pixelsResult != ANDROID_BITMAP_RESULT_SUCCESS ||
              jbitmapPixels == nullptr) {
            failed = true;
            break;
          }

          // Copy pixels.
          void* copyPixels = (void*)malloc(bitmapInfo.stride * bitmapInfo.height);
          if (copyPixels == nullptr) {
            AndroidBitmap_unlockPixels(env, b);
            failed = true;
            break;
          }

          memcpy(copyPixels, jbitmapPixels,
                bitmapInfo.stride * bitmapInfo.height);
          AndroidBitmap_unlockPixels(env, b);

          imageInfo->frames.push_back(copyPixels);
        }

        if (failed) {
          // Release malloced frame pixels.
          for (void* buffer : imageInfo->frames) {
            free(buffer);
          }
          delete imageInfo;
          callback(platformImage, nullptr);
          return;
        }

        // Telling decoder that we have the image indeed.
        platformImage.handle = 1;  
      }

      imageInfo->isPremul = isPremul;
      imageInfo->bitmapInfo = bitmapInfo;

      platformImage.width = (int)bitmapInfo.width;
      platformImage.height = (int)bitmapInfo.height;
      platformImage.userData = static_cast<void*>(imageInfo);

      // Multiframe image.
      if (frameCount > 1) {
        platformImage.frameCount = frameCount;
        platformImage.durationInMs = duration * 1000;
        platformImage.repetitionCount = InfiniteLoop;
      }

      callback(platformImage, [](PlatformImage image) {
        if (image.userData) {
          AndroidPlatformImageInfo* imageInfo =
              static_cast<AndroidPlatformImageInfo*>(image.userData);
          if (imageInfo->isSingleBitmapAnimated) {
            // Notify to stop animation and release Java instance.
            JNIEnv* env = fml::jni::AttachCurrentThread();
            if (env != nullptr) {
              // Stop animation.
              ExternalAdapterImageJNIBridge::StopAnimation(env, 
                  static_cast<jobject>((void*)(image.handle)));

              if ((void*)image.handle != nullptr && image.handle != 1) {
                env->DeleteGlobalRef(static_cast<jobject>
                    ((void*)(image.handle)));
              }

              if (imageInfo->bitmap != nullptr) {
                env->DeleteGlobalRef(imageInfo->bitmap);
              }
            }
          } else {
            // Release bitmap pixels
            for (void* buffer : imageInfo->frames) {
              if (buffer) {
                free(buffer);
              }
            }            
          }
          delete imageInfo;
        }
      });
    }
  }
};

void InstallFlutterExternalAdapterImageProvider(JNIEnv* env) {
  auto adapter = new flutter::ExternalAdapterImageAndroidAdapter();
  if (ExternalAdapterImageJNIBridge::Setup(env, adapter)) {
    flutter::SetExternalAdapterImageProvider(adapter);
  } else {
    delete adapter;
  }
}

}  // namespace flutter
