//
//  VROARGeospatialAnchor.mm
//  ViroKit
//
//  Created by ViroCore on 11/19/24.
//  Copyright © 2024 Viro Media. All rights reserved.
//

#import "VROARGeospatialAnchor.h"
#import <ARCore/ARCore.h>

std::shared_ptr<VROARGeospatialAnchor> VROARGeospatialAnchor::create(double latitude, 
                                                                    double longitude, 
                                                                    double altitude,
                                                                    const VROQuaternion& orientation,
                                                                    VROARAltitudeMode altitudeMode) {
    return std::make_shared<VROARGeospatialAnchor>(latitude, longitude, altitude, orientation, altitudeMode);
}

VROARGeospatialAnchor::VROARGeospatialAnchor(double latitude, double longitude, double altitude,
                                           const VROQuaternion& orientation, VROARAltitudeMode altitudeMode) :
    VROARAnchor("", VROMatrix4f::identity()),
    _latitude(latitude),
    _longitude(longitude),
    _altitude(altitude),
    _altitudeMode(altitudeMode),
    _trackingState(VROARGeospatialTrackingState::NOT_TRACKING),
    _garAnchor(nullptr) {
}

VROARGeospatialAnchor::~VROARGeospatialAnchor() {
    if (_garAnchor) {
        // Clean up ARCore anchor reference
        _garAnchor = nullptr;
    }
}

void VROARGeospatialAnchor::checkVPSAvailability(double latitude, double longitude, 
                                                std::function<void(bool)> callback) {
    // Check with ARCore Geospatial API
    // This would integrate with ARCore's VPS availability API
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        // Simulate VPS check - in real implementation this would call ARCore
        bool isAvailable = true; // Placeholder
        
        dispatch_async(dispatch_get_main_queue(), ^{
            callback(isAvailable);
        });
    });
}

void VROARGeospatialAnchor::updateFromARCore(void* garAnchor) {
    _garAnchor = garAnchor;
    
    if (_garAnchor) {
        // Extract pose and update anchor transform
        // GARGeospatialAnchor *anchor = (__bridge GARGeospatialAnchor *)_garAnchor;
        // Update transform matrix from ARCore anchor data
        _trackingState = VROARGeospatialTrackingState::TRACKING;
    } else {
        _trackingState = VROARGeospatialTrackingState::NOT_TRACKING;
    }
}