package com.viro.openxrtest;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.util.Log;

import com.viro.core.Box;
import com.viro.core.Material;
import com.viro.core.Node;
import com.viro.core.RendererConfiguration;
import com.viro.core.Scene;
import com.viro.core.Vector;
import com.viro.core.ViroViewOpenXR;

import java.util.Arrays;

/**
 * Minimal test activity for Meta Quest OpenXR validation (Week 3+).
 *
 * Expected result on device:
 *   Red box (0.5 m cube) centered 2 m in front of the user, in stereo at 90 fps.
 *
 * This confirms that:
 *   - OpenXR instance + session created successfully
 *   - EGL context bound to the XrSession
 *   - Per-eye swapchain acquire / render / release cycle works
 *   - VRORenderer::prepareFrame + renderEye wired end-to-end
 *   - Stereo geometry submitted via xrEndFrame with correct view/projection matrices
 *
 * Troubleshooting:
 *   Black screen  → hasRenderContext() returning false; check setScene() was called
 *   Crash on boot → check logcat for XrResult error; see Technical Challenges in plan
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
                Log.i(TAG, "ViroViewOpenXR initialized — loading dummy scene");
                loadDummyScene();
            }

            @Override
            public void onFailure(ViroViewOpenXR.StartupError error, String errorMessage) {
                Log.e(TAG, "ViroViewOpenXR failed to initialize: " + errorMessage);
            }
        }, config);

        setContentView(mViroView);
    }

    /**
     * Minimal Viro scene for Week 3 validation on Quest 3.
     *
     * Expected result:
     *   - Red box floating 2 m in front of the user
     *   - White ambient light so the box is fully visible without shadows
     *   - Stereo rendering at 90 fps confirms VRORenderer is wired end-to-end
     *
     * If you see the red box: Week 3 is working.
     * If you see black: renderEye() is not reaching VRORenderer (check hasRenderContext).
     * If you see a crash: check logcat for the first XrResult error code.
     */
    private void loadDummyScene() {
        Scene scene = new Scene();

        // Ambient light — keeps the box fully lit without needing a directional light.
        com.viro.core.AmbientLight light = new com.viro.core.AmbientLight(Color.WHITE, 1000f);
        scene.getRootNode().addLight(light);

        // Red box: 0.5 m × 0.5 m × 0.5 m, placed 2 m in front of the origin.
        Box box = new Box(0.5f, 0.5f, 0.5f);
        Material mat = new Material();
        mat.setDiffuseColor(Color.RED);
        mat.setLightingModel(Material.LightingModel.CONSTANT);  // unlit — visible regardless of lights
        box.setMaterials(Arrays.asList(mat));

        Node boxNode = new Node();
        boxNode.setGeometry(box);
        boxNode.setPosition(new Vector(0f, 0f, -2f));  // -Z = forward in Viro/OpenXR
        scene.getRootNode().addChildNode(boxNode);

        mViroView.setScene(scene);
        Log.i(TAG, "Dummy scene set — red box at (0, 0, -2)");
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
