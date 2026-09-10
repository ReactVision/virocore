//
//  VROColocationSession.h
//  ViroRenderer
//
//  Copyright © 2026 ReactVision. All rights reserved.
//
//  Platform-neutral holder for the co-location frame channel.
//
//  The channel itself lives in ReactVisionCCA (RVCCAColocationSession) and
//  carries frame-native data — where each peer is, and whether they have
//  localised — between devices that already share a coordinate frame.
//
//  This wrapper exists so the two platform bridges reach it the same way, and
//  so nothing above has to know whether the proprietary library was linked at
//  all: without it every call fails cleanly and `isAvailable()` says why.
//
//  Deliberately not hung off VROARSession. The channel needs no AR session —
//  only a room id and poses — and visionOS has no VROARSession to hang it from,
//  so making it independent is what lets one implementation serve all three
//  platforms.
//
//  One session per process: a device is in one room at a time, and a second
//  join is a switch, not a second membership.
//

#ifndef VROColocationSession_h
#define VROColocationSession_h

#include <string>
#include <vector>
#include <memory>
#include <functional>

#include "VROMatrix4f.h"

/** One peer's last known pose, in the shared location frame. */
struct VROColocationPeer {
    std::string peerId;
    float position[3] = {0, 0, 0};
    float rotation[4] = {0, 0, 0, 1};   // quaternion x, y, z, w
    double timestampMs = 0.0;
    bool   localized = false;
};

enum class VROColocationState {
    Idle,
    Joining,
    Joined,
    Reconnecting,
    Failed
};

class VROColocationSession {
public:

    /** The process-wide session. Created on first use. */
    static VROColocationSession &get();

    /**
     * False when ReactVisionCCA was not linked, or the platform has no
     * WebSocket transport. Every other call is a no-op in that case.
     */
    bool isAvailable() const;

    /**
     * Join the room for `roomId` — the cloud anchor id, Meta group id or
     * visionOS session id that already names the shared frame.
     *
     * Leaves any room already joined. `endpoint` may be empty for the default.
     */
    void join(const std::string &roomId,
              const std::string &apiKey,
              const std::string &projectId,
              const std::string &endpoint,
              std::function<void(bool success, std::string error)> callback);

    void leave();

    /**
     * Publish the local pose, **in the shared location frame** — not world
     * coordinates, which are per-session and meaningless to the receiver.
     * Safe to call every frame; the rate limit lives below this.
     */
    void setLocalPose(const VROMatrix4f &locationFramePose);

    VROColocationState state() const;
    std::string localPeerId() const;
    std::vector<VROColocationPeer> peers() const;

private:
    VROColocationSession();
    ~VROColocationSession();
    VROColocationSession(const VROColocationSession &) = delete;
    VROColocationSession &operator=(const VROColocationSession &) = delete;

    class Impl;
    std::unique_ptr<Impl> _impl;
};

#endif /* VROColocationSession_h */
