//
//  VRODynamicMeshNode.cpp
//  ViroKit
//
//  Copyright © 2026 ReactVision. All rights reserved.

#include "VRODynamicMeshNode.h"
#include "VRODynamicGeometry.h"

void VRODynamicMeshNode::init(std::shared_ptr<VRODriver> driver,
                               int maxVertices,
                               VRODynamicGeometryFeatures features) {
    _dynGeom = std::make_shared<VRODynamicGeometry>(driver, maxVertices, features);
    setGeometry(_dynGeom);
}

void VRODynamicMeshNode::setBuffers(const std::vector<VROVector3f> &positions,
                                     const std::vector<VROVector3f> &normals,
                                     const std::vector<VROVector2f> &uvs,
                                     const std::vector<VROVector4f> &colors,
                                     int vertexCount) {
    if (!_dynGeom || vertexCount <= 0) return;

    _scratchPos.resize(vertexCount * 3);
    _scratchNorm.resize(vertexCount * 3);
    _scratchUV.resize(vertexCount * 2);
    _scratchColor.resize(vertexCount * 4);

    for (int i = 0; i < vertexCount; ++i) {
        _scratchPos[i*3+0] = positions[i].x;
        _scratchPos[i*3+1] = positions[i].y;
        _scratchPos[i*3+2] = positions[i].z;

        _scratchNorm[i*3+0] = normals[i].x;
        _scratchNorm[i*3+1] = normals[i].y;
        _scratchNorm[i*3+2] = normals[i].z;

        _scratchUV[i*2+0] = uvs[i].x;
        _scratchUV[i*2+1] = uvs[i].y;

        _scratchColor[i*4+0] = (uint8_t)(colors[i].x * 255.0f);
        _scratchColor[i*4+1] = (uint8_t)(colors[i].y * 255.0f);
        _scratchColor[i*4+2] = (uint8_t)(colors[i].z * 255.0f);
        _scratchColor[i*4+3] = (uint8_t)(colors[i].w * 255.0f);
    }

    _dynGeom->setBuffers(
        _scratchPos.data(),
        _scratchNorm.data(),
        _scratchUV.data(),
        _scratchColor.data(),
        vertexCount / 3   // triangleCount
    );
}
