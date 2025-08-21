//
//  VROARGeospatialAnchor.h
//  ViroRenderer
//
//  Created for Geospatial API support
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

#ifndef VROARGeospatialAnchor_h
#define VROARGeospatialAnchor_h

#include "VROARAnchor.h"
#include "VROVector3f.h"
#include "VROQuaternion.h"

enum class VROARGeospatialMode {
    Disabled,
    Enabled,
    EnabledWithTerrainAnchors
};

enum class VROARRooftopAnchorState {
    None,
    Success,
    ErrorInternal,
    ErrorNotAuthorized,
    ErrorUnsupportedLocation
};

/*
 Geospatial anchor that can be placed at real-world coordinates using
 latitude, longitude, and altitude. Supports both WGS84 and terrain-relative
 altitude modes.
 */
class VROARGeospatialAnchor : public VROARAnchor {
    
public:
    
    VROARGeospatialAnchor() : 
        _latitude(0), 
        _longitude(0), 
        _altitude(0),
        _useTerrainAnchor(false),
        _useRooftopAnchor(false),
        _rooftopState(VROARRooftopAnchorState::None) {}
        
    VROARGeospatialAnchor(double latitude, double longitude, double altitude, 
                         const VROQuaternion& eastUpSouthOrientation);
                         
    VROARGeospatialAnchor(double latitude, double longitude, 
                         const VROQuaternion& eastUpSouthOrientation,
                         bool isTerrainAnchor, bool isRooftopAnchor);
        
    virtual ~VROARGeospatialAnchor() {}
    
    /*
     The geographic coordinates of the anchor.
     */
    double getLatitude() const { return _latitude; }
    void setLatitude(double latitude) { _latitude = latitude; }
    
    double getLongitude() const { return _longitude; }
    void setLongitude(double longitude) { _longitude = longitude; }
    
    double getAltitude() const { return _altitude; }
    void setAltitude(double altitude) { _altitude = altitude; }
    
    /*
     The orientation of the anchor in East-Up-South coordinate system.
     */
    VROQuaternion getEastUpSouthOrientation() const { return _eastUpSouthQuat; }
    void setEastUpSouthOrientation(VROQuaternion quat) { _eastUpSouthQuat = quat; }
    
    /*
     Whether this anchor should use terrain-relative altitude (ARCore 1.31+)
     or rooftop-relative altitude (ARCore 1.38+).
     */
    bool isTerrainAnchor() const { return _useTerrainAnchor; }
    void setTerrainAnchor(bool useTerrainAnchor) { _useTerrainAnchor = useTerrainAnchor; }
    
    bool isRooftopAnchor() const { return _useRooftopAnchor; }
    void setRooftopAnchor(bool useRooftopAnchor) { _useRooftopAnchor = useRooftopAnchor; }
    
    /*
     The state of rooftop anchor resolution.
     */
    VROARRooftopAnchorState getRooftopState() const { return _rooftopState; }
    void setRooftopState(VROARRooftopAnchorState state) { _rooftopState = state; }
    
private:
    
    double _latitude;
    double _longitude;
    double _altitude;
    VROQuaternion _eastUpSouthQuat;
    bool _useTerrainAnchor;
    bool _useRooftopAnchor;
    VROARRooftopAnchorState _rooftopState;
    
};

#endif /* VROARGeospatialAnchor_h */