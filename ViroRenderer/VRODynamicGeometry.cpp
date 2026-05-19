//
//  VRODynamicGeometry.cpp
//  ViroKit
//
//  Copyright © 2026 ReactVision. All rights reserved.
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

#include "VRODynamicGeometry.h"
#include "VROVertexBuffer.h"
#include "VRODriver.h"
#include "VROData.h"
#include "VROGeometrySource.h"
#include "VROGeometryElement.h"
#include "VROLog.h"
#include <vector>

VRODynamicGeometry::VRODynamicGeometry(std::shared_ptr<VRODriver> driver,
                                       int maxVertices,
                                       VRODynamicGeometryFeatures features)
    : VROGeometry(),
      _driver(driver),
      _features(features),
      _maxVertices(maxVertices),
      _currentTriangleCount(0) {
    // Positions are implicitly required.
    _features = _features | VRODynamicGeometryFeatures::Positions;
    initBuffers();
}

VRODynamicGeometry::~VRODynamicGeometry() {
    // Base VROGeometry destructor handles the substrate; shared_ptrs handle the
    // vertex buffers and the element.
}

void VRODynamicGeometry::initBuffers() {
    std::shared_ptr<VRODriver> driver = _driver.lock();
    if (!driver) {
        pwarn("VRODynamicGeometry: driver is null, cannot allocate buffers");
        return;
    }
    if (_maxVertices <= 0) {
        pwarn("VRODynamicGeometry: invalid maxVertices (%d); must be > 0", _maxVertices);
        return;
    }

    auto makeBuffer = [&](size_t bytes) -> std::shared_ptr<VROVertexBuffer> {
        // Zero-initialised CPU-side backing storage. VROData will copy it.
        std::vector<uint8_t> zeros((size_t)bytes, 0);
        auto data = std::make_shared<VROData>((const void *)zeros.data(), (int)bytes);
        auto buf = driver->newVertexBuffer(data, VROVertexBufferUsage::Dynamic);
        buf->hydrate();
        return buf;
    };

    std::vector<std::shared_ptr<VROGeometrySource>> sources;

    // Positions: required, vec3 of float.
    {
        const int components = 3;
        const int bytesPerComp = sizeof(float);
        const int stride = components * bytesPerComp;
        size_t bytes = (size_t)_maxVertices * stride;
        _positionBuffer = makeBuffer(bytes);
        sources.push_back(std::make_shared<VROGeometrySource>(
            _positionBuffer,
            VROGeometrySourceSemantic::Vertex,
            _maxVertices,
            /*floatComponents*/ true,
            components,
            bytesPerComp,
            /*dataOffset*/ 0,
            /*dataStride*/ stride));
    }

    if (hasFeature(_features, VRODynamicGeometryFeatures::Normals)) {
        const int components = 3;
        const int bytesPerComp = sizeof(float);
        const int stride = components * bytesPerComp;
        size_t bytes = (size_t)_maxVertices * stride;
        _normalBuffer = makeBuffer(bytes);
        sources.push_back(std::make_shared<VROGeometrySource>(
            _normalBuffer,
            VROGeometrySourceSemantic::Normal,
            _maxVertices,
            true,
            components,
            bytesPerComp,
            0,
            stride));
    }

    if (hasFeature(_features, VRODynamicGeometryFeatures::UVs)) {
        const int components = 2;
        const int bytesPerComp = sizeof(float);
        const int stride = components * bytesPerComp;
        size_t bytes = (size_t)_maxVertices * stride;
        _uvBuffer = makeBuffer(bytes);
        sources.push_back(std::make_shared<VROGeometrySource>(
            _uvBuffer,
            VROGeometrySourceSemantic::Texcoord,
            _maxVertices,
            true,
            components,
            bytesPerComp,
            0,
            stride));
    }

    if (hasFeature(_features, VRODynamicGeometryFeatures::Colors)) {
        const int components = 4;
        const int bytesPerComp = sizeof(uint8_t);
        const int stride = components * bytesPerComp;
        size_t bytes = (size_t)_maxVertices * stride;
        _colorBuffer = makeBuffer(bytes);
        sources.push_back(std::make_shared<VROGeometrySource>(
            _colorBuffer,
            VROGeometrySourceSemantic::Color,
            _maxVertices,
            /*floatComponents*/ false,   // RGBA8
            components,
            bytesPerComp,
            0,
            stride));
    }

    // Pre-allocate a sequential index buffer [0, 1, 2, ..., maxVertices-1].
    //
    // We render via glDrawElements rather than glDrawArrays because the existing
    // VROGeometry::isRenderable() check requires the element to have non-null
    // index data, and the substrate dispatch path is well-tested for the indexed
    // case. Sequential indices give us the same effect as a triangle soup at the
    // cost of one extra index lookup per vertex (negligible) and
    // maxVertices * sizeof(int) bytes of static memory (~12 KB for 3000 verts).
    //
    // Per-frame: only primitiveCount is updated. The index buffer never changes.
    {
        const int bytesPerIndex = sizeof(int);
        std::vector<int> seqIndices((size_t)_maxVertices);
        for (int i = 0; i < _maxVertices; ++i) {
            seqIndices[i] = i;
        }
        auto indexData = std::make_shared<VROData>(
            (const void *)seqIndices.data(),
            (int)(_maxVertices * bytesPerIndex));

        _triangleSoupElement = std::make_shared<VROGeometryElement>();
        _triangleSoupElement->setPrimitiveType(VROGeometryPrimitiveType::Triangle);
        _triangleSoupElement->setPrimitiveCount(0);
        _triangleSoupElement->setData(indexData);
        _triangleSoupElement->setBytesPerIndex(bytesPerIndex);
    }

    // Push into the parent VROGeometry. setSources/setElements call
    // updateSubstrate(), which is a no-op at construction time because
    // _substrate is still null.
    setSources(sources);
    setElements({_triangleSoupElement});
}

