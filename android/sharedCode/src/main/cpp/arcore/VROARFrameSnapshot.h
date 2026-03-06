//
//  VROARFrameSnapshot.h
//  ViroKit (Android)
//
//  Copyright © 2026 ReactVision. All rights reserved.
//  Proprietary and Confidential
//
//  Thread-safe snapshot of a VROARFrame.
//
//  VROARFrameARCore is owned by the AR session and replaced every ~16 ms via
//  updateFrame().  Background threads (e.g. cloud anchor host/resolve) must
//  NOT hold a raw pointer to the live frame across thread boundaries.
//
//  VROARFrameSnapshot copies all data the feature extractor needs at
//  construction time so it can be safely passed to a detached thread.
//

#ifndef VROARFrameSnapshot_h
#define VROARFrameSnapshot_h

#include "VROARFrame.h"
#include "VROARCamera.h"
#include "VROARPointCloud.h"
#include "VROMatrix4f.h"
#include "VROVector3f.h"
#include "VROCameraTexture.h"    // VROCameraOrientation
#include "VROViewport.h"

#include <memory>
#include <vector>
#include <set>

// ─── VROARCameraSnapshot ─────────────────────────────────────────────────────
// Owns copied camera data.  Implements VROARCamera so it can be returned from
// VROARFrameSnapshot::getCamera().

class VROARCameraSnapshot : public VROARCamera {
public:
    VROARTrackingState         _trackingState  = VROARTrackingState::Normal;
    VROARTrackingStateReason   _trackingReason = VROARTrackingStateReason::None;
    VROMatrix4f                _rotation;
    VROVector3f                _position;
    VROVector3f                _imageSize;
    VROMatrix4f                _projection;    // cached for image-size viewport

    VROARTrackingState       getTrackingState()             const override { return _trackingState;  }
    VROARTrackingStateReason getLimitedTrackingStateReason() const override { return _trackingReason; }
    VROMatrix4f              getRotation()                   const override { return _rotation;       }
    VROVector3f              getPosition()                   const override { return _position;       }
    VROVector3f              getImageSize()                        override { return _imageSize;      }

    // RVCCACloudAnchorProvider always queries with viewport (0,0,imgW,imgH),
    // near=0.01, far=100.  We pre-computed that exact matrix at snapshot time.
    VROMatrix4f getProjection(VROViewport /*vp*/, float /*near*/, float /*far*/,
                               VROFieldOfView* /*outFOV*/) override {
        return _projection;
    }
};

// ─── VROARFrameSnapshot ──────────────────────────────────────────────────────

class VROARFrameSnapshot : public VROARFrame {
public:
    // Factory — copies all relevant data from src on the calling thread.
    // Returns nullptr if src's camera is unavailable.
    static std::shared_ptr<VROARFrame> fromFrame(VROARFrame& src);

    // ── VROARFrame interface ──────────────────────────────────────────────────

    double                              getTimestamp()   const override { return _timestamp;   }
    const std::shared_ptr<VROARCamera>& getCamera()      const override { return _camera;      }
    VROCameraOrientation                getOrientation() const override { return _orientation; }
    std::shared_ptr<VROARPointCloud>    getPointCloud()        override { return _pointCloud;  }
    bool getCameraImageY(const uint8_t** data, int* width, int* height) override {
        // Lazy luma acquisition (Android P7 equivalent):
        // Only copies the Y plane (~2MB) when actually needed (motion gate pass
        // or active resolve).  Most frames never call this → zero copy cost.
        if (_lumaData.empty() && _liveFrame) {
            const uint8_t* srcData = nullptr; int srcW = 0, srcH = 0;
            if (_liveFrame->getCameraImageY(&srcData, &srcW, &srcH)
                && srcData && srcW > 0 && srcH > 0) {
                _lumaW = srcW;
                _lumaH = srcH;
                _lumaData.assign(srcData, srcData + (size_t)srcW * srcH);
            }
            _liveFrame = nullptr; // one-shot: release reference after attempt
        }
        if (_lumaData.empty()) return false;
        *data = _lumaData.data(); *width = _lumaW; *height = _lumaH;
        return true;
    }

    // Call from bridge after updateWithFrame() returns to prevent dangling access.
    void invalidateLiveFrame() { _liveFrame = nullptr; }

    // Stubs — not needed by the feature extractor
    std::vector<std::shared_ptr<VROARHitTestResult>>
        hitTest(int, int, std::set<VROARHitTestResultType>) override { return {}; }

    std::vector<std::shared_ptr<VROARHitTestResult>>
        hitTestRay(VROVector3f*, VROVector3f*,
                   std::set<VROARHitTestResultType>) override { return {}; }

    VROMatrix4f  getViewportToCameraImageTransform() const override { return VROMatrix4f(); }
    float        getAmbientLightIntensity()          const override { return 1.f; }
    VROVector3f  getAmbientLightColor()              const override { return VROVector3f(1,1,1); }

    const std::vector<std::shared_ptr<VROARAnchor>>& getAnchors() const override {
        return _anchors;
    }

private:
    double                            _timestamp   = 0.0;
    VROCameraOrientation              _orientation = VROCameraOrientation::Portrait;
    std::shared_ptr<VROARCamera>      _camera;
    std::shared_ptr<VROARPointCloud>  _pointCloud;
    std::vector<std::shared_ptr<VROARAnchor>> _anchors; // always empty

    // Lazy luma: _liveFrame is valid only on the render thread during
    // updateWithFrame().  On first getCameraImageY() call, luma is copied
    // from the live frame into _lumaData, then _liveFrame is nulled.
    VROARFrame* _liveFrame = nullptr;

    // Tightly-packed Y plane (populated lazily from _liveFrame)
    std::vector<uint8_t> _lumaData;
    int _lumaW = 0, _lumaH = 0;
};

#endif /* VROARFrameSnapshot_h */
