//
//  VROARFaceTrackingiOS.mm
//  ViroKit
//
//  Implementation of iOS Face Tracking using ARKit
//  Copyright © 2024 Viro Media. All rights reserved.

#include "VROARFaceTrackingiOS.h"

#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110000

#include "VROConvert.h"
#include "VROLog.h"
#include "VROGeometry.h"
#include "VROGeometrySource.h"
#include "VROGeometryElement.h"
#include <Foundation/Foundation.h>

API_AVAILABLE(ios(11.0))
VROARFaceTrackingiOS::VROARFaceTrackingiOS(ARFaceAnchor *faceAnchor) :
    _faceAnchor(faceAnchor),
    _faceGeometry(nullptr),
    _meshDataDirty(true) {
    
    [_faceAnchor retain];
    
    // Set initial properties
    if (_faceAnchor.identifier) {
        setId([_faceAnchor.identifier.UUIDString UTF8String]);
    }
    
    // Update from ARKit data
    updateFromFaceAnchor(_faceAnchor);
}

API_AVAILABLE(ios(11.0))
VROARFaceTrackingiOS::~VROARFaceTrackingiOS() {
    if (_faceAnchor) {
        [_faceAnchor release];
        _faceAnchor = nullptr;
    }
    
    if (_faceGeometry) {
        [_faceGeometry release];
        _faceGeometry = nullptr;
    }
}

void VROARFaceTrackingiOS::updateFromFaceAnchor(ARFaceAnchor *faceAnchor) {
    if (!faceAnchor) {
        return;
    }
    
    if (@available(iOS 11.0, *)) {
        // Update face anchor reference
        if (_faceAnchor != faceAnchor) {
            if (_faceAnchor) {
                [_faceAnchor release];
            }
            _faceAnchor = faceAnchor;
            [_faceAnchor retain];
        }
        
        // Update face geometry
        if (_faceGeometry) {
            [_faceGeometry release];
        }
        _faceGeometry = [[ARFaceGeometry alloc] initWithBlendShapes:_faceAnchor.blendShapes];
        
        // Update transforms
        _faceTransform = convertTransform(_faceAnchor.transform);
        setCenterPose(_faceTransform);
        
        // Update blend shapes
        updateBlendShapes();
        
        // Update eye transforms
        updateEyeTransforms();
        
        // Mark mesh data as dirty for regeneration
        _meshDataDirty = true;
    }
}

VROMatrix4f VROARFaceTrackingiOS::getTransform() const {
    return _faceTransform;
}

API_AVAILABLE(ios(11.0))
ARFaceGeometry* VROARFaceTrackingiOS::getFaceGeometry() const {
    return _faceGeometry;
}

API_AVAILABLE(ios(11.0))
float VROARFaceTrackingiOS::getBlendShapeValue(ARBlendShapeLocation location) const {
    if (@available(iOS 11.0, *)) {
        if (_blendShapes && location) {
            NSNumber *value = _blendShapes[location];
            return value ? [value floatValue] : 0.0f;
        }
    }
    return 0.0f;
}

// Override parent class blend shape accessors with ARKit mapping
float VROARFaceTrackingiOS::getEyeBlinkLeft() const {
    if (@available(iOS 11.0, *)) {
        return getBlendShapeValue(ARBlendShapeLocationEyeBlinkLeft);
    }
    return 0.0f;
}

float VROARFaceTrackingiOS::getEyeBlinkRight() const {
    if (@available(iOS 11.0, *)) {
        return getBlendShapeValue(ARBlendShapeLocationEyeBlinkRight);
    }
    return 0.0f;
}

float VROARFaceTrackingiOS::getMouthSmileLeft() const {
    if (@available(iOS 11.0, *)) {
        return getBlendShapeValue(ARBlendShapeLocationMouthSmileLeft);
    }
    return 0.0f;
}

float VROARFaceTrackingiOS::getMouthSmileRight() const {
    if (@available(iOS 11.0, *)) {
        return getBlendShapeValue(ARBlendShapeLocationMouthSmileRight);
    }
    return 0.0f;
}

float VROARFaceTrackingiOS::getMouthOpen() const {
    if (@available(iOS 11.0, *)) {
        return getBlendShapeValue(ARBlendShapeLocationJawOpen);
    }
    return 0.0f;
}

float VROARFaceTrackingiOS::getBrowUpLeft() const {
    if (@available(iOS 11.0, *)) {
        return getBlendShapeValue(ARBlendShapeLocationBrowOuterUpLeft);
    }
    return 0.0f;
}

float VROARFaceTrackingiOS::getBrowUpRight() const {
    if (@available(iOS 11.0, *)) {
        return getBlendShapeValue(ARBlendShapeLocationBrowOuterUpRight);
    }
    return 0.0f;
}

