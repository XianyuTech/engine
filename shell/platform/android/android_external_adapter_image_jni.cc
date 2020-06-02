// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_external_adapter_image_jni.h"
#include "flutter/fml/log_level.h"
#include "flutter/fml/logging.h"
#include "flutter/fml/macros.h"
#include "flutter/fml/platform/android/jni_util.h"
#include "flutter/fml/size.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace flutter {
namespace ExternalAdapterImageJNIBridge {

static fml::jni::ScopedJavaGlobalRef<jclass>*
    g_external_adapter_image_bridge_class = nullptr;
static jmethodID g_bridge_request_method = nullptr;
static jmethodID g_bridge_cancel_method = nullptr;
static ImageAdapter* g_adapter = nullptr;

static fml::jni::ScopedJavaGlobalRef<jclass>*
    g_provider_dot_image_class = nullptr;
static jmethodID g_provider_dot_image_start_method = nullptr;
static jmethodID g_provider_dot_image_stop_method = nullptr;

static void ImageTaskFinish(JNIEnv* env,
                            jclass jcaller,
                            jstring id,
                            jobject image,
                            jobjectArray bitmaps,
                            jint bitmapCount,
                            jint frameCount,
                            jdouble duration,
                            jboolean isSingleBitmapAnimated,
                            jboolean isPremul) {
  if (g_adapter) {
    const char* cId = env->GetStringUTFChars(id, NULL);
    if (cId) {
      uint32_t nId = static_cast<uint32_t>(std::stoul(std::string(cId)));
      env->ReleaseStringUTFChars(id, cId);

      // Put bitmaps to vector
      std::vector<jobject> vBitmaps;
      if (bitmaps != nullptr) {
        for (int i = 0; i < bitmapCount; i++) {
          vBitmaps.push_back(env->GetObjectArrayElement(bitmaps, i));
        }
      }

      g_adapter->onImageFinished(env, nId, image, vBitmaps, frameCount, duration,
                                 isSingleBitmapAnimated == JNI_TRUE,
                                 isPremul == JNI_TRUE);
    }
  }
}

bool Setup(JNIEnv* env, ImageAdapter* adapter) {
  static const JNINativeMethod call_native_methods[] = {
      {
          .name = "notifyExternalImageFinish",
          .signature = "("
                       "Ljava/lang/String;"
                       "Lio/flutter/plugin/external_adapter_image/"
                       "ExternalAdapterImageProvider$Image;"
                       "[Landroid/graphics/Bitmap;"
                       "I"
                       "I"
                       "D"
                       "Z"
                       "Z"
                       ")V",
          .fnPtr = reinterpret_cast<void*>(&ImageTaskFinish),
      },
  };

  g_external_adapter_image_bridge_class =
      new fml::jni::ScopedJavaGlobalRef<jclass>(
          env, env->FindClass("io/flutter/plugin/external_adapter_image/"
                              "ExternalAdapterImageBridge"));
  if (g_external_adapter_image_bridge_class->is_null()) {
    FML_LOG(ERROR) << "Failed to find ExternalAdapterImageBridge Class.";
    return false;
  }

  if (env->RegisterNatives(g_external_adapter_image_bridge_class->obj(),
                           call_native_methods,
                           fml::size(call_native_methods)) != 0) {
    FML_LOG(ERROR)
        << "Failed to register native methods with ExternalAdapterImageBridge.";
    return false;
  }

  g_bridge_request_method = env->GetStaticMethodID(
      g_external_adapter_image_bridge_class->obj(), "request",
      "(Ljava/lang/String;Ljava/lang/String;IILjava/lang/String;Ljava/lang/"
      "String;)Z");
  if (g_bridge_request_method == nullptr) {
    FML_LOG(ERROR)
        << "Could not locate ExternalAdapterImageBridge::request method.";
    return false;
  }

  g_bridge_cancel_method =
      env->GetStaticMethodID(g_external_adapter_image_bridge_class->obj(),
                             "cancel", "(Ljava/lang/String;)V");
  if (g_bridge_cancel_method == nullptr) {
    FML_LOG(ERROR)
        << "Could not locate ExternalAdapterImageBridge::cancel method.";
    return false;
  }

  g_provider_dot_image_class = 
      new fml::jni::ScopedJavaGlobalRef<jclass>(
          env, env->FindClass("io/flutter/plugin/external_adapter_image/"
                              "ExternalAdapterImageProvider$Image"));

  if (g_provider_dot_image_class->is_null()) {
    FML_LOG(ERROR) << "Failed to find ExternalAdapterImageProvider.Image Class.";
    return false;    
  }

  g_provider_dot_image_start_method = env->GetMethodID(
      g_provider_dot_image_class->obj(), "start", "()V");
  if (g_provider_dot_image_start_method == nullptr) {
    FML_LOG(ERROR)
        << "Could not locate ExternalAdapterImageBridge.Image::start method.";
    return false;
  }

  g_provider_dot_image_stop_method = env->GetMethodID(
      g_provider_dot_image_class->obj(), "stop", "()V");
  if (g_provider_dot_image_stop_method == nullptr) {
    FML_LOG(ERROR)
        << "Could not locate ExternalAdapterImageBridge.Image::stop method.";
    return false;
  }

  g_adapter = adapter;
  return true;
}

static std::string Map2JSONString(
    const std::map<std::string, std::string>& map) {
  using namespace rapidjson;
  Document document;
  auto& allocator = document.GetAllocator();
  Value root(kObjectType);
  Value key(kStringType);
  Value value(kStringType);

  for (auto it = map.begin(); it != map.end(); ++it) {
    key.SetString(it->first.c_str(), allocator);
    value.SetString(it->second.c_str(), allocator);
    root.AddMember(key, value, allocator);
  }

  StringBuffer buffer;
  Writer<StringBuffer> writer(buffer);
  root.Accept(writer);
  return buffer.GetString();
}

bool Request(uint32_t id,
             const std::string& url,
             int targetWidth,
             int targetHeight,
             const std::map<std::string, std::string>& parameters,
             const std::map<std::string, std::string>& extraInfo) {
  JNIEnv* env = fml::jni::AttachCurrentThread();
  if (env == nullptr) {
    return false;
  }

  // Convert parameters & extraInfo to json string.
  auto jParams = fml::jni::StringToJavaString(env, Map2JSONString(parameters));
  auto jExtraInfo =
      fml::jni::StringToJavaString(env, Map2JSONString(extraInfo));
  auto jId = fml::jni::StringToJavaString(env, std::to_string(id));
  auto jUrl = fml::jni::StringToJavaString(env, url);

  return (env->CallStaticBooleanMethod(
              g_external_adapter_image_bridge_class->obj(),
              g_bridge_request_method, jId.obj(), jUrl.obj(), targetWidth,
              targetHeight, jParams.obj(), jExtraInfo.obj()) == JNI_TRUE);
}

bool Cancel(uint32_t id) {
  JNIEnv* env = fml::jni::AttachCurrentThread();
  if (env == nullptr) {
    return false;
  }

  auto jId = fml::jni::StringToJavaString(env, std::to_string(id));
  env->CallStaticVoidMethod(g_external_adapter_image_bridge_class->obj(),
                            g_bridge_cancel_method, jId.obj());
  return true;
}

void StartAnimation(JNIEnv* env, jobject image) {
  if (env == nullptr) {
    return;
  }
  env->CallVoidMethod(image, g_provider_dot_image_start_method);
}

void StopAnimation(JNIEnv* env, jobject image) {
  if (env == nullptr) {
    return;
  }
  env->CallVoidMethod(image, g_provider_dot_image_stop_method);
}

}  // namespace ExternalAdapterImageJNIBridge
}  // namespace flutter
