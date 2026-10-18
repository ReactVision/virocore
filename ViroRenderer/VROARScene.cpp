//
//  VROARScene.cpp
//  ViroKit
//
//  Created by Andy Chu on 6/13/17.
//  Copyright © 2017 Viro Media. All rights reserved.
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

#include "VROARScene.h"
#include "VROARAnchor.h"
#include "VROPortal.h"
#include "VROARSession.h"
#include "VROARFrame.h"
#include "VROARDeclarativeNode.h"
#include "VROARConstraintMatcher.h"
#include "VROFixedParticleEmitter.h"
#include "VROARImperativeSession.h"
#include "VROARDeclarativeSession.h"
#include "VROMaterial.h"
#include "VROImageUtil.h"
#include "VROARCamera.h"
#include "VROPencil.h"
#include "VROARDepthMesh.h"
#include "VROPhysicsWorld.h"
#include "VROPhysicsShape.h"
#include "VROPlatformUtil.h"
#include "VROVPSAnchorNode.h"
#include <btBulletDynamicsCommon.h>
#include <algorithm>
#include <cmath>

VROARScene::~VROARScene() {
    // no-op, we define this here vs in header because for some reason if you want
    // to dynamic cast from a base to a derived class, the derived class had better
    // override a virtual function, but not in the header. See this link for related
    // issue: https://bytes.com/topic/c/answers/851223-q-strange-dynamic_cast-problem
}

void VROARScene::initDeclarativeSession() {
    // Guard against double-init: Fabric (New Architecture) pre-allocates views via
    // PreAllocateViewMountItem before the renderer is ready, which can invoke this
    // function a second time on the same scene. ReactVisionCCA's .so loading also
    // introduces a runtime interaction that can trigger this path unexpectedly.
    // Return safely if already initialized instead of crashing via passert.
    if (_declarativeSession != nullptr) {
        return;
    }
    if (_imperativeSession != nullptr) {
        pwarn("[Viro] initDeclarativeSession called but imperative session already exists — skipping");
        return;
    }
    _declarativeSession = std::make_shared<VROARDeclarativeSession>();
    _declarativeSession->init();
    std::shared_ptr<VROARSession> session = _arSession.lock();
    if (session) {
        _declarativeSession->setARSession(session);
    }
}

void VROARScene::initImperativeSession() {
    // Guard against double-init (see initDeclarativeSession comment above).
    if (_imperativeSession != nullptr) {
        return;
    }
    if (_declarativeSession != nullptr) {
        pwarn("[Viro] initImperativeSession called but declarative session already exists — skipping");
        return;
    }
    std::shared_ptr<VROARScene> scene = std::dynamic_pointer_cast<VROARScene>(shared_from_this());
    _imperativeSession = std::make_shared<VROARImperativeSession>(scene);
    std::shared_ptr<VROARSession> session = _arSession.lock();
    if (session) {
        _imperativeSession->setARSession(session);
    }
}

std::shared_ptr<VROARSessionDelegate> VROARScene::getSessionDelegate() {
    if (_declarativeSession) {
        return _declarativeSession;
    } else {
        return _imperativeSession;
    }
}

void VROARScene::setAnchorDetectionTypes(std::set<VROAnchorDetection> detectionTypes) {
    _detectionTypes = detectionTypes;
    std::shared_ptr<VROARSession> arSession = _arSession.lock();
    if (arSession) {
        arSession->setAnchorDetection(_detectionTypes);
    }
}

void VROARScene::addNode(std::shared_ptr<VRONode> node) {
    getRootNode()->addChildNode(node);
}

void VROARScene::setARSession(std::shared_ptr<VROARSession> arSession) {
    _arSession = arSession;

    // If there wasn't already a emitter, then reset _displayPointCloud
    // to run through the creation/addition logic now that the session is set.
    if (!_pointCloudEmitter) {
        displayPointCloud(_displayPointCloud);
    }

    if (_declarativeSession) {
        _declarativeSession->setARSession(arSession);
    }
    if (_imperativeSession) {
        _imperativeSession->setARSession(arSession);
    }

    arSession->setAnchorDetection(_detectionTypes);

    std::vector<std::function<void(std::shared_ptr<VROARSession>)>> tasks;
    tasks.swap(_sessionReadyTasks);
    for (auto &task : tasks) {
        task(arSession);
    }
}

