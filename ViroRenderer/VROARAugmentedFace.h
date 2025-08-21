//
//  VROARAugmentedFace.h
//  ViroRenderer
//
//  Created for Augmented Faces API support
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

#ifndef VROARAugmentedFace_h
#define VROARAugmentedFace_h

#include "VROARAnchor.h"
#include "VROVector3f.h"
#include "VROMatrix4f.h"
#include <vector>
#include <memory>

enum class VROARFaceTrackingMode {
    Disabled,
    FrontCamera,  // ARCore front camera face tracking
    MeshEnabled   // ARKit face mesh tracking
};

enum class VROARFaceRegion {
    ForeheadLeft,
    ForeheadRight,
    TempleLeft,
    TempleRight,
    EyebrowLeft,
    EyebrowRight,
    EyeLeft,
    EyeRight,
    EyeballLeft,
    EyeballRight,
    NoseBridge,
    NoseTip,
    NostrilLeft,
    NostrilRight,
    CheekLeft,
    CheekRight,
    UpperLipLeft,
    UpperLipCenter,
    UpperLipRight,
    LowerLipLeft,
    LowerLipCenter,
    LowerLipRight,
    ChinLeft,
    ChinCenter,
    ChinRight
};

/*
 Represents an augmented face anchor for face tracking and mesh generation.
 Supports both ARCore Augmented Faces and ARKit Face Tracking.
 */
class VROARAugmentedFace : public VROARAnchor {
    
public:
    
    // Standard face mesh has 468 vertices (ARCore) or 1220 vertices (ARKit)
    static const int kARCoreFaceMeshVertexCount = 468;
    static const int kARKitFaceMeshVertexCount = 1220;
    
    VROARAugmentedFace() : 
        _trackingConfidence(0.0f),
        _hasBlendShapes(false) {}
        
    virtual ~VROARAugmentedFace() {}
    
    /*
     Get the face mesh vertices in the face coordinate space.
     The mesh follows a standard UV mapping for texture application.
     */
    const std::vector<VROVector3f>& getMeshVertices() const { return _meshVertices; }
    void setMeshVertices(const std::vector<VROVector3f>& vertices) { _meshVertices = vertices; }
    
    /*
     Get the face mesh triangle indices for rendering.
     */
    const std::vector<uint16_t>& getMeshIndices() const { return _meshIndices; }
    void setMeshIndices(const std::vector<uint16_t>& indices) { _meshIndices = indices; }
    
    /*
     Get the face mesh texture coordinates for texture mapping.
     */
    const std::vector<VROVector3f>& getMeshUVs() const { return _meshUVs; }
    void setMeshUVs(const std::vector<VROVector3f>& uvs) { _meshUVs = uvs; }
    
    /*
     Get the face mesh normals for lighting calculations.
     */
    const std::vector<VROVector3f>& getMeshNormals() const { return _meshNormals; }
    void setMeshNormals(const std::vector<VROVector3f>& normals) { _meshNormals = normals; }
    
    /*
     Get the center pose of the face (between the eyes).
     */
    VROMatrix4f getCenterPose() const { return _centerPose; }
    void setCenterPose(const VROMatrix4f& pose) { _centerPose = pose; }
    
    /*
     Get a specific face region's pose for attachment points.
     */
    VROMatrix4f getRegionPose(VROARFaceRegion region) const;
    void setRegionPose(VROARFaceRegion region, const VROMatrix4f& pose);
    
    /*
     Get the tracking confidence value [0.0, 1.0].
     */
    float getTrackingConfidence() const { return _trackingConfidence; }
    void setTrackingConfidence(float confidence) { _trackingConfidence = confidence; }
    
    /*
     ARKit-specific blend shape coefficients for facial expressions.
     Maps expression names to coefficient values [0.0, 1.0].
     */
    float getBlendShapeCoefficient(const std::string& blendShapeName) const;
    void setBlendShapeCoefficient(const std::string& blendShapeName, float value);
    bool hasBlendShapes() const { return _hasBlendShapes; }
    
    /*
     Common blend shape accessors for cross-platform compatibility.
     */
    float getEyeBlinkLeft() const { return getBlendShapeCoefficient("eyeBlinkLeft"); }
    float getEyeBlinkRight() const { return getBlendShapeCoefficient("eyeBlinkRight"); }
    float getMouthSmileLeft() const { return getBlendShapeCoefficient("mouthSmileLeft"); }
    float getMouthSmileRight() const { return getBlendShapeCoefficient("mouthSmileRight"); }
    float getMouthOpen() const { return getBlendShapeCoefficient("jawOpen"); }
    float getBrowUpLeft() const { return getBlendShapeCoefficient("browOuterUpLeft"); }
    float getBrowUpRight() const { return getBlendShapeCoefficient("browOuterUpRight"); }
    
    /*
     Create a face mesh geometry for rendering.
     */
    std::shared_ptr<VROGeometry> createFaceMeshGeometry() const;
    
    /*
     Apply face mesh as an occlusion mask (for realistic occlusion).
     */
    void applyOcclusionMask(std::shared_ptr<VRONode> node) const;
    
    /*
     Helper to check if a point is within the face region.
     */
    bool containsPoint(const VROVector3f& point) const;
    
    /*
     Get face landmarks for 2D overlay rendering.
     Returns key facial points in screen space.
     */
    std::vector<VROVector3f> getFaceLandmarks2D() const;
    
    /*
     Utility methods for face mesh processing.
     */
    void smoothMesh();
    void computeNormals();
    
private:
    
    // Face mesh data
    std::vector<VROVector3f> _meshVertices;
    std::vector<uint16_t> _meshIndices;
    std::vector<VROVector3f> _meshUVs;
    std::vector<VROVector3f> _meshNormals;
    
    // Face poses
    VROMatrix4f _centerPose;
    std::map<VROARFaceRegion, VROMatrix4f> _regionPoses;
    
    // Tracking quality
    float _trackingConfidence;
    
    // Blend shapes (ARKit specific)
    bool _hasBlendShapes;
    std::map<std::string, float> _blendShapeCoefficients;
    
    // Cached geometry
    mutable std::shared_ptr<VROGeometry> _cachedMeshGeometry;
    mutable bool _meshGeometryDirty = true;
};

#endif /* VROARAugmentedFace_h */