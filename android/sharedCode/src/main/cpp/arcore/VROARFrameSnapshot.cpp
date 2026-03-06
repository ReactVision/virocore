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
    // NOTE: Do NOT call cam->getImageSize() here.  On ARCore it triggers
    // acquireCameraImage() which holds a CPU image buffer.  ARCore's default
    // concurrent-image limit is 1, so holding one here prevents the later
    // getCameraImageY() call (in collectBgKeyframe / resolve luma capture)
    // from acquiring the Y plane → SIFT gets no luma → v2 fallback.
    // _imageSize stays (0,0,0); RVCA code uses luma dimensions instead.

    // ARCore's getProjectionMatrix(near, far) is viewport-independent, so the
    // dummy viewport below gives the same matrix as any real viewport.
    snapCam->_projection = cam->getProjection(VROViewport(0, 0, 1, 1),
                                               0.01f, 100.0f, nullptr);

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

    // ── Lazy luma (Android P7 equivalent) ──────────────────────────────────────
    // Do NOT copy the Y plane eagerly — it's ~2 MB per frame at 30 fps.
    // Instead, store a pointer to the live frame.  getCameraImageY() will
    // lazily acquire + cache the luma only when actually needed (motion gate
    // pass in collectBgKeyframe, or active resolve luma capture).
    snap->_liveFrame = &src;

    return snap;
}
