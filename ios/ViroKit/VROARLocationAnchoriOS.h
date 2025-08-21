//
//  VROARLocationAnchoriOS.h
//  ViroKit
//
//  Created for ARKit Location Anchors support (iOS 14.0+)
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

#ifndef VROARLocationAnchoriOS_h
#define VROARLocationAnchoriOS_h

#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 140000
#include "VROARGeospatialAnchor.h"
#import <ARCore/ARCore.h>
#include <CoreLocation/CoreLocation.h>

/*
 iOS implementation of location-based anchors using ARCore's Geospatial API
 for world-scale AR experiences with VPS (Visual Positioning Service).
 */
class API_AVAILABLE(ios(14.0)) VROARLocationAnchoriOS : public VROARGeospatialAnchor {
public:
    
    VROARLocationAnchoriOS(GARAnchor *geospatialAnchor, GARSession *session);
    virtual ~VROARLocationAnchoriOS();
    
    /*
     Create a geospatial anchor at the specified coordinates using ARCore.
     */
    static std::shared_ptr<VROARLocationAnchoriOS> createGeospatialAnchor(
        GARSession *session,
        double latitude,
        double longitude,
        double altitude,
        float *orientation = nullptr);
    
    /*
     Get the underlying ARCore geospatial anchor.
     */
    GARAnchor* getGeospatialAnchor() const { return _geospatialAnchor; }
    
    /*
     Get the VPS availability for this anchor location.
     */
    GARVpsAvailability getVPSAvailability() const;
    
    /*
     Get the terrain anchor state (if using terrain altitude).
     */
    GARTerrainAnchorState getTerrainAnchorState() const;
    
    /*
     Get the rooftop anchor state (if using rooftop altitude).
     */
    GARRooftopAnchorState getRooftopAnchorState() const;
    
    /*
     Override from VROARAnchor to get transform.
     */
    VROMatrix4f getTransform() const override;
    
    /*
     Check VPS availability at a specific location.
     */
    static void checkVPSAvailability(GARSession *session,
                                    double latitude,
                                    double longitude,
                                    std::function<void(GARVpsAvailability availability)> callback);
    
    /*
     Get Earth tracking state from ARCore session.
     */
    static GARFeatureMapQuality getEarthTrackingState(GARSession *session);
    
private:
    
    GARAnchor *_geospatialAnchor;
    GARSession *_session;
    
    // Cached properties
    VROMatrix4f _transform;
    double _latitude;
    double _longitude;
    double _altitude;
    
    /*
     Update the anchor's properties from ARCore.
     */
    void updateFromARCore();
    
    /*
     Convert ARCore transform to VROMatrix4f.
     */
    static VROMatrix4f convertARCoreTransform(const float* transform);
};

#endif // iOS 14.0+
#endif /* VROARLocationAnchoriOS_h */