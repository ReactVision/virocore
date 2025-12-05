//
//  VROCloudAnchorProviderARCore.mm
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

#import "VROCloudAnchorProviderARCore.h"
#import "VROLog.h"

#if ARCORE_AVAILABLE

@interface VROCloudAnchorProviderARCore ()
@property (nonatomic, strong) GARSession *garSession;
@property (nonatomic, strong) NSMutableArray<GARHostCloudAnchorFuture *> *hostFutures;
@property (nonatomic, strong) NSMutableArray<GARResolveCloudAnchorFuture *> *resolveFutures;
@end

@implementation VROCloudAnchorProviderARCore

+ (BOOL)isAvailable {
    return YES;
}

- (nullable instancetype)init {
    self = [super init];
    if (self) {
        NSError *error = nil;

        // Get API key from Info.plist
        NSString *apiKey = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"GARAPIKey"];
        if (!apiKey || apiKey.length == 0) {
            pwarn("GARAPIKey not found in Info.plist. Cloud anchors require a Google Cloud API key.");
            return nil;
        }

        // Initialize GARSession with API key
        // bundleIdentifier:nil uses the app's main bundle identifier
        _garSession = [GARSession sessionWithAPIKey:apiKey
                                   bundleIdentifier:nil
                                              error:&error];
        if (error || !_garSession) {
            pabort("Failed to create GARSession: %s",
                   error ? [[error localizedDescription] UTF8String] : "Unknown error");
            return nil;
        }

        // Create and apply configuration with cloud anchors enabled
        GARSessionConfiguration *config = [[GARSessionConfiguration alloc] init];
        config.cloudAnchorMode = GARCloudAnchorModeEnabled;

        [_garSession setConfiguration:config error:&error];
        if (error) {
            pabort("Failed to configure GARSession: %s", [[error localizedDescription] UTF8String]);
            return nil;
        }

        _hostFutures = [NSMutableArray new];
        _resolveFutures = [NSMutableArray new];
        pinfo("ARCore Cloud Anchors initialized successfully");
    }
    return self;
}

- (void)hostAnchor:(ARAnchor *)anchor
           ttlDays:(NSInteger)ttlDays
         onSuccess:(void (^)(NSString *cloudAnchorId, ARAnchor *resolvedAnchor))onSuccess
         onFailure:(void (^)(NSString *error))onFailure {

    if (!_garSession) {
        if (onFailure) {
            onFailure(@"GARSession not initialized");
        }
        return;
    }

    NSError *error = nil;

    // Use the new async API with completion handler
    GARHostCloudAnchorFuture *future = [_garSession hostCloudAnchor:anchor
                                                            TTLDays:ttlDays
                                                  completionHandler:^(NSString * _Nullable cloudAnchorId,
                                                                      GARCloudAnchorState cloudState) {
        if (cloudState == GARCloudAnchorStateSuccess && cloudAnchorId) {
            pinfo("Cloud anchor hosted successfully: %s", [cloudAnchorId UTF8String]);
            if (onSuccess) {
                // Create an ARAnchor from the original anchor's transform
                ARAnchor *resultAnchor = [[ARAnchor alloc] initWithTransform:anchor.transform];
                onSuccess(cloudAnchorId, resultAnchor);
            }
        } else {
            NSString *errorMsg = [self errorMessageForState:cloudState];
            pwarn("Cloud anchor hosting failed: %s", [errorMsg UTF8String]);
            if (onFailure) {
                onFailure(errorMsg);
            }
        }
    }
                                                              error:&error];

    if (error || !future) {
        NSString *errorMsg = error ? [error localizedDescription] : @"Failed to start hosting";
        pwarn("Failed to start cloud anchor hosting: %s", [errorMsg UTF8String]);
        if (onFailure) {
            onFailure(errorMsg);
        }
        return;
    }

    // Keep a reference to the future
    [_hostFutures addObject:future];
    pinfo("Cloud anchor hosting started");
}

- (void)resolveAnchor:(NSString *)cloudAnchorId
            onSuccess:(void (^)(NSString *cloudAnchorId, ARAnchor *resolvedAnchor))onSuccess
            onFailure:(void (^)(NSString *error))onFailure {

    if (!_garSession) {
        if (onFailure) {
            onFailure(@"GARSession not initialized");
        }
        return;
    }

    NSError *error = nil;

    // Use the new async API with completion handler
    GARResolveCloudAnchorFuture *future = [_garSession resolveCloudAnchorWithIdentifier:cloudAnchorId
                                                                      completionHandler:^(GARAnchor * _Nullable anchor,
                                                                                          GARCloudAnchorState cloudState) {
        if (cloudState == GARCloudAnchorStateSuccess && anchor) {
            pinfo("Cloud anchor resolved successfully: %s", [cloudAnchorId UTF8String]);
            if (onSuccess) {
                // Create an ARAnchor from the resolved GARAnchor's transform
                ARAnchor *resultAnchor = [[ARAnchor alloc] initWithTransform:anchor.transform];
                onSuccess(cloudAnchorId, resultAnchor);
            }
        } else {
            NSString *errorMsg = [self errorMessageForState:cloudState];
            pwarn("Cloud anchor resolve failed: %s", [errorMsg UTF8String]);
            if (onFailure) {
                onFailure(errorMsg);
            }
        }
    }
                                                                                  error:&error];

    if (error || !future) {
        NSString *errorMsg = error ? [error localizedDescription] : @"Failed to start resolving";
        pwarn("Failed to start cloud anchor resolving: %s", [errorMsg UTF8String]);
        if (onFailure) {
            onFailure(errorMsg);
        }
        return;
    }

    // Keep a reference to the future
    [_resolveFutures addObject:future];
    pinfo("Cloud anchor resolve started for: %s", [cloudAnchorId UTF8String]);
}

