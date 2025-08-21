//
//  VROARGeospatialAnchor.cpp
//  ViroRenderer
//
//  Implementation of Geospatial Anchors
//  Copyright © 2024 Viro Media. All rights reserved.

#include "VROARGeospatialAnchor.h"
#include "VROQuaternion.h"
#include "VROLog.h"
#include <algorithm>
#include <cmath>

VROARGeospatialAnchor::VROARGeospatialAnchor(double latitude, double longitude, double altitude, 
                                           const VROQuaternion& eastUpSouthOrientation) :
    _latitude(latitude),
    _longitude(longitude), 
    _altitude(altitude),
    _eastUpSouthQuat(eastUpSouthOrientation),
    _useTerrainAnchor(false),
    _useRooftopAnchor(false),
    _rooftopState(VROARRooftopAnchorState::None) {
        
    setId("geospatial_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
}

VROARGeospatialAnchor::VROARGeospatialAnchor(double latitude, double longitude, 
                                           const VROQuaternion& eastUpSouthOrientation,
                                           bool isTerrainAnchor, bool isRooftopAnchor) :
    _latitude(latitude),
    _longitude(longitude),
    _altitude(0.0), // Will be resolved later
    _eastUpSouthQuat(eastUpSouthOrientation),
    _useTerrainAnchor(isTerrainAnchor),
    _useRooftopAnchor(isRooftopAnchor),
    _rooftopState(VROARRooftopAnchorState::None) {
        
    setId("geospatial_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
}