API_AVAILABLE(ios(12.0))
VROVector3f VROARFaceTrackingiOS::getLookAtPoint() const {
    if (@available(iOS 12.0, *)) {
        if (_faceAnchor && _faceAnchor.lookAtPoint) {
            simd_float3 lookAt = _faceAnchor.lookAtPoint;
            return VROVector3f(lookAt.x, lookAt.y, lookAt.z);
        }
    }
    return VROVector3f(0, 0, -1); // Default forward
}

VROMatrix4f VROARFaceTrackingiOS::getLeftEyeTransform() const {
    return _leftEyeTransform;
}

VROMatrix4f VROARFaceTrackingiOS::getRightEyeTransform() const {
    return _rightEyeTransform;
}

API_AVAILABLE(ios(11.0))
SCNGeometry* VROARFaceTrackingiOS::createSCNFaceGeometry() const {
    if (@available(iOS 11.0, *)) {
        if (_faceGeometry) {
            return [SCNGeometry geometryWithSources:@[] elements:@[]]; // Placeholder
        }
    }
    return nullptr;
}

API_AVAILABLE(ios(11.0))
void VROARFaceTrackingiOS::applyOcclusionGeometry(SCNNode* node) const {
    if (@available(iOS 11.0, *)) {
        if (!node || !_faceGeometry) {
            return;
        }
        
        // Create occlusion geometry using ARFaceGeometry
        SCNGeometry *occlusionGeometry = createSCNFaceGeometry();
        if (occlusionGeometry) {
            // Configure for occlusion (depth-only rendering)
            SCNMaterial *occlusionMaterial = [SCNMaterial material];
            occlusionMaterial.colorBufferWriteMask = SCNColorMaskNone;
            occlusionMaterial.writesToDepthBuffer = YES;
            occlusionMaterial.readsFromDepthBuffer = YES;
            
            occlusionGeometry.materials = @[occlusionMaterial];
            node.geometry = occlusionGeometry;
        }
    }
}

API_AVAILABLE(ios(11.0))
SCNMorpher* VROARFaceTrackingiOS::createFaceMorpher() const {
    if (@available(iOS 11.0, *)) {
        if (!_faceGeometry || !_blendShapes) {
            return nullptr;
        }
        
        SCNMorpher *morpher = [SCNMorpher morpher];
        
        // Create morph targets for blend shapes
        NSMutableArray *targets = [NSMutableArray array];
        NSMutableArray *targetWeights = [NSMutableArray array];
        
        for (ARBlendShapeLocation location in _blendShapes.allKeys) {
            NSNumber *weight = _blendShapes[location];
            if (weight && [weight floatValue] > 0.01f) {
                // Create target geometry for this blend shape
                // This would require creating modified face geometry
                // For now, we'll just record the weight
                [targetWeights addObject:weight];
            }
        }
        
        morpher.targets = targets;
        
        return morpher;
    }
    return nullptr;
}

bool VROARFaceTrackingiOS::isFaceTrackingSupported() {
    if (@available(iOS 11.0, *)) {
        return [ARFaceTrackingConfiguration isSupported];
    }
    return false;
}

API_AVAILABLE(ios(13.0))
int VROARFaceTrackingiOS::getMaximumNumberOfTrackedFaces() {
    if (@available(iOS 13.0, *)) {
        return (int)[ARFaceTrackingConfiguration supportedNumberOfTrackedFaces];
    }
    return 1;
}

API_AVAILABLE(ios(11.0))
ARFaceTrackingConfiguration* VROARFaceTrackingiOS::createFaceTrackingConfiguration(
    bool trackMultipleFaces,
    bool useWorldTracking) {
    
    if (@available(iOS 11.0, *)) {
        if (![ARFaceTrackingConfiguration isSupported]) {
            return nullptr;
        }
        
        ARFaceTrackingConfiguration *config = [ARFaceTrackingConfiguration new];
        
        // Configure number of tracked faces
        if (@available(iOS 13.0, *)) {
            if (trackMultipleFaces) {
                config.maximumNumberOfTrackedFaces = [ARFaceTrackingConfiguration supportedNumberOfTrackedFaces];
            } else {
                config.maximumNumberOfTrackedFaces = 1;
            }
        }
        
        // Enable world tracking if requested and supported
        if (@available(iOS 13.0, *)) {
            if (useWorldTracking && [ARConfiguration supportsUserFaceTracking]) {
                config.userFaceTrackingEnabled = YES;
            }
        }
        
        return config;
    }
    
    return nullptr;
}