void VROARScene::runWhenARSessionReady(std::function<void(std::shared_ptr<VROARSession>)> task) {
    std::shared_ptr<VROARSession> arSession = _arSession.lock();
    if (arSession) {
        task(arSession);
    } else {
        _sessionReadyTasks.push_back(std::move(task));
    }
}

void VROARScene::setDriver(std::shared_ptr<VRODriver> driver) {
    _driver = driver;

    // If there wasn't already a emitter, then reset _displayPointCloud
    // to run through the creation/addition logic now that the driver is set.
    if (!_pointCloudEmitter) {
        displayPointCloud(_displayPointCloud);
    }
}

void VROARScene::updateParticles(const VRORenderContext &context) {
    updatePointCloud();
    updateWorldMesh();
    updateVPSPlacement();
    serviceVPSQueryFrameRequests();
    VROScene::updateParticles(context);
}

void VROARScene::updatePointCloud(){
    if (!_pointCloudEmitter || !_displayPointCloud){
        return;
    }

    std::shared_ptr<VROARSession> arSession = _arSession.lock();
    if (!arSession) {
        return;
    }

    std::unique_ptr<VROARFrame> &frame = arSession->getLastFrame();
    if (!frame) {
        return;
    }

    std::vector<VROVector4f> pointCloudPoints = frame->getPointCloud()->getPoints();
    _pointCloudEmitter->setParticleTransforms(pointCloudPoints);
}

void VROARScene::displayPointCloud(bool displayPointCloud) {
    _displayPointCloud = displayPointCloud;

    // If we should have an emitter and none exists, try to create one...
    if (_displayPointCloud && !_pointCloudEmitter) {
        // Note: the creation could fail!
        initPointCloudEmitter();
    }

    // Point cloud emitter has not yet initialized.
    if (_pointCloudEmitter == nullptr){
        return;
    }

    // Everything has been initialized, add / remove emitter as needed.
    if (_displayPointCloud) {
        _pointCloudNode->setParticleEmitter(_pointCloudEmitter);
        if (_pointCloudNode->getParentNode() == nullptr) {
            addNode(_pointCloudNode);
        }
    } else {
        _pointCloudNode->removeParticleEmitter();
        _pointCloudEmitter->forceClearParticles();
    }
}

void VROARScene::resetPointCloudSurface() {
    if (!_pointCloudEmitter) {
        return;
    }

    // Set the default surface on the point cloud emitter.
    _pointCloudSurface = VROSurface::createSurface(1, 1);
    _pointCloudSurface->getMaterials()[0]->getDiffuse().setTexture(getPointCloudTexture());
    _pointCloudSurface->getMaterials()[0]->setBloomThreshold(-1);
    _pointCloudSurface->getMaterials()[0]->setBlendMode(VROBlendMode::Add);
    _pointCloudEmitter->setParticleSurface(_pointCloudSurface);
}

void VROARScene::setPointCloudSurface(std::shared_ptr<VROSurface> surface) {
    _pointCloudSurface = surface;
    if (_pointCloudEmitter) {
        _pointCloudEmitter->setParticleSurface(surface);
    }
}

void VROARScene::setPointCloudSurfaceScale(VROVector3f scale) {
    _pointCloudSurfaceScale = scale;
    if (_pointCloudEmitter) {
        _pointCloudEmitter->setParticleScale(scale);
    }
}

void VROARScene::setPointCloudMaxPoints(int maxPoints) {
    _pointCloudMaxPoints = maxPoints;
    if (_pointCloudEmitter) {
        _pointCloudEmitter->setMaxParticles(_pointCloudMaxPoints);
    }
}

void VROARScene::initPointCloudEmitter() {
    std::shared_ptr<VRODriver> driver = _driver.lock();
    std::shared_ptr<VROARSession> arSession = _arSession.lock();
    if (!driver || !arSession) {
        return;
    }

    _pointCloudEmitter = std::make_shared<VROFixedParticleEmitter>(driver);
    _pointCloudEmitter->setMaxParticles(_pointCloudMaxPoints);
    _pointCloudEmitter->setParticleScale(_pointCloudSurfaceScale);

    // Set a generic point cloud surface on the emitter if none is given.
    if (!_pointCloudSurface) {
        resetPointCloudSurface();
    } else {
        _pointCloudEmitter->setParticleSurface(_pointCloudSurface);
    }
}

void VROARScene::setDelegate(std::shared_ptr<VROARSceneDelegate> delegate) {
    _delegate = delegate;
    setTrackingState(_currentTrackingState, _currentTrackingStateReason, true);
}

