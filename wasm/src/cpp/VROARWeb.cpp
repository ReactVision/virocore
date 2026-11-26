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

VROARCameraWeb::View VROARCameraWeb::getView(VROViewport viewport) const {
    float aspect = (viewport.getHeight() > 0)
                       ? (float) viewport.getWidth() / (float) viewport.getHeight()
                       : 1.0f;
    View v = {0, 0, 0, 0, 0, 0};

    // Derive the frustum from the camera's own intrinsics when the host has
    // supplied them. Off-axis, because a real camera's principal point is not
    // the centre of its image: a symmetric frustum built from the focal lengths
    // alone leaves a constant offset behind, which reads as content sitting a
    // little to one side of where it was placed.
    if (!(hasIntrinsics() && _height > 0 && _width > 0)) {
        // No intrinsics: a 60-degree vertical field of view, centred, over the
        // whole viewport. It aligns with the camera image only by luck.
        v.t = v.b = tanf(toRadians(30.0f));
        v.l = v.r = v.t * aspect;
        return v;
    }
    v.l = _cx / _fx;
    v.r = (_width - _cx) / _fx;
    v.t = _cy / _fy;
    v.b = (_height - _cy) / _fy;

    const float w = v.l + v.r;
    const float h = v.t + v.b;
    if (aspect > w / h) {
        const float c = (h - w / aspect) / 2;
        v.t -= c;
        v.b -= c;
        v.trimV = c / h;
    } else {
        const float c = (w - h * aspect) / 2;
        v.l -= c;
        v.r -= c;
        v.trimU = c / w;
    }
    return v;
}

VROMatrix4f VROARCameraWeb::getProjection(VROViewport viewport, float near, float far,
                                          VROFieldOfView *outFOV) {
    View v = getView(viewport);
    VROFieldOfView fov(toDegrees(atanf(v.l)), toDegrees(atanf(v.r)),
                       toDegrees(atanf(v.b)), toDegrees(atanf(v.t)));
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
    _pointCloud = std::make_shared<VROARPointCloud>();
}

void VROARSessionWeb::setPointCloud(std::vector<VROVector4f> points,
                                    std::vector<uint64_t> identifiers) {
    _pointCloud = std::make_shared<VROARPointCloud>(std::move(points), std::move(identifiers));
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
        _camera, _frameCounter, _orientation, _anchors, _pointCloud));
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
