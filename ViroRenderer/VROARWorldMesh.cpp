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

void BulletRigidBodyDeleter::operator()(btRigidBody *body) const {
    if (!body) return;
    auto pw = physicsWorld.lock();
    if (pw) pw->removeRigidBody(body);
    delete body;
}

static VROWorldMeshSource sourceFromMeshTag(const std::string& tag) {
    if (tag == "lidar")     return VROWorldMeshSource::LiDAR;
    if (tag == "monocular") return VROWorldMeshSource::Monocular;
    if (tag == "plane")     return VROWorldMeshSource::Plane;
    return VROWorldMeshSource::Unknown;
}

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

    // Source priority:
    //  1. ARMeshAnchor  — persistent, LiDAR iOS 13.4+
    //  2. depth map     — LiDAR depth image or monocular
    //  3. plane anchors — non-LiDAR / non-Depth-API fallback
    std::shared_ptr<VROARDepthMesh> mesh = frame->generateMeshAnchorMesh();

    if (!mesh || !mesh->isValid()) {
        if (frame->hasDepthData()) {
            mesh = frame->generateDepthMesh(_config.stride, _config.minConfidence, _config.maxDepth);
        }
    }

    if (!mesh || !mesh->isValid()) {
        mesh = frame->generatePlaneMesh();
    }

    if (mesh && mesh->isValid()) {
        _lastDepthTimeMs = getCurrentTimeMs();
        applyMeshToPhysics(mesh);
        notifySubscribers(mesh);
    } else if (isMeshStale()) {
        pinfo("VROARWorldMesh: no mesh source available, mesh is stale");
    }

    _lastUpdateTimeMs = getCurrentTimeMs();
}

void VROARWorldMesh::forceUpdate(const std::unique_ptr<VROARFrame>& frame) {
    if (!_enabled || !frame) {
        return;
    }

    std::shared_ptr<VROARDepthMesh> mesh = frame->generateMeshAnchorMesh();

    if (!mesh || !mesh->isValid()) {
        if (frame->hasDepthData()) {
            mesh = frame->generateDepthMesh(_config.stride, _config.minConfidence, _config.maxDepth);
        }
    }

    if (!mesh || !mesh->isValid()) {
        mesh = frame->generatePlaneMesh();
    }

    if (mesh && mesh->isValid()) {
        _lastDepthTimeMs = getCurrentTimeMs();
        _lastUpdateTimeMs = getCurrentTimeMs();
        applyMeshToPhysics(mesh);
        notifySubscribers(mesh);
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
    _motionState = std::make_unique<btDefaultMotionState>(transform);

    // Create rigid body with mass 0 (static body)
    btRigidBody::btRigidBodyConstructionInfo rbInfo(
        0.0f,  // mass = 0 for static body
        _motionState.get(),
        _physicsShape->getBulletShape(),
        btVector3(0, 0, 0)  // local inertia (unused for static)
    );

    _rigidBody = std::unique_ptr<btRigidBody, BulletRigidBodyDeleter>(
        new btRigidBody(rbInfo), BulletRigidBodyDeleter{_physicsWorld}
    );
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
        physicsWorld->addRigidBody(_rigidBody.get());
        pinfo("VROARWorldMesh::addToPhysicsWorld - added rigid body to physics world");
    } else {
        pwarn("VROARWorldMesh::addToPhysicsWorld - physics world is null!");
    }
}

void VROARWorldMesh::removeFromPhysicsWorld() {
    // BulletRigidBodyDeleter removes the body from the world before deleting it.
    _rigidBody.reset();
    _motionState.reset();
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

VROWorldMeshSubscriberId VROARWorldMesh::subscribe(VROWorldMeshSubscriberCallback callback,
                                                    VROWorldMeshSubscriberOptions options) {
    std::lock_guard<std::mutex> lock(_subscriberMutex);
    VROWorldMeshSubscriberId id = _nextSubscriberId++;
    _subscribers[id] = { std::move(callback), options };
    return id;
}

void VROARWorldMesh::unsubscribe(VROWorldMeshSubscriberId id) {
    std::lock_guard<std::mutex> lock(_subscriberMutex);
    _subscribers.erase(id);
}

void VROARWorldMesh::notifySubscribers(std::shared_ptr<VROARDepthMesh> mesh) {
    VROWorldMeshStats stats = getStats();

    if (_updateCallback) {
        _updateCallback(stats);
    }

    if (_subscribers.empty()) {
        return;
    }

    VROWorldMeshUpdate update;
    update.mesh   = mesh;
    update.stats  = stats;
    update.source = sourceFromMeshTag(mesh->getSource());

    // Snapshot under lock, fire outside to avoid re-entrant deadlock
    decltype(_subscribers) snapshot;
    {
        std::lock_guard<std::mutex> lock(_subscriberMutex);
        snapshot = _subscribers;
    }
    for (auto& kv : snapshot) {
        kv.second.first(update);
    }
}

void VROARWorldMesh::debugDraw(std::shared_ptr<VROPencil> pencil) {
    if (!pencil || !_config.debugDrawEnabled || !_currentMesh || !_currentMesh->isValid()) {
        return;
    }

    pencil->setBrushThickness(_config.debugDrawLineThickness);

    const std::vector<VROVector3f>& vertices = _currentMesh->getVertices();
    const std::vector<int>& indices = _currentMesh->getIndices();

    const size_t maxTriangles = (size_t)_config.debugDrawMaxTriangles;
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
