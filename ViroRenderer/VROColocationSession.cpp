//
//  VROColocationSession.cpp
//  ViroRenderer
//
//  Copyright © 2026 ReactVision. All rights reserved.
//

#include "VROColocationSession.h"
#include "VROLog.h"

// ReactVisionCCA is optional. Without it this compiles to a holder that always
// reports unavailable, so an open-source build links and runs — it simply has
// no channel, exactly like it has no cloud anchors.
#if __has_include("ReactVisionCCA/RVCCAColocationSession.h")
#  define VRO_RVCCA_COLOCATION 1
#  include "ReactVisionCCA/RVCCAColocationSession.h"
#else
#  define VRO_RVCCA_COLOCATION 0
#endif

class VROColocationSession::Impl {
public:
#if VRO_RVCCA_COLOCATION
    std::unique_ptr<ReactVisionCCA::RVCCAColocationSession> session;
    std::string roomId;
#endif
};

VROColocationSession::VROColocationSession() : _impl(new Impl()) {}
VROColocationSession::~VROColocationSession() = default;

VROColocationSession &VROColocationSession::get() {
    static VROColocationSession instance;
    return instance;
}

bool VROColocationSession::isAvailable() const {
#if VRO_RVCCA_COLOCATION
    return ReactVisionCCA::RVCCAColocationSession::isSupported();
#else
    return false;
#endif
}

void VROColocationSession::join(const std::string &roomId,
                                 const std::string &apiKey,
                                 const std::string &projectId,
                                 const std::string &endpoint,
                                 std::function<void(bool, std::string)> callback) {
#if VRO_RVCCA_COLOCATION
    if (!isAvailable()) {
        if (callback) callback(false, "Co-location channel unavailable on this platform");
        return;
    }
    if (roomId.empty()) {
        if (callback) callback(false, "roomId is required");
        return;
    }
    if (apiKey.empty() || projectId.empty()) {
        if (callback) callback(false, "apiKey and projectId are required");
        return;
    }

    ReactVisionCCA::RVCCAColocationSession::Config cfg;
    cfg.apiKey    = apiKey;
    cfg.projectId = projectId;
    if (!endpoint.empty()) cfg.endpoint = endpoint;

    // Rebuilt rather than reused: Config is read at construction, so switching
    // rooms with a different endpoint or key would otherwise keep the old one.
    _impl->session.reset(new ReactVisionCCA::RVCCAColocationSession(cfg));
    _impl->roomId = roomId;

    ReactVisionCCA::RVCCAColocationSession::Callbacks cb;
    // The channel reports terminal failures through onError and recoverable
    // drops as Reconnecting, so only the former resolves the join as failed.
    auto done = std::make_shared<bool>(false);
    cb.onStateChange = [callback, done](ReactVisionCCA::RVCCAColocationSession::State s) {
        if (*done) return;
        if (s == ReactVisionCCA::RVCCAColocationSession::State::Joined) {
            *done = true;
            if (callback) callback(true, "");
        }
    };
    cb.onError = [callback, done](const std::string &error) {
        if (*done) return;
        *done = true;
        if (callback) callback(false, error);
    };

    _impl->session->join(roomId, std::move(cb));
#else
    (void)roomId; (void)apiKey; (void)projectId; (void)endpoint;
    if (callback) callback(false, "ReactVisionCCA is not linked in this build");
#endif
}

void VROColocationSession::leave() {
#if VRO_RVCCA_COLOCATION
    if (_impl->session) _impl->session->leave();
    _impl->session.reset();
    _impl->roomId.clear();
#endif
}

void VROColocationSession::setLocalPose(const VROMatrix4f &locationFramePose) {
#if VRO_RVCCA_COLOCATION
    if (_impl->session) _impl->session->setLocalPose(locationFramePose);
#else
    (void)locationFramePose;
#endif
}

VROColocationState VROColocationSession::state() const {
#if VRO_RVCCA_COLOCATION
    if (!_impl->session) return VROColocationState::Idle;
    using S = ReactVisionCCA::RVCCAColocationSession::State;
    switch (_impl->session->state()) {
        case S::Idle:         return VROColocationState::Idle;
        case S::Joining:      return VROColocationState::Joining;
        case S::Joined:       return VROColocationState::Joined;
        case S::Reconnecting: return VROColocationState::Reconnecting;
        case S::Failed:       return VROColocationState::Failed;
    }
#endif
    return VROColocationState::Idle;
}

std::string VROColocationSession::localPeerId() const {
#if VRO_RVCCA_COLOCATION
    if (_impl->session) return _impl->session->localPeerId();
#endif
    return "";
}

std::vector<VROColocationPeer> VROColocationSession::peers() const {
    std::vector<VROColocationPeer> out;
#if VRO_RVCCA_COLOCATION
    if (!_impl->session) return out;
    for (const auto &p : _impl->session->peers()) {
        VROColocationPeer peer;
        peer.peerId      = p.peerId;
        peer.timestampMs = p.timestampMs;
        peer.localized   = p.localized;
        for (int i = 0; i < 3; i++) peer.position[i] = p.position[i];
        for (int i = 0; i < 4; i++) peer.rotation[i] = p.rotation[i];
        out.push_back(peer);
    }
#endif
    return out;
}
