// Copyright © 2026 ReactVision. All rights reserved.

package com.viro.core;

import android.util.Log;

/**
 * @hide
 */
public final class ReactVisionAuth {

    // Loaded here for the reason ColocationSession gives: this is called at
    // sign-in, before any AR view has loaded the renderer.
    private static final String TAG = "Viro";

    private static Throwable sLoadError;
    private static final boolean sNativeLoaded = loadNativeLibraries();
    private static boolean sWarned;

    private static boolean loadNativeLibraries() {
        try {
            System.loadLibrary("gvr");
            System.loadLibrary("gvr_audio");
            System.loadLibrary("viro_renderer");
            return true;
        } catch (Throwable t) {
            sLoadError = t;
            return false;
        }
    }

    // Once, not per call: a sign-in flow can set and clear the session many times,
    // and the cause does not change. Without it the session was dropped silently
    // and the next cloud anchor or co-location call failed as "not signed in".
    private static synchronized boolean nativeReady() {
        if (sNativeLoaded) return true;
        if (!sWarned) {
            sWarned = true;
            Log.w(TAG, "ReactVisionAuth: the Viro native libraries failed to load, so the "
                    + "ReactVision session was not set. Cloud anchors and co-location will run "
                    + "without it.", sLoadError);
        }
        return false;
    }

    private ReactVisionAuth() {}

    /** A null or empty {@code baseUrl} or {@code accessToken} clears the session. */
    public static void setSession(String baseUrl, String accessToken, String clientTag) {
        setSession(baseUrl, accessToken, clientTag, null);
    }

    /**
     * As above, plus the session project's database region, which cloud anchors
     * send as x-region so its edge functions run beside the database. Null or
     * empty adds none here; ReactVisionCCA still pins a session on the default
     * platform URL to that platform's region.
     */
    public static void setSession(String baseUrl, String accessToken, String clientTag,
                                  String functionRegion) {
        if (!nativeReady()) return;
        nativeSetSession(baseUrl, accessToken, clientTag, functionRegion);
    }

    public static void clearSession() {
        if (!nativeReady()) return;
        nativeClearSession();
    }

    /** Overrides the manifest project id for cloud anchors. Null or empty clears the override. */
    public static void setProjectId(String projectId) {
        if (!nativeReady()) return;
        nativeSetProjectId(projectId);
    }

    private static native void nativeSetSession(String baseUrl, String accessToken, String clientTag,
                                                String functionRegion);
    private static native void nativeClearSession();
    private static native void nativeSetProjectId(String projectId);
}