void VROARScene::setTrackingState(VROARTrackingState state, VROARTrackingStateReason reason,
                                  bool force) {
    if (_currentTrackingState == state && _currentTrackingStateReason == reason && !force) {
        return;
    }
    _currentTrackingState = state;
    _currentTrackingStateReason = reason;

    // Notify delegates.
    std::shared_ptr<VROARSceneDelegate> delegate = _delegate.lock();
    if (delegate) {
        delegate->onTrackingUpdated(state, reason);
    }
}

void VROARScene::notifyDepthReady() {
    if (_depthReadyNotified) {
        return;
    }
    _depthReadyNotified = true;

    std::shared_ptr<VROARSceneDelegate> delegate = _delegate.lock();
    if (delegate) {
        delegate->onDepthReady();
    }
}

void VROARScene::updateAmbientLight(float intensity, VROVector3f color) {
    _ambientLightIntensity = intensity;
    _ambientLightColor = color;

    std::shared_ptr<VROARSceneDelegate> delegate = _delegate.lock();
    if (delegate) {
        delegate->onAmbientLightUpdate(intensity, color);
    }
}

void VROARScene::willAppear() {
    if (_declarativeSession) {
        _declarativeSession->sceneWillAppear();
    }
}

void VROARScene::willDisappear() {
    if (_declarativeSession) {
        _declarativeSession->sceneWillDisappear();
    }
}

#pragma mark - World Mesh

void VROARScene::setWorldMeshEnabled(bool enabled) {
    _worldMeshEnabled = enabled;

    if (enabled && !_worldMesh) {
        // Create world mesh manager with our physics world
        _worldMesh = std::make_shared<VROARWorldMesh>(getPhysicsWorld());
        _worldMesh->setConfig(_worldMeshConfig);

        // Set up callback to notify delegate
        std::weak_ptr<VROARSceneDelegate> weakDelegate = _delegate;
        _worldMesh->setUpdateCallback([weakDelegate](const VROWorldMeshStats& stats) {
            std::shared_ptr<VROARSceneDelegate> delegate = weakDelegate.lock();
            if (delegate) {
                delegate->onWorldMeshUpdated(stats);
            }
        });
    }

    if (_worldMesh) {
        _worldMesh->setEnabled(enabled);
    }

    // Notify the platform session so it can activate depth sensing if needed
    std::shared_ptr<VROARSession> arSession = _arSession.lock();
    if (arSession) {
        arSession->onWorldMeshEnabled(enabled);
    }

    pinfo("VROARScene: World mesh %s", enabled ? "enabled" : "disabled");
}

bool VROARScene::isWorldMeshEnabled() const {
    return _worldMeshEnabled;
}

void VROARScene::setWorldMeshConfig(const VROWorldMeshConfig& config) {
    _worldMeshConfig = config;

    if (_worldMesh) {
        _worldMesh->setConfig(config);
    }
}

VROWorldMeshStats VROARScene::getWorldMeshStats() const {
    if (_worldMesh) {
        return _worldMesh->getStats();
    }
    return VROWorldMeshStats();
}

void VROARScene::updateWorldMesh() {
    if (!_worldMesh || !_worldMeshEnabled) {
        return;
    }

    std::shared_ptr<VROARSession> arSession = _arSession.lock();
    if (!arSession) {
        return;
    }

    std::unique_ptr<VROARFrame> &frame = arSession->getLastFrame();
    if (!frame) {
        return;
    }

    _worldMesh->updateFromFrame(frame);
}

void VROARScene::computePhysics(const VRORenderContext &context) {
    // Call base class physics computation
    VROScene::computePhysics(context);

    // Draw world mesh wireframe if enabled
    debugDrawWorldMesh(context);
}

void VROARScene::debugDrawWorldMesh(const VRORenderContext &context) {
    if (_worldMesh && _worldMeshEnabled) {
        _worldMesh->debugDraw(context.getPencil());
    }
}

#pragma mark - VPS map mesh

bool VROARScene::setVPSMapMesh(const std::vector<uint8_t> &rvwm, bool occlusion, bool physics) {
    // Parsed in the map's own frame; the per-frame transform places it.
    std::shared_ptr<VROARDepthMesh> mesh = VROARWorldMesh::loadMeshSnapshot(rvwm, VROMatrix4f::identity());
    if (!mesh || !mesh->isValid()) {
        return false;
    }
    std::weak_ptr<VROARScene> weakSelf = std::dynamic_pointer_cast<VROARScene>(shared_from_this());
    VROPlatformDispatchAsyncRenderer([weakSelf, mesh, occlusion, physics]() {
        auto self = weakSelf.lock();
        if (self) {
            self->installVPSMapMesh(mesh, occlusion, physics);
        }
    });
    return true;
}

