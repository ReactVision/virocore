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

// ReactVisionCCA is an optional proprietary library.
// CMakeLists.txt defines RVCCA_AVAILABLE=1 automatically when the prebuilt
// libreactvisioncca.so is found in jniLibs/.  Open-source builds of ViroCore
// compile stubs that report the feature as unavailable.
#ifndef RVCCA_AVAILABLE
#  define RVCCA_AVAILABLE 0
#endif

#if RVCCA_AVAILABLE
#  include "ReactVisionCCA/RVCCACloudAnchorProvider.h"
#endif

#include <stdexcept>

// ============================================================================
// Impl
// ============================================================================

#if RVCCA_AVAILABLE

class VROCloudAnchorProviderReactVision::Impl {
public:
    std::weak_ptr<VROARSessionARCore>                         session;
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

#else // !RVCCA_AVAILABLE

// Minimal stub so std::unique_ptr<Impl> compiles without RVCCA headers.
class VROCloudAnchorProviderReactVision::Impl {};

#endif // RVCCA_AVAILABLE

// ============================================================================
// Public API
// ============================================================================

VROCloudAnchorProviderReactVision::VROCloudAnchorProviderReactVision(
    std::shared_ptr<VROARSessionARCore> session,
    const std::string &apiKey,
    const std::string &projectId,
    const std::string &endpoint)
{
#if RVCCA_AVAILABLE
    try {
        _impl = std::make_unique<Impl>(session, apiKey, projectId, endpoint);
    } catch (const std::exception &e) {
        pwarn("VROCloudAnchorProviderReactVision init failed: %s", e.what());
    }
#else
    pwarn("VROCloudAnchorProviderReactVision: ReactVisionCCA library not available. "
          "Build reactvisioncca and deploy libreactvisioncca.so to "
          "android/sharedCode/src/main/jniLibs/ before building ViroCore.");
#endif
}

VROCloudAnchorProviderReactVision::~VROCloudAnchorProviderReactVision() = default;

void VROCloudAnchorProviderReactVision::hostCloudAnchor(
    std::shared_ptr<VROARAnchor> anchor,
    int ttlDays,
    std::function<void(std::shared_ptr<VROARAnchor>)> onSuccess,
    std::function<void(std::string)>                  onFailure)
{
#if RVCCA_AVAILABLE
    if (!_impl || !_impl->provider) {
        onFailure("ReactVisionCCA provider not initialised");
        return;
    }

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

    std::shared_ptr<VROARFrame> frame = VROARFrameSnapshot::fromFrame(*frameUniq);
    if (!frame) {
        onFailure("Failed to snapshot AR frame");
        return;
    }

    _impl->provider->hostCloudAnchor(
        anchor, frame, ttlDays,
        [anchor, onSuccess](const std::string &cloudId) {
            anchor->setCloudAnchorId(cloudId);
            anchor->setId(cloudId);
            onSuccess(anchor);
        },
        [onFailure](const std::string &error,
                    ReactVisionCCA::RVCCACloudAnchorProvider::ErrorCode) {
            onFailure(error);
        });
#else
    onFailure("ReactVision Cloud Anchors not available: ReactVisionCCA library not linked");
#endif
}

void VROCloudAnchorProviderReactVision::resolveCloudAnchor(
    std::string cloudAnchorId,
    std::function<void(std::shared_ptr<VROARAnchor>)> onSuccess,
    std::function<void(std::string)>                  onFailure)
{
#if RVCCA_AVAILABLE
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
#else
    onFailure("ReactVision Cloud Anchors not available: ReactVisionCCA library not linked");
#endif
}