- (void)cancelAllOperations {
    // Cancel all pending host operations
    for (GARHostCloudAnchorFuture *future in _hostFutures) {
        [future cancel];
    }
    [_hostFutures removeAllObjects];

    // Cancel all pending resolve operations
    for (GARResolveCloudAnchorFuture *future in _resolveFutures) {
        [future cancel];
    }
    [_resolveFutures removeAllObjects];

    pinfo("All cloud anchor operations cancelled");
}

- (void)updateWithFrame:(ARFrame *)frame {
    if (!_garSession || !frame) {
        return;
    }

    // Update GARSession with the current frame
    // This is required for cloud anchors to work - it syncs ARKit tracking with GARSession
    NSError *error = nil;
    GARFrame *garFrame = [_garSession update:frame error:&error];

    if (error) {
        pwarn("GARSession update error: %s", [[error localizedDescription] UTF8String]);
        return;
    }

    // Clean up completed futures
    NSMutableArray<GARHostCloudAnchorFuture *> *completedHostFutures = [NSMutableArray new];
    for (GARHostCloudAnchorFuture *future in _hostFutures) {
        if (future.state == GARFutureStateDone) {
            [completedHostFutures addObject:future];
        }
    }
    [_hostFutures removeObjectsInArray:completedHostFutures];

    NSMutableArray<GARResolveCloudAnchorFuture *> *completedResolveFutures = [NSMutableArray new];
    for (GARResolveCloudAnchorFuture *future in _resolveFutures) {
        if (future.state == GARFutureStateDone) {
            [completedResolveFutures addObject:future];
        }
    }
    [_resolveFutures removeObjectsInArray:completedResolveFutures];
}

- (NSString *)errorMessageForState:(GARCloudAnchorState)state {
    switch (state) {
        case GARCloudAnchorStateNone:
            return @"No cloud anchor state";
        case GARCloudAnchorStateTaskInProgress:
            return @"Operation in progress";
        case GARCloudAnchorStateSuccess:
            return @"Success";
        case GARCloudAnchorStateErrorInternal:
            return @"Internal error occurred";
        case GARCloudAnchorStateErrorNotAuthorized:
            return @"Not authorized. Check your Google Cloud API key configuration.";
        case GARCloudAnchorStateErrorResourceExhausted:
            return @"Resource exhausted. Too many cloud anchors or API quota exceeded.";
        case GARCloudAnchorStateErrorHostingDatasetProcessingFailed:
            return @"Hosting failed. Not enough visual data captured around the anchor.";
        case GARCloudAnchorStateErrorCloudIdNotFound:
            return @"Cloud anchor ID not found. The anchor may have expired.";
        case GARCloudAnchorStateErrorResolvingSdkVersionTooOld:
            return @"SDK version too old to resolve this anchor.";
        case GARCloudAnchorStateErrorResolvingSdkVersionTooNew:
            return @"SDK version too new to resolve this anchor.";
        case GARCloudAnchorStateErrorHostingServiceUnavailable:
            return @"Cloud anchor hosting service is unavailable.";
        default:
            return @"Unknown error";
    }
}

@end

#else // ARCORE_AVAILABLE not defined

@implementation VROCloudAnchorProviderARCore

+ (BOOL)isAvailable {
    return NO;
}

- (nullable instancetype)init {
    pwarn("ARCore SDK not available. Cloud anchors require ARCore/CloudAnchors pod.");
    return nil;
}

- (void)hostAnchor:(ARAnchor *)anchor
           ttlDays:(NSInteger)ttlDays
         onSuccess:(void (^)(NSString *cloudAnchorId, ARAnchor *resolvedAnchor))onSuccess
         onFailure:(void (^)(NSString *error))onFailure {
    if (onFailure) {
        onFailure(@"ARCore SDK not available. Add ARCore/CloudAnchors pod to your Podfile.");
    }
}

- (void)resolveAnchor:(NSString *)cloudAnchorId
            onSuccess:(void (^)(NSString *cloudAnchorId, ARAnchor *resolvedAnchor))onSuccess
            onFailure:(void (^)(NSString *error))onFailure {
    if (onFailure) {
        onFailure(@"ARCore SDK not available. Add ARCore/CloudAnchors pod to your Podfile.");
    }
}

- (void)cancelAllOperations {
    // No-op
}

- (void)updateWithFrame:(ARFrame *)frame {
    // No-op
}

@end

#endif // ARCORE_AVAILABLE
