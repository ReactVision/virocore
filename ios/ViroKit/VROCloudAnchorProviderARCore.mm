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
#import "VROGeospatialAnchor.h"
#import "VROMatrix4f.h"
#import "VROQuaternion.h"
#include <string>

#if ARCORE_AVAILABLE

@interface VROCloudAnchorProviderARCore ()
@property (nonatomic, strong) GARSession *garSession;
@property (nonatomic, strong) NSMutableArray<GARHostCloudAnchorFuture *> *hostFutures;
@property (nonatomic, strong) NSMutableArray<GARResolveCloudAnchorFuture *> *resolveFutures;
@property (nonatomic, assign) BOOL geospatialModeEnabled;
@property (nonatomic, strong) GARFrame *currentGARFrame;
@property (nonatomic, strong) NSMutableDictionary<NSString *, GARAnchor *> *geospatialAnchors;
@end

@implementation VROCloudAnchorProviderARCore

+ (BOOL)isAvailable {
    pinfo("[ViroAR] ARCore SDK is available (ARCORE_AVAILABLE=1)");
    return YES;
}

- (nullable instancetype)init {
    self = [super init];
    if (self) {
        NSError *error = nil;

        pinfo("[ViroAR] Initializing ARCore Cloud Anchors provider...");

        // Get API key from Info.plist
        NSString *apiKey = [[NSBundle mainBundle] objectForInfoDictionaryKey:@"GARAPIKey"];
        if (!apiKey || apiKey.length == 0) {
            pwarn("[ViroAR] GARAPIKey not found in Info.plist. Cloud anchors require a Google Cloud API key.");
            pwarn("[ViroAR] Add <key>GARAPIKey</key><string>YOUR_API_KEY</string> to Info.plist");
            return nil;
        }

        pinfo("[ViroAR] GARAPIKey found (length: %lu)", (unsigned long)apiKey.length);

        // Initialize GARSession with API key
        // bundleIdentifier:nil uses the app's main bundle identifier
        _garSession = [GARSession sessionWithAPIKey:apiKey
                                   bundleIdentifier:nil
                                              error:&error];
        if (error || !_garSession) {
            pwarn("[ViroAR] Failed to create GARSession: %s",
                   error ? [[error localizedDescription] UTF8String] : "Unknown error");
            return nil;
        }

        pinfo("[ViroAR] GARSession created successfully");

        // Create and apply configuration with cloud anchors enabled
        GARSessionConfiguration *config = [[GARSessionConfiguration alloc] init];
        config.cloudAnchorMode = GARCloudAnchorModeEnabled;

        [_garSession setConfiguration:config error:&error];
        if (error) {
            pwarn("[ViroAR] Failed to configure GARSession: %s", [[error localizedDescription] UTF8String]);
            return nil;
        }

        _hostFutures = [NSMutableArray new];
        _resolveFutures = [NSMutableArray new];
        _geospatialAnchors = [NSMutableDictionary new];
        _geospatialModeEnabled = NO;
        pinfo("[ViroAR] ARCore Cloud Anchors initialized successfully - ready for hosting/resolving");
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
        // Only log non-initialization errors
        if (error.code != 0) {
            pwarn("GARSession update error: %s", [[error localizedDescription] UTF8String]);
        }
        return;
    }

    // Store the current GARFrame for geospatial queries
    _currentGARFrame = garFrame;

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

// ========================================================================
// Geospatial API Implementation
// ========================================================================

#if ARCORE_GEOSPATIAL_AVAILABLE

+ (BOOL)isGeospatialAvailable {
    pinfo("[ViroAR] ARCore Geospatial SDK is available (ARCORE_GEOSPATIAL_AVAILABLE=1)");
    return YES;
}

- (BOOL)isGeospatialModeSupported {
    if (!_garSession) {
        return NO;
    }
    return [_garSession isGeospatialModeSupported:GARGeospatialModeEnabled];
}

- (void)setGeospatialModeEnabled:(BOOL)enabled {
    if (!_garSession) {
        pwarn("[ViroAR] Cannot set geospatial mode - GARSession not initialized");
        return;
    }

    GARSessionConfiguration *config = [[GARSessionConfiguration alloc] init];
    config.cloudAnchorMode = GARCloudAnchorModeEnabled;
    config.geospatialMode = enabled ? GARGeospatialModeEnabled : GARGeospatialModeDisabled;

    NSError *error = nil;
    [_garSession setConfiguration:config error:&error];

    if (error) {
        pwarn("[ViroAR] Failed to set geospatial mode: %s", [[error localizedDescription] UTF8String]);
        return;
    }

    _geospatialModeEnabled = enabled;
    pinfo("[ViroAR] Geospatial mode %s", enabled ? "enabled" : "disabled");
}

- (VROEarthTrackingState)getEarthTrackingState {
    if (!_currentGARFrame || !_currentGARFrame.earth) {
        return VROEarthTrackingState::Stopped;
    }

    switch (_currentGARFrame.earth.trackingState) {
        case GARTrackingStateTracking:
            return VROEarthTrackingState::Enabled;
        case GARTrackingStatePaused:
            return VROEarthTrackingState::Paused;
        case GARTrackingStateStopped:
        default:
            return VROEarthTrackingState::Stopped;
    }
}

- (VROGeospatialPose)getCameraGeospatialPose {
    VROGeospatialPose pose;

    if (!_currentGARFrame || !_currentGARFrame.earth ||
        _currentGARFrame.earth.trackingState != GARTrackingStateTracking) {
        return pose;
    }

    GAREarth *earth = _currentGARFrame.earth;
    GARGeospatialTransform *transform = earth.cameraGeospatialTransform;

    if (!transform) {
        return pose;
    }

    pose.latitude = transform.coordinate.latitude;
    pose.longitude = transform.coordinate.longitude;
    pose.altitude = transform.altitude;

    // Convert simd_quatf to VROQuaternion
    simd_quatf q = transform.eastUpSouthQTarget;
    pose.quaternion = VROQuaternion(q.vector.x, q.vector.y, q.vector.z, q.vector.w);

    // Calculate heading from quaternion
    double siny_cosp = 2.0 * (q.vector.w * q.vector.y + q.vector.x * q.vector.z);
    double cosy_cosp = 1.0 - 2.0 * (q.vector.y * q.vector.y + q.vector.z * q.vector.z);
    double yaw = atan2(siny_cosp, cosy_cosp);
    pose.heading = fmod(yaw * 180.0 / M_PI + 360.0, 360.0);

    pose.horizontalAccuracy = transform.horizontalAccuracy;
    pose.verticalAccuracy = transform.verticalAccuracy;
    // ARCore iOS SDK uses orientationYawAccuracy for heading accuracy
    pose.headingAccuracy = transform.orientationYawAccuracy;
    pose.orientationYawAccuracy = transform.orientationYawAccuracy;
    pose.timestamp = [[NSDate date] timeIntervalSince1970] * 1000.0;

    return pose;
}

- (void)checkVPSAvailability:(double)latitude
                   longitude:(double)longitude
                    callback:(void (^)(VROVPSAvailability))callback {
    if (!_garSession) {
        if (callback) {
            callback(VROVPSAvailability::Unknown);
        }
        return;
    }

    CLLocationCoordinate2D coordinate = CLLocationCoordinate2DMake(latitude, longitude);

    [_garSession checkVPSAvailabilityAtCoordinate:coordinate
                                completionHandler:^(GARVPSAvailability availability) {
        VROVPSAvailability result;
        switch (availability) {
            case GARVPSAvailabilityAvailable:
                result = VROVPSAvailability::Available;
                break;
            case GARVPSAvailabilityUnavailable:
                result = VROVPSAvailability::Unavailable;
                break;
            case GARVPSAvailabilityErrorNetworkConnection:
                result = VROVPSAvailability::ErrorNetwork;
                break;
            default:
                result = VROVPSAvailability::Unknown;
                break;
        }

        if (callback) {
            callback(result);
        }
    }];
}

- (void)createGeospatialAnchor:(double)latitude
                     longitude:(double)longitude
                      altitude:(double)altitude
                    quaternion:(simd_quatf)quaternion
                     onSuccess:(void (^)(std::shared_ptr<VROGeospatialAnchor>))onSuccess
                     onFailure:(void (^)(NSString *error))onFailure {

    if (!_garSession || !_geospatialModeEnabled) {
        if (onFailure) {
            onFailure(@"Geospatial mode not enabled");
        }
        return;
    }

    NSError *error = nil;
    CLLocationCoordinate2D coordinate = CLLocationCoordinate2DMake(latitude, longitude);

    GARAnchor *garAnchor = [_garSession createAnchorWithCoordinate:coordinate
                                                          altitude:altitude
                                                eastUpSouthQAnchor:quaternion
                                                             error:&error];

    if (error || !garAnchor) {
        if (onFailure) {
            onFailure(error ? [error localizedDescription] : @"Failed to create geospatial anchor");
        }
        return;
    }

    // Convert quaternion to VROQuaternion
    VROQuaternion vQuaternion(quaternion.vector.x, quaternion.vector.y, quaternion.vector.z, quaternion.vector.w);

    // Create VROGeospatialAnchor
    auto anchor = std::make_shared<VROGeospatialAnchor>(
        VROGeospatialAnchorType::WGS84,
        latitude,
        longitude,
        altitude,
        vQuaternion
    );

    // Generate anchor ID
    NSString *anchorId = [[NSUUID UUID] UUIDString];
    anchor->setId(std::string([anchorId UTF8String]));
    anchor->setResolveState(VROGeospatialAnchorResolveState::Success);

    // Convert GARAnchor transform to VROMatrix4f
    VROMatrix4f transform = [self matrix4fFromSimd:garAnchor.transform];
    anchor->updateFromGeospatialTransform(transform);

    // Store anchor reference
    [_geospatialAnchors setObject:garAnchor forKey:anchorId];

    pinfo("[ViroAR] Created WGS84 geospatial anchor at (%.6f, %.6f, %.1f)", latitude, longitude, altitude);

    if (onSuccess) {
        onSuccess(anchor);
    }
}

- (void)createTerrainAnchor:(double)latitude
                  longitude:(double)longitude
        altitudeAboveTerrain:(double)altitudeAboveTerrain
                 quaternion:(simd_quatf)quaternion
                  onSuccess:(void (^)(std::shared_ptr<VROGeospatialAnchor>))onSuccess
                  onFailure:(void (^)(NSString *error))onFailure {

    if (!_garSession || !_geospatialModeEnabled) {
        if (onFailure) {
            onFailure(@"Geospatial mode not enabled");
        }
        return;
    }

    CLLocationCoordinate2D coordinate = CLLocationCoordinate2DMake(latitude, longitude);
    NSError *error = nil;

    // Convert quaternion to VROQuaternion for the anchor
    VROQuaternion vQuaternion(quaternion.vector.x, quaternion.vector.y, quaternion.vector.z, quaternion.vector.w);

    // Generate anchor ID upfront
    NSString *anchorId = [[NSUUID UUID] UUIDString];
    std::string anchorIdStr = std::string([anchorId UTF8String]);

    // Create terrain anchor asynchronously
    [_garSession createAnchorWithCoordinate:coordinate
                                  altitudeAboveTerrain:altitudeAboveTerrain
                                    eastUpSouthQAnchor:quaternion
                                     completionHandler:^(GARAnchor * _Nullable garAnchor, GARTerrainAnchorState terrainState) {

        if (terrainState == GARTerrainAnchorStateSuccess && garAnchor) {
            // Create VROGeospatialAnchor
            auto anchor = std::make_shared<VROGeospatialAnchor>(
                VROGeospatialAnchorType::Terrain,
                latitude,
                longitude,
                altitudeAboveTerrain,
                vQuaternion
            );

            anchor->setId(anchorIdStr);
            anchor->setResolveState(VROGeospatialAnchorResolveState::Success);

            // Convert GARAnchor transform to VROMatrix4f
            VROMatrix4f transform = [self matrix4fFromSimd:garAnchor.transform];
            anchor->updateFromGeospatialTransform(transform);

            // Store anchor reference
            [self->_geospatialAnchors setObject:garAnchor forKey:anchorId];

            pinfo("[ViroAR] Created terrain anchor at (%.6f, %.6f) +%.1fm", latitude, longitude, altitudeAboveTerrain);

            if (onSuccess) {
                onSuccess(anchor);
            }
        } else {
            NSString *errorMsg = [self terrainAnchorStateToString:terrainState];
            pwarn("[ViroAR] Failed to create terrain anchor: %s", [errorMsg UTF8String]);
            if (onFailure) {
                onFailure(errorMsg);
            }
        }
    } error:&error];

    if (error) {
        if (onFailure) {
            onFailure([error localizedDescription]);
        }
    }
}

- (void)createRooftopAnchor:(double)latitude
                  longitude:(double)longitude
       altitudeAboveRooftop:(double)altitudeAboveRooftop
                 quaternion:(simd_quatf)quaternion
                  onSuccess:(void (^)(std::shared_ptr<VROGeospatialAnchor>))onSuccess
                  onFailure:(void (^)(NSString *error))onFailure {

    if (!_garSession || !_geospatialModeEnabled) {
        if (onFailure) {
            onFailure(@"Geospatial mode not enabled");
        }
        return;
    }

    CLLocationCoordinate2D coordinate = CLLocationCoordinate2DMake(latitude, longitude);
    NSError *error = nil;

    // Convert quaternion to VROQuaternion for the anchor
    VROQuaternion vQuaternion(quaternion.vector.x, quaternion.vector.y, quaternion.vector.z, quaternion.vector.w);

    // Generate anchor ID upfront
    NSString *anchorId = [[NSUUID UUID] UUIDString];
    std::string anchorIdStr = std::string([anchorId UTF8String]);

    // Create rooftop anchor asynchronously
    [_garSession createAnchorWithCoordinate:coordinate
                                  altitudeAboveRooftop:altitudeAboveRooftop
                                    eastUpSouthQAnchor:quaternion
                                     completionHandler:^(GARAnchor * _Nullable garAnchor, GARRooftopAnchorState rooftopState) {

        if (rooftopState == GARRooftopAnchorStateSuccess && garAnchor) {
            // Create VROGeospatialAnchor
            auto anchor = std::make_shared<VROGeospatialAnchor>(
                VROGeospatialAnchorType::Rooftop,
                latitude,
                longitude,
                altitudeAboveRooftop,
                vQuaternion
            );

            anchor->setId(anchorIdStr);
            anchor->setResolveState(VROGeospatialAnchorResolveState::Success);

            // Convert GARAnchor transform to VROMatrix4f
            VROMatrix4f transform = [self matrix4fFromSimd:garAnchor.transform];
            anchor->updateFromGeospatialTransform(transform);

            // Store anchor reference
            [self->_geospatialAnchors setObject:garAnchor forKey:anchorId];

            pinfo("[ViroAR] Created rooftop anchor at (%.6f, %.6f) +%.1fm", latitude, longitude, altitudeAboveRooftop);

            if (onSuccess) {
                onSuccess(anchor);
            }
        } else {
            NSString *errorMsg = [self rooftopAnchorStateToString:rooftopState];
            pwarn("[ViroAR] Failed to create rooftop anchor: %s", [errorMsg UTF8String]);
            if (onFailure) {
                onFailure(errorMsg);
            }
        }
    } error:&error];

    if (error) {
        if (onFailure) {
            onFailure([error localizedDescription]);
        }
    }
}

- (void)removeGeospatialAnchor:(NSString *)anchorId {
    GARAnchor *garAnchor = [_geospatialAnchors objectForKey:anchorId];
    if (garAnchor) {
        [_garSession removeAnchor:garAnchor];
        [_geospatialAnchors removeObjectForKey:anchorId];
        pinfo("[ViroAR] Removed geospatial anchor: %s", [anchorId UTF8String]);
    }
}

- (VROMatrix4f)matrix4fFromSimd:(simd_float4x4)simdMatrix {
    VROMatrix4f result;
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            result[col * 4 + row] = simdMatrix.columns[col][row];
        }
    }
    return result;
}

