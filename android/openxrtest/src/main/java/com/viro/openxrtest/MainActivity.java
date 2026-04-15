package com.viro.openxrtest;

import android.app.Activity;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;

import com.viro.core.AmbientLight;
import com.viro.core.AsyncObject3DListener;
import com.viro.core.Box;
import com.viro.core.DirectionalLight;
import com.viro.core.Material;
import com.viro.core.Node;
import com.viro.core.Object3D;
import com.viro.core.RendererConfiguration;
import com.viro.core.Scene;
import com.viro.core.Sphere;
import com.viro.core.Surface;
import com.viro.core.Vector;
import com.viro.core.ViroContext;
import com.viro.core.ViroViewOpenXR;

import java.util.Arrays;

/**
 * M1 full-feature validation for Meta Quest OpenXR.
 *
 * Validates in one scene:
 *   [HDR]     Config setHDREnabled(true) — floating-point render target + tone mapping
 *   [PBR]     Config setPBREnabled(true) — blue metallic box, shiba GLB, dragon VRX
 *   [Shadow]  DirectionalLight.setCastsShadow(true) — shadows on floor plane
 *   [Bloom]   Emissive white sphere with bloomThreshold=0.0 — visible halo
 *   [GLB]     shiba.glb loaded via Object3D
 *   [VRX]     object_dragon_pbr_anim.vrx loaded via Object3D
 *
 * Pass criteria:
 *   - All objects visible in stereo at 90 fps
 *   - Blue PBR box has bright/dark shading from directional light
 *   - Floor plane shows directional shadow from the red box
 *   - White emissive sphere has a visible bloom halo
 *   - Shiba model visible and textured
 *   - Dragon model visible and textured
 */
public class MainActivity extends Activity {

    private static final String TAG = "OpenXRTest";

    private ViroViewOpenXR mViroView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        RendererConfiguration config = new RendererConfiguration();
        config.setShadowsEnabled(true);
        config.setHDREnabled(true);
        config.setPBREnabled(true);
        config.setBloomEnabled(true);

        mViroView = new ViroViewOpenXR(this, new ViroViewOpenXR.StartupListener() {
            @Override
            public void onSuccess() {
                Log.i(TAG, "ViroViewOpenXR initialized — loading M1 scene");
                loadM1Scene();
            }

            @Override
            public void onFailure(ViroViewOpenXR.StartupError error, String errorMessage) {
                Log.e(TAG, "ViroViewOpenXR failed: " + errorMessage);
            }
        }, config);

