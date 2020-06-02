// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_FLUTTEREXTERNALADAPTERIMAGEPROVIDER_H_
#define FLUTTER_FLUTTEREXTERNALADAPTERIMAGEPROVIDER_H_

#import <UIKit/UIKit.h>

NS_ASSUME_NONNULL_BEGIN

@protocol FlutterExternalAdapterImageRequest <NSObject>

@required

- (void)cancel;

@end

@protocol FlutterExternalAdapterImageProvider <NSObject>

@required

/**
 * Request for UIImage instance by url and other parameters.
 *
 * @param url URL for the resource.
 * @param targetWidth Desired width of the image in pixels.
 * @param targetHeight Desired height of the image in pixels.
 * @param parameters Additional paramerters which may affect image pixels.
 * @param extraInfo Additional paramerters which not affect image pixels.
 * @param callback A callback lambda providing the UIImage instance.
 * @return Request handle which is cancellable.
 */
- (id<FlutterExternalAdapterImageRequest>)request:(NSString*)url
                                      targetWidth:(NSInteger)targetWidth
                                     targetHeight:(NSInteger)targetHeight
                                       parameters:(NSDictionary<NSString*, NSString*>*)parameters
                                        extraInfo:(NSDictionary<NSString*, NSString*>*)extraInfo
                                         callback:(void (^)(UIImage* image))callback;

/**
 * Log key information requesting, decoding images.
 */
- (void)log:(NSString*)log;

@end

NS_ASSUME_NONNULL_END;

#endif  // FLUTTER_FLUTTEREXTERNALADAPTERIMAGEPROVIDER_H_
