//
//  VROARWorldMesh.cpp
//  ViroRenderer
//
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

#include "VROARWorldMesh.h"
#include "VROARDepthMesh.h"
#include "VROARFrame.h"
#include "VROPhysicsWorld.h"
#include "VROPhysicsShape.h"
#include "VROPencil.h"
#include "VROLog.h"
#include <btBulletDynamicsCommon.h>

VROARWorldMesh::VROARWorldMesh(std::shared_ptr<VROPhysicsWorld> physicsWorld)
    : _physicsWorld(physicsWorld) {
}

VROARWorldMesh::~VROARWorldMesh() {
    removeFromPhysicsWorld();
}

void VROARWorldMesh::setConfig(const VROWorldMeshConfig& config) {
    _config = config;

    // Update physics properties on existing rigid body
    if (_rigidBody) {
        _rigidBody->setFriction(_config.friction);
        _rigidBody->setRestitution(_config.restitution);
    }
}

void VROARWorldMesh::setEnabled(bool enabled) {
    if (_enabled == enabled) {
        return;
    }
    _enabled = enabled;

    if (_enabled) {
        // If we have a mesh, add it to the physics world
        if (_currentMesh && _currentMesh->isValid() && _rigidBody) {
            addToPhysicsWorld();
        }
    } else {
        // Remove from physics world when disabled
        removeFromPhysicsWorld();
    }

    pinfo("VROARWorldMesh: %s", _enabled ? "enabled" : "disabled");
}

void VROARWorldMesh::updateFromFrame(const std::unique_ptr<VROARFrame>& frame) {
    if (!_enabled || !frame) {
        return;
    }

    // Check if it's time for an update
    if (!shouldUpdate()) {
        return;
    }

    // Check if depth data is available
    if (!frame->hasDepthData()) {
        if (isMeshStale()) {
            pinfo("VROARWorldMesh: depth data lost, mesh is stale");
        }
        return;
    }

    // Generate new mesh from depth data
    auto mesh = frame->generateDepthMesh(
        _config.stride,
        _config.minConfidence,
        _config.maxDepth
    );

    if (mesh && mesh->isValid()) {
        _lastDepthTimeMs = getCurrentTimeMs();
        applyMeshToPhysics(mesh);

        // Notify callback
        if (_updateCallback) {
            _updateCallback(getStats());
        }
    }

    _lastUpdateTimeMs = getCurrentTimeMs();
}

void VROARWorldMesh::forceUpdate(const std::unique_ptr<VROARFrame>& frame) {
    if (!_enabled || !frame || !frame->hasDepthData()) {
        return;
    }

    auto mesh = frame->generateDepthMesh(
        _config.stride,
        _config.minConfidence,
        _config.maxDepth
    );

    if (mesh && mesh->isValid()) {
        _lastDepthTimeMs = getCurrentTimeMs();
        _lastUpdateTimeMs = getCurrentTimeMs();
        applyMeshToPhysics(mesh);

        if (_updateCallback) {
            _updateCallback(getStats());
        }
    }
}

void VROARWorldMesh::applyMeshToPhysics(std::shared_ptr<VROARDepthMesh> mesh) {
    // Remove old physics body first
    removeFromPhysicsWorld();

    _currentMesh = mesh;

    // Create new physics shape from mesh vertices and indices
    _physicsShape = std::make_shared<VROPhysicsShape>(
        mesh->getVertices(),
        mesh->getIndices()
    );

    if (!_physicsShape->getBulletShape()) {
        pwarn("VROARWorldMesh: failed to create physics shape from depth mesh");
        return;
    }

    // Create motion state at identity transform (mesh is already in world space)
    btTransform transform;
    transform.setIdentity();
    _motionState = new btDefaultMotionState(transform);

    // Create rigid body with mass 0 (static body)
    btRigidBody::btRigidBodyConstructionInfo rbInfo(
        0.0f,  // mass = 0 for static body
        _motionState,
        _physicsShape->getBulletShape(),
        btVector3(0, 0, 0)  // local inertia (unused for static)
    );

    _rigidBody = new btRigidBody(rbInfo);
    _rigidBody->setFriction(_config.friction);
    _rigidBody->setRestitution(_config.restitution);

    // Set collision flags for static object
    _rigidBody->setCollisionFlags(
        _rigidBody->getCollisionFlags() | btCollisionObject::CF_STATIC_OBJECT
    );

    // Add to physics world
    addToPhysicsWorld();

    pinfo("VROARWorldMesh: updated mesh with %d vertices, %d triangles",
          mesh->getVertexCount(), mesh->getTriangleCount());
}

