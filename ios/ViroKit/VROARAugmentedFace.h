//
//  VROARAugmentedFace.h
//  ViroKit
//
//  Created by ViroCore on 11/19/24.
//  Copyright © 2024 Viro Media. All rights reserved.
//

#ifndef VROARAugmentedFace_h
#define VROARAugmentedFace_h

#include "VROARAnchor.h"
#include "VROVector3f.h"
#include "VRONode.h"
#include <vector>
#include <memory>

enum class VROBlendShapeLocation {
    BROW_DOWN_LEFT,
    BROW_DOWN_RIGHT, 
    BROW_INNER_UP,
    BROW_OUTER_UP_LEFT,
    BROW_OUTER_UP_RIGHT,
    CHEEK_PUFF,
    CHEEK_SQUINT_LEFT,
    CHEEK_SQUINT_RIGHT,
    EYE_BLINK_LEFT,
    EYE_BLINK_RIGHT,
    EYE_LOOK_DOWN_LEFT,
    EYE_LOOK_DOWN_RIGHT,
    EYE_LOOK_IN_LEFT,
    EYE_LOOK_IN_RIGHT,
    EYE_LOOK_OUT_LEFT,
    EYE_LOOK_OUT_RIGHT,
    EYE_LOOK_UP_LEFT,
    EYE_LOOK_UP_RIGHT,
    EYE_SQUINT_LEFT,
    EYE_SQUINT_RIGHT,
    EYE_WIDE_LEFT,
    EYE_WIDE_RIGHT,
    JAW_FORWARD,
    JAW_LEFT,
    JAW_OPEN,
    JAW_RIGHT,
    MOUTH_CLOSE,
    MOUTH_DIMPLE_LEFT,
    MOUTH_DIMPLE_RIGHT,
    MOUTH_FROWN_LEFT,
    MOUTH_FROWN_RIGHT,
    MOUTH_FUNNEL,
    MOUTH_LEFT,
    MOUTH_LOWER_DOWN_LEFT,
    MOUTH_LOWER_DOWN_RIGHT,
    MOUTH_OPEN,
    MOUTH_PRESS_LEFT,
    MOUTH_PRESS_RIGHT,
    MOUTH_PUCKER,
    MOUTH_RIGHT,
    MOUTH_ROLL_LOWER,
    MOUTH_ROLL_UPPER,
    MOUTH_SHRUGE_LOWER,
    MOUTH_SHRUGE_UPPER,
    MOUTH_SMILE_LEFT,
    MOUTH_SMILE_RIGHT,
    MOUTH_STRETCH_LEFT,
    MOUTH_STRETCH_RIGHT,
    MOUTH_UPPER_UP_LEFT,
    MOUTH_UPPER_UP_RIGHT,
    NOSE_SNEER_LEFT,
    NOSE_SNEER_RIGHT
};

enum class VROFaceRegion {
    NOSE_TIP,
    FOREHEAD_LEFT,
    FOREHEAD_RIGHT,
    FOREHEAD_TOP,
    FOREHEAD_CENTER
};

class VROARAugmentedFace : public VROARAnchor {
public:
    VROARAugmentedFace();
    virtual ~VROARAugmentedFace();
    
    // Face mesh data
    std::vector<VROVector3f> getMeshVertices() const;
    std::vector<int> getMeshTriangleIndices() const;
    std::vector<VROVector3f> getMeshTextureCoordinates() const;
    std::vector<VROVector3f> getMeshNormals() const;
    
    // Blend shape coefficients (0.0 to 1.0)
    float getBlendShapeValue(VROBlendShapeLocation blendShape) const;
    void setBlendShapeValue(VROBlendShapeLocation blendShape, float value);
    
    // Face regions for attaching objects
    VROVector3f getRegionPose(VROFaceRegion region) const;
    
    // Face tracking state
    bool isFaceDetected() const { return _faceDetected; }
    void setFaceDetected(bool detected) { _faceDetected = detected; }
    
    // Attached content management
    void setAttachedNode(std::shared_ptr<VRONode> node) { _attachedNode = node; }
    std::shared_ptr<VRONode> getAttachedNode() const { return _attachedNode; }
    
    // Update from ARCore face data
    void updateFromARCore(void* garFace);
    
    // Convenience methods for common expressions
    float getEyeBlinkLeft() const { return getBlendShapeValue(VROBlendShapeLocation::EYE_BLINK_LEFT); }
    float getEyeBlinkRight() const { return getBlendShapeValue(VROBlendShapeLocation::EYE_BLINK_RIGHT); }
    float getMouthOpen() const { return getBlendShapeValue(VROBlendShapeLocation::MOUTH_OPEN); }
    float getJawOpen() const { return getBlendShapeValue(VROBlendShapeLocation::JAW_OPEN); }
    
private:
    bool _faceDetected;
    std::shared_ptr<VRONode> _attachedNode;
    void* _garFace; // Reference to GARAugmentedFace
    
    // Cached mesh data
    std::vector<VROVector3f> _meshVertices;
    std::vector<int> _meshIndices;
    std::vector<VROVector3f> _meshUVs;
    std::vector<VROVector3f> _meshNormals;
    
    // Blend shape coefficients
    std::map<VROBlendShapeLocation, float> _blendShapes;
    
    void updateMeshData();
    void updateBlendShapes();
};

#endif /* VROARAugmentedFace_h */