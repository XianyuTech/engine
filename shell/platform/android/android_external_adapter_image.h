// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHELL_PLATFORM_ANDROID_EXTERNAL_ADAPTER_IMAGE_H
#define SHELL_PLATFORM_ANDROID_EXTERNAL_ADAPTER_IMAGE_H

#include <vector>
#include <jni.h>
#include <android/bitmap.h>

namespace flutter {

struct AndroidPlatformImageInfo {
  bool isPremul;
  bool isSingleBitmapAnimated;
  jobject bitmap; // Used when isSingleBitmapAnimated == true
  std::vector<void*> frames;
  AndroidBitmapInfo bitmapInfo;
};

void InstallFlutterExternalAdapterImageProvider(JNIEnv* env);

}  // namespace flutter

#endif  // SHELL_PLATFORM_ANDROID_EXTERNAL_ADAPTER_IMAGE_H
