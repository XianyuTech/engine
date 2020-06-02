// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "FlutterExternalAdapterImageDelegate.h"
#import "flutter/shell/platform/darwin/ios/framework/Headers/FlutterExternalAdapterImageProvider.h"

#include "flutter/lib/ui/external_adapter_image/ExternalAdapterImage.h"

#include <map>
#include <mutex>
#include <string>

namespace flutter {
#define NSSTRING(cstr)                                                                \
  ((__bridge_transfer NSString*)(CFStringCreateWithCString(NULL, (const char*)(cstr), \
                                                           kCFStringEncodingUTF8)))

static id<FlutterExternalAdapterImageProvider> ExternalImageProvider;

static NSDictionary* ConvertMap(const std::map<std::string, std::string> source) {
  NSMutableDictionary* result = [[NSMutableDictionary alloc] init];
  for (auto&& entry : source) {
    result[NSSTRING(entry.first.c_str())] = NSSTRING(entry.second.c_str());
  }
  return result;
}

class ExternalAdapterImageIOSAdapter : public ExternalAdapterImageProvider {
 private:
  std::mutex _imageRequestLock;
  std::map<RequestId, __strong id<FlutterExternalAdapterImageRequest>> _imageRequests;

 public:
  virtual void request(RequestId rid,
                       const RequestInfo& requestInfo,
                       RequestCallback&& callback) override {
    if (!ExternalImageProvider) {
      PlatformImage platformImage;
      callback(platformImage, nullptr);
      return;
    }

    @autoreleasepool {
      RequestCallback callbackCpy = callback;
      RequestInfo requestInfoCpy = requestInfo;

      // Switch off Dart UI thread for safety.
      dispatch_async(dispatch_get_global_queue(0, DISPATCH_QUEUE_PRIORITY_HIGH), ^{
        {
          /* Save a nil operation value to request dict so that if the real
           operation is not yet added to _imageRequests and downloadImageWithURL is completed, we
           misthink that the image is cancelled. But this rarely happens. */
          std::lock_guard<std::mutex> lock(_imageRequestLock);
          _imageRequests[rid] = nil;
        }

        NSString* url = [NSString stringWithUTF8String:requestInfoCpy.url.c_str()];

        __block BOOL requestAlreadyFinished = NO;  // Image library may return image synchronously?
        id<FlutterExternalAdapterImageRequest> request = [ExternalImageProvider
                 request:url
             targetWidth:(NSInteger)requestInfoCpy.targetWidth
            targetHeight:(NSInteger)requestInfoCpy.targetHeight
              parameters:ConvertMap(requestInfoCpy.parameters)
               extraInfo:ConvertMap(requestInfoCpy.extraInfo)
                callback:^(UIImage* _Nonnull image) {
                  requestAlreadyFinished = YES;
                  {
                    std::lock_guard<std::mutex> lock(_imageRequestLock);
                    auto findRequest = _imageRequests.find(rid);
                    if (findRequest == _imageRequests.end()) {
                      // not found operation which means this image request was cancelled
                      image = nil;  // Let code below do callback.
                    } else {
                      _imageRequests.erase(rid);
                    }
                  }

                  // Switch to non-main thread to Dart, more safe.
                  void (^backToDart)() = ^{
                    PlatformImage platformImage;
                    if (image == nil) {
                      callbackCpy(platformImage, nullptr);
                    } else {
                      platformImage.handle =
                          (uintptr_t)CFBridgingRetain(image);  // retain the UIImage.
                      platformImage.width = (int)(image.size.width * image.scale);
                      platformImage.height = (int)(image.size.height * image.scale);
                      if ([image.images count] > 0) {
                        // This is an animated image.
                        platformImage.frameCount = (int)[image.images count];
                        platformImage.durationInMs = image.duration * 1000;
                        platformImage.repetitionCount = InfiniteLoop;
                      }

                      callbackCpy(platformImage, [](PlatformImage image) {
                        @autoreleasepool {
                          if (image.handle != 0) {
                            CFBridgingRelease((CFTypeRef)(image.handle));
                          }
                        }
                      });
                    }
                  };

                  if ([NSThread isMainThread]) {
                    dispatch_async(dispatch_get_global_queue(0, DISPATCH_QUEUE_PRIORITY_HIGH),
                                   backToDart);
                  } else {
                    backToDart();
                  }
                }];

        if (!requestAlreadyFinished && request) {
          // Save the real opertion to dict.
          std::lock_guard<std::mutex> lock(_imageRequestLock);
          if (!requestAlreadyFinished) {
            _imageRequests[rid] = request;
          }
        }
      });
    }
  }

