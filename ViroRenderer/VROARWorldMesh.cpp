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
#include "VROPlatformUtil.h"
#include <btBulletDynamicsCommon.h>
#include <unordered_map>
#include <algorithm>

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
    // Remove old body immediately on the render thread (fast — just unlinking).
    removeFromPhysicsWorld();
    _currentMesh = mesh;

    // Simplify for physics. Vertex clustering (gap-free) takes priority over stride decimation.
    std::shared_ptr<VROARDepthMesh> physicsMesh = mesh;
    if (_config.physicsCellSize > 0.0f) {
        physicsMesh = clusterMesh(mesh, _config.physicsCellSize);
    } else if (_config.physicsMaxTriangles > 0 &&
               mesh->getTriangleCount() > _config.physicsMaxTriangles) {
        physicsMesh = decimateMesh(mesh, _config.physicsMaxTriangles);
        pinfo("VROARWorldMesh: decimated physics mesh from %d to %d triangles",
              mesh->getTriangleCount(), physicsMesh->getTriangleCount());
    }

    pinfo("VROARWorldMesh: updated mesh with %d vertices, %d triangles",
          mesh->getVertexCount(), mesh->getTriangleCount());

    // ── Async BVH construction ────────────────────────────────────────────────
    // btBvhTriangleMeshShape construction is O(n log n) and can take 200–400 ms
    // for dense LiDAR meshes, blocking the render thread and causing ARKit to
    // back up retaining ARFrames. Move it to a background thread; marshal only
    // the rigid-body creation and physics-world insertion back to render thread.
    std::weak_ptr<VROARWorldMesh> weakSelf = shared_from_this();
    auto vertices = physicsMesh->getVertices();
    auto indices  = physicsMesh->getIndices();
    float friction    = _config.friction;
    float restitution = _config.restitution;
    std::weak_ptr<VROPhysicsWorld> weakPhysicsWorld = _physicsWorld;

    VROPlatformDispatchAsyncBackground([weakSelf, vertices, indices,
                                        friction, restitution, weakPhysicsWorld]() {
        // Build the expensive BVH on the background thread.
        auto shape = std::make_shared<VROPhysicsShape>(vertices, indices);
        if (!shape->getBulletShape()) {
            pwarn("VROARWorldMesh: failed to create physics shape (background)");
            return;
        }

        // Marshal rigid-body creation + physics-world insertion back to render thread.
        VROPlatformDispatchAsyncRenderer([weakSelf, shape, friction, restitution,
                                          weakPhysicsWorld]() {
            auto self = weakSelf.lock();
            if (!self) return;
            // Abort if the world mesh was disabled or a newer update arrived first.
            if (!self->_enabled) return;

            self->_physicsShape = shape;

            btTransform transform;
            transform.setIdentity();
            self->_motionState = std::unique_ptr<btDefaultMotionState>(
                new btDefaultMotionState(transform));

            btRigidBody::btRigidBodyConstructionInfo rbInfo(
                0.0f, self->_motionState.get(),
                shape->getBulletShape(), btVector3(0, 0, 0));

            self->_rigidBody = std::unique_ptr<btRigidBody, BulletRigidBodyDeleter>(
                new btRigidBody(rbInfo), BulletRigidBodyDeleter{weakPhysicsWorld});
            self->_rigidBody->setFriction(friction);
            self->_rigidBody->setRestitution(restitution);
            self->_rigidBody->setCollisionFlags(
                self->_rigidBody->getCollisionFlags() | btCollisionObject::CF_STATIC_OBJECT);

            self->addToPhysicsWorld();
        });
    });
}

void VROARWorldMesh::addToPhysicsWorld() {
    if (!_rigidBody) {
        pinfo("VROARWorldMesh::addToPhysicsWorld - no rigid body");
        return;
    }
    if (_isAddedToPhysicsWorld) {
        return;  // already in world — removeFromPhysicsWorld() must be called first
    }

    std::shared_ptr<VROPhysicsWorld> physicsWorld = _physicsWorld.lock();
    if (physicsWorld) {
        physicsWorld->addRigidBody(_rigidBody.get());
        _isAddedToPhysicsWorld = true;
        pinfo("VROARWorldMesh::addToPhysicsWorld - added rigid body to physics world");
    } else {
        pwarn("VROARWorldMesh::addToPhysicsWorld - physics world is null!");
    }
}

