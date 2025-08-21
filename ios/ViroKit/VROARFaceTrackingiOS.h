//
//  VROARFaceTrackingiOS.h
//  ViroKit
//
//  Created for ARKit Face Tracking support (iOS 11.0+)
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

#ifndef VROARFaceTrackingiOS_h
#define VROARFaceTrackingiOS_h

#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110000
#include "VROARAugmentedFace.h"
#import <ARCore/ARCore.h>
#include <memory>

/*
 iOS implementation of face tracking using ARCore's GARAugmentedFace
 for cross-platform face tracking capabilities.
 */
class API_AVAILABLE(ios(11.0)) VROARFaceTrackingiOS : public VROARAugmentedFace {
public:
    
    VROARFaceTrackingiOS(GARAugmentedFace *arCoreFace);
    virtual ~VROARFaceTrackingiOS();
    
    /*
     Update the face data from ARCore.
     */
    void updateFromARCoreFace(GARAugmentedFace *arCoreFace);
    
    /*
     Get the underlying ARCore augmented face.
     */
    GARAugmentedFace* getARCoreFace() const { return _arCoreFace; }
    
    /*
     Override from VROARAnchor to get transform.
     */
    VROMatrix4f getTransform() const override;
    
    /*
     Get face mesh from ARCore.
     */
    const float* getFaceMeshVertices() const;
    const int* getFaceMeshTriangleIndices() const;
    const float* getFaceMeshTextureCoordinates() const;
    
    /*
     Get face region data from ARCore.
     */
    GARFaceMeshRegion* getFaceRegion(GARFaceMeshRegionType regionType) const;
    
    /*
     Common blend shape accessors mapped to ARKit blend shapes.
     */
    float getEyeBlinkLeft() const override;
    float getEyeBlinkRight() const override;
    float getMouthSmileLeft() const override;
    float getMouthSmileRight() const override;
    float getMouthOpen() const override;
    float getBrowUpLeft() const override;
    float getBrowUpRight() const override;
    
    /*
     Get face pose (center point) from ARCore.
     */
    VROVector3f getFaceCenterPose() const;
    
    /*
     Get face normals from ARCore.
     */
    const float* getFaceMeshNormals() const;
    
    /*
     Check if face tracking is supported on this device.
     */
    static bool isFaceTrackingSupported();
    
    /*
     Check if ARCore augmented faces are supported.
     */
    static bool isAugmentedFacesSupported();
    
    /*
     Create ARCore session configuration for face tracking.
     */
    static GARSessionConfiguration* createFaceTrackingConfiguration();
    
private:
    
    GARAugmentedFace *_arCoreFace;
    GARSession *_session;
    
    // Cached mesh data
    bool _meshDataDirty;
    std::vector<VROVector3f> _cachedVertices;
    std::vector<uint16_t> _cachedIndices;
    std::vector<VROVector2f> _cachedUVs;
    std::vector<VROVector3f> _cachedNormals;
    
    // Face transform
    VROMatrix4f _faceTransform;
    VROVector3f _faceCenterPose;
    
    void updateMeshData();
    void updateFaceTransform();
    void updateBlendShapes();
    
    static VROMatrix4f convertARCoreTransform(const float* transform);
    static VROVector3f convertARCorePose(const float* pose);
};

/*
 Delegate for handling multiple face tracking sessions.
 */
class API_AVAILABLE(ios(11.0)) VROARFaceTrackingDelegate {
public:
    virtual ~VROARFaceTrackingDelegate() {}
    
    /*
     Called when a new face is detected.
     */
    virtual void onFaceDetected(std::shared_ptr<VROARFaceTrackingiOS> face) = 0;
    
    /*
     Called when a face is updated.
     */
    virtual void onFaceUpdated(std::shared_ptr<VROARFaceTrackingiOS> face) = 0;
    
    /*
     Called when a face is lost.
     */
    virtual void onFaceLost(std::shared_ptr<VROARFaceTrackingiOS> face) = 0;
};

#endif // iOS 11.0+
#endif /* VROARFaceTrackingiOS_h */