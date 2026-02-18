//
//  VROCloudAnchorProviderReactVision.cpp
//  ViroKit (Android)
//
//  Copyright © 2026 ReactVision. All rights reserved.
//  Proprietary and Confidential
//

#include "VROCloudAnchorProviderReactVision.h"
#include "VROARSessionARCore.h"
#include "VROARFrame.h"
#include "VROARAnchor.h"
#include "VROARFrameSnapshot.h"
#include "VROLog.h"

// ReactVisionCCA C++ library
#include "ReactVisionCCA/RVCCACloudAnchorProvider.h"

#include <stdexcept>

// ============================================================================
// Impl
// ============================================================================

class VROCloudAnchorProviderReactVision::Impl {
public:
    std::weak_ptr<VROARSessionARCore>                       session;
    std::shared_ptr<ReactVisionCCA::RVCCACloudAnchorProvider> provider;

    Impl(std::shared_ptr<VROARSessionARCore> sess,
         const std::string &apiKey,
         const std::string &projectId,
         const std::string &endpoint)
        : session(sess)
    {
        ReactVisionCCA::RVCCACloudAnchorProvider::Config cfg;
        cfg.apiKey    = apiKey;
        cfg.projectId = projectId;
        if (!endpoint.empty()) cfg.endpoint = endpoint;

        provider = std::make_shared<ReactVisionCCA::RVCCACloudAnchorProvider>(cfg);
    }
};

// ============================================================================
// Public API
// ============================================================================

VROCloudAnchorProviderReactVision::VROCloudAnchorProviderReactVision(
    std::shared_ptr<VROARSessionARCore> session,
    const std::string &apiKey,
    const std::string &projectId,
    const std::string &endpoint)
{
    try {
        _impl = std::make_unique<Impl>(session, apiKey, projectId, endpoint);
    } catch (const std::exception &e) {
        pwarn("VROCloudAnchorProviderReactVision init failed: %s", e.what());
    }
}

VROCloudAnchorProviderReactVision::~VROCloudAnchorProviderReactVision() = default;

void VROCloudAnchorProviderReactVision::hostCloudAnchor(
    std::shared_ptr<VROARAnchor> anchor,
    int ttlDays,
    std::function<void(std::shared_ptr<VROARAnchor>)> onSuccess,
    std::function<void(std::string)>                  onFailure)
{
    if (!_impl || !_impl->provider) {
        onFailure("ReactVisionCCA provider not initialised");
        return;
    }

    // Capture current frame from ARCore session.
    // We snapshot the point cloud data here, on the renderer thread, before
    // spawning any background work.  The session can advance to the next frame
    // at any time (every ~16 ms), so we must NOT hold a raw pointer across
    // thread boundaries — the underlying VROARFrameARCore would be destroyed.
    auto sess = _impl->session.lock();
    if (!sess) {
        onFailure("AR session no longer available");
        return;
    }

    auto &frameUniq = sess->getLastFrame();
    if (!frameUniq) {
        onFailure("No AR frame available for feature extraction");
        return;
    }

    // Build a self-contained snapshot frame that owns copies of all data the
    // background thread will need.  This avoids the race condition where
    // updateFrame() destroys the original VROARFrameARCore mid-operation.
    std::shared_ptr<VROARFrame> frame = VROARFrameSnapshot::fromFrame(*frameUniq);
    if (!frame) {
        onFailure("Failed to snapshot AR frame");
        return;
    }

    _impl->provider->hostCloudAnchor(
        anchor, frame, ttlDays,
        [anchor, onSuccess](const std::string &cloudId) {
            // Attach the cloud anchor ID to the original anchor object
            anchor->setCloudAnchorId(cloudId);
            anchor->setId(cloudId);
            onSuccess(anchor);
        },
        [onFailure](const std::string &error,
                    ReactVisionCCA::RVCCACloudAnchorProvider::ErrorCode) {
            onFailure(error);
        });
}

void VROCloudAnchorProviderReactVision::resolveCloudAnchor(
    std::string cloudAnchorId,
    std::function<void(std::shared_ptr<VROARAnchor>)> onSuccess,
    std::function<void(std::string)>                  onFailure)
{
    if (!_impl || !_impl->provider) {
        onFailure("ReactVisionCCA provider not initialised");
        return;
    }

    auto sess = _impl->session.lock();
    if (!sess) {
        onFailure("AR session no longer available");
        return;
    }

    auto &frameUniq2 = sess->getLastFrame();
    if (!frameUniq2) {
        onFailure("No AR frame available for localisation");
        return;
    }
    std::shared_ptr<VROARFrame> frame2 = VROARFrameSnapshot::fromFrame(*frameUniq2);
    if (!frame2) {
        onFailure("Failed to snapshot AR frame for localisation");
        return;
    }

    _impl->provider->resolveCloudAnchor(
        cloudAnchorId, frame2,
        [onSuccess](std::shared_ptr<VROARAnchor> resolved) {
            onSuccess(resolved);
        },
        [onFailure](const std::string &error,
                    ReactVisionCCA::RVCCACloudAnchorProvider::ErrorCode) {
            onFailure(error);
        });
}
