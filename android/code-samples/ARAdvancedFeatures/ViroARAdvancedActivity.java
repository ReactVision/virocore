package com.example.virosample;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.widget.Toast;

import com.viro.core.ARAnchor;
import com.viro.core.ARGeospatialAnchor;
import com.viro.core.ARNode;
import com.viro.core.ARScene;
import com.viro.core.ARSceneNavigator;
import com.viro.core.Box;
import com.viro.core.Material;
import com.viro.core.Node;
import com.viro.core.Quaternion;
import com.viro.core.Text;
import com.viro.core.Vector;
import com.viro.core.ViroView;
import com.viro.core.ViroViewARCore;

import java.util.Arrays;

/**
 * Sample activity demonstrating advanced AR features:
 * - Geospatial anchors
 * - Cloud anchors
 * - Scene semantics
 * - Augmented faces
 */
public class ViroARAdvancedActivity extends Activity {
    private static final String TAG = "ViroARAdvanced";
    
    private ViroView mViroView;
    private ARScene mScene;
    private ARSceneNavigator mSceneNavigator;
    
    // Feature flags
    private boolean mGeospatialEnabled = false;
    private boolean mCloudAnchorsEnabled = false;
    private boolean mSemanticsEnabled = false;
    private boolean mFaceTrackingEnabled = false;
    
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
        // Create ViroView with ARCore
        mViroView = new ViroViewARCore(this, new ViroViewARCore.StartupListener() {
            @Override
            public void onSuccess() {
                displayScene();
                setupAdvancedFeatures();
            }
            
            @Override
            public void onFailure(ViroViewARCore.StartupError error, String errorMessage) {
                Log.e(TAG, "Error initializing AR: " + errorMessage);
            }
        });
        
