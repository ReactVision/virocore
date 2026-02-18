//
//  VROARFrameSnapshot.cpp
//  ViroKit (Android)
//
//  Copyright © 2026 ReactVision. All rights reserved.
//  Proprietary and Confidential
//

#include "VROARFrameSnapshot.h"

std::shared_ptr<VROARFrame> VROARFrameSnapshot::fromFrame(VROARFrame& src)
{
    const auto& cam = src.getCamera();
    if (!cam) return nullptr;

    // ── Camera snapshot ───────────────────────────────────────────────────────
    auto snapCam = std::make_shared<VROARCameraSnapshot>();
    snapCam->_trackingState  = cam->getTrackingState();
    snapCam->_trackingReason = cam->getLimitedTrackingStateReason();
    snapCam->_rotation       = cam->getRotation();
    snapCam->_position       = cam->getPosition();
    snapCam->_imageSize      = cam->getImageSize();

    // Pre-compute the projection for the image-size viewport — the only
    // viewport RVCCACloudAnchorProvider ever requests.
    VROVector3f sz = snapCam->_imageSize;
    int w = static_cast<int>(sz.x);
    int h = static_cast<int>(sz.y);
    if (w > 0 && h > 0) {
        VROViewport vp(0, 0, w, h);
        snapCam->_projection = cam->getProjection(vp, 0.01f, 100.0f, nullptr);
    }

    // ── Point cloud snapshot (deep copy) ─────────────────────────────────────
    std::shared_ptr<VROARPointCloud> snapPc;
    auto srcPc = src.getPointCloud();
    if (srcPc) {
        // getPoints() / getIdentifiers() return by value — this IS the copy.
        snapPc = std::make_shared<VROARPointCloud>(
            srcPc->getPoints(), srcPc->getIdentifiers());
    } else {
        snapPc = std::make_shared<VROARPointCloud>();
    }

    // ── Assemble frame snapshot ───────────────────────────────────────────────
    // Use a local subclass-accessible constructor via the private default ctor.
    auto snap = std::shared_ptr<VROARFrameSnapshot>(new VROARFrameSnapshot());
    snap->_timestamp   = src.getTimestamp();
    snap->_orientation = src.getOrientation();
    snap->_camera      = snapCam;
    snap->_pointCloud  = snapPc;
    return snap;
}