void VROARWorldMesh::addToPhysicsWorld() {
    if (!_rigidBody) {
        pinfo("VROARWorldMesh::addToPhysicsWorld - no rigid body");
        return;
    }

    std::shared_ptr<VROPhysicsWorld> physicsWorld = _physicsWorld.lock();
    if (physicsWorld) {
        // Add with collision group/mask that collides with everything
        // Group 1 = default static, Mask -1 = collide with all
        physicsWorld->addRigidBody(_rigidBody);
        pinfo("VROARWorldMesh::addToPhysicsWorld - added rigid body to physics world");
    } else {
        pwarn("VROARWorldMesh::addToPhysicsWorld - physics world is null!");
    }
}

void VROARWorldMesh::removeFromPhysicsWorld() {
    std::shared_ptr<VROPhysicsWorld> physicsWorld = _physicsWorld.lock();

    if (_rigidBody) {
        if (physicsWorld) {
            physicsWorld->removeRigidBody(_rigidBody);
        }
        delete _rigidBody;
        _rigidBody = nullptr;
    }

    if (_motionState) {
        delete _motionState;
        _motionState = nullptr;
    }

    // Physics shape will be cleaned up by shared_ptr
    _physicsShape = nullptr;
}

VROWorldMeshStats VROARWorldMesh::getStats() const {
    VROWorldMeshStats stats;

    if (_currentMesh && _currentMesh->isValid()) {
        stats.vertexCount = _currentMesh->getVertexCount();
        stats.triangleCount = _currentMesh->getTriangleCount();
        stats.averageConfidence = _currentMesh->getAverageConfidence();
    }

    stats.lastUpdateTimeMs = _lastUpdateTimeMs;
    stats.isStale = isMeshStale();

    return stats;
}

double VROARWorldMesh::getCurrentTimeMs() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::milliseconds>(duration).count()
    );
}

bool VROARWorldMesh::shouldUpdate() const {
    double currentTime = getCurrentTimeMs();
    return (currentTime - _lastUpdateTimeMs) >= _config.updateIntervalMs;
}

bool VROARWorldMesh::isMeshStale() const {
    if (_lastDepthTimeMs == 0) {
        return false;  // Never had depth data, not "stale"
    }
    double currentTime = getCurrentTimeMs();
    return (currentTime - _lastDepthTimeMs) > _config.meshPersistenceMs;
}

void VROARWorldMesh::debugDraw(std::shared_ptr<VROPencil> pencil) {
    if (!pencil || !_config.debugDrawEnabled || !_currentMesh || !_currentMesh->isValid()) {
        return;
    }

    // Set thin line thickness for wireframe (1mm for crisp lines)
    pencil->setBrushThickness(0.001f);

    const std::vector<VROVector3f>& vertices = _currentMesh->getVertices();
    const std::vector<int>& indices = _currentMesh->getIndices();

    // Draw more triangles for a better representation of the surroundings
    // Max 1000 triangles (3000 edges) should give good coverage without overwhelming performance
    const size_t maxTriangles = 1000;
    size_t totalTriangles = indices.size() / 3;
    size_t triangleStride = (totalTriangles > maxTriangles) ? (totalTriangles / maxTriangles) : 1;

    // Draw complete wireframe triangles (all 3 edges) for proper mesh visualization
    size_t trianglesDrawn = 0;
    for (size_t i = 0; i + 2 < indices.size() && trianglesDrawn < maxTriangles; i += 3 * triangleStride) {
        int i0 = indices[i];
        int i1 = indices[i + 1];
        int i2 = indices[i + 2];

        if (i0 >= 0 && i0 < (int)vertices.size() &&
            i1 >= 0 && i1 < (int)vertices.size() &&
            i2 >= 0 && i2 < (int)vertices.size()) {

            const VROVector3f& v0 = vertices[i0];
            const VROVector3f& v1 = vertices[i1];
            const VROVector3f& v2 = vertices[i2];

            // Draw all 3 edges of each triangle for complete wireframe
            pencil->draw(v0, v1);
            pencil->draw(v1, v2);
            pencil->draw(v2, v0);
            trianglesDrawn++;
        }
    }
}
