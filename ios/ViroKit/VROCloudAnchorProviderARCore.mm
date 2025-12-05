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

// Structure to track pending cloud anchor operations
@interface VROCloudAnchorOperation : NSObject
@property (nonatomic, strong) GARAnchor *garAnchor;
@property (nonatomic, copy) void (^onSuccess)(NSString *cloudAnchorId, ARAnchor *resolvedAnchor);
@property (nonatomic, copy) void (^onFailure)(NSString *error);
@property (nonatomic, assign) BOOL isHosting; // YES for host, NO for resolve
@end

@implementation VROCloudAnchorOperation
@end

@interface VROCloudAnchorProviderARCore ()
@property (nonatomic, strong) GARSession *garSession;
@property (nonatomic, strong) NSMutableDictionary<NSString *, VROCloudAnchorOperation *> *pendingOperations;
@end

@implementation VROCloudAnchorProviderARCore

+ (BOOL)isAvailable {
    return YES;
}

- (nullable instancetype)initWithARSession:(ARSession *)session {
    self = [super init];
    if (self) {
        NSError *error = nil;

        // Create GARSession configuration
        GARSessionConfiguration *config = [[GARSessionConfiguration alloc] init];
        config.cloudAnchorMode = GARCloudAnchorModeEnabled;

        // Initialize GARSession with ARKit session
        _garSession = [GARSession sessionWithARSession:session error:&error];
        if (error) {
            pabort("Failed to create GARSession: %s", [[error localizedDescription] UTF8String]);
            return nil;
        }

        // Apply configuration
        [_garSession setConfiguration:config error:&error];
        if (error) {
            pabort("Failed to configure GARSession: %s", [[error localizedDescription] UTF8String]);
            return nil;
        }

        _pendingOperations = [NSMutableDictionary new];
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
    GARAnchor *garAnchor = [_garSession hostCloudAnchor:anchor
                                                TTLDays:ttlDays
                                                  error:&error];

    if (error || !garAnchor) {
        if (onFailure) {
            NSString *errorMsg = error ? [error localizedDescription] : @"Failed to start hosting";
            onFailure(errorMsg);
        }
        return;
    }

    // Track the operation
    VROCloudAnchorOperation *operation = [VROCloudAnchorOperation new];
    operation.garAnchor = garAnchor;
    operation.onSuccess = onSuccess;
    operation.onFailure = onFailure;
    operation.isHosting = YES;

    // Use the GARAnchor's identifier as the key
    NSString *key = [[NSUUID UUID] UUIDString];
    _pendingOperations[key] = operation;

    pinfo("Cloud anchor hosting started, tracking operation: %s", [key UTF8String]);
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
    GARAnchor *garAnchor = [_garSession resolveCloudAnchorWithIdentifier:cloudAnchorId
                                                                   error:&error];

    if (error || !garAnchor) {
        if (onFailure) {
            NSString *errorMsg = error ? [error localizedDescription] : @"Failed to start resolving";
            onFailure(errorMsg);
        }
        return;
    }

    // Track the operation
    VROCloudAnchorOperation *operation = [VROCloudAnchorOperation new];
    operation.garAnchor = garAnchor;
    operation.onSuccess = onSuccess;
    operation.onFailure = onFailure;
    operation.isHosting = NO;

    _pendingOperations[cloudAnchorId] = operation;

    pinfo("Cloud anchor resolve started for: %s", [cloudAnchorId UTF8String]);
}

- (void)cancelAllOperations {
    // Remove all pending operations
    // Note: GARSession doesn't have explicit cancel, operations will just be ignored
    [_pendingOperations removeAllObjects];
    pinfo("All cloud anchor operations cancelled");
}

- (void)updateWithFrame:(ARFrame *)frame {
    if (!_garSession || !frame) {
        return;
    }

    // Update GARSession with the current frame
    NSError *error = nil;
    GARFrame *garFrame = [_garSession update:frame error:&error];

    if (error) {
        pwarn("GARSession update error: %s", [[error localizedDescription] UTF8String]);
        return;
    }

    // Check status of pending operations
    NSMutableArray<NSString *> *completedKeys = [NSMutableArray new];

    for (NSString *key in _pendingOperations) {
        VROCloudAnchorOperation *operation = _pendingOperations[key];
        GARAnchor *garAnchor = operation.garAnchor;

        // Check the cloud state
        GARCloudAnchorState state = garAnchor.cloudState;

        switch (state) {
            case GARCloudAnchorStateNone:
            case GARCloudAnchorStateTaskInProgress:
                // Still in progress, continue waiting
                break;

            case GARCloudAnchorStateSuccess: {
                // Operation succeeded
                [completedKeys addObject:key];

                if (operation.onSuccess) {
                    NSString *cloudId = garAnchor.cloudIdentifier;
                    // Get the underlying ARKit anchor if available
                    // For resolved anchors, we need to create one from the transform
                    ARAnchor *arAnchor = [[ARAnchor alloc] initWithTransform:garAnchor.transform];
                    operation.onSuccess(cloudId, arAnchor);
                }

                pinfo("Cloud anchor operation succeeded: %s", [key UTF8String]);
                break;
            }

            case GARCloudAnchorStateErrorInternal:
            case GARCloudAnchorStateErrorNotAuthorized:
            case GARCloudAnchorStateErrorResourceExhausted:
            case GARCloudAnchorStateErrorHostingDatasetProcessingFailed:
            case GARCloudAnchorStateErrorCloudIdNotFound:
            case GARCloudAnchorStateErrorResolvingSdkVersionTooOld:
            case GARCloudAnchorStateErrorResolvingSdkVersionTooNew:
            case GARCloudAnchorStateErrorHostingServiceUnavailable: {
                // Operation failed
                [completedKeys addObject:key];

                if (operation.onFailure) {
                    NSString *errorMsg = [self errorMessageForState:state];
                    operation.onFailure(errorMsg);
                }

                pwarn("Cloud anchor operation failed: %s - %s", [key UTF8String],
                      [[self errorMessageForState:state] UTF8String]);
                break;
            }
        }
    }

    // Remove completed operations
    for (NSString *key in completedKeys) {
        [_pendingOperations removeObjectForKey:key];
    }
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

- (nullable instancetype)initWithARSession:(ARSession *)session {
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
