// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package io.flutter.plugin.external_adapter_image;

import java.util.Map;
import java.util.Vector;
import java.util.Arrays;
import android.graphics.Bitmap;
import android.support.annotation.NonNull;
import android.support.annotation.Keep;

/**
 * Interface of external image provider.
 */
@Keep
public interface ExternalAdapterImageProvider {

  /**
   * An interface wrapping a bitmap object who acts as the provider
   * of each frame. It is the responsibility of adapter to 
   * subclass AnimatedBitmap and schedule each frame in user
   * handler loop.
   */
  @Keep
  public interface AnimatedBitmap {
    public abstract Bitmap getBufferBitmap();
    public abstract double getDuration(); // In seconds.
    public abstract int getFrameCount();
    public abstract void start();
    public abstract void stop();
  }

  /**
   * Image class representing decoded single frame and multiframe images.
   */
  @Keep
  public final class Image {
    private final Vector<Bitmap> bitmaps;
    private final double duration; // in seconds
    private final int frameCount;
    private final boolean isSingleBitmapAnimated;
    private final AnimatedBitmap animatedBitmap;
  
    public Image(@NonNull Bitmap bitmap) {
      this.bitmaps = new Vector<Bitmap>();
      this.bitmaps.add(bitmap);
      this.duration = 0;
      this.frameCount = 1;
      this.isSingleBitmapAnimated = false;
      this.animatedBitmap = null;
    }
  
    public Image(@NonNull Vector<Bitmap> bitmaps, double duration) {
      this.bitmaps = bitmaps;
      this.duration = duration < 0 ? 1.0 : duration;
      this.frameCount = bitmaps.size();
      this.isSingleBitmapAnimated = false;
      this.animatedBitmap = null;
    }

    public Image(@NonNull AnimatedBitmap bitmap) {
      // Animated image and frames pixels are progressively decoded into bitmap.
      this.bitmaps = new Vector<Bitmap>();
      this.bitmaps.add(bitmap.getBufferBitmap());
      this.duration = bitmap.getDuration();
      this.frameCount = bitmap.getFrameCount();
      this.isSingleBitmapAnimated = true;
      this.animatedBitmap = bitmap;
    }

    public double getDuration() {
      return duration;
    }

    public int getFrameCount() {
      return frameCount;
    }

    public int getBitmapCount() {
      return bitmaps.size();
    }

    public boolean isMultiframe() {
      return frameCount > 1;
    }

    public boolean isSingleBitmapAnimated() {
      return this.isSingleBitmapAnimated;
    }

    public Bitmap getBitmap() {
      if (bitmaps.size() > 0) {
        return bitmaps.firstElement();
      }
      return null;
    }

    public Bitmap getBitmap(int frameIndex) {
      return frameIndex < bitmaps.size() ? bitmaps.elementAt(frameIndex) : null;
    }

    public Bitmap[] getBitmaps() {
      Object[] array = bitmaps.toArray();
      return Arrays.copyOf(array, array.length, Bitmap[].class);
    }

    public void start() {
      if (animatedBitmap != null) {
        animatedBitmap.start();
      }
    }

    public void stop() {
      if (animatedBitmap != null) {
        animatedBitmap.stop();
      }
    }
  }

  /**
   * Each request for image returns an ExternalAdapterImageRequest instance.
   * So that Flutter engine could cancel the request.
   */
  @Keep
  public interface Request {
    public abstract void cancel();
  }

  /**
   * Response object is provided in request method. And when external 
   * image adapter finishes downloading an image, call response.finish
   * and pack the image as decoded bitmaps using Image class.
   */
  @Keep
  public interface Response {
    public abstract void finish(Image image);
  }

  @Keep
  public abstract Request request(@NonNull String url, 
    int targetWidth,
    int targetHeight,
    Map<String, String> parameters,
    Map<String, String> extraInfo,
    @NonNull Response response
  );

  public abstract void log(String log);

}
