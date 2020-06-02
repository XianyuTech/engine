// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHELL_PLATFORM_ANDROID_EXTERNAL_ADAPTER_IMAGE_JNI_H
#define SHELL_PLATFORM_ANDROID_EXTERNAL_ADAPTER_IMAGE_JNI_H

#include <jni.h>
#include <map>
#include <string>

namespace flutter {
namespace ExternalAdapterImageJNIBridge {

class ImageAdapter {
 public:
  virtual void onImageFinished(JNIEnv* env,
                               uint32_t id,
                               jobject image,
                               const std::vector<jobject>& bitmaps,
                               int frameCount,
                               double duration,
                               bool isSingleBitmapAnimated,
                               bool isPremul) = 0;
};

bool Setup(JNIEnv* env, ImageAdapter* adapter);

bool Request(uint32_t id,
             const std::string& url,
             int targetWidth,
             int targetHeight,
             const std::map<std::string, std::string>& parameters,
             const std::map<std::string, std::string>& extraInfo);

bool Cancel(uint32_t id);

void StartAnimation(JNIEnv* env, jobject image);
void StopAnimation(JNIEnv* env, jobject image);

}  // namespace ExternalAdapterImageJNIBridge
}  // namespace flutter

#endif  // SHELL_PLATFORM_ANDROID_EXTERNAL_ADAPTER_IMAGE_JNI_H