        setContentView(mViroView);
    }

    private void loadM1Scene() {
        Scene scene = new Scene();
        ViroContext context = mViroView.getViroContext();

        // ── Lighting ──────────────────────────────────────────────────────────

        // Low ambient fill so PBR shading gradient is clearly visible
        AmbientLight ambient = new AmbientLight(Color.WHITE, 300f);
        scene.getRootNode().addLight(ambient);

        // Directional light — shadow-casting, drives PBR specular/diffuse
        DirectionalLight dirLight = new DirectionalLight(Color.WHITE, 3000f,
                new Vector(0.5f, -1f, -0.5f));
        dirLight.setCastsShadow(true);
        dirLight.setShadowOrthographicSize(10f);
        dirLight.setShadowOrthographicPosition(new Vector(0f, 5f, 0f));
        dirLight.setShadowNearZ(0.1f);
        dirLight.setShadowFarZ(20f);
        dirLight.setShadowOpacity(0.7f);
        scene.getRootNode().addLight(dirLight);

        // ── Floor plane (receives shadows) ────────────────────────────────────
        Surface floor = new Surface(6f, 6f);
        Material floorMat = new Material();
        floorMat.setDiffuseColor(Color.rgb(180, 180, 180));
        floorMat.setLightingModel(Material.LightingModel.PHYSICALLY_BASED);
        floorMat.setRoughness(0.9f);
        floorMat.setMetalness(0.0f);
        floor.setMaterials(Arrays.asList(floorMat));

        Node floorNode = new Node();
        floorNode.setGeometry(floor);
        floorNode.setRotation(new Vector((float) -Math.PI / 2, 0, 0));
        floorNode.setPosition(new Vector(0f, -0.5f, -2.5f));
        scene.getRootNode().addChildNode(floorNode);

        // ── [PBR] Red CONSTANT box — M0 reference ────────────────────────────
        Box redBox = new Box(0.5f, 0.5f, 0.5f);
        Material redMat = new Material();
        redMat.setDiffuseColor(Color.RED);
        redMat.setLightingModel(Material.LightingModel.CONSTANT);
        redBox.setMaterials(Arrays.asList(redMat));

        Node redNode = new Node();
        redNode.setGeometry(redBox);
        redNode.setPosition(new Vector(0f, 0f, -2f));
        scene.getRootNode().addChildNode(redNode);

        // ── [PBR] Blue metallic box ───────────────────────────────────────────
        Box blueBox = new Box(0.4f, 0.4f, 0.4f);
        Material pbrMat = new Material();
        pbrMat.setDiffuseColor(Color.rgb(70, 130, 180));
        pbrMat.setLightingModel(Material.LightingModel.PHYSICALLY_BASED);
        pbrMat.setMetalness(0.8f);
        pbrMat.setRoughness(0.3f);
        blueBox.setMaterials(Arrays.asList(pbrMat));

        Node blueNode = new Node();
        blueNode.setGeometry(blueBox);
        blueNode.setPosition(new Vector(0.8f, 0f, -2f));
        scene.getRootNode().addChildNode(blueNode);

        // ── [Bloom] White emissive sphere ─────────────────────────────────────
        Sphere bloomSphere = new Sphere(0.15f);
        Material bloomMat = new Material();
        bloomMat.setDiffuseColor(Color.WHITE);
        bloomMat.setLightingModel(Material.LightingModel.CONSTANT);
        bloomMat.setBloomThreshold(0.0f);   // always blooms
        bloomSphere.setMaterials(Arrays.asList(bloomMat));

        Node bloomNode = new Node();
        bloomNode.setGeometry(bloomSphere);
        bloomNode.setPosition(new Vector(0f, 0.6f, -1.5f));
        scene.getRootNode().addChildNode(bloomNode);

        // ── [GLB] Shiba Inu ───────────────────────────────────────────────────
        Object3D shiba = new Object3D();
        shiba.loadModel(context,
                Uri.parse("file:///android_asset/shiba.glb"),
                Object3D.Type.GLB,
                new AsyncObject3DListener() {
                    @Override
                    public void onObject3DLoaded(Object3D obj, Object3D.Type type) {
                        Log.i(TAG, "shiba.glb loaded");
                    }
                    @Override
                    public void onObject3DFailed(String error) {
                        Log.e(TAG, "shiba.glb failed: " + error);
                    }
                });
        shiba.setPosition(new Vector(-1.2f, 0.0f, -2.5f));
        shiba.setScale(new Vector(0.5f, 0.5f, 0.5f));
        scene.getRootNode().addChildNode(shiba);

        // ── [VRX] Dragon ──────────────────────────────────────────────────────
        Object3D dragon = new Object3D();
        dragon.loadModel(context,
                Uri.parse("file:///android_asset/object_dragon_pbr_anim.vrx"),
                Object3D.Type.FBX,
                new AsyncObject3DListener() {
                    @Override
                    public void onObject3DLoaded(Object3D obj, Object3D.Type type) {
                        Log.i(TAG, "dragon VRX loaded");
                    }
                    @Override
                    public void onObject3DFailed(String error) {
                        Log.e(TAG, "dragon VRX failed: " + error);
                    }
                });
        dragon.setPosition(new Vector(1.5f, -0.5f, -2.0f));
        dragon.setScale(new Vector(1.0f, 1.0f, 1.0f));
        scene.getRootNode().addChildNode(dragon);

        mViroView.setScene(scene);
        Log.i(TAG, "M1 full scene set — HDR+PBR+shadow+bloom+GLB+VRX");
    }

    @Override protected void onResume() {
        super.onResume();
        if (mViroView != null) mViroView.onActivityResumed(this);
    }
    @Override protected void onPause() {
        super.onPause();
        if (mViroView != null) mViroView.onActivityPaused(this);
    }
    @Override protected void onStop() {
        super.onStop();
        if (mViroView != null) mViroView.onActivityStopped(this);
    }
    @Override protected void onStart() {
        super.onStart();
        if (mViroView != null) mViroView.onActivityStarted(this);
    }
    @Override protected void onDestroy() {
        super.onDestroy();
        if (mViroView != null) mViroView.onActivityDestroyed(this);
    }
}
