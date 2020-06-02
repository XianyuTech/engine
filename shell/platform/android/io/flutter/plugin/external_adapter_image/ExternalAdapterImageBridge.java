// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package io.flutter.plugin.external_adapter_image;

import android.os.Build;
import android.graphics.Bitmap;
import android.support.annotation.NonNull;
import io.flutter.embedding.engine.FlutterJNI;
import java.util.Map;
import java.util.HashMap;
import java.util.concurrent.ConcurrentHashMap;
import org.json.JSONObject;
import org.json.JSONArray;
import android.support.annotation.Keep;

@Keep
public class ExternalAdapterImageBridge {

  static private class Response implements ExternalAdapterImageProvider.Response {
    final String id;

    Response(String id) {
      this.id = id;
    }

    @Override
    public void finish(ExternalAdapterImageProvider.Image image) {
      ExternalAdapterImageBridge.finish(id, image);
    }
  }

  static private class Task {
    final ExternalAdapterImageProvider.Request request;
    final Response response;

    Task(ExternalAdapterImageProvider.Request request, Response response) {
      this.request = request;
      this.response = response;
    }
  }

  private static Map<String, Task> allTasks = new ConcurrentHashMap<>();

  public static boolean request(String id, String url, int width, int height, String paramsJson, String paramsExtraInfo) {
    ExternalAdapterImageProvider provider = FlutterJNI.getExternalAdapterImageProvider();
    if (provider == null) {
      return false;
    }

    Map<String, String> paramsMap = new HashMap<>();
    Map<String, String> infoMap = new HashMap<>();
    try {
      JSONObject params = new JSONObject(paramsJson);
      JSONArray paramKeys = params.names();
      if (paramKeys != null) {
        for (int i = 0; i < paramKeys.length(); i ++) {
          String key = paramKeys.getString(i);
          paramsMap.put(key, params.getString(key));
        }
      }

      JSONObject info = new JSONObject(paramsExtraInfo);
      JSONArray infoKeys = info.names();
      if (infoKeys != null) {
        for (int i = 0; i < infoKeys.length(); i ++) {
          String key = infoKeys.getString(i);
          infoMap.put(key, info.getString(key));
        }
      }
    }
    catch (Exception e) {
      e.printStackTrace();
    }

    Response response = new Response(id);
    ExternalAdapterImageProvider.Request request = 
      provider.request(url, width, height, paramsMap, infoMap, response);

    Task task = new Task(request, response);
    allTasks.put(id, task);
    
    return true;
  }

  public static void cancel(String id) {
    Task task = allTasks.remove(id);
    if (task != null && task.request != null) {
      task.request.cancel();
    }
  }

  public static void finish(String id, ExternalAdapterImageProvider.Image image) {
    boolean isPremul = false;
    if (image != null) {
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR1) {
        Bitmap bitmap = image.getBitmap();
        if (bitmap != null) {
          isPremul = bitmap.isPremultiplied();
        }
      }
    }
    ExternalAdapterImageBridge.notifyExternalImageFinish(
      id,
      image,
      image != null ? image.getBitmaps() : null,
      image != null ? image.getBitmapCount() : 0,
      image != null ? image.getFrameCount() : 1,
      image != null ? image.getDuration() : 0,
      image != null ? image.isSingleBitmapAnimated() : false,
      isPremul);
    allTasks.remove(id);
  }

  private static native void notifyExternalImageFinish(
    String id,
    ExternalAdapterImageProvider.Image image,
    Bitmap[] bitmaps,
    int bitmapCount,
    int frameCount,
    double duration,
    boolean isSingleBitmapAnimated,
    boolean isPremul);
}