void VROARWorldMesh::removeFromPhysicsWorld() {
    _isAddedToPhysicsWorld = false;
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

std::shared_ptr<VROARDepthMesh> VROARWorldMesh::decimateMesh(
    std::shared_ptr<VROARDepthMesh> mesh, int maxTriangles)
{
    int totalTriangles = mesh->getTriangleCount();
    if (totalTriangles <= maxTriangles) {
        return mesh;
    }

    const std::vector<int>& srcIndices = mesh->getIndices();
    int stride = totalTriangles / maxTriangles;

    std::vector<int> newIndices;
    newIndices.reserve((size_t)maxTriangles * 3);

    for (int t = 0; t < totalTriangles && (int)(newIndices.size() / 3) < maxTriangles; t += stride) {
        newIndices.push_back(srcIndices[t * 3 + 0]);
        newIndices.push_back(srcIndices[t * 3 + 1]);
        newIndices.push_back(srcIndices[t * 3 + 2]);
    }

    return std::make_shared<VROARDepthMesh>(
        mesh->getVertices(),
        std::move(newIndices),
        mesh->getConfidences(),
        mesh->getSource()
    );
}

std::shared_ptr<VROARDepthMesh> VROARWorldMesh::clusterMesh(
    std::shared_ptr<VROARDepthMesh> mesh, float cellSize)
{
    if (cellSize <= 0.0f) return mesh;

    const std::vector<VROVector3f>& verts = mesh->getVertices();
    const std::vector<int>&         idxs  = mesh->getIndices();
    if (verts.empty() || idxs.empty()) return mesh;

    // Compute bounding box
    VROVector3f lo(verts[0]), hi(verts[0]);
    for (const auto& v : verts) {
        lo.x = std::min(lo.x, v.x); lo.y = std::min(lo.y, v.y); lo.z = std::min(lo.z, v.z);
        hi.x = std::max(hi.x, v.x); hi.y = std::max(hi.y, v.y); hi.z = std::max(hi.z, v.z);
    }

    // Map each vertex to its grid cell; keep one representative per cell.
    // Encode (ix, iy, iz) into a 64-bit key (max grid 2^20 per axis ≈ 1km at 1mm cells).
    std::unordered_map<uint64_t, int> cellToRep;
    cellToRep.reserve(verts.size() / 4);

    std::vector<VROVector3f> newVerts;
    newVerts.reserve(verts.size() / 4);

    std::vector<int> vertToRep(verts.size(), -1);

    for (int i = 0; i < (int)verts.size(); ++i) {
        int ix = (int)((verts[i].x - lo.x) / cellSize);
        int iy = (int)((verts[i].y - lo.y) / cellSize);
        int iz = (int)((verts[i].z - lo.z) / cellSize);
        uint64_t key = ((uint64_t)(ix & 0xFFFFF) << 40)
                     | ((uint64_t)(iy & 0xFFFFF) << 20)
                     |  (uint64_t)(iz & 0xFFFFF);

        auto it = cellToRep.find(key);
        if (it == cellToRep.end()) {
            int repIdx = (int)newVerts.size();
            cellToRep[key] = repIdx;
            newVerts.push_back(verts[i]);
            vertToRep[i] = repIdx;
        } else {
            vertToRep[i] = it->second;
        }
    }

    // Rebuild index buffer; drop degenerate triangles (2+ verts in same cell).
    std::vector<int> newIdxs;
    newIdxs.reserve(idxs.size());

    for (int t = 0; t < (int)idxs.size(); t += 3) {
        int r0 = vertToRep[idxs[t]];
        int r1 = vertToRep[idxs[t + 1]];
        int r2 = vertToRep[idxs[t + 2]];
        if (r0 == r1 || r1 == r2 || r0 == r2) continue;
        newIdxs.push_back(r0);
        newIdxs.push_back(r1);
        newIdxs.push_back(r2);
    }

    pinfo("VROARWorldMesh: clustered mesh from %d to %d triangles (cellSize=%.2fm)",
          (int)(idxs.size() / 3), (int)(newIdxs.size() / 3), cellSize);

    return std::make_shared<VROARDepthMesh>(
        std::move(newVerts),
        std::move(newIdxs),
        std::vector<float>(),
        mesh->getSource()
    );
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
        const VROWorldMeshSubscriberOptions& opts = kv.second.second;
        if (opts.maxTriangles > 0 && mesh->getTriangleCount() > opts.maxTriangles) {
            VROWorldMeshUpdate decimatedUpdate = update;
            decimatedUpdate.mesh = decimateMesh(mesh, opts.maxTriangles);
            kv.second.first(decimatedUpdate);
        } else {
            kv.second.first(update);
        }
    }
}

void VROARWorldMesh::debugDraw(std::shared_ptr<VROPencil> pencil) {
    if (!pencil || !_config.debugDrawEnabled || !_currentMesh || !_currentMesh->isValid()) {
        return;
    }

    pencil->setDepthTestEnabled(_config.debugDrawDepthTest);
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
