//
//  VROCloudAnchorProviderReactVision.h
//  ViroKit
//
//  Copyright © 2026 ReactVision. All rights reserved.
//  Proprietary and Confidential
//
//  iOS bridge between ViroCore's AR session and the ReactVisionCCA C++ library.
//  Drop-in replacement for VROCloudAnchorProviderARCore on iOS when the
//  ReactVision backend is preferred over Google Cloud Anchors.
//
//  Usage (in VROARSessioniOS.mm):
//    if (provider == VROCloudAnchorProvider::ReactVision) {
//        _cloudAnchorProviderRV = [[VROCloudAnchorProviderReactVision alloc]
//            initWithApiKey:@"<key>" projectId:@"<uuid>"];
//    }
//

#import <Foundation/Foundation.h>
#import <ARKit/ARKit.h>

NS_ASSUME_NONNULL_BEGIN

@interface VROCloudAnchorProviderReactVision : NSObject

/**
 * Returns YES if the ReactVisionCCA library is linked and ready.
 * Always returns YES (no runtime framework dependency like GARSession).
 */
+ (BOOL)isAvailable;

/**
 * Designated initialiser.
 *
 * @param apiKey    ReactVision API key (from platform.reactvision.xyz dashboard)
 * @param projectId ReactVision project UUID
 * @param endpoint  Optional custom base URL; pass nil to use the default
 */
- (nullable instancetype)initWithApiKey:(NSString *)apiKey
                              projectId:(NSString *)projectId
                               endpoint:(nullable NSString *)endpoint;

/**
 * Host an anchor to the ReactVision cloud.
 *
 * Call this in response to VROARSessioniOS::hostCloudAnchor().
 *
 * @param anchor    The ARAnchor whose transform will be hosted.
 * @param frame     The current ARFrame (used for feature extraction).
 * @param ttlDays   Time-to-live 1-3650 days (ReactVision default: 365).
 * @param onSuccess Block called on success with the cloud anchor ID string.
 * @param onFailure Block called on failure with a human-readable error.
 */
- (void)hostAnchor:(ARAnchor *)anchor
             frame:(ARFrame *)frame
           ttlDays:(NSInteger)ttlDays
         onSuccess:(void (^)(NSString *cloudAnchorId))onSuccess
         onFailure:(void (^)(NSString *error))onFailure;

/**
 * Resolve a previously hosted anchor by its cloud ID.
 *
 * @param cloudAnchorId The cloud anchor ID returned by hostAnchor.
 * @param frame         The current ARFrame (used for localisation).
 * @param onSuccess     Block called with the resolved world-space transform.
 * @param onFailure     Block called on failure with a human-readable error.
 */
- (void)resolveCloudAnchorWithId:(NSString *)cloudAnchorId
                           frame:(ARFrame *)frame
                       onSuccess:(void (^)(NSString *cloudAnchorId,
                                          simd_float4x4 transform))onSuccess
                       onFailure:(void (^)(NSString *error))onFailure;

/**
 * Cancel all pending host/resolve operations.
 */
- (void)cancelAllOperations;

@end

NS_ASSUME_NONNULL_END
