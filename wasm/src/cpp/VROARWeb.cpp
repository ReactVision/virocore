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
    // Derive the frustum from the camera's own intrinsics when the host has
    // supplied them. The alternative below is a fixed 60-degree vertical field
    // of view, which aligns with the camera image only by coincidence: the
    // background is a screen-space surface and fills the viewport either way,
    // so a mismatch is invisible in the feed and shows up as 3-D content
    // sitting in the wrong place -- slightly wrong near the optical axis and
    // hundreds of pixels wrong toward the edges.
    float aspect = (viewport.getHeight() > 0)
                       ? (float) viewport.getWidth() / (float) viewport.getHeight()
                       : 1.0f;

    float l, r, b, t; // half-extents at unit depth, one per frustum side
    if (hasIntrinsics() && _height > 0 && _width > 0) {
        // Off-axis, because a real camera's principal point is not the centre
        // of its image. A symmetric frustum built from the focal lengths alone
        // gets the scale right and leaves a constant offset behind -- small,
        // but constant, which is exactly what reads as content sitting a little
        // to one side of where it was placed.
        l = _cx / _fx;
        r = (_width - _cx) / _fx;
        t = _cy / _fy;
        b = (_height - _cy) / _fy;
    } else {
        // No intrinsics: a 60-degree vertical field of view, centred. This is
        // an assumption about a camera nobody measured, and it aligns with the
        // image only by luck -- the background fills the viewport regardless,
        // so the mismatch never shows in the feed and instead puts the 3-D
        // content in the wrong place, by a little near the optical axis and by
        // a lot toward the edges.
        t = b = tanf(toRadians(30.0f));
        l = r = t * aspect;
    }

    // The viewport rarely has the image's aspect ratio. Widen the axis with
    // room to spare rather than stretching either one: stretching changes the
    // pixel scale and misaligns everything at once. The principal point keeps
    // its position, so the widening is off-axis too.
    const float imageAspect = (r + l) / (t + b);
    if (aspect > imageAspect) {
        const float total = (t + b) * aspect;
        const float lr = l + r;
        l = total * (l / lr);
        r = total * (r / lr);
    } else {
        const float total = (l + r) / aspect;
        const float tb = t + b;
        t = total * (t / tb);
        b = total * (b / tb);
    }

    VROFieldOfView fov(toDegrees(atanf(l)), toDegrees(atanf(r)),
                       toDegrees(atanf(b)), toDegrees(atanf(t)));
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

void VROARSessionWeb::setCameraIntrinsics(float fx, float fy, float cx, float cy,
                                          float w, float h) {
    _camera->setImageSize(w, h);
    _camera->setIntrinsics(fx, fy, cx, cy);
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
