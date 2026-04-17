package com.viro.openxrtest;

import android.app.Activity;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;

import com.viro.core.AmbientLight;
import com.viro.core.AsyncObject3DListener;
import com.viro.core.Box;
import com.viro.core.ClickListener;
import com.viro.core.ClickState;
import com.viro.core.DirectionalLight;
import com.viro.core.HoverListener;
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
 * M1 + M2 + M3 validation scene for Meta Quest OpenXR.
 *
 * M1 checks (unchanged):
 *   [HDR]     HDR render target + tone mapping
 *   [PBR]     Blue metallic box, shiba GLB, dragon VRX
 *   [Shadow]  DirectionalLight shadow on floor plane
 *   [Bloom]   Emissive white sphere with bloomThreshold=0.0
 *   [GLB]     shiba.glb
 *   [VRX]     object_dragon_pbr_anim.vrx
 *
 * M2 checks (new interactive nodes):
 *   Right trigger  → source=1  (ViroOculus::Controller)
 *   Left trigger   → source=4  (ViroOculus::LeftController)
 *   Right grip     → source=9  (ViroOculus::RightGrip)
 *   Left grip      → source=8  (ViroOculus::LeftGrip)
 *   A button       → source=5  (ViroOculus::AButton)
 *   B button       → source=3  (ViroOculus::BackButton)
 *   X button       → source=6  (ViroOculus::XButton)
 *   Y button       → source=7  (ViroOculus::YButton)
 *   Hover          → node turns yellow, back to original on exit
 *
 * M3 checks (hand tracking):
 *   Right pinch → source=1 click on cyan sphere (right aim ray)
 *   Left pinch  → source=4 click on cyan sphere (left aim ray)
 *   Grab        → onClickState CLICK_DOWN/UP logged with source
 *
 * Pass criteria:
 *   All logcat lines "M2-CLICK source=X" fire for each button/grip
 *   Hover lines "M2-HOVER" appear when ray enters/exits node
 *   M3 pinch fires "M2-CLICK source=1" (right) or "M2-CLICK source=4" (left)
 */
public class MainActivity extends Activity {

    private static final String TAG = "OpenXRTest";

