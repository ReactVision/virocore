//
//  VROARLocationAnchoriOS.mm
//  ViroKit
//
//  Implementation of iOS Location Anchors (ARKit Geo Anchors)
//  Copyright © 2024 Viro Media. All rights reserved.

#include "VROARLocationAnchoriOS.h"

#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 140000

#include "VROConvert.h"
#include "VROLog.h"
#include <Foundation/Foundation.h>

API_AVAILABLE(ios(14.0))
VROARLocationAnchoriOS::VROARLocationAnchoriOS(ARGeoAnchor *geoAnchor) :
    _geoAnchor(geoAnchor) {
    
    [_geoAnchor retain];
    
    // Extract coordinate information from the geo anchor
    CLLocationCoordinate2D coordinate = _geoAnchor.coordinate;
    setLatitude(coordinate.latitude);
    setLongitude(coordinate.longitude);
    setAltitude(_geoAnchor.altitude);
    
    // Set ID from anchor identifier
    if (_geoAnchor.identifier) {
        setId([_geoAnchor.identifier.UUIDString UTF8String]);
    }
    
    // Update properties from ARKit
    updateFromARKit();
}

API_AVAILABLE(ios(14.0))
VROARLocationAnchoriOS::~VROARLocationAnchoriOS() {
    if (_geoAnchor) {
        [_geoAnchor release];
        _geoAnchor = nullptr;
    }
}

API_AVAILABLE(ios(14.0))
std::shared_ptr<VROARLocationAnchoriOS> VROARLocationAnchoriOS::createLocationAnchor(
    CLLocationCoordinate2D coordinate,
    CLLocationDistance altitude,
    float *orientation) {
    
    if (@available(iOS 14.0, *)) {
        // Check if geo tracking is supported
        if (![ARGeoTrackingConfiguration isSupported]) {
            pinfo("Geo tracking not supported on this device");
            return nullptr;
        }
        
        ARGeoAnchor *geoAnchor;
        if (orientation) {
            // Create with custom orientation
            simd_quatf quat = simd_quaternion(orientation[0], orientation[1], orientation[2], orientation[3]);
            geoAnchor = [[ARGeoAnchor alloc] initWithCoordinate:coordinate 
                                                       altitude:altitude 
                                                    orientation:quat];
        } else {
            // Create with default orientation
            geoAnchor = [[ARGeoAnchor alloc] initWithCoordinate:coordinate 
                                                       altitude:altitude];
        }
        
        if (!geoAnchor) {
            pinfo("Failed to create ARGeoAnchor");
            return nullptr;
        }
        
        auto locationAnchor = std::make_shared<VROARLocationAnchoriOS>(geoAnchor);
        [geoAnchor release];
        
        return locationAnchor;
    }
    
    return nullptr;
}

API_AVAILABLE(ios(14.0))
ARGeoTrackingState VROARLocationAnchoriOS::getGeoTrackingState() const {
    if (@available(iOS 14.0, *)) {
        if (_geoAnchor) {
            return _geoAnchor.trackingState;
        }
    }
    return ARGeoTrackingStateNotAvailable;
}

API_AVAILABLE(ios(14.0))
ARGeoTrackingAccuracy VROARLocationAnchoriOS::getGeoTrackingAccuracy() const {
    if (@available(iOS 14.0, *)) {
        if (_geoAnchor) {
            return _geoAnchor.trackingAccuracy;
        }
    }
    return ARGeoTrackingAccuracyUndetermined;
}

API_AVAILABLE(ios(14.0))
ARGeoTrackingStateReason VROARLocationAnchoriOS::getGeoTrackingStateReason() const {
    if (@available(iOS 14.0, *)) {
        if (_geoAnchor) {
            return _geoAnchor.trackingStateReason;
        }
    }
    return ARGeoTrackingStateReasonNotAvailableAtLocation;
}

VROMatrix4f VROARLocationAnchoriOS::getTransform() const {
    if (@available(iOS 14.0, *)) {
        if (_geoAnchor) {
            return VROConvert::toMatrix4f(_geoAnchor.transform);
        }
    }
    return VROMatrix4f::identity();
}

