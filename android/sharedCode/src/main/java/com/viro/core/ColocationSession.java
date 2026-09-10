// Copyright © 2026 ReactVision. All rights reserved.
//
// Java surface for the co-location frame channel.
//
// Deliberately a plain class and not part of ARScene: the channel needs no AR
// session, only a room id and poses, so binding it to one would put it out of
// reach on the platforms that have no ARScene.
//
// One session per process — a device is in one room at a time, and joining
// again is a switch, not a second membership.

package com.viro.core;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class ColocationSession {

    /** One peer's last known pose, in the shared location frame. */
    public static class Peer {
        public final String peerId;
        /** Location-frame position. Never world coordinates. */
        public final float[] position;   // x, y, z
        public final float[] rotation;   // quaternion x, y, z, w
        public final double timestampMs;
        public final boolean localized;

        Peer(String peerId, float[] position, float[] rotation,
             double timestampMs, boolean localized) {
            this.peerId = peerId;
            this.position = position;
            this.rotation = rotation;
            this.timestampMs = timestampMs;
            this.localized = localized;
        }
    }

    public static final int STATE_IDLE         = 0;
    public static final int STATE_JOINING      = 1;
    public static final int STATE_JOINED       = 2;
    public static final int STATE_RECONNECTING = 3;
    public static final int STATE_FAILED       = 4;

    public interface JoinCallback {
        void onResult(boolean success, String error);
    }

    private static ColocationSession sInstance;

    public static synchronized ColocationSession getInstance() {
        if (sInstance == null) sInstance = new ColocationSession();
        return sInstance;
    }

    private final Map<String, JoinCallback> mJoinCallbacks = new HashMap<>();

    private ColocationSession() {}

    /** False when ReactVisionCCA is not linked or the platform has no socket. */
    public boolean isAvailable() {
        return nativeIsAvailable();
    }

    /**
     * Join the room named by {@code roomId} — the cloud anchor id, Meta group
     * id or visionOS session id that already names the shared frame.
     */
    public void join(String roomId, String apiKey, String projectId,
                     String endpoint, JoinCallback callback) {
        String key = "colocationJoin_" + System.nanoTime();
        mJoinCallbacks.put(key, callback);
        nativeJoin(key, roomId, apiKey, projectId, endpoint == null ? "" : endpoint);
    }

    public void leave() {
        nativeLeave();
    }

    /**
     * Publish the local pose as 16 comma-separated floats, column-major, **in
     * the shared location frame**. World coordinates are per-session and mean
     * nothing to the receiver.
     */
    public void setLocalPose(String locationFramePoseCsv) {
        if (locationFramePoseCsv == null) return;
        nativeSetLocalPose(locationFramePoseCsv);
    }

    public int getState() {
        return nativeGetState();
    }

    public String getLocalPeerId() {
        return nativeGetLocalPeerId();
    }

    /**
     * Current peers, excluding this device.
     *
     * Native returns one flat string rather than an object array: this is
     * polled several times a second, and building an array across JNI costs a
     * local ref per field for no benefit.
     */
    public List<Peer> getPeers() {
        List<Peer> out = new ArrayList<>();
        String raw = nativeGetPeers();
        if (raw == null || raw.isEmpty()) return out;

        for (String row : raw.split(";")) {
            String[] f = row.split(",");
            if (f.length != 10) continue;   // a malformed row is dropped, not fatal
            try {
                out.add(new Peer(
                        f[0],
                        new float[]{ Float.parseFloat(f[1]), Float.parseFloat(f[2]), Float.parseFloat(f[3]) },
                        new float[]{ Float.parseFloat(f[4]), Float.parseFloat(f[5]),
                                     Float.parseFloat(f[6]), Float.parseFloat(f[7]) },
                        Double.parseDouble(f[8]),
                        "1".equals(f[9])));
            } catch (NumberFormatException ignored) {
                // Same: skip the row rather than lose the whole poll.
            }
        }
        return out;
    }

    // Called from JNI.
    void onJoinResult(String key, boolean success, String error) {
        JoinCallback cb = mJoinCallbacks.remove(key);
        if (cb != null) cb.onResult(success, error);
    }

    private native boolean nativeIsAvailable();
    private native void    nativeJoin(String key, String roomId, String apiKey,
                                      String projectId, String endpoint);
    private native void    nativeLeave();
    private native void    nativeSetLocalPose(String csv);
    private native int     nativeGetState();
    private native String  nativeGetLocalPeerId();
    private native String  nativeGetPeers();
}
