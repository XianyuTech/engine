// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_LIB_UI_EXTERNAL_ADAPTER_IMAGE_DECODE_COORD_H_
#define FLUTTER_LIB_UI_EXTERNAL_ADAPTER_IMAGE_DECODE_COORD_H_

#include <list>
#include "flutter/fml/concurrent_message_loop.h"

namespace flutter {

// A class to balance memory consumption while decoding images.
class ExternalAdapterImageDecodeCoordinator {
 public:
  ExternalAdapterImageDecodeCoordinator(
      std::shared_ptr<fml::ConcurrentTaskRunner> concurrentTaskRunner);

  void postTask(uint64_t memoryUsing, const fml::closure& task);
  void finishTask(uint64_t memoryUsing);

  void updateCapacity(uint32_t maxConcurrentCount, uint64_t maxMemoryUsing);

 private:
  static constexpr uint64_t MegaBytes = 1024 * 1024;

  std::shared_ptr<fml::ConcurrentTaskRunner> concurrentTaskRunner_;
  uint32_t maxConcurrentCount_ = 4;
  uint64_t maxMemoryUsing_ = 20 * MegaBytes;

  std::atomic<uint64_t> memoryUsed_ = 0;
  std::atomic<uint32_t> taskRunning_ = 0;

  std::mutex taskMutex_;
  std::list<std::pair<uint64_t, fml::closure>> taskList_;

  bool allowToRun(uint64_t memoryUsing);
  void runTask(uint64_t memoryUsing, const fml::closure& task);
  void checkTasks();
};

}  // namespace flutter

#endif  // FLUTTER_LIB_UI_EXTERNAL_ADAPTER_IMAGE_DECODE_COORD_H_
