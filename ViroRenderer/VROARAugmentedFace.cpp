//
//  VROARAugmentedFace.cpp
//  ViroRenderer
//
//  Implementation of Augmented Face functionality
//  Copyright © 2024 Viro Media. All rights reserved.

#include "VROARAugmentedFace.h"
#include "VROGeometry.h"
#include "VROGeometrySource.h"
#include "VROGeometryElement.h"
#include "VROData.h"
#include "VROMaterial.h"
#include "VRONode.h"
#include "VROLog.h"
#include <algorithm>
#include <cmath>

float VROARAugmentedFace::getBlendShapeCoefficient(const std::string& blendShapeName) const {
    if (!_hasBlendShapes) {
        return 0.0f;
    }
    
    auto it = _blendShapeCoefficients.find(blendShapeName);
    if (it != _blendShapeCoefficients.end()) {
        return it->second;
    }
    
    return 0.0f;
}

void VROARAugmentedFace::setBlendShapeCoefficient(const std::string& blendShapeName, float value) {
    _hasBlendShapes = true;
    _blendShapeCoefficients[blendShapeName] = std::max(0.0f, std::min(1.0f, value));
    _meshGeometryDirty = true;
}

VROMatrix4f VROARAugmentedFace::getRegionPose(VROARFaceRegion region) const {
    auto it = _regionPoses.find(region);
    if (it != _regionPoses.end()) {
        return it->second;
    }
    
    // Return identity matrix if region pose not found
    return VROMatrix4f::identity();
}

void VROARAugmentedFace::setRegionPose(VROARFaceRegion region, const VROMatrix4f& pose) {
    _regionPoses[region] = pose;
}

std::shared_ptr<VROGeometry> VROARAugmentedFace::createFaceMeshGeometry() const {
    if (_cachedMeshGeometry && !_meshGeometryDirty) {
        return _cachedMeshGeometry;
    }
    
    if (_meshVertices.empty() || _meshIndices.empty()) {
        return nullptr;
    }
    
    // Create geometry sources
    std::vector<std::shared_ptr<VROGeometrySource>> sources;
    
    // Vertex positions
    size_t vertexDataSize = _meshVertices.size() * sizeof(VROVector3f);
    std::shared_ptr<VROData> vertexData = std::make_shared<VROData>((void*)_meshVertices.data(), vertexDataSize);
    std::shared_ptr<VROGeometrySource> vertexSource = std::make_shared<VROGeometrySource>(
        vertexData,
        VROGeometrySourceSemantic::Vertex,
        _meshVertices.size(),
        true,  // float
        3,     // components per vertex
        sizeof(float),  // bytes per component
        0,     // data offset
        sizeof(VROVector3f)   // data stride
    );
    sources.push_back(vertexSource);
    
    // Normals
    if (!_meshNormals.empty() && _meshNormals.size() == _meshVertices.size()) {
        size_t normalDataSize = _meshNormals.size() * sizeof(VROVector3f);
        std::shared_ptr<VROData> normalData = std::make_shared<VROData>((void*)_meshNormals.data(), normalDataSize);
        std::shared_ptr<VROGeometrySource> normalSource = std::make_shared<VROGeometrySource>(
            normalData,
            VROGeometrySourceSemantic::Normal,
            _meshNormals.size(),
            true,  // float
            3,     // components per normal
            sizeof(float),  // bytes per component
            0,     // data offset
            sizeof(VROVector3f)   // data stride
        );
        sources.push_back(normalSource);
    }
    
    // Texture coordinates
    if (!_meshUVs.empty() && _meshUVs.size() == _meshVertices.size()) {
        size_t uvDataSize = _meshUVs.size() * sizeof(VROVector3f);
        std::shared_ptr<VROData> uvData = std::make_shared<VROData>((void*)_meshUVs.data(), uvDataSize);
        std::shared_ptr<VROGeometrySource> uvSource = std::make_shared<VROGeometrySource>(
            uvData,
            VROGeometrySourceSemantic::Texcoord,
            _meshUVs.size(),
            true,  // float
            2,     // components per UV
            sizeof(float),  // bytes per component
            0,     // data offset
            sizeof(VROVector3f)   // data stride
        );
        sources.push_back(uvSource);
    }
    
    // Create geometry element (indices)
    size_t indexDataSize = _meshIndices.size() * sizeof(uint16_t);
    std::shared_ptr<VROData> indexData = std::make_shared<VROData>((void*)_meshIndices.data(), indexDataSize);
    std::shared_ptr<VROGeometryElement> element = std::make_shared<VROGeometryElement>(
        indexData,
        VROGeometryPrimitiveType::Triangle,
        _meshIndices.size() / 3,  // primitive count
        sizeof(uint16_t)  // bytes per index
    );
    
    // Create the geometry
    std::shared_ptr<VROGeometry> geometry = std::make_shared<VROGeometry>(
        sources,
        std::vector<std::shared_ptr<VROGeometryElement>>{element}
    );
    
    // Cache the geometry
    _cachedMeshGeometry = geometry;
    _meshGeometryDirty = false;
    
    return geometry;
}

