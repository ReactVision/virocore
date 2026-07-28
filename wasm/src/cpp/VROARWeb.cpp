//
//  VROARWeb.cpp
//  ViroRenderer — Web (WASM) AR backend. See VROARWeb.h.
//

#include "VROARWeb.h"
#include "VROARPointCloud.h"
#include "VROARHitTestResult.h"
#include "VROARAnchor.h"
#include "VROFieldOfView.h"
#include "VROMath.h"
#include <algorithm>
#include <cmath>

#pragma mark - Camera

VROMatrix4f VROARCameraWeb::getProjection(VROViewport viewport, float near, float far,
                                          VROFieldOfView *outFOV) {
    // Build a perspective from a fixed vertical FOV and the viewport aspect. A
    // future refinement can derive this from the slam camera intrinsics for
    // exact overlay alignment.
    float aspect = (viewport.getHeight() > 0)
                       ? (float) viewport.getWidth() / (float) viewport.getHeight()
                       : 1.0f;
    float halfV = 30.0f; // degrees (60° vertical FOV)
    float halfH = toDegrees(atanf(tanf(toRadians(halfV)) * aspect));

    VROFieldOfView fov(halfH, halfH, halfV, halfV);
    if (outFOV != nullptr) {
        *outFOV = fov;
    }
    return fov.toPerspectiveProjection(near, far);
}

#pragma mark - Frame

std::vector<std::shared_ptr<VROARHitTestResult>> VROARFrameWeb::hitTest(
    int x, int y, std::set<VROARHitTestResultType> types) {
    // Plane/point-cloud hit-testing is a follow-up (done JS-side against slam
    // planes for now). Return empty so callers degrade gracefully.
    return {};
}

std::vector<std::shared_ptr<VROARHitTestResult>> VROARFrameWeb::hitTestRay(
    VROVector3f *origin, VROVector3f *destination, std::set<VROARHitTestResultType> types) {
    return {};
}

#pragma mark - Session

VROARSessionWeb::VROARSessionWeb()
    : VROARSession(VROTrackingType::DOF6, VROWorldAlignment::Gravity),
      _orientation(VROCameraOrientation::Portrait),
      _running(false),
      _frameCounter(0) {
    _camera = std::make_shared<VROARCameraWeb>();
}

VROARSessionWeb::~VROARSessionWeb() {}

void VROARSessionWeb::setPose(VROMatrix4f rotation, VROVector3f position, VROARTrackingState state) {
    _camera->setPose(rotation, position);
    _camera->setTrackingState(state);
}

void VROARSessionWeb::setCameraBackground(std::shared_ptr<VROTexture> texture) {
    _background = texture;
}

void VROARSessionWeb::setCameraImageSize(float w, float h) {
    _camera->setImageSize(w, h);
}

void VROARSessionWeb::resetSession(bool resetTracking, bool removeAnchors) {
    if (removeAnchors) {
        _anchors.clear();
    }
}

std::unique_ptr<VROARFrame> &VROARSessionWeb::updateFrame() {
    _frameCounter += 1;
    // Rebuild the frame each call with the latest injected pose/anchors. The
    // camera shared_ptr is stable, so its pose updates propagate.
    _currentFrame = std::unique_ptr<VROARFrame>(new VROARFrameWeb(
        _camera, _frameCounter, _orientation, _anchors,
        std::make_shared<VROARPointCloud>()));
    return _currentFrame;
}

void VROARSessionWeb::addAnchor(std::shared_ptr<VROARAnchor> anchor) {
    if (!anchor) return;
    _anchors.push_back(anchor);
    std::shared_ptr<VROARSessionDelegate> delegate = getDelegate();
    if (delegate) {
        delegate->anchorWasDetected(anchor);
    }
}

void VROARSessionWeb::removeAnchor(std::shared_ptr<VROARAnchor> anchor) {
    _anchors.erase(std::remove(_anchors.begin(), _anchors.end(), anchor), _anchors.end());
    std::shared_ptr<VROARSessionDelegate> delegate = getDelegate();
    if (delegate) {
        delegate->anchorWasRemoved(anchor);
    }
}

void VROARSessionWeb::updateAnchor(std::shared_ptr<VROARAnchor> anchor) {
    std::shared_ptr<VROARSessionDelegate> delegate = getDelegate();
    if (delegate) {
        delegate->anchorWillUpdate(anchor);
        delegate->anchorDidUpdate(anchor);
    }
}