        setContentView(mViroView);
    }
    
    private void displayScene() {
        // Create the AR scene
        mScene = new ARScene();
        
        // Create scene navigator
        mSceneNavigator = new ARSceneNavigator();
        mSceneNavigator.setScene(mScene);
        
        // Set the scene in ViroView
        mViroView.setSceneNavigator(mSceneNavigator);
        
        // Enable anchor detection
        mScene.setAnchorDetectionTypes(ARScene.AnchorDetectionType.PLANES_HORIZONTAL);
        
        // Add UI elements
        addUIElements();
    }
    
    private void setupAdvancedFeatures() {
        // 1. Setup Geospatial API
        setupGeospatialFeatures();
        
        // 2. Setup Cloud Anchors
        setupCloudAnchors();
        
        // 3. Setup Scene Semantics
        setupSceneSemantics();
        
        // 4. Setup Face Tracking
        setupFaceTracking();
    }
    
    // ================== Geospatial Features ==================
    
    private void setupGeospatialFeatures() {
        if (!mScene.isGeospatialSupported()) {
            Log.w(TAG, "Geospatial API not supported on this device");
            return;
        }
        
        // Enable Geospatial mode with terrain anchors
        mScene.setGeospatialMode(ARGeospatialAnchor.GeospatialMode.ENABLED_WITH_TERRAIN_ANCHORS);
        mGeospatialEnabled = true;
        
        // Set up geospatial pose listener
        mScene.setGeospatialPoseListener(new ARScene.GeospatialPoseListener() {
            @Override
            public void onGeospatialPoseUpdate(double latitude, double longitude, 
                                              double altitude, float heading,
                                              float horizontalAccuracy, float verticalAccuracy) {
                Log.d(TAG, String.format("Geospatial pose: %.6f, %.6f, %.2fm", 
                                        latitude, longitude, altitude));
                
                // Create anchors when accuracy is good
                if (horizontalAccuracy < 10.0f && verticalAccuracy < 2.0f) {
                    createGeospatialAnchors(latitude, longitude, altitude);
                }
            }
        });
        
        // Check VPS availability at current location
        checkVPSAvailability();
    }
    
    private void createGeospatialAnchors(double latitude, double longitude, double altitude) {
        // Create a standard geospatial anchor 100 meters north
        double northLat = latitude + 0.0009; // ~100m north
        Quaternion orientation = new Quaternion(0, 0, 0, 1);
        
        ARGeospatialAnchor anchor = ARGeospatialAnchor.create(
            mScene.getARSession(),
            northLat,
            longitude,
            altitude + 1.0, // 1 meter above current altitude
            orientation
        );
        
        if (anchor != null) {
            // Create AR content at the anchor
            ARNode arNode = new ARNode();
            arNode.setAnchor(anchor);
            
            // Add a 3D marker
            Box marker = new Box(0.2f, 0.2f, 0.2f);
            Material material = new Material();
            material.setDiffuseColor(android.graphics.Color.BLUE);
            marker.setMaterials(Arrays.asList(material));
            
            arNode.addChildNode(marker);
            mScene.getRootNode().addChildNode(arNode);
            
            Log.d(TAG, "Created geospatial anchor at: " + northLat + ", " + longitude);
        }
        
        // Create a terrain anchor
        createTerrainAnchor(latitude, longitude);
        
        // Create a rooftop anchor (if in urban area)
        createRooftopAnchor(latitude, longitude);
    }
    
    private void createTerrainAnchor(double latitude, double longitude) {
        // Place anchor 50 meters east
        double eastLon = longitude + 0.00045; // ~50m east
        Quaternion orientation = new Quaternion(0, 0, 0, 1);
        
        ARGeospatialAnchor.createTerrainAnchor(
            mScene.getARSession(),
            latitude,
            eastLon,
            orientation,
            new ARGeospatialAnchor.AnchorResolveCallback() {
                @Override
                public void onResolve(ARGeospatialAnchor anchor, double altitude) {
                    runOnUiThread(() -> {
                        Log.d(TAG, "Terrain anchor resolved at altitude: " + altitude);
                        addContentToAnchor(anchor, "Terrain Anchor", android.graphics.Color.GREEN);
                    });
                }
                
                @Override
                public void onError(String error) {
                    Log.e(TAG, "Failed to create terrain anchor: " + error);
                }
            }
        );
    }
    
    private void createRooftopAnchor(double latitude, double longitude) {
        // Place anchor 50 meters west
        double westLon = longitude - 0.00045; // ~50m west
        Quaternion orientation = new Quaternion(0, 0, 0, 1);
        
        ARGeospatialAnchor.createRooftopAnchor(
            mScene.getARSession(),
            latitude,
            westLon,
            orientation,
            new ARGeospatialAnchor.AnchorResolveCallback() {
                @Override
                public void onResolve(ARGeospatialAnchor anchor, double altitude) {
                    runOnUiThread(() -> {
                        Log.d(TAG, "Rooftop anchor resolved at altitude: " + altitude);
                        Log.d(TAG, "Rooftop state: " + anchor.getRooftopState());
                        addContentToAnchor(anchor, "Rooftop Anchor", android.graphics.Color.YELLOW);
                    });
                }
                
                @Override
                public void onError(String error) {
                    Log.e(TAG, "Failed to create rooftop anchor: " + error);
                }
            }
        );
    }
    
    private void checkVPSAvailability() {
        // Check VPS at current location (would need actual GPS coordinates)
        double testLat = 37.4220; // Example: Google HQ
        double testLon = -122.0841;
        
        ARGeospatialAnchor.checkVPSAvailability(
            mScene.getARSession(),
            testLat,
            testLon,
            new ARGeospatialAnchor.VPSAvailabilityCallback() {
                @Override
                public void onResult(ARGeospatialAnchor.VPSAvailability availability) {
                    runOnUiThread(() -> {
                        String message = "VPS availability: " + availability;
                        Toast.makeText(ViroARAdvancedActivity.this, message, Toast.LENGTH_SHORT).show();
                        Log.d(TAG, message);
                    });
                }
            }
        );
    }
    
    // ================== Cloud Anchors ==================
    
    private void setupCloudAnchors() {
        // Enable cloud anchors
        mScene.setCloudAnchorMode(ARScene.CloudAnchorMode.ENABLED);
        mCloudAnchorsEnabled = true;
        
        // Set up plane tap listener to create cloud anchors
        mScene.setListener(new ARScene.ClickListener() {
            @Override
            public void onClick(int source, Node node, Vector clickPosition) {
                // Create local anchor first
                ARAnchor localAnchor = mScene.addAnchor(clickPosition);
                
                // Check hosting quality
                ARScene.CloudAnchorQuality quality = mScene.getCloudAnchorHostingQuality(localAnchor);
                Log.d(TAG, "Cloud anchor hosting quality: " + quality);
                
                if (quality == ARScene.CloudAnchorQuality.GOOD || 
                    quality == ARScene.CloudAnchorQuality.SUFFICIENT) {
                    // Host the cloud anchor with 365 day TTL
                    hostCloudAnchor(localAnchor);
                } else {
                    Toast.makeText(ViroARAdvancedActivity.this, 
                                 "Insufficient quality for cloud anchor", 
                                 Toast.LENGTH_SHORT).show();
                }
            }
        });
    }
    
    private void hostCloudAnchor(ARAnchor localAnchor) {
        mScene.hostCloudAnchorWithTTL(
            localAnchor,
            365, // Max TTL in days
            new ARScene.CloudAnchorCallback() {
                @Override
                public void onSuccess(String cloudAnchorId) {
                    runOnUiThread(() -> {
                        Log.d(TAG, "Cloud anchor hosted successfully: " + cloudAnchorId);
                        Toast.makeText(ViroARAdvancedActivity.this,
                                     "Cloud anchor ID: " + cloudAnchorId,
                                     Toast.LENGTH_LONG).show();
                        
                        // Save cloud anchor ID for sharing/persistence
                        saveCloudAnchorId(cloudAnchorId);
                    });
                }
                
                @Override
                public void onFailure(String error) {
                    Log.e(TAG, "Failed to host cloud anchor: " + error);
                }
            }
        );
    }
    
    private void resolveCloudAnchor(String cloudAnchorId) {
        mScene.resolveCloudAnchor(
            cloudAnchorId,
            new ARScene.CloudAnchorCallback() {
                @Override
                public void onSuccess(String anchorId) {
                    Log.d(TAG, "Cloud anchor resolved: " + anchorId);
                    // AR content will appear at the resolved anchor automatically
                }
                
                @Override
                public void onFailure(String error) {
                    Log.e(TAG, "Failed to resolve cloud anchor: " + error);
                }
            }
        );
    }
    
    // ================== Scene Semantics ==================
    
    private void setupSceneSemantics() {
        if (!mScene.isSemanticsSupported()) {
            Log.w(TAG, "Scene semantics not supported on this device");
            return;
        }
        
        // Enable scene semantics
        mScene.setSemanticMode(ARScene.SemanticMode.ENABLED);
        mSemanticsEnabled = true;
        
        // Set up semantic frame listener
        mScene.setSemanticFrameListener(new ARScene.SemanticFrameListener() {
            @Override
            public void onSemanticFrameUpdate(ARScene.SemanticFrame frame) {
                // Check for specific labels
                if (frame.hasLabel(ARScene.SemanticLabel.PERSON, 0.7f)) {
                    Log.d(TAG, "Person detected in scene");
                }
                
                if (frame.hasLabel(ARScene.SemanticLabel.SKY, 0.5f)) {
                    float skyPercentage = frame.getLabelCoverage(ARScene.SemanticLabel.SKY, 0.5f);
                    Log.d(TAG, "Sky coverage: " + (skyPercentage * 100) + "%");
                }
                
                // Get semantic texture for visualization
                updateSemanticOverlay(frame);
            }
        });
    }
    
    private void updateSemanticOverlay(ARScene.SemanticFrame frame) {
        // Create overlay showing semantic labels
        // This would typically render the semantic texture as an overlay
        // with different colors for different labels
    }
    
    // ================== Face Tracking ==================
    
    private void setupFaceTracking() {
        if (!mScene.isFaceTrackingSupported()) {
            Log.w(TAG, "Face tracking not supported on this device");
            return;
        }
        
        // Note: Face tracking requires front camera configuration
        // This would typically be set up differently than world tracking
        
        mScene.setFaceTrackingListener(new ARScene.FaceTrackingListener() {
            @Override
            public void onFaceDetected(ARScene.AugmentedFace face) {
                Log.d(TAG, "Face detected with confidence: " + face.getTrackingConfidence());
                
                // Add face mesh or effects
                addFaceEffects(face);
            }
            
            @Override
            public void onFaceUpdated(ARScene.AugmentedFace face) {
                // Update face mesh/effects
                updateFaceEffects(face);
            }
            
            @Override
            public void onFaceLost(ARScene.AugmentedFace face) {
                Log.d(TAG, "Face tracking lost");
                removeFaceEffects(face);
            }
        });
    }
    
    private void addFaceEffects(ARScene.AugmentedFace face) {
        // Create face mesh geometry
        Node faceNode = face.createFaceMeshNode();
        
        // Add face texture or material
        Material faceMaterial = new Material();
        faceMaterial.setDiffuseTexture("face_texture.png");
        faceMaterial.setTransparency(0.5f);
        
        // Apply to face mesh
        if (faceNode.getGeometry() != null) {
            faceNode.getGeometry().setMaterials(Arrays.asList(faceMaterial));
        }
        
        mScene.getRootNode().addChildNode(faceNode);
    }
    
    private void updateFaceEffects(ARScene.AugmentedFace face) {
        // Update based on facial expressions
        float smileAmount = (face.getMouthSmileLeft() + face.getMouthSmileRight()) / 2.0f;
        if (smileAmount > 0.5f) {
            Log.d(TAG, "Smile detected: " + smileAmount);
        }
    }
    
    private void removeFaceEffects(ARScene.AugmentedFace face) {
        // Remove face node from scene
    }
    
    // ================== Helper Methods ==================
    
    private void addContentToAnchor(ARAnchor anchor, String label, int color) {
        ARNode arNode = new ARNode();
        arNode.setAnchor(anchor);
        
        // Add colored box
        Box box = new Box(0.15f, 0.15f, 0.15f);
        Material material = new Material();
        material.setDiffuseColor(color);
        box.setMaterials(Arrays.asList(material));
        
        // Add label text
        Text text = new Text(label, "Arial", 12);
        text.setPosition(new Vector(0, 0.2f, 0));
        
        arNode.addChildNode(box);
        arNode.addChildNode(text);
        mScene.getRootNode().addChildNode(arNode);
    }
    
    private void addUIElements() {
        // Add UI text showing feature status
        Text statusText = new Text(getFeatureStatusText(), "Arial", 10);
        statusText.setPosition(new Vector(0, 2, -3));
        mScene.getRootNode().addChildNode(statusText);
    }
    
    private String getFeatureStatusText() {
        StringBuilder status = new StringBuilder("AR Features:\n");
        status.append("Geospatial: ").append(mGeospatialEnabled ? "✓" : "✗").append("\n");
        status.append("Cloud Anchors: ").append(mCloudAnchorsEnabled ? "✓" : "✗").append("\n");
        status.append("Scene Semantics: ").append(mSemanticsEnabled ? "✓" : "✗").append("\n");
        status.append("Face Tracking: ").append(mFaceTrackingEnabled ? "✓" : "✗");
        return status.toString();
    }
    
    private void saveCloudAnchorId(String cloudAnchorId) {
        // Save to SharedPreferences or database for persistence
        getSharedPreferences("cloud_anchors", MODE_PRIVATE)
            .edit()
            .putString("last_anchor_id", cloudAnchorId)
            .apply();
    }
    
    @Override
    protected void onStart() {
        super.onStart();
        mViroView.onActivityStarted(this);
    }
    
    @Override
    protected void onResume() {
        super.onResume();
        mViroView.onActivityResumed(this);
    }
    
    @Override
    protected void onPause() {
        super.onPause();
        mViroView.onActivityPaused(this);
    }
    
    @Override
    protected void onStop() {
        super.onStop();
        mViroView.onActivityStopped(this);
    }
    
    @Override
    protected void onDestroy() {
        super.onDestroy();
        mViroView.onActivityDestroyed(this);
    }
}