  virtual void cancel(RequestId rid) override {
    @autoreleasepool {
      id<FlutterExternalAdapterImageRequest> request = nil;
      {
        std::lock_guard<std::mutex> lock(_imageRequestLock);
        auto findRequest = _imageRequests.find(rid);
        if (findRequest != _imageRequests.end()) {
          request = findRequest->second;
          _imageRequests.erase(findRequest);
        }
      }

      if (request) {
        [request cancel];
      }
    }
  }

  virtual DecodeResult decode(PlatformImage image, int frameIndex = 0) override {
    @autoreleasepool {
      Bitmap bitmap;
      UIImage* platformImage = (__bridge id)((void*)image.handle);
      do {
        if (platformImage == nil) {
          break;
        }

        // Animated images, we decode specific frame.
        if (platformImage.images != nil) {
          if (frameIndex < (int)[platformImage.images count]) {
            platformImage = platformImage.images[frameIndex];
          } else {
            platformImage = [platformImage.images firstObject];
          }
        }

        CGImageRef cgImage = platformImage.CGImage;
        if (!cgImage) {
          break;
        }

        bitmap.width = (int)CGImageGetWidth(cgImage);
        bitmap.height = (int)CGImageGetHeight(cgImage);

        if (bitmap.width == 0 || bitmap.height == 0) {
          break;
        }

        CGSize pixelSizeToUseForTexture = CGSizeMake(bitmap.width, bitmap.height);

        BOOL isLitteEndian = YES;
        BOOL alphaFirst = NO;
        BOOL premultiplied = NO;

        // For resized or incompatible image: redraw
        bitmap.pixels = calloc(
            1, (int)pixelSizeToUseForTexture.width * (int)pixelSizeToUseForTexture.height * 4);
        if (bitmap.pixels == nullptr) {
          break;
        }

        CGColorSpaceRef genericRGBColorspace = CGColorSpaceCreateDeviceRGB();
        CGContextRef imageContext = CGBitmapContextCreate(
            bitmap.pixels, (size_t)pixelSizeToUseForTexture.width,
            (size_t)pixelSizeToUseForTexture.height, 8, (size_t)pixelSizeToUseForTexture.width * 4,
            genericRGBColorspace, kCGBitmapByteOrderDefault | kCGImageAlphaPremultipliedLast);
        //        CGContextSetBlendMode(imageContext, kCGBlendModeCopy); // From Technical Q&A
        //        QA1708: http://developer.apple.com/library/ios/#qa/qa1708/_index.html
        CGContextDrawImage(
            imageContext,
            CGRectMake(0.0, 0.0, pixelSizeToUseForTexture.width, pixelSizeToUseForTexture.height),
            cgImage);
        CGContextRelease(imageContext);
        CGColorSpaceRelease(genericRGBColorspace);
        isLitteEndian = YES;
        alphaFirst = YES;
        premultiplied = YES;
        bitmap.bytesPerRow = pixelSizeToUseForTexture.width * 4;
        bitmap.colorType = ColorType::RGBA8888;

        // Pixels are always copied.
        bitmap.pixelsCopied = true;

        if (premultiplied) {
          bitmap.alphaType = AlphaType::Premul;
        } else {
          bitmap.alphaType = AlphaType::Unpremul;
        }

        return std::make_pair(bitmap, [=](Bitmap bitmap) {
          if (bitmap.pixels) {
            free((void*)bitmap.pixels);
          }
        });
      } while (NO);

      return std::make_pair(Bitmap(), [](Bitmap bitmap) {
        // Nothing
      });
    }
  }

  virtual bool shouldEvaluateDeviceStatus() override { return false; }

  virtual void evaluateDeviceStatus(uint32_t& cpuCoreCount, uint64_t& maxMemoryUsing) override {
    @autoreleasepool {
      const uint64_t MegaBytes = 1024 * 1024;

      cpuCoreCount = 2;

      uint64_t physicalMemory = (uint64_t)([NSProcessInfo processInfo].physicalMemory);
      if (physicalMemory <= 1024 * MegaBytes) {
        maxMemoryUsing = 20 * MegaBytes;
      } else if (physicalMemory <= 2 * 1024 * MegaBytes) {
        maxMemoryUsing = 40 * MegaBytes;
      } else {
        maxMemoryUsing = 60 * MegaBytes;
      }
    }
  }

  virtual void log(LogLevel level, const char* string) override {
    if (ExternalImageProvider) {
      [ExternalImageProvider log:NSSTRING(string)];
    } else {
      printf("%s\n", string);
    }
  }
};

void InstallFlutterExternalAdapterImageProvider(id<FlutterExternalAdapterImageProvider> provider) {
  ExternalImageProvider = provider;
  flutter::SetExternalAdapterImageProvider(new flutter::ExternalAdapterImageIOSAdapter());
}
}
