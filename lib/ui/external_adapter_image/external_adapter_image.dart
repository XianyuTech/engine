// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// @dart = 2.6
part of dart.ui;

Codec ExternalAdapterInstantiateImageCodec(
  String url,
{
  int targetWidth,
  int targetHeight,
  Map<String, String> parameters,
  Map<String, String> extraInfo
}) {
  return _externalAdapterInstantiateImageCodec(
    url,
    targetWidth ?? _kDoNotResizeDimension,
    targetHeight ?? _kDoNotResizeDimension,
    parameters,
    extraInfo
  );
}

Codec _externalAdapterInstantiateImageCodec(
  String url,
  int targetWidth,
  int targetHeight,
  Map<String, String> parameters,
  Map<String, String> extraInfo
) native 'ExternalAdapterInstantiateImageCodec';

/// A handle to an external adapter image codec.
@pragma('vm:entry-point')
class ExternalAdapterImageFrameCodec extends Codec {
  /// This class is created by the engine, and should not be instantiated
  /// or extended directly.
  @pragma('vm:entry-point')
  ExternalAdapterImageFrameCodec(): super._();

  /// This class is created by the engine, and should not be instantiated
  /// or extended directly.
  @pragma('vm:entry-point')
  ExternalAdapterImageFrameCodec._(): super._();
  
  /// Cancel the image request.
  void cancel() native 'ExternalAdapterImageFrameCodec_cancel';
  
  /// Get image info without uploading as texture.
  Future<List<int>> getImageInfo() {
    return _futurize(_getImageInfo);
  }

  String _getImageInfo(_Callback<List<int>> callback) native 'ExternalAdapterImageFrameCodec_getImageInfo';
}
