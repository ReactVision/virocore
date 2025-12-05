//
//  VROCloudAnchorProviderARCore.h
//  ViroKit
//
//  Copyright © 2024 Viro Media. All rights reserved.
//
//  Permission is hereby granted, free of charge, to any person obtaining
//  a copy of this software and associated documentation files (the
//  "Software"), to deal in the Software without restriction, including
//  without limitation the rights to use, copy, modify, merge, publish,
//  distribute, sublicense, and/or sell copies of the Software, and to
//  permit persons to whom the Software is furnished to do so, subject to
//  the following conditions:
//
//  The above copyright notice and this permission notice shall be included
//  in all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#ifndef VROCloudAnchorProviderARCore_h
#define VROCloudAnchorProviderARCore_h

#import <Foundation/Foundation.h>
#import <ARKit/ARKit.h>

#if __has_include(<ARCore/ARCore.h>)
#define ARCORE_AVAILABLE 1
#import <ARCore/ARCore.h>
#else
#define ARCORE_AVAILABLE 0
#endif

#include <memory>
#include <string>
#include <functional>
#include <map>

class VROARAnchor;

/**
 * Wrapper class for ARCore Cloud Anchors on iOS.
 * This class manages the GARSession and handles hosting/resolving cloud anchors.
 */
API_AVAILABLE(ios(12.0))
@interface VROCloudAnchorProviderARCore : NSObject

/**
 * Initialize the cloud anchor provider.
 * The API key is read from Info.plist (GARAPIKey).
 * @return Initialized provider, or nil if ARCore SDK is not available or API key missing
 */
- (nullable instancetype)init;

/**
 * Check if the ARCore Cloud Anchors SDK is available.
 */
+ (BOOL)isAvailable;

/**
 * Host an anchor to the cloud.
 * @param anchor The ARKit anchor to host
 * @param ttlDays Time-to-live in days (1-365)
 * @param onSuccess Callback with the cloud anchor ID on success
 * @param onFailure Callback with error message on failure
 */
- (void)hostAnchor:(ARAnchor *)anchor
           ttlDays:(NSInteger)ttlDays
         onSuccess:(void (^)(NSString *cloudAnchorId, ARAnchor *resolvedAnchor))onSuccess
         onFailure:(void (^)(NSString *error))onFailure;

/**
 * Resolve a cloud anchor by its ID.
 * @param cloudAnchorId The cloud anchor ID to resolve
 * @param onSuccess Callback with the resolved anchor on success
 * @param onFailure Callback with error message on failure
 */
- (void)resolveAnchor:(NSString *)cloudAnchorId
            onSuccess:(void (^)(NSString *cloudAnchorId, ARAnchor *resolvedAnchor))onSuccess
            onFailure:(void (^)(NSString *error))onFailure;

/**
 * Cancel all pending cloud anchor operations.
 */
- (void)cancelAllOperations;

/**
 * Must be called each frame to process cloud anchor updates.
 * @param frame The current ARFrame
 */
- (void)updateWithFrame:(ARFrame *)frame;

@end

#endif /* VROCloudAnchorProviderARCore_h */