    // ViroOculus InputSource values (mirrors VROInputType.h)
    private static final int SRC_RIGHT_TRIGGER  = 1;
    private static final int SRC_BACK_BUTTON    = 3;
    private static final int SRC_LEFT_TRIGGER   = 4;
    private static final int SRC_A_BUTTON       = 5;
    private static final int SRC_X_BUTTON       = 6;
    private static final int SRC_Y_BUTTON       = 7;
    private static final int SRC_LEFT_GRIP      = 8;
    private static final int SRC_RIGHT_GRIP     = 9;

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
                Log.i(TAG, "ViroViewOpenXR initialized — loading M1+M2+M3 scene");
                loadScene();
            }

            @Override
            public void onFailure(ViroViewOpenXR.StartupError error, String errorMessage) {
                Log.e(TAG, "ViroViewOpenXR failed: " + errorMessage);
            }
        }, config);

        setContentView(mViroView);
    }

    private void loadScene() {
        Scene scene = new Scene();
        ViroContext context = mViroView.getViroContext();

        // ── Lighting ──────────────────────────────────────────────────────────
        AmbientLight ambient = new AmbientLight(Color.WHITE, 300f);
        scene.getRootNode().addLight(ambient);

        DirectionalLight dirLight = new DirectionalLight(Color.WHITE, 3000f,
                new Vector(0.5f, -1f, -0.5f));
        dirLight.setCastsShadow(true);
        dirLight.setShadowOrthographicSize(10f);
        dirLight.setShadowOrthographicPosition(new Vector(0f, 5f, 0f));
        dirLight.setShadowNearZ(0.1f);
        dirLight.setShadowFarZ(20f);
        dirLight.setShadowOpacity(0.7f);
        scene.getRootNode().addLight(dirLight);

        // ── Floor plane ───────────────────────────────────────────────────────
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

        // ── [M1] Red CONSTANT box ─────────────────────────────────────────────
        Box redBox = new Box(0.5f, 0.5f, 0.5f);
        Material redMat = new Material();
        redMat.setDiffuseColor(Color.RED);
        redMat.setLightingModel(Material.LightingModel.CONSTANT);
        redBox.setMaterials(Arrays.asList(redMat));
        Node redNode = new Node();
        redNode.setGeometry(redBox);
        redNode.setPosition(new Vector(0f, 0f, -2f));
        scene.getRootNode().addChildNode(redNode);

        // ── [M1] Blue PBR metallic box ────────────────────────────────────────
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

        // ── [M1] Bloom sphere ─────────────────────────────────────────────────
        Sphere bloomSphere = new Sphere(0.15f);
        Material bloomMat = new Material();
        bloomMat.setDiffuseColor(Color.WHITE);
        bloomMat.setLightingModel(Material.LightingModel.CONSTANT);
        bloomMat.setBloomThreshold(0.0f);
        bloomSphere.setMaterials(Arrays.asList(bloomMat));
        Node bloomNode = new Node();
        bloomNode.setGeometry(bloomSphere);
        bloomNode.setPosition(new Vector(0f, 0.6f, -1.5f));
        scene.getRootNode().addChildNode(bloomNode);

        // ── [M1] Shiba GLB ────────────────────────────────────────────────────
        Object3D shiba = new Object3D();
        shiba.loadModel(context,
                Uri.parse("file:///android_asset/shiba.glb"),
                Object3D.Type.GLB,
                new AsyncObject3DListener() {
                    @Override public void onObject3DLoaded(Object3D obj, Object3D.Type type) {
                        Log.i(TAG, "[M1] shiba.glb loaded");
                    }
                    @Override public void onObject3DFailed(String error) {
                        Log.e(TAG, "[M1] shiba.glb failed: " + error);
                    }
                });
        shiba.setPosition(new Vector(-1.2f, 0.0f, -2.5f));
        shiba.setScale(new Vector(0.5f, 0.5f, 0.5f));
        scene.getRootNode().addChildNode(shiba);

        // ── [M1] Dragon VRX ───────────────────────────────────────────────────
        Object3D dragon = new Object3D();
        dragon.loadModel(context,
                Uri.parse("file:///android_asset/object_dragon_pbr_anim.vrx"),
                Object3D.Type.FBX,
                new AsyncObject3DListener() {
                    @Override public void onObject3DLoaded(Object3D obj, Object3D.Type type) {
                        Log.i(TAG, "[M1] dragon VRX loaded");
                    }
                    @Override public void onObject3DFailed(String error) {
                        Log.e(TAG, "[M1] dragon VRX failed: " + error);
                    }
                });
        dragon.setPosition(new Vector(1.5f, -0.5f, -2.0f));
        dragon.setScale(new Vector(1.0f, 1.0f, 1.0f));
        scene.getRootNode().addChildNode(dragon);

        // ── [M2] Interactive test nodes ───────────────────────────────────────
        // Arranged in an arc at -1.5m depth, -0.2m height
        // Point any controller ray at them and pull trigger / press button.
        // Expected logcat per button:
        //   Right trigger → M2-CLICK source=1
        //   Left trigger  → M2-CLICK source=4
        //   Right grip    → M2-CLICK source=9
        //   Left grip     → M2-CLICK source=8
        //   A button      → M2-CLICK source=5
        //   B / Menu      → M2-CLICK source=3
        //   X button      → M2-CLICK source=6
        //   Y button      → M2-CLICK source=7

        addInteractiveBox(scene, "TRIGGER-TARGET",
                new Vector(-1.8f, 0.2f, -1.5f), Color.rgb(255, 140, 0));   // orange
        addInteractiveBox(scene, "GRIP-TARGET",
                new Vector(-0.9f, 0.2f, -1.5f), Color.rgb(160, 32, 240));  // purple
        addInteractiveBox(scene, "BUTTON-A",
                new Vector(0f, 0.2f, -1.5f), Color.rgb(0, 200, 80));       // green
        addInteractiveBox(scene, "BUTTON-B",
                new Vector(0.9f, 0.2f, -1.5f), Color.rgb(200, 50, 50));    // dark red
        addInteractiveBox(scene, "BUTTON-XY",
                new Vector(1.8f, 0.2f, -1.5f), Color.rgb(0, 150, 255));    // cyan-blue

        // ── [M3] Hand-tracking pinch/grab target (cyan sphere) ────────────────
        // Aim either hand ray at this sphere and pinch (index strength ≥ 0.7).
        // Right-hand pinch → M2-CLICK source=1
        // Left-hand pinch  → M2-CLICK source=4
        // Grab (fist)      → M2-STATE CLICK_DOWN/CLICK_UP source=1 or 4
        Sphere handSphere = new Sphere(0.18f);
        Material handMat = new Material();
        handMat.setDiffuseColor(Color.CYAN);
        handMat.setLightingModel(Material.LightingModel.PHYSICALLY_BASED);
        handMat.setMetalness(0.0f);
        handMat.setRoughness(0.5f);
        handSphere.setMaterials(Arrays.asList(handMat));

        Node handNode = new Node();
        handNode.setGeometry(handSphere);
        handNode.setPosition(new Vector(0f, 0.8f, -1.2f));
        attachListeners(handNode, "HAND-PINCH-TARGET",
                handMat, Color.CYAN, Color.YELLOW);
        scene.getRootNode().addChildNode(handNode);

        mViroView.setScene(scene);
        Log.i(TAG, "M1+M2+M3 scene set");
    }

    /**
     * Creates a labeled interactive box and attaches click+hover listeners.
     * Hover turns the box yellow; click logs "M2-CLICK source=X label=NAME".
     */
    private void addInteractiveBox(Scene scene, String label,
                                   Vector position, int baseColor) {
        Box box = new Box(0.35f, 0.35f, 0.35f);
        Material mat = new Material();
        mat.setDiffuseColor(baseColor);
        mat.setLightingModel(Material.LightingModel.PHYSICALLY_BASED);
        mat.setMetalness(0.1f);
        mat.setRoughness(0.6f);
        box.setMaterials(Arrays.asList(mat));

        Node node = new Node();
        node.setGeometry(box);
        node.setPosition(position);
        attachListeners(node, label, mat, baseColor, Color.YELLOW);
        scene.getRootNode().addChildNode(node);
    }

    private void attachListeners(Node node, String label,
                                 Material mat, int idleColor, int hoverColor) {
        node.setClickListener(new ClickListener() {
            @Override
            public void onClick(int source, Node n, Vector location) {
                Log.i(TAG, "M2-CLICK source=" + source
                        + " label=" + label
                        + " src=" + sourceLabel(source));
            }

            @Override
            public void onClickState(int source, Node n, ClickState state, Vector location) {
                if (state != ClickState.CLICKED) {
                    Log.i(TAG, "M2-STATE " + state.name()
                            + " source=" + source
                            + " label=" + label
                            + " src=" + sourceLabel(source));
                }
            }
        });

        node.setHoverListener(new HoverListener() {
            @Override
            public void onHover(int source, Node n, boolean isHovering, Vector location) {
                mat.setDiffuseColor(isHovering ? hoverColor : idleColor);
                Log.i(TAG, "M2-HOVER " + (isHovering ? "ENTER" : "EXIT")
                        + " source=" + source + " label=" + label);
            }
        });
    }

    private static String sourceLabel(int source) {
        switch (source) {
            case SRC_RIGHT_TRIGGER: return "RIGHT_TRIGGER";
            case SRC_BACK_BUTTON:   return "BACK/B/MENU";
            case SRC_LEFT_TRIGGER:  return "LEFT_TRIGGER";
            case SRC_A_BUTTON:      return "A_BUTTON";
            case SRC_X_BUTTON:      return "X_BUTTON";
            case SRC_Y_BUTTON:      return "Y_BUTTON";
            case SRC_LEFT_GRIP:     return "LEFT_GRIP";
            case SRC_RIGHT_GRIP:    return "RIGHT_GRIP";
            default:                return "UNKNOWN(" + source + ")";
        }
    }

    @Override protected void onResume()  { super.onResume();  if (mViroView != null) mViroView.onActivityResumed(this); }
    @Override protected void onPause()   { super.onPause();   if (mViroView != null) mViroView.onActivityPaused(this); }
    @Override protected void onStop()    { super.onStop();    if (mViroView != null) mViroView.onActivityStopped(this); }
    @Override protected void onStart()   { super.onStart();   if (mViroView != null) mViroView.onActivityStarted(this); }
    @Override protected void onDestroy() { super.onDestroy(); if (mViroView != null) mViroView.onActivityDestroyed(this); }
}