void VROARScene::clearVPSMapMesh() {
    std::weak_ptr<VROARScene> weakSelf = std::dynamic_pointer_cast<VROARScene>(shared_from_this());
    VROPlatformDispatchAsyncRenderer([weakSelf]() {
        auto self = weakSelf.lock();
        if (self) {
            self->clearVPSMapMeshNow();
        }
    });
}

void VROARScene::clearVPSMapMeshNow() {
    _vpsMapMeshGen++;
    if (_vpsMapMeshNode) {
        _vpsMapMeshNode->removeFromParentNode();
        _vpsMapMeshNode = nullptr;
    }
    // The deleter takes the body out of the physics world first.
    _vpsMapMeshBody = nullptr;
    _vpsMapMeshMotion = nullptr;
    _vpsMapMeshShape = nullptr;
    _vpsMapMeshInWorld = false;
}

void VROARScene::installVPSMapMesh(std::shared_ptr<VROARDepthMesh> mesh, bool occlusion, bool physics) {
    clearVPSMapMeshNow();
    const uint64_t gen = _vpsMapMeshGen;

    // The node exists for physics-only meshes too: it carries the transform.
    _vpsMapMeshNode = std::make_shared<VRONode>();
    _vpsMapMeshNode->setHidden(true);
    if (occlusion) {
        std::shared_ptr<VROGeometry> geometry = VROARWorldMesh::buildOcclusionGeometry(mesh);
        if (geometry) {
            _vpsMapMeshNode->setGeometry(geometry);
        } else {
            pwarn("VROARScene::setVPSMapMesh: could not build the occlusion geometry");
        }
    }
    getRootNode()->addChildNode(_vpsMapMeshNode);

    if (!physics) {
        return;
    }
    // Same simplification as the live world mesh, then the BVH off the render
    // thread: a room's mesh takes a few hundred ms to build.
    std::shared_ptr<VROARDepthMesh> physicsMesh = VROARWorldMesh::clusterMesh(mesh, VROWorldMeshConfig().physicsCellSize);
    if (!physicsMesh || physicsMesh->getTriangleCount() == 0) {
        physicsMesh = mesh;
    }
    std::weak_ptr<VROARScene> weakSelf = std::dynamic_pointer_cast<VROARScene>(shared_from_this());
    auto vertices = physicsMesh->getVertices();
    auto indices  = physicsMesh->getIndices();
    VROPlatformDispatchAsyncBackground([weakSelf, vertices, indices, gen]() {
        auto shape = std::make_shared<VROPhysicsShape>(vertices, indices);
        if (!shape->getBulletShape()) {
            pwarn("VROARScene::setVPSMapMesh: could not build the physics shape");
            return;
        }
        VROPlatformDispatchAsyncRenderer([weakSelf, shape, gen]() {
            auto self = weakSelf.lock();
            if (!self || gen != self->_vpsMapMeshGen) {
                return; // cleared or replaced while the shape was building
            }
            VROWorldMeshConfig defaults;
            self->_vpsMapMeshShape = shape;
            self->_vpsMapMeshMotion = std::make_shared<btDefaultMotionState>(btTransform::getIdentity());
            btRigidBody::btRigidBodyConstructionInfo info(0.0f, self->_vpsMapMeshMotion.get(),
                                                          shape->getBulletShape(), btVector3(0, 0, 0));
            std::weak_ptr<VROPhysicsWorld> weakWorld = self->getPhysicsWorld();
            self->_vpsMapMeshBody = std::shared_ptr<btRigidBody>(new btRigidBody(info),
                                                                 BulletRigidBodyDeleter{weakWorld});
            self->_vpsMapMeshBody->setFriction(defaults.friction);
            self->_vpsMapMeshBody->setRestitution(defaults.restitution);
            // Kinematic: Bullet reads its pose from the motion state every step.
            self->_vpsMapMeshBody->setCollisionFlags(self->_vpsMapMeshBody->getCollisionFlags() |
                                                     btCollisionObject::CF_KINEMATIC_OBJECT);
            self->_vpsMapMeshBody->setActivationState(DISABLE_DEACTIVATION);
            self->_vpsMapMeshBody->setUserIndex(kVROPhysicsUserIndexWorldMesh);
            self->_vpsMapMeshBody->setUserPointer(&self->_vpsMapMeshTag);
            self->_vpsMapMeshInWorld = false; // added once the fuser has converged
        });
    });
}