API_AVAILABLE(ios(14.0))
void VROARLocationAnchoriOS::checkAvailability(CLLocationCoordinate2D coordinate,
                                              std::function<void(bool available, NSError* error)> callback) {
    if (@available(iOS 14.0, *)) {
        [ARGeoTrackingConfiguration checkAvailabilityAtCoordinate:coordinate 
                                                completionHandler:^(BOOL available, NSError *error) {
            dispatch_async(dispatch_get_main_queue(), ^{
                if (callback) {
                    callback(available, error);
                }
            });
        }];
    } else {
        if (callback) {
            NSError *error = [NSError errorWithDomain:@"VROARLocationAnchor" 
                                                 code:-1 
                                             userInfo:@{NSLocalizedDescriptionKey: @"iOS 14.0+ required"}];
            callback(false, error);
        }
    }
}

API_AVAILABLE(ios(14.0))
NSString* VROARLocationAnchoriOS::getCoachingMessage(ARGeoTrackingState state,
                                                    ARGeoTrackingAccuracy accuracy,
                                                    ARGeoTrackingStateReason reason) {
    if (@available(iOS 14.0, *)) {
        switch (state) {
            case ARGeoTrackingStateNotAvailable:
                return @"Geo tracking is not available at this location";
                
            case ARGeoTrackingStateInitializing:
                return @"Initializing geo tracking...";
                
            case ARGeoTrackingStateLocalizing:
                switch (reason) {
                    case ARGeoTrackingStateReasonWaitingForLocation:
                        return @"Waiting for location data...";
                    case ARGeoTrackingStateReasonGeoDataNotLoaded:
                        return @"Loading location data...";
                    case ARGeoTrackingStateReasonVisualLocalizationFailed:
                        return @"Move your device to help with localization";
                    case ARGeoTrackingStateReasonWaitingForAvailabilityCheck:
                        return @"Checking availability at this location...";
                    default:
                        return @"Localizing to your current location...";
                }
                
            case ARGeoTrackingStateLocalized:
                switch (accuracy) {
                    case ARGeoTrackingAccuracyUndetermined:
                        return @"Geo tracking accuracy undetermined";
                    case ARGeoTrackingAccuracyLow:
                        return @"Geo tracking active (low accuracy)";
                    case ARGeoTrackingAccuracyMedium:
                        return @"Geo tracking active (medium accuracy)";
                    case ARGeoTrackingAccuracyHigh:
                        return @"Geo tracking active (high accuracy)";
                }
        }
    }
    
    return @"Geo tracking status unknown";
}

void VROARLocationAnchoriOS::updateFromARKit() {
    if (@available(iOS 14.0, *)) {
        if (!_geoAnchor) {
            return;
        }
        
        // Update coordinate information
        CLLocationCoordinate2D coordinate = _geoAnchor.coordinate;
        setLatitude(coordinate.latitude);
        setLongitude(coordinate.longitude);
        setAltitude(_geoAnchor.altitude);
        
        // Convert ARKit orientation to VRO quaternion
        simd_quatf arkitQuat = simd_quaternion(_geoAnchor.transform);
        VROQuaternion vroQuat(arkitQuat.vector.x, arkitQuat.vector.y, 
                              arkitQuat.vector.z, arkitQuat.vector.w);
        setEastUpSouthOrientation(vroQuat);
        
        // Update tracking state information
        ARGeoTrackingState state = _geoAnchor.trackingState;
        ARGeoTrackingAccuracy accuracy = _geoAnchor.trackingAccuracy;
        ARGeoTrackingStateReason reason = _geoAnchor.trackingStateReason;
        
        // Log tracking state changes
        static ARGeoTrackingState lastState = ARGeoTrackingStateNotAvailable;
        static ARGeoTrackingAccuracy lastAccuracy = ARGeoTrackingAccuracyUndetermined;
        
        if (state != lastState || accuracy != lastAccuracy) {
            NSString *message = getCoachingMessage(state, accuracy, reason);
            pinfo("Geo tracking update: %s", [message UTF8String]);
            
            lastState = state;
            lastAccuracy = accuracy;
        }
    }
}

#endif // iOS 14.0+