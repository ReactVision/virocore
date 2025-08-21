//
//  VROARAugmentedFace.mm
//  ViroKit
//
//  Created by ViroCore on 11/19/24.
//  Copyright © 2024 Viro Media. All rights reserved.
//

#import "VROARAugmentedFace.h"
#import <ARCore/ARCore.h>
#import <map>

VROARAugmentedFace::VROARAugmentedFace() :
    VROARAnchor("face", VROMatrix4f::identity()),
    _faceDetected(false),
    _attachedNode(nullptr),
    _garFace(nullptr) {
    
    // Initialize blend shapes to 0
    for (int i = 0; i <= static_cast<int>(VROBlendShapeLocation::NOSE_SNEER_RIGHT); i++) {
        _blendShapes[static_cast<VROBlendShapeLocation>(i)] = 0.0f;
    }
}

VROARAugmentedFace::~VROARAugmentedFace() {
    if (_garFace) {
        _garFace = nullptr;
    }
}

std::vector<VROVector3f> VROARAugmentedFace::getMeshVertices() const {
    return _meshVertices;
}

std::vector<int> VROARAugmentedFace::getMeshTriangleIndices() const {
    return _meshIndices;
}

std::vector<VROVector3f> VROARAugmentedFace::getMeshTextureCoordinates() const {
    return _meshUVs;
}

std::vector<VROVector3f> VROARAugmentedFace::getMeshNormals() const {
    return _meshNormals;
}

float VROARAugmentedFace::getBlendShapeValue(VROBlendShapeLocation blendShape) const {
    auto it = _blendShapes.find(blendShape);
    if (it != _blendShapes.end()) {
        return it->second;
    }
    return 0.0f;
}

void VROARAugmentedFace::setBlendShapeValue(VROBlendShapeLocation blendShape, float value) {
    _blendShapes[blendShape] = std::max(0.0f, std::min(1.0f, value));
}

VROVector3f VROARAugmentedFace::getRegionPose(VROFaceRegion region) const {
    // Return face region positions in face coordinate space
    switch (region) {
        case VROFaceRegion::NOSE_TIP:
            return VROVector3f(0.0f, -0.03f, 0.01f);
        case VROFaceRegion::FOREHEAD_LEFT:
            return VROVector3f(-0.03f, 0.05f, -0.02f);
        case VROFaceRegion::FOREHEAD_RIGHT:
            return VROVector3f(0.03f, 0.05f, -0.02f);
        case VROFaceRegion::FOREHEAD_TOP:
            return VROVector3f(0.0f, 0.07f, -0.03f);
        case VROFaceRegion::FOREHEAD_CENTER:
            return VROVector3f(0.0f, 0.04f, -0.02f);
        default:
            return VROVector3f(0.0f, 0.0f, 0.0f);
    }
}

void VROARAugmentedFace::updateFromARCore(void* garFace) {
    _garFace = garFace;
    
    if (_garFace) {
        _faceDetected = true;
        updateMeshData();
        updateBlendShapes();
        
        // Update transform from ARCore face pose
        // GARAugmentedFace *face = (__bridge GARAugmentedFace *)_garFace;
        // Extract pose matrix and update anchor transform
    } else {
        _faceDetected = false;
    }
}

void VROARAugmentedFace::updateMeshData() {
    if (!_garFace) {
        return;
    }
    
    // In real implementation, extract mesh data from ARCore
    // GARAugmentedFace *face = (__bridge GARAugmentedFace *)_garFace;
    
    // Placeholder - create basic face mesh
    _meshVertices.clear();
    _meshIndices.clear();
    _meshUVs.clear();
    _meshNormals.clear();
    
    // This would normally extract 468 vertices from ARCore face mesh
    int numVertices = 468;
    _meshVertices.reserve(numVertices);
    _meshUVs.reserve(numVertices);
    _meshNormals.reserve(numVertices);
    
    for (int i = 0; i < numVertices; i++) {
        // Placeholder vertex data
        _meshVertices.push_back(VROVector3f(0.0f, 0.0f, 0.0f));
        _meshUVs.push_back(VROVector3f(0.0f, 0.0f, 0.0f));
        _meshNormals.push_back(VROVector3f(0.0f, 0.0f, 1.0f));
    }
    
    // Placeholder triangle indices
    int numTriangles = 878; // ARCore face mesh has 878 triangles
    _meshIndices.reserve(numTriangles * 3);
    
    for (int i = 0; i < numTriangles * 3; i++) {
        _meshIndices.push_back(i % numVertices);
    }
}

void VROARAugmentedFace::updateBlendShapes() {
    if (!_garFace) {
        return;
    }
    
    // In real implementation, extract blend shape coefficients from ARCore
    // GARAugmentedFace *face = (__bridge GARAugmentedFace *)_garFace;
    
    // Placeholder - set some random blend shape values for testing
    // This would normally extract 50+ blend shape coefficients from ARCore
    
    // Example: extract specific blend shapes
    // _blendShapes[VROBlendShapeLocation::EYE_BLINK_LEFT] = [face blendShapeForLocation:GARAugmentedFaceBlendShapeLocationEyeBlinkLeft];
}