void VROARScene::updateVPSPlacement() {
    if (!_vpsMapMeshNode && _vpsAnchorNodes.empty()) {
        return;
    }
    std::shared_ptr<VROARSession> session = _arSession.lock();
    VROMatrix4f T_world_map;
    bool placed = session && session->rvGetVPSWorldFromMap(T_world_map);
    updateVPSMapMesh(placed, T_world_map);
    updateVPSAnchorNodes(placed, T_world_map);
}

void VROARScene::updateVPSMapMesh(bool placed, const VROMatrix4f &T_world_map) {
    if (!_vpsMapMeshNode) {
        return;
    }
    if (!placed) {
        // Not placed yet: an occluder or collider at the map origin would be wrong.
        _vpsMapMeshNode->setHidden(true);
        return;
    }

    VROVector3f position = T_world_map.extractTranslation();
    VROQuaternion rotation = T_world_map.extractRotation(T_world_map.extractScale());
    _vpsMapMeshNode->setPosition(position);
    _vpsMapMeshNode->setRotation(rotation);
    _vpsMapMeshNode->setHidden(false);

    if (_vpsMapMeshBody && _vpsMapMeshMotion) {
        btTransform t(btQuaternion(rotation.X, rotation.Y, rotation.Z, rotation.W),
                      btVector3(position.x, position.y, position.z));
        _vpsMapMeshMotion->setWorldTransform(t);
        _vpsMapMeshBody->setWorldTransform(t);
        if (!_vpsMapMeshInWorld) {
            getPhysicsWorld()->addRigidBody(_vpsMapMeshBody.get());
            _vpsMapMeshInWorld = true;
        }
    }
}

#pragma mark - VPS anchor nodes

void VROARScene::addVPSAnchorNode(std::shared_ptr<VROVPSAnchorNode> node) {
    std::weak_ptr<VROARScene> weakSelf = std::dynamic_pointer_cast<VROARScene>(shared_from_this());
    std::weak_ptr<VROVPSAnchorNode> weakNode = node;
    VROPlatformDispatchAsyncRenderer([weakSelf, weakNode]() {
        auto self = weakSelf.lock();
        auto node = weakNode.lock();
        if (!self || !node) {
            return;
        }
        for (auto &w : self->_vpsAnchorNodes) {
            if (w.lock() == node) {
                return;
            }
        }
        self->_vpsAnchorNodes.push_back(node);
        node->setPlaced(false, true);
    });
}

void VROARScene::removeVPSAnchorNode(std::shared_ptr<VROVPSAnchorNode> node) {
    std::weak_ptr<VROARScene> weakSelf = std::dynamic_pointer_cast<VROARScene>(shared_from_this());
    std::weak_ptr<VROVPSAnchorNode> weakNode = node;
    VROPlatformDispatchAsyncRenderer([weakSelf, weakNode]() {
        auto self = weakSelf.lock();
        if (!self) {
            return;
        }
        auto node = weakNode.lock();
        auto &nodes = self->_vpsAnchorNodes;
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&node](const std::weak_ptr<VROVPSAnchorNode> &w) {
            auto n = w.lock();
            return !n || n == node;
        }), nodes.end());
    });
}

void VROARScene::updateVPSAnchorNodes(bool placed, const VROMatrix4f &T_world_map) {
    if (_vpsAnchorNodes.empty()) {
        return;
    }
    VROVector3f position;
    VROQuaternion rotation;
    if (placed) {
        position = T_world_map.extractTranslation();
        rotation = T_world_map.extractRotation(T_world_map.extractScale());
    }
    bool expired = false;
    for (auto &w : _vpsAnchorNodes) {
        std::shared_ptr<VROVPSAnchorNode> node = w.lock();
        if (!node) {
            expired = true;
            continue;
        }
        if (placed) {
            // World, not local: an anchor nested under a moved parent still lands in the map frame.
            node->setWorldTransform(position, rotation);
        }
        node->setPlaced(placed);
    }
    if (expired) {
        _vpsAnchorNodes.erase(std::remove_if(_vpsAnchorNodes.begin(), _vpsAnchorNodes.end(),
                                             [](const std::weak_ptr<VROVPSAnchorNode> &w) { return w.expired(); }),
                              _vpsAnchorNodes.end());
    }
}

