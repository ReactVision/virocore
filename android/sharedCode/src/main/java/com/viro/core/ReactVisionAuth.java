// Copyright © 2026 ReactVision. All rights reserved.

package com.viro.core;

/**
 * @hide
 */
public final class ReactVisionAuth {

    // Loaded here for the reason ColocationSession gives: this is called at
    // sign-in, before any AR view has loaded the renderer.
    private static final boolean sNativeLoaded = loadNativeLibraries();

    private static boolean loadNativeLibraries() {
        try {
            System.loadLibrary("gvr");
            System.loadLibrary("gvr_audio");
            System.loadLibrary("viro_renderer");
            return true;
        } catch (Throwable t) {
            return false;
        }
    }

    private ReactVisionAuth() {}

    /** A null or empty {@code baseUrl} or {@code accessToken} clears the session. */
    public static void setSession(String baseUrl, String accessToken, String clientTag) {
        if (!sNativeLoaded) return;
        nativeSetSession(baseUrl, accessToken, clientTag);
    }

    public static void clearSession() {
        if (!sNativeLoaded) return;
        nativeClearSession();
    }

    /** Overrides the manifest project id for cloud anchors. Null or empty clears the override. */
    public static void setProjectId(String projectId) {
        if (!sNativeLoaded) return;
        nativeSetProjectId(projectId);
    }

    private static native void nativeSetSession(String baseUrl, String accessToken, String clientTag);
    private static native void nativeClearSession();
    private static native void nativeSetProjectId(String projectId);
}
