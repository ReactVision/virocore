// VROInputPresenterOpenXR.h
// ViroRenderer
//
// Visual presenter for the OpenXR (Meta Quest) input controller. Provides:
//   1. A reticle (existing VROReticle) positioned at the hit point in world
//      space (not headlocked — Cardboard-style fixed pointer is wrong here
//      because we have actual 6DOF aim from controllers / FB hand-aim).
//   2. A laser line from the aim origin to the reticle.
//
// Both are children of the presenter's _rootNode, which VROScene attaches to
// the scene root via attachInputController(). The reticle position is updated
// via the existing onGazeHit → onReticleGazeHit chain. The laser endpoints
// are updated each frame by VROInputControllerOpenXR via updateAimRay().
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#ifndef ANDROID_VROINPUTPRESENTEROPENXR_H
#define ANDROID_VROINPUTPRESENTEROPENXR_H

#include <memory>
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

        // Build the laser geometry once. Per-frame updates mutate the existing
        // polyline's points via setPaths() rather than swapping the node's
        // geometry — swapping caused a one-frame blink because the new
        // geometry isn't fully uploaded to the GPU until the next render.
        std::vector<VROVector3f> initialPath = { {0, 0, 0}, {0, 0, -1} };
        _laserGeom = VROPolyline::createPolyline(initialPath, 0.003f /* thickness */);
        _laserGeom->setName("AimLaserGeom");
        auto material = _laserGeom->getMaterials().front();
        material->getDiffuse().setColor({ 0.33f, 0.976f, 0.968f, 1.0f }); // cyan
        material->setWritesToDepthBuffer(false);
        material->setReadsFromDepthBuffer(false);
        material->setReceivesShadows(false);

        _laserNode = std::make_shared<VRONode>();
        _laserNode->setName("AimLaser");
        _laserNode->setGeometry(_laserGeom);
        _laserNode->setHidden(true);  // hidden until first updateAimRay()
        _rootNode->addChildNode(_laserNode);
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
     * Update the visible aim ray each frame.
     *
     * Mutates the persistent polyline's points in place via setPaths(). This
     * avoids the one-frame blink that occurs when VRONode::setGeometry()
     * swaps the geometry pointer and the new mesh hasn't been uploaded to
     * the GPU yet.
     *
     * @param origin     World-space origin of the ray
     * @param hitPoint   World-space endpoint
     * @param visible    True to show, false to hide (no input active)
     */
    void updateAimRay(const VROVector3f &origin, const VROVector3f &hitPoint, bool visible) {
        if (!visible) {
            _laserNode->setHidden(true);
            return;
        }
        _laserNode->setHidden(false);

        std::vector<std::vector<VROVector3f>> paths = { { origin, hitPoint } };
        _laserGeom->setPaths(paths);
    }

private:
    std::shared_ptr<VRONode>     _laserNode;
    std::shared_ptr<VROPolyline> _laserGeom;
};

#endif // ANDROID_VROINPUTPRESENTEROPENXR_H