- (NSString *)terrainAnchorStateToString:(GARTerrainAnchorState)state {
    switch (state) {
        case GARTerrainAnchorStateSuccess:
            return @"Success";
        case GARTerrainAnchorStateTaskInProgress:
            return @"Task in progress";
        case GARTerrainAnchorStateErrorInternal:
            return @"Internal error";
        case GARTerrainAnchorStateErrorNotAuthorized:
            return @"Not authorized";
        case GARTerrainAnchorStateErrorUnsupportedLocation:
            return @"Unsupported location";
        default:
            return @"Unknown error";
    }
}

- (NSString *)rooftopAnchorStateToString:(GARRooftopAnchorState)state {
    switch (state) {
        case GARRooftopAnchorStateSuccess:
            return @"Success";
        case GARRooftopAnchorStateErrorInternal:
            return @"Internal error";
        case GARRooftopAnchorStateErrorNotAuthorized:
            return @"Not authorized";
        case GARRooftopAnchorStateErrorUnsupportedLocation:
            return @"Unsupported location";
        default:
            return @"Unknown error";
    }
}

#else // ARCORE_GEOSPATIAL_AVAILABLE not defined

+ (BOOL)isGeospatialAvailable {
    pwarn("[ViroAR] ARCore Geospatial SDK is NOT available (ARCORE_GEOSPATIAL_AVAILABLE=0)");
    pwarn("[ViroAR] To enable Geospatial, add to your Podfile: pod 'ARCore/Geospatial', '~> 1.51.0'");
    return NO;
}