#pragma mark - VPS query frame

void VROARScene::captureVPSQueryFrame(int maxSide, VPSQueryFrameCallback callback) {
    std::weak_ptr<VROARScene> weakSelf = std::dynamic_pointer_cast<VROARScene>(shared_from_this());
    VROPlatformDispatchAsyncRenderer([weakSelf, maxSide, callback]() {
        auto self = weakSelf.lock();
        if (!self) {
            callback(false, {}, 0, 0, 0, 0, 0, 0, VROMatrix4f());
            return;
        }
        // About a second at 60 fps for a frame with intrinsics to show up.
        self->_vpsQueryFrameRequests.push_back({ std::max(maxSide, 64), 60, callback });
    });
}

// Box-filters a w x h luma plane down to dw x dh.
static std::vector<uint8_t> downscaleLuma(const std::vector<uint8_t> &src, int w, int h, int dw, int dh) {
    std::vector<uint8_t> out((size_t)dw * dh);
    for (int y = 0; y < dh; ++y) {
        int y0 = (int)((int64_t)y * h / dh), y1 = std::max(y0 + 1, (int)((int64_t)(y + 1) * h / dh));
        for (int x = 0; x < dw; ++x) {
            int x0 = (int)((int64_t)x * w / dw), x1 = std::max(x0 + 1, (int)((int64_t)(x + 1) * w / dw));
            uint32_t sum = 0;
            for (int yy = y0; yy < y1; ++yy) {
                const uint8_t *row = src.data() + (size_t)yy * w;
                for (int xx = x0; xx < x1; ++xx) {
                    sum += row[xx];
                }
            }
            out[(size_t)y * dw + x] = (uint8_t)(sum / (uint32_t)((y1 - y0) * (x1 - x0)));
        }
    }
    return out;
}

void VROARScene::serviceVPSQueryFrameRequests() {
    if (_vpsQueryFrameRequests.empty()) {
        return;
    }
    const uint8_t *luma = nullptr;
    int w = 0, h = 0;
    float fx = 0, fy = 0, cx = 0, cy = 0;
    VROMatrix4f T_world_cam;
    bool have = false;
    std::shared_ptr<VROARSession> session = _arSession.lock();
    if (session) {
        std::unique_ptr<VROARFrame> &frame = session->getLastFrame();
        if (frame) {
            const std::shared_ptr<VROARCamera> &cam = frame->getCamera();
            have = cam && cam->getImageIntrinsics(&fx, &fy, &cx, &cy) && fx > 1.f && fy > 1.f &&
                   frame->getCameraImageY(&luma, &w, &h) && luma && w > 0 && h > 0;
            if (have) {
                T_world_cam = cam->getImageTransform();
            }
        }
    }

    std::vector<VPSQueryFrameRequest> pending;
    std::shared_ptr<std::vector<uint8_t>> copy;
    for (auto &request : _vpsQueryFrameRequests) {
        if (!have) {
            if (--request.framesLeft > 0) {
                pending.push_back(request);
            } else {
                VPSQueryFrameCallback callback = request.callback;
                VROPlatformDispatchAsyncBackground([callback]() {
                    callback(false, {}, 0, 0, 0, 0, 0, 0, VROMatrix4f());
                });
            }
            continue;
        }
        if (!copy) {
            // The frame's plane is only valid this frame: copy it here, scale it off the render thread.
            copy = std::make_shared<std::vector<uint8_t>>(luma, luma + (size_t)w * h);
        }
        float scale = std::min(1.f, (float)request.maxSide / (float)std::max(w, h));
        int dw = std::max(1, (int)std::lround(w * scale));
        int dh = std::max(1, (int)std::lround(h * scale));
        VPSQueryFrameCallback callback = request.callback;
        VROPlatformDispatchAsyncBackground([copy, w, h, dw, dh, fx, fy, cx, cy, T_world_cam, callback]() {
            float sx = (float)dw / (float)w, sy = (float)dh / (float)h;
            std::vector<uint8_t> gray = (dw == w && dh == h) ? *copy : downscaleLuma(*copy, w, h, dw, dh);
            // Pixel centres: (c + 0.5) * s - 0.5.
            callback(true, std::move(gray), dw, dh, fx * sx, fy * sy, (cx + 0.5f) * sx - 0.5f, (cy + 0.5f) * sy - 0.5f,
                     T_world_cam);
        });
    }
    _vpsQueryFrameRequests.swap(pending);
}
