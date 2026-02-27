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

        // ARCore feature points have much lower confidence values than ARKit (typically
        // 0.1–0.5 vs 0.8–1.0). The default minPointConfidence of 0.5 discards the
        // majority of ARCore points, leaving too few keypoints for cross-platform resolve.
        cfg.minPointConfidence = 0.15f;

        // ARCore produces sparser point clouds per frame than ARKit (~50–150 pts vs ~300+).
        // A longer scan window compensates, giving Android enough accumulated points to
        // build a descriptor set dense enough for cross-platform matching.
        cfg.scanWindowMs = 4000;

        provider = std::make_shared<ReactVisionCCA::RVCCACloudAnchorProvider>(cfg);
    }
};

// Improvement 2: map RVCCACloudAnchorProvider::ErrorCode to Google-compatible
// state strings so callers can pattern-match on the same state names as ARCore.
static std::string encodeError(
    const std::string &msg,
    ReactVisionCCA::RVCCACloudAnchorProvider::ErrorCode code)
{
    using EC = ReactVisionCCA::RVCCACloudAnchorProvider::ErrorCode;
    const char* state;
    switch (code) {
        case EC::NetworkError:         state = "ErrorNetworkFailure";                        break;
        case EC::AuthenticationFailed: state = "ErrorAuthenticationFailed";                  break;
        case EC::InsufficientFeatures: state = "ErrorHostingInsufficientVisualFeatures";     break;
        case EC::LocalizationFailed:   state = "ErrorResolvingLocalizationNoMatch";          break;
        case EC::AnchorNotFound:       state = "ErrorCloudIdNotFound";                       break;
        case EC::AnchorExpired:        state = "ErrorAnchorExpired";                         break;
        case EC::Timeout:              state = "ErrorNetworkFailure";                        break;
        default:                       state = "ErrorInternal";                              break;
    }
    return msg + "|" + state;
}

#else // !RVCCA_AVAILABLE

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

    // Improvement 3: pass the latest GPS fix into the provider before hosting
    {
        double lat = 0.0, lng = 0.0, alt = 0.0;
        sess->getLastKnownLocation(lat, lng, alt);
        if (lat != 0.0 || lng != 0.0) {
            _impl->provider->setLastKnownLocation(lat, lng, alt);
        }
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
        // Improvement 2: encode ErrorCode as "|StateString" suffix
        [onFailure](const std::string &error,
                    ReactVisionCCA::RVCCACloudAnchorProvider::ErrorCode code) {
            onFailure(encodeError(error, code));
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

    // Improvement 1: resolve no longer needs a frame snapshot at call time —
    // localization runs across multiple frames via updateWithFrame().
    // We pass nullptr; RVCCACloudAnchorProvider ignores the frame for localize.
    _impl->provider->resolveCloudAnchor(
        cloudAnchorId, nullptr,
        [onSuccess](std::shared_ptr<VROARAnchor> resolved) {
            onSuccess(resolved);
        },
        // Improvement 2: encode ErrorCode as "|StateString" suffix
        [onFailure](const std::string &error,
                    ReactVisionCCA::RVCCACloudAnchorProvider::ErrorCode code) {
            onFailure(encodeError(error, code));
        });
#else
    onFailure("ReactVision Cloud Anchors not available: ReactVisionCCA library not linked");
#endif
}

// Improvement 1 + 6B: called every frame by VROFrameSynchronizer.
// Takes a fresh frame snapshot and feeds it to the provider's updateWithFrame()
// which drives both host point-cloud accumulation and resolve localization.
void VROCloudAnchorProviderReactVision::onFrameDidRender(const VRORenderContext&) {
#if RVCCA_AVAILABLE
    if (!_impl || !_impl->provider) return;

    // Avoid the snapshot cost when there is nothing pending.
    const bool hasWork =
        _impl->provider->hasPendingLocalizations() ||
        _impl->provider->hasPendingHosts();
    if (!hasWork) return;

    auto sess = _impl->session.lock();
    if (!sess) return;

    auto &frameUniq = sess->getLastFrame();
    if (!frameUniq) return;

    auto snap = VROARFrameSnapshot::fromFrame(*frameUniq);
    if (snap) {
        _impl->provider->updateWithFrame(snap);
    }
#endif
}
