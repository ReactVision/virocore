// VROInputPresenterOpenXR.h
// ViroRenderer
//
// Visual presenter for the OpenXR (Meta Quest) input controller. Provides:
//   1. A reticle (existing VROReticle) positioned at the hit point in world
//      space (not headlocked — Cardboard-style fixed pointer is wrong here
//      because we have actual 6DOF aim from controllers / FB hand-aim).
//   2. Two laser lines, one per hand, drawn from the aim origin to the hit
//      (or a fixed forward distance when nothing is hit). Each laser is
//      independent so apps can show both controllers / both hands at once.
//
// All laser nodes are children of the presenter's _rootNode, which VROScene
// attaches to the scene root via attachInputController(). The reticle position
// is updated via the existing onGazeHit → onReticleGazeHit chain. The laser
// endpoints are updated each frame by VROInputControllerOpenXR via
// updateAimRay(source, ...).
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#ifndef ANDROID_VROINPUTPRESENTEROPENXR_H
#define ANDROID_VROINPUTPRESENTEROPENXR_H

#include <memory>
#include <unordered_map>
#include <vector>
#include "VROInputPresenter.h"
#include "VROReticle.h"
#include "VRONode.h"
#include "VROPolyline.h"
#include "VROMaterial.h"

class VROInputPresenterOpenXR : public VROInputPresenter {
public:
    VROInputPresenterOpenXR() {
        // Reticle at world-space hit point (not headlocked).
        auto reticle = std::make_shared<VROReticle>(nullptr);
        reticle->setPointerFixed(false);
        setReticle(reticle);
    }

    ~VROInputPresenterOpenXR() override = default;

    /**
     * Inherit the existing reticle-positioning chain. processGazeEvent() in
     * VROInputControllerBase calls onGazeHit() on registered delegates; we
     * forward to onReticleGazeHit() which moves the reticle to hit world pos.
     */
    void onGazeHit(int source, std::shared_ptr<VRONode> node, const VROHitTestResult &hit) override {
        VROInputPresenter::onReticleGazeHit(hit);
    }

    /**
     * Trigger reticle pulse animation on click for parity with OVR/Cardboard.
     */
    void onClick(int source, std::shared_ptr<VRONode> node, ClickState clickState,
                 std::vector<float> position) override {
        VROInputPresenter::onClick(source, node, clickState, position);
        if (clickState == ClickState::ClickUp && getReticle()) {
            getReticle()->trigger();
        }
    }

    /**
     * Update one source's aim ray each frame. Each source (right pointer,
     * left pointer) keeps its own polyline so both can be visible at once.
     *
     * Mutates the persistent polyline's points in place via setPaths(). This
     * avoids the one-frame blink that occurs when VRONode::setGeometry()
     * swaps the geometry pointer and the new mesh hasn't been uploaded to
     * the GPU yet.
     *
     * @param source     Source ID (e.g. ViroOculus::Controller, LeftController)
     * @param origin     World-space origin of the ray
     * @param hitPoint   World-space endpoint
     * @param visible    True to show, false to hide (no input active)
     */
    void updateAimRay(int source, const VROVector3f &origin,
                      const VROVector3f &hitPoint, bool visible) {
        Laser &laser = getOrCreateLaser(source);
        if (!visible) {
            laser.node->setHidden(true);
            return;
        }
        // Defensive: a zero-length segment (origin == hitPoint, e.g. degenerate
        // forward direction or a hit reported exactly at the controller pose)
        // produces an invisible polyline. Treat as "no aim available".
        VROVector3f delta = hitPoint - origin;
        if (delta.magnitude() < 0.001f) {
            laser.node->setHidden(true);
            return;
        }
        laser.node->setHidden(false);
        std::vector<std::vector<VROVector3f>> paths = { { origin, hitPoint } };
        laser.geom->setPaths(paths);
    }

private:
    struct Laser {
        std::shared_ptr<VRONode>     node;
        std::shared_ptr<VROPolyline> geom;
    };

    Laser &getOrCreateLaser(int source) {
        auto it = _lasers.find(source);
        if (it != _lasers.end()) {
            return it->second;
        }

        Laser laser;
        std::vector<VROVector3f> initialPath = { {0, 0, 0}, {0, 0, -1} };
        laser.geom = VROPolyline::createPolyline(initialPath, 0.003f /* thickness */);
        laser.geom->setName("AimLaserGeom");
        auto material = laser.geom->getMaterials().front();
        material->getDiffuse().setColor({ 0.33f, 0.976f, 0.968f, 1.0f }); // cyan
        material->setWritesToDepthBuffer(false);
        material->setReadsFromDepthBuffer(false);
        material->setReceivesShadows(false);

        laser.node = std::make_shared<VRONode>();
        laser.node->setName("AimLaser");
        laser.node->setGeometry(laser.geom);
        laser.node->setHidden(true);  // hidden until first updateAimRay()
        _rootNode->addChildNode(laser.node);

        return _lasers.emplace(source, std::move(laser)).first->second;
    }

    std::unordered_map<int, Laser> _lasers;
};

#endif // ANDROID_VROINPUTPRESENTEROPENXR_H
