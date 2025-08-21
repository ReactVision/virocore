//
//  VROARGeospatialAnchor.h
//  ViroKit
//
//  Created by ViroCore on 11/19/24.
//  Copyright © 2024 Viro Media. All rights reserved.
//

#ifndef VROARGeospatialAnchor_h
#define VROARGeospatialAnchor_h

#include "VROARAnchor.h"
#include "VROQuaternion.h"
#include <memory>

enum class VROARAltitudeMode {
    WGS84,
    TERRAIN, 
    ROOFTOP
};

enum class VROARGeospatialTrackingState {
    NOT_TRACKING,
    TRACKING
};

class VROARGeospatialAnchor : public VROARAnchor {
public:
    
    static std::shared_ptr<VROARGeospatialAnchor> create(double latitude, 
                                                        double longitude, 
                                                        double altitude,
                                                        const VROQuaternion& orientation,
                                                        VROARAltitudeMode altitudeMode = VROARAltitudeMode::WGS84);
    
    VROARGeospatialAnchor(double latitude, double longitude, double altitude,
                         const VROQuaternion& orientation, VROARAltitudeMode altitudeMode);
    virtual ~VROARGeospatialAnchor();
    
    // Geospatial properties
    double getLatitude() const { return _latitude; }
    double getLongitude() const { return _longitude; }
    double getAltitude() const { return _altitude; }
    VROARAltitudeMode getAltitudeMode() const { return _altitudeMode; }
    
    // Tracking state
    VROARGeospatialTrackingState getTrackingState() const { return _trackingState; }
    void setTrackingState(VROARGeospatialTrackingState state) { _trackingState = state; }
    
    // VPS availability check
    static void checkVPSAvailability(double latitude, double longitude, 
                                   std::function<void(bool)> callback);
    
    // Update anchor from ARCore data
    void updateFromARCore(void* garAnchor);
    
private:
    double _latitude;
    double _longitude; 
    double _altitude;
    VROARAltitudeMode _altitudeMode;
    VROARGeospatialTrackingState _trackingState;
    void* _garAnchor; // Holds reference to GARGeospatialAnchor
};

#endif /* VROARGeospatialAnchor_h */