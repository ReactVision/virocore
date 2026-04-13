package com.viro.openxrtest;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;

import com.viro.core.RendererConfiguration;
import com.viro.core.ViroViewOpenXR;

/**
 * Minimal POC activity for Meta Quest OpenXR validation.
 *
 * Expected result on device:
 *   Left eye  → solid blue
 *   Right eye → solid green
 *
 * This confirms that:
 *   - OpenXR instance + session created successfully
 *   - EGL context bound to the XrSession
 *   - Per-eye swapchain acquire / render / release cycle works
 *   - Stereo composition submitted via xrEndFrame
 *
 * No Scene is set intentionally — the C++ renderer falls back to the
 * POC color-clear in renderEye() when no scene controller is present.
 */
public class MainActivity extends Activity {

    private static final String TAG = "OpenXRTest";

    private ViroViewOpenXR mViroView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        RendererConfiguration config = new RendererConfiguration();
        config.setShadowsEnabled(false);
        config.setHDREnabled(false);
        config.setPBREnabled(false);
        config.setBloomEnabled(false);

        mViroView = new ViroViewOpenXR(this, new ViroViewOpenXR.StartupListener() {
            @Override
            public void onSuccess() {
                Log.i(TAG, "ViroViewOpenXR initialized — Quest should show blue/green stereo");
                // No scene set: POC color-clear in renderEye() will render
            }

            @Override
            public void onFailure(ViroViewOpenXR.StartupError error, String errorMessage) {
                Log.e(TAG, "ViroViewOpenXR failed to initialize: " + errorMessage);
            }
        }, config);

        setContentView(mViroView);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (mViroView != null) mViroView.onActivityResumed(this);
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (mViroView != null) mViroView.onActivityPaused(this);
    }

    @Override
    protected void onStop() {
        super.onStop();
        if (mViroView != null) mViroView.onActivityStopped(this);
    }

    @Override
    protected void onStart() {
        super.onStart();
        if (mViroView != null) mViroView.onActivityStarted(this);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (mViroView != null) mViroView.onActivityDestroyed(this);
    }
}
