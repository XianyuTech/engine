// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_LIB_UI_EXTERNAL_ADAPTER_IMAGE_CODEC_H_
#define FLUTTER_LIB_UI_EXTERNAL_ADAPTER_IMAGE_CODEC_H_

#include "ExternalAdapterImage.h"
#include "flutter/common/task_runners.h"
#include "flutter/fml/concurrent_message_loop.h"
#include "flutter/lib/ui/io_manager.h"
#include "flutter/lib/ui/painting/codec.h"

namespace flutter {

class ExternalAdapterImageFrameCodec : public Codec {
  DEFINE_WRAPPERTYPEINFO();

 public:
  ExternalAdapterImageFrameCodec(
      std::unique_ptr<ExternalAdapterImageProvider::RequestInfo> descriptor);
  ~ExternalAdapterImageFrameCodec();

  static void RegisterNatives(tonic::DartLibraryNatives* natives);

  // Codec
  int frameCount() const override;
  int repetitionCount() const override;
  Dart_Handle getNextFrame(Dart_Handle callback) override;

  // tonic::DartWrappable
  size_t GetAllocationSize() override;

  // Cancel image request from platform.
  void cancel();
  bool isCanceled();

  // Only get the image info without uploading the texture.
  Dart_Handle getImageInfo(Dart_Handle callback);

 private:
  std::unique_ptr<ExternalAdapterImageProvider::RequestInfo> descriptor_;

  enum class Status { New, Downloading, Complete };
  Status status_ = Status::New;

  bool destroyed_ = false;
  bool assignedPlatformImage_ = false;
  bool requestingImageInfo = false;
  std::atomic<bool> canceled_ = false;
  fml::RefPtr<FrameInfo> cachedFrame_;
  std::vector<DartPersistentValue> getFrameCallbacks_;
  std::vector<DartPersistentValue> getInfoCallbacks_;

  // Platform image info.
  std::recursive_mutex platformImageLock_;
  ExternalAdapterImageProvider::RequestId requestId_;
  ExternalAdapterImageProvider::PlatformImage platformImage_;
  ExternalAdapterImageProvider::ReleaseImageCallback
      releasePlatformImageCallback_;

  // For multiframe images.
  int nextFrameIndex_ = 0;

  FML_FRIEND_MAKE_REF_COUNTED(ExternalAdapterImageFrameCodec);
  FML_FRIEND_REF_COUNTED_THREAD_SAFE(ExternalAdapterImageFrameCodec);

  void assignPlatformImage(
      const ExternalAdapterImageProvider::PlatformImage& image,
      const ExternalAdapterImageProvider::ReleaseImageCallback& release);
  void releasePlatformImage();
  void getNextMultiframe(Dart_Handle callback);

  void logError(const char* message) const;
};

extern ExternalAdapterImageProvider* GetExternalAdapterImageProvider();

extern void InitializeExternalAdapterImageManager(
    const TaskRunners& runners,
    std::shared_ptr<fml::ConcurrentTaskRunner> concurrent_task_runner,
    fml::WeakPtr<IOManager> io_manager);

}  // namespace flutter

#endif  // FLUTTER_LIB_UI_EXTERNAL_ADAPTER_IMAGE_CODEC_H_