- (BOOL)isGeospatialModeSupported {
    return NO;
}

- (void)setGeospatialModeEnabled:(BOOL)enabled {
    pwarn("[ViroAR] ARCore Geospatial SDK not available");
}

- (VROEarthTrackingState)getEarthTrackingState {
    return VROEarthTrackingState::Stopped;
}

- (VROGeospatialPose)getCameraGeospatialPose {
    return VROGeospatialPose();
}

- (void)checkVPSAvailability:(double)latitude
                   longitude:(double)longitude
                    callback:(void (^)(VROVPSAvailability))callback {
    if (callback) {
        callback(VROVPSAvailability::Unknown);
    }
}

- (void)createGeospatialAnchor:(double)latitude
                     longitude:(double)longitude
                      altitude:(double)altitude
                    quaternion:(simd_quatf)quaternion
                     onSuccess:(void (^)(std::shared_ptr<VROGeospatialAnchor>))onSuccess
                     onFailure:(void (^)(NSString *error))onFailure {
    if (onFailure) {
        onFailure(@"ARCore Geospatial SDK not available");
    }
}

- (void)createTerrainAnchor:(double)latitude
                  longitude:(double)longitude
        altitudeAboveTerrain:(double)altitudeAboveTerrain
                 quaternion:(simd_quatf)quaternion
                  onSuccess:(void (^)(std::shared_ptr<VROGeospatialAnchor>))onSuccess
                  onFailure:(void (^)(NSString *error))onFailure {
    if (onFailure) {
        onFailure(@"ARCore Geospatial SDK not available");
    }
}

