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

    // Capture current frame from ARCore session
    auto sess = _impl->session.lock();
    if (!sess) {
        onFailure("AR session no longer available");
        return;
    }

    // getLastFrame() returns unique_ptr<VROARFrame>&; wrap raw pointer in a
    // shared_ptr with a no-op deleter — the session owns the frame lifetime.
    auto &frameUniq = sess->getLastFrame();
    if (!frameUniq) {
        onFailure("No AR frame available for feature extraction");
        return;
    }
    std::shared_ptr<VROARFrame> frame(frameUniq.get(), [](VROARFrame*){});

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
    std::shared_ptr<VROARFrame> frame2(frameUniq2.get(), [](VROARFrame*){});

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