void VROARAugmentedFace::applyOcclusionMask(std::shared_ptr<VRONode> node) const {
    if (!node) {
        return;
    }
    
    // Create occlusion geometry (simplified face mesh for depth testing)
    auto occlusionGeometry = createFaceMeshGeometry();
    if (!occlusionGeometry) {
        return;
    }
    
    // Create occlusion material (writes to depth buffer only)
    std::shared_ptr<VROMaterial> occlusionMaterial = std::make_shared<VROMaterial>();
    occlusionMaterial->setColorWriteMask(VROColorMaskNone); // Don't write color
    occlusionMaterial->setWritesToDepthBuffer(true);  // Write to depth buffer
    occlusionMaterial->setReadsFromDepthBuffer(true);   // Read from depth buffer
    occlusionMaterial->setCullMode(VROCullMode::None); // Render both sides
    
    // Apply material to geometry
    occlusionGeometry->setMaterials({occlusionMaterial});
    
    // Set geometry on node
    node->setGeometry(occlusionGeometry);
}

bool VROARAugmentedFace::containsPoint(const VROVector3f& point) const {
    if (_meshVertices.empty() || _meshIndices.empty()) {
        return false;
    }
    
    // Transform point to face coordinate space
    VROMatrix4f invTransform = _centerPose.invert();
    VROVector4f localPoint = invTransform.multiply(VROVector4f(point.x, point.y, point.z, 1.0f));
    VROVector3f localPoint3D(localPoint.x, localPoint.y, localPoint.z);
    
    // Simple bounding box check first
    VROVector3f minBounds(FLT_MAX, FLT_MAX, FLT_MAX);
    VROVector3f maxBounds(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    
    for (const auto& vertex : _meshVertices) {
        minBounds.x = std::min(minBounds.x, vertex.x);
        minBounds.y = std::min(minBounds.y, vertex.y);
        minBounds.z = std::min(minBounds.z, vertex.z);
        maxBounds.x = std::max(maxBounds.x, vertex.x);
        maxBounds.y = std::max(maxBounds.y, vertex.y);
        maxBounds.z = std::max(maxBounds.z, vertex.z);
    }
    
    if (localPoint3D.x < minBounds.x || localPoint3D.x > maxBounds.x ||
        localPoint3D.y < minBounds.y || localPoint3D.y > maxBounds.y ||
        localPoint3D.z < minBounds.z || localPoint3D.z > maxBounds.z) {
        return false;
    }
    
    // For more accurate containment, we'd need ray-triangle intersection tests
    // This is a simplified implementation
    return true;
}

std::vector<VROVector3f> VROARAugmentedFace::getFaceLandmarks2D() const {
    std::vector<VROVector3f> landmarks;
    
    if (_meshVertices.empty()) {
        return landmarks;
    }
    
    // Define key landmark indices for common face features
    // These indices would depend on the specific face mesh topology
    std::vector<int> landmarkIndices = {
        // Eyes (approximate indices)
        33, 7, 163, 144, 145, 153, 154, 155, 133, 173, 157, 158, 159, 160, 161, 246, // Right eye
        362, 398, 384, 385, 386, 387, 388, 466, 263, 249, 390, 373, 374, 380, 381, 382, // Left eye
        
        // Nose (approximate indices)
        1, 2, 5, 4, 6, 168, 8, 9, 10, 151,
        
        // Mouth (approximate indices)
        0, 11, 12, 13, 14, 15, 16, 17, 18, 200, 199, 175, 178, 181, 185, 40, 39, 37, 185, 310, 311, 312, 13, 82, 81, 80, 78
    };
    
    // Project 3D landmarks to 2D screen space
    // This would require the current view projection matrix
    for (int index : landmarkIndices) {
        if (index < static_cast<int>(_meshVertices.size())) {
            const VROVector3f& vertex3D = _meshVertices[index];
            
            // Apply face transform
            VROVector4f worldPos = _centerPose.multiply(VROVector4f(vertex3D.x, vertex3D.y, vertex3D.z, 1.0f));
            
            // For now, return the transformed 3D positions as 2D (would need proper projection)
            landmarks.push_back(VROVector3f(worldPos.x, worldPos.y));
        }
    }
    
    return landmarks;
}








// Utility methods for face mesh processing
void VROARAugmentedFace::smoothMesh() {
    if (_meshVertices.empty() || _meshIndices.empty()) {
        return;
    }
    
    // Simple Laplacian smoothing
    std::vector<VROVector3f> smoothedVertices = _meshVertices;
    std::vector<int> vertexConnections(_meshVertices.size(), 0);
    
    // Build vertex adjacency information
    for (size_t i = 0; i < _meshIndices.size(); i += 3) {
        uint16_t v0 = _meshIndices[i];
        uint16_t v1 = _meshIndices[i + 1];
        uint16_t v2 = _meshIndices[i + 2];
        
        if (v0 < _meshVertices.size() && v1 < _meshVertices.size() && v2 < _meshVertices.size()) {
            smoothedVertices[v0] += _meshVertices[v1] + _meshVertices[v2];
            smoothedVertices[v1] += _meshVertices[v0] + _meshVertices[v2];
            smoothedVertices[v2] += _meshVertices[v0] + _meshVertices[v1];
            
            vertexConnections[v0] += 2;
            vertexConnections[v1] += 2;
            vertexConnections[v2] += 2;
        }
    }
    
    // Average the positions
    for (size_t i = 0; i < _meshVertices.size(); ++i) {
        if (vertexConnections[i] > 0) {
            smoothedVertices[i] = smoothedVertices[i] / static_cast<float>(vertexConnections[i] + 1);
        }
    }
    
    _meshVertices = smoothedVertices;
    _meshGeometryDirty = true;
}

void VROARAugmentedFace::computeNormals() {
    if (_meshVertices.empty() || _meshIndices.empty()) {
        return;
    }
    
    // Initialize normals
    _meshNormals.clear();
    _meshNormals.resize(_meshVertices.size(), VROVector3f(0, 0, 0));
    
    // Compute face normals and accumulate vertex normals
    for (size_t i = 0; i < _meshIndices.size(); i += 3) {
        uint16_t i0 = _meshIndices[i];
        uint16_t i1 = _meshIndices[i + 1];
        uint16_t i2 = _meshIndices[i + 2];
        
        if (i0 >= _meshVertices.size() || i1 >= _meshVertices.size() || i2 >= _meshVertices.size()) {
            continue;
        }
        
        const VROVector3f& v0 = _meshVertices[i0];
        const VROVector3f& v1 = _meshVertices[i1];
        const VROVector3f& v2 = _meshVertices[i2];
        
        // Compute face normal
        VROVector3f edge1 = v1 - v0;
        VROVector3f edge2 = v2 - v0;
        VROVector3f faceNormal = edge1.cross(edge2).normalize();
        
        // Accumulate to vertex normals
        _meshNormals[i0] += faceNormal;
        _meshNormals[i1] += faceNormal;
        _meshNormals[i2] += faceNormal;
    }
    
    // Normalize vertex normals
    for (auto& normal : _meshNormals) {
        normal = normal.normalize();
    }
    
    _meshGeometryDirty = true;
}