- (void)createRooftopAnchor:(double)latitude
                  longitude:(double)longitude
       altitudeAboveRooftop:(double)altitudeAboveRooftop
                 quaternion:(simd_quatf)quaternion
                  onSuccess:(void (^)(std::shared_ptr<VROGeospatialAnchor>))onSuccess
                  onFailure:(void (^)(NSString *error))onFailure {
    if (onFailure) {
        onFailure(@"ARCore Geospatial SDK not available");
    }
}

- (void)removeGeospatialAnchor:(NSString *)anchorId {
    // No-op
}

#endif // ARCORE_GEOSPATIAL_AVAILABLE

@end

#else // ARCORE_AVAILABLE not defined

@implementation VROCloudAnchorProviderARCore

+ (BOOL)isAvailable {
    pwarn("[ViroAR] ARCore SDK is NOT available (ARCORE_AVAILABLE=0)");
    pwarn("[ViroAR] To enable Cloud Anchors, add to your Podfile: pod 'ARCore/CloudAnchors', '~> 1.51.0'");
    return NO;
}

- (nullable instancetype)init {
    pwarn("[ViroAR] ARCore SDK not available. Cloud anchors require ARCore/CloudAnchors pod.");
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

// Geospatial stubs when ARCore is not available
+ (BOOL)isGeospatialAvailable {
    return NO;
}

- (BOOL)isGeospatialModeSupported {
    return NO;
}

- (void)setGeospatialModeEnabled:(BOOL)enabled {
    // No-op when ARCore SDK not available
}

- (VROEarthTrackingState)getEarthTrackingState {
    return VROEarthTrackingState::Stopped;
}

- (VROGeospatialPose)getCameraGeospatialPose {
    return VROGeospatialPose();
}

- (void)checkVPSAvailability:(double)latitude
                   longitude:(double)longitude
                    callback:(void (^)(VROVPSAvailability))callback {
    if (callback) {
        callback(VROVPSAvailability::Unknown);
    }
}

- (void)createGeospatialAnchor:(double)latitude
                     longitude:(double)longitude
                      altitude:(double)altitude
                    quaternion:(simd_quatf)quaternion
                     onSuccess:(void (^)(std::shared_ptr<VROGeospatialAnchor>))onSuccess
                     onFailure:(void (^)(NSString *error))onFailure {
    if (onFailure) {
        onFailure(@"ARCore SDK not available");
    }
}

- (void)createTerrainAnchor:(double)latitude
                  longitude:(double)longitude
        altitudeAboveTerrain:(double)altitudeAboveTerrain
                 quaternion:(simd_quatf)quaternion
                  onSuccess:(void (^)(std::shared_ptr<VROGeospatialAnchor>))onSuccess
                  onFailure:(void (^)(NSString *error))onFailure {
    if (onFailure) {
        onFailure(@"ARCore SDK not available");
    }
}

- (void)createRooftopAnchor:(double)latitude
                  longitude:(double)longitude
       altitudeAboveRooftop:(double)altitudeAboveRooftop
                 quaternion:(simd_quatf)quaternion
                  onSuccess:(void (^)(std::shared_ptr<VROGeospatialAnchor>))onSuccess
                  onFailure:(void (^)(NSString *error))onFailure {
    if (onFailure) {
        onFailure(@"ARCore SDK not available");
    }
}

- (void)removeGeospatialAnchor:(NSString *)anchorId {
    // No-op
}

@end

#endif // ARCORE_AVAILABLE