void VRODynamicGeometry::setBuffers(const float    *positions,
                                    const float    *normals,
                                    const float    *uvs,
                                    const uint8_t  *colors,
                                    int             triangleCount) {
    if (triangleCount < 0) {
        pwarn("VRODynamicGeometry::setBuffers: negative triangleCount (%d); ignoring",
              triangleCount);
        return;
    }
    const int vertexCount = triangleCount * 3;

    if (vertexCount > _maxVertices) {
        pwarn("VRODynamicGeometry::setBuffers: vertexCount %d exceeds capacity %d. "
              "The underlying VBOs will be reallocated. Construct with a larger "
              "maxVertices to avoid this in steady state.",
              vertexCount, _maxVertices);
        _maxVertices = vertexCount;
        // Note: this does NOT grow the geometry sources' declared vertexCount,
        // which was baked at construction. A capacity-exceeding update may render
        // incorrectly until the substrate is rebuilt. Acceptable as a degraded
        // path; the warning makes it visible.
    }

    if (!positions) {
        pwarn("VRODynamicGeometry::setBuffers: positions is null (required)");
        return;
    }
    updateBuffer(_positionBuffer, positions, (size_t)vertexCount * 3 * sizeof(float));

    if (_normalBuffer) {
        if (!normals) {
            pwarn("VRODynamicGeometry::setBuffers: Normals feature declared but "
                  "normals pointer is null");
        } else {
            updateBuffer(_normalBuffer, normals, (size_t)vertexCount * 3 * sizeof(float));
        }
    } else if (normals) {
        pwarn("VRODynamicGeometry::setBuffers: normals supplied but Normals feature "
              "was not declared at construction; ignoring");
    }

    if (_uvBuffer) {
        if (!uvs) {
            pwarn("VRODynamicGeometry::setBuffers: UVs feature declared but uvs "
                  "pointer is null");
        } else {
            updateBuffer(_uvBuffer, uvs, (size_t)vertexCount * 2 * sizeof(float));
        }
    } else if (uvs) {
        pwarn("VRODynamicGeometry::setBuffers: uvs supplied but UVs feature was "
              "not declared at construction; ignoring");
    }

    if (_colorBuffer) {
        if (!colors) {
            pwarn("VRODynamicGeometry::setBuffers: Colors feature declared but "
                  "colors pointer is null");
        } else {
            updateBuffer(_colorBuffer, colors,
                         (size_t)vertexCount * 4 * sizeof(uint8_t));
        }
    } else if (colors) {
        pwarn("VRODynamicGeometry::setBuffers: colors supplied but Colors feature "
              "was not declared at construction; ignoring");
    }

    _currentTriangleCount = triangleCount;
    if (_triangleSoupElement) {
        _triangleSoupElement->setPrimitiveCount(triangleCount);
    }

    // Bounding box is stale after a buffer swap; force recompute on next query.
    updateBoundingBox();
}

void VRODynamicGeometry::updateBuffer(const std::shared_ptr<VROVertexBuffer> &buffer,
                                       const void *data,
                                       size_t byteSize) {
    if (!buffer || !data || byteSize == 0) {
        return;
    }
    // VROData copies on construction by default for the const-pointer constructor,
    // so the caller's buffer can be reused/freed immediately after this call.
    auto newData = std::make_shared<VROData>(data, (int)byteSize);
    buffer->updateData(newData);
}