void VROARFaceTrackingiOS::updateMeshData() {
    if (!_meshDataDirty || !_faceGeometry) {
        return;
    }
    
    if (@available(iOS 11.0, *)) {
        // Get vertex data
        const float *vertices = (const float *)_faceGeometry.vertices.bytes;
        NSInteger vertexCount = _faceGeometry.vertices.length / (sizeof(float) * 3);
        
        _cachedVertices.clear();
        for (NSInteger i = 0; i < vertexCount; i++) {
            _cachedVertices.emplace_back(vertices[i*3], vertices[i*3+1], vertices[i*3+2]);
        }
        setMeshVertices(_cachedVertices);
        
        // Get index data
        const int16_t *indices = (const int16_t *)_faceGeometry.triangleIndices.bytes;
        NSInteger indexCount = _faceGeometry.triangleIndices.length / sizeof(int16_t);
        
        _cachedIndices.clear();
        for (NSInteger i = 0; i < indexCount; i++) {
            _cachedIndices.push_back(static_cast<uint16_t>(indices[i]));
        }
        setMeshIndices(_cachedIndices);
        
        // Get UV coordinates
        const float *uvs = (const float *)_faceGeometry.textureCoordinates.bytes;
        NSInteger uvCount = _faceGeometry.textureCoordinates.length / (sizeof(float) * 2);
        
        _cachedUVs.clear();
        for (NSInteger i = 0; i < uvCount; i++) {
            _cachedUVs.emplace_back(uvs[i*2], uvs[i*2+1]);
        }
        setMeshUVs(_cachedUVs);
        
        // Compute normals from geometry
        computeNormals();
        
        _meshDataDirty = false;
    }
}

void VROARFaceTrackingiOS::updateBlendShapes() {
    if (@available(iOS 11.0, *)) {
        if (_faceAnchor.blendShapes) {
            _blendShapes = _faceAnchor.blendShapes;
            [_blendShapes retain];
            
            // Update parent class blend shapes
            for (ARBlendShapeLocation location in _blendShapes.allKeys) {
                NSNumber *value = _blendShapes[location];
                if (value && location) {
                    std::string locationStr = [location UTF8String];
                    setBlendShapeCoefficient(locationStr, [value floatValue]);
                }
            }
            
            setTrackingConfidence(1.0f); // ARKit doesn't provide explicit confidence
        }
    }
}

void VROARFaceTrackingiOS::updateEyeTransforms() {
    if (@available(iOS 11.0, *)) {
        if (_faceAnchor.leftEyeTransform) {
            _leftEyeTransform = convertTransform(_faceAnchor.leftEyeTransform);
        }
        
        if (_faceAnchor.rightEyeTransform) {
            _rightEyeTransform = convertTransform(_faceAnchor.rightEyeTransform);
        }
    }
}

VROMatrix4f VROARFaceTrackingiOS::convertTransform(matrix_float4x4 transform) {
    return VROConvert::toMatrix4f(transform);
}

VROVector3f VROARFaceTrackingiOS::convertVector(simd_float3 vector) {
    return VROVector3f(vector.x, vector.y, vector.z);
}

void VROARFaceTrackingiOS::computeNormals() {
    if (_cachedVertices.empty() || _cachedIndices.empty()) {
        return;
    }
    
    // Initialize normals
    _cachedNormals.clear();
    _cachedNormals.resize(_cachedVertices.size(), VROVector3f(0, 0, 0));
    
    // Compute face normals and accumulate vertex normals
    for (size_t i = 0; i < _cachedIndices.size(); i += 3) {
        uint16_t i0 = _cachedIndices[i];
        uint16_t i1 = _cachedIndices[i + 1];
        uint16_t i2 = _cachedIndices[i + 2];
        
        if (i0 >= _cachedVertices.size() || i1 >= _cachedVertices.size() || i2 >= _cachedVertices.size()) {
            continue;
        }
        
        const VROVector3f& v0 = _cachedVertices[i0];
        const VROVector3f& v1 = _cachedVertices[i1];
        const VROVector3f& v2 = _cachedVertices[i2];
        
        // Compute face normal
        VROVector3f edge1 = v1 - v0;
        VROVector3f edge2 = v2 - v0;
        VROVector3f faceNormal = edge1.cross(edge2).normalize();
        
        // Accumulate to vertex normals
        _cachedNormals[i0] += faceNormal;
        _cachedNormals[i1] += faceNormal;
        _cachedNormals[i2] += faceNormal;
    }
    
    // Normalize vertex normals
    for (auto& normal : _cachedNormals) {
        normal = normal.normalize();
    }
    
    setMeshNormals(_cachedNormals);
}

#endif // iOS 11.0+