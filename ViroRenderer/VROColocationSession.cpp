//
//  VROColocationSession.cpp
//  ViroRenderer
//
//  Copyright © 2026 ReactVision. All rights reserved.
//

#include "VROColocationSession.h"
#include "VROLog.h"
#include "VROReactVisionAuth.h"

#include <cstdint>
#include <cstdio>
#include <mutex>
#include <random>

// ReactVisionCCA is optional. Without it this compiles to a holder that always
// reports unavailable, so an open-source build links and runs — it simply has
// no channel, exactly like it has no cloud anchors.
#if __has_include("ReactVisionCCA/RVCCAColocationSession.h")
#  define VRO_RVCCA_COLOCATION 1
#  include "ReactVisionCCA/RVCCAColocationSession.h"
#else
#  define VRO_RVCCA_COLOCATION 0
#endif

namespace {

// Shared with the headers provider, which transport threads can call after the
// session that installed it is gone.
class AccessToken {
public:
    void set(const std::string &value) {
        std::lock_guard<std::mutex> lock(_mutex);
        _value = value;
    }
    std::string get() const {
        std::lock_guard<std::mutex> lock(_mutex);
        return _value;
    }
private:
    mutable std::mutex _mutex;
    std::string _value;
};

} // namespace

class VROColocationSession::Impl {
public:
    std::shared_ptr<AccessToken> accessToken = std::make_shared<AccessToken>();
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

std::string VROColocationSession::deviceId() {
    // Random per process rather than a hardware or vendor id: all the relay
    // needs is that the two sockets of one device send the same value.
    static const std::string id = [] {
        std::random_device rd;
        std::mt19937_64 gen((static_cast<uint64_t>(rd()) << 32) ^ rd());
        const uint64_t hi = (gen() & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
        const uint64_t lo = (gen() & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
        char buf[37];
        snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx",
                 static_cast<unsigned>(hi >> 32),
                 static_cast<unsigned>((hi >> 16) & 0xFFFF),
                 static_cast<unsigned>(hi & 0xFFFF),
                 static_cast<unsigned>(lo >> 48),
                 static_cast<unsigned long long>(lo & 0xFFFFFFFFFFFFULL));
        return std::string(buf);
    }();
    return id;
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
    std::shared_ptr<AccessToken> accessToken = _impl->accessToken;
    auto hasCredentials = [accessToken, apiKey]() {
        return !accessToken->get().empty() || VROReactVisionAuth::get().hasCredentials(apiKey);
    };
    if (!hasCredentials()) {
        if (callback) callback(false, VROReactVisionAuth::kNoCredentialsError);
        return;
    }
    if (projectId.empty()) {
        if (callback) callback(false, "projectId is required");
        return;
    }

    ReactVisionCCA::RVCCAColocationSession::Config cfg;
    cfg.apiKey    = apiKey;
    cfg.projectId = projectId;
    // The endpoint is the relay, a different host from the session's platform
    // base URL, so the session has nothing to say about it.
    if (!endpoint.empty()) cfg.endpoint = endpoint;
    // Always set, so a token or session wins over a key whenever one exists, as
    // a session does for cloud anchors: the channel sends the key only on a
    // handshake whose headers carry no Authorization. Read at every connect,
    // reconnects included.
    cfg.headersProvider = [accessToken]() {
        std::map<std::string, std::string> headers;
        std::string token = accessToken->get();
        if (!token.empty()) {
            headers["Authorization"] = "Bearer " + token;
        } else {
            headers = VROReactVisionAuth::get().sessionHeaders();
        }
        headers["x-rv-device"] = deviceId();
        return headers;
    };

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
    // A session cleared between the check above and the handshake leaves the
    // connect with no credential, and the relay's 401 would read as a bad
    // token. With neither a session nor a key now, say that instead.
    cb.onError = [callback, done, hasCredentials](const std::string &error) {
        if (*done) return;
        *done = true;
        if (!callback) return;
        if (!hasCredentials()) {
            callback(false, VROReactVisionAuth::kNoCredentialsError);
        } else {
            callback(false, error);
        }
    };

    // Checked again at the last moment before connecting: nothing above waits,
    // but a sign-out on another thread can land in between.
    if (!hasCredentials()) {
        _impl->session.reset();
        _impl->roomId.clear();
        if (callback) callback(false, VROReactVisionAuth::kNoCredentialsError);
        return;
    }
    _impl->session->join(roomId, std::move(cb));
#else
    (void)roomId; (void)apiKey; (void)projectId; (void)endpoint;
    if (callback) callback(false, "ReactVisionCCA is not linked in this build");
#endif
}

void VROColocationSession::leave() {
    _impl->accessToken->set("");
#if VRO_RVCCA_COLOCATION
    if (_impl->session) _impl->session->leave();
    _impl->session.reset();
    _impl->roomId.clear();
#endif
}

void VROColocationSession::setAccessToken(const std::string &accessToken) {
    _impl->accessToken->set(accessToken);
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
