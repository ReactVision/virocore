package com.viromedia.aradvancedfeatures;

import android.Manifest;
import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.widget.Toast;
import com.viro.core.*;
import java.util.List;

/**
 * Sample app demonstrating ViroCore's advanced AR features:
 * - Geospatial API
 * - Scene Semantics
 * - Augmented Faces
 * - Cloud Anchors
 */
public class MainActivity extends ViroActivity {
    
    private static final String TAG = "ARAdvancedFeatures";
    private ViroView mViroView;
    private ARScene mARScene;
    private ARSession mARSession;
    
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        
        // Create ViroView in AR mode
        mViroView = new ViroViewARCore(this, new ViroViewARCore.StartupListener() {
            @Override
            public void onSuccess() {
                initializeAR();
            }
            
            @Override
            public void onFailure(ViroViewARCore.StartupError error, String errorMessage) {
                Log.e(TAG, "Failed to start AR: " + errorMessage);
            }
        });
        
        setContentView(mViroView);
    }
    
    private void initializeAR() {
        // Create AR scene
        mARScene = new ARScene();
        mViroView.setScene(mARScene);
        
        // Get AR session
        mARSession = mViroView.getARSession();
        
        // Setup AR features
        setupGeospatialAnchors();
        setupSceneSemantics();
        setupAugmentedFaces();
        setupCloudAnchors();
        setupMemoryManagement();
    }
    
    /**
     * Demonstrate Geospatial API
     */
    private void setupGeospatialAnchors() {
        // Check for location permission
        if (!hasPermission(Manifest.permission.ACCESS_FINE_LOCATION)) {
            requestPermissions(new String[]{Manifest.permission.ACCESS_FINE_LOCATION}, 100);
            return;
        }
        
        // Example: Place anchor at Statue of Liberty
        double latitude = 40.6892;
        double longitude = -74.0445;
        double altitude = 10.0; // 10 meters above ground
        
        // Check VPS availability
        ARGeospatialAnchor.checkVPSAvailability(mARSession, latitude, longitude,
            new ARGeospatialAnchor.VPSAvailabilityCallback() {
                @Override
                public void onResult(VPSAvailability availability) {
                    if (availability == VPSAvailability.AVAILABLE) {
                        createGeospatialAnchor(latitude, longitude, altitude);
                    } else {
                        Log.w(TAG, "VPS not available at this location");
                    }
                }
            });
    }
    
    private void createGeospatialAnchor(double lat, double lon, double alt) {
        // Create geospatial anchor
        Quaternion orientation = new Quaternion(0, 0, 0, 1);
        ARGeospatialAnchor anchor = ARGeospatialAnchor.create(
            mARSession, lat, lon, alt, orientation
        );
        
        // Create 3D object at anchor
        Node anchorNode = new Node();
        
        // Add a 3D model
        Object3D model = new Object3D();
        model.loadModel(mViroView.getViroContext(), 
            Uri.parse("file:///android_asset/models/earth.vrx"),
            Object3D.Type.VRX, new AsyncObject3DListener() {
                @Override
                public void onObject3DLoaded(Object3D object, Object3D.Type type) {
                    // Scale and position model
                    object.setScale(new Vector(0.5f, 0.5f, 0.5f));
                }
                
                @Override
                public void onObject3DFailed(String error) {
                    Log.e(TAG, "Failed to load model: " + error);
                }
            });
        
        anchorNode.addChildNode(model);
        mARScene.getRootNode().addChildNode(anchorNode);
        
        // Different altitude modes
        if (anchor.getAltitudeMode() == ARGeospatialAnchor.AltitudeMode.TERRAIN) {
            // Anchor relative to terrain
            Log.d(TAG, "Anchor placed relative to terrain");
        } else if (anchor.getAltitudeMode() == ARGeospatialAnchor.AltitudeMode.ROOFTOP) {
            // Anchor relative to rooftop
            Log.d(TAG, "Anchor placed relative to rooftop");
        }
    }
    
    /**
     * Demonstrate Scene Semantics
     */
    private void setupSceneSemantics() {
        ARSceneSemantics semantics = new ARSceneSemantics(mARSession);
        semantics.enable(true);
        
        // Set up click listener to identify objects
        mARScene.setClickListener(new ClickListener() {
            @Override
            public void onClick(int source, Node node, Vector clickLocation) {
                identifySemantics(semantics, clickLocation);
            }
            
            @Override
            public void onClickState(int source, Node node, ClickState clickState, 
                                    Vector clickLocation) {
                // Handle click states
            }
        });
    }
    
    private void identifySemantics(ARSceneSemantics semantics, Vector clickLocation) {
        // Convert 3D location to screen coordinates
        float screenX = clickLocation.x;
        float screenY = clickLocation.y;
        
        // Get semantic labels at clicked point
        List<ARSemanticLabel> labels = semantics.getLabelsAtPoint(screenX, screenY);
        
        StringBuilder detected = new StringBuilder("Detected: ");
        for (ARSemanticLabel label : labels) {
            detected.append(getLabelName(label)).append(", ");
            
            // React to specific labels
            if (label == ARSemanticLabel.PERSON) {
                highlightPerson(clickLocation);
            } else if (label == ARSemanticLabel.CAR) {
                showCarInfo(clickLocation);
            }
        }
        
        Toast.makeText(this, detected.toString(), Toast.LENGTH_SHORT).show();
    }
    
    private String getLabelName(ARSemanticLabel label) {
        switch (label) {
            case PERSON: return "Person";
            case SKY: return "Sky";
            case BUILDING: return "Building";
            case TREE: return "Tree";
            case ROAD: return "Road";
            case SIDEWALK: return "Sidewalk";
            case CAR: return "Car";
            case TERRAIN: return "Terrain";
            case VEGETATION: return "Vegetation";
            default: return "Unknown";
        }
    }
    
    private void highlightPerson(Vector location) {
        // Add highlight effect around detected person
        Particle particle = new Particle(location, 
            new Vector(1, 0, 0), // Red color
            1.0f, // Scale
            2000); // Duration in ms
        
        ParticleEmitter emitter = new ParticleEmitter(mViroView.getViroContext());
        emitter.setParticles(Arrays.asList(particle));
        emitter.setEmissionRate(100);
        
        Node particleNode = new Node();
        particleNode.setParticleEmitter(emitter);
        particleNode.setPosition(location);
        mARScene.getRootNode().addChildNode(particleNode);
    }
    
    private void showCarInfo(Vector location) {
        // Show text label above detected car
        Text carLabel = new Text(mViroView.getViroContext(), 
            "Vehicle Detected", "Roboto", 12,
            Color.WHITE, 1f, 1f, Text.HorizontalAlignment.CENTER,
            Text.VerticalAlignment.CENTER, Text.LineBreakMode.WORD_WRAP,
            Text.ClipMode.NONE, 0);
        
        Node textNode = new Node();
        textNode.setGeometry(carLabel);
        textNode.setPosition(new Vector(location.x, location.y + 0.5f, location.z));
        mARScene.getRootNode().addChildNode(textNode);
    }
    
    /**
     * Demonstrate Augmented Faces
     */
    private void setupAugmentedFaces() {
        // Enable face tracking
        mARSession.setFaceTrackingEnabled(true);
        
        // Listen for face updates
        mARSession.setARFaceListener(new ARFaceListener() {
            @Override
            public void onFaceFound(ARAugmentedFace face) {
                attachFaceEffects(face);
            }
            
            @Override
            public void onFaceUpdated(ARAugmentedFace face) {
                updateFaceEffects(face);
            }
            
            @Override
            public void onFaceLost(ARAugmentedFace face) {
                removeFaceEffects(face);
            }
        });
    }
    
    private void attachFaceEffects(ARAugmentedFace face) {
        // Get face mesh
        float[] vertices = face.getMeshVertices();
        int[] indices = face.getMeshTriangleIndices();
        float[] uvs = face.getMeshTextureCoordinates();
        
        // Create face mesh geometry
        Geometry faceMesh = new Geometry(vertices, indices, uvs);
        
        // Apply face texture/material
        Material faceMaterial = new Material();
        faceMaterial.setDiffuseTexture(new Texture(
            "file:///android_asset/textures/face_mask.png", 
            Texture.Format.RGBA8, true, true));
        faceMaterial.setTransparency(0.7f);
        faceMesh.setMaterials(Arrays.asList(faceMaterial));
        
        // Create face node
        Node faceNode = new Node();
        faceNode.setGeometry(faceMesh);
        face.setAttachedNode(faceNode);
        mARScene.getRootNode().addChildNode(faceNode);
        
        // Add accessories based on face regions
        addGlasses(face);
        addHat(face);
    }
    
    private void updateFaceEffects(ARAugmentedFace face) {
        // Update based on blend shapes
        float eyeBlinkLeft = face.getBlendShapeValue(BlendShape.EYE_BLINK_LEFT);
        float eyeBlinkRight = face.getBlendShapeValue(BlendShape.EYE_BLINK_RIGHT);
        float mouthOpen = face.getBlendShapeValue(BlendShape.MOUTH_OPEN);
        
        // Trigger effects based on expressions
        if (eyeBlinkLeft > 0.5f && eyeBlinkRight < 0.2f) {
            // Winking - trigger effect
            showWinkEffect();
        }
        
        if (mouthOpen > 0.7f) {
            // Mouth wide open - trigger effect
            showSurpriseEffect();
        }
    }
    
    private void removeFaceEffects(ARAugmentedFace face) {
        Node faceNode = face.getAttachedNode();
        if (faceNode != null) {
            mARScene.getRootNode().removeChildNode(faceNode);
        }
    }
    
    private void addGlasses(ARAugmentedFace face) {
        // Load glasses model
        Object3D glasses = new Object3D();
        glasses.loadModel(mViroView.getViroContext(),
            Uri.parse("file:///android_asset/models/glasses.vrx"),
            Object3D.Type.VRX, null);
        
        // Position at nose bridge
        Vector noseBridge = face.getRegionPose(FaceRegion.NOSE_TIP);
        glasses.setPosition(noseBridge);
        glasses.setScale(new Vector(0.1f, 0.1f, 0.1f));
        
        face.getAttachedNode().addChildNode(glasses);
    }
    
    private void addHat(ARAugmentedFace face) {
        // Load hat model
        Object3D hat = new Object3D();
        hat.loadModel(mViroView.getViroContext(),
            Uri.parse("file:///android_asset/models/hat.vrx"),
            Object3D.Type.VRX, null);
        
        // Position above forehead
        Vector forehead = face.getRegionPose(FaceRegion.FOREHEAD_TOP);
        hat.setPosition(new Vector(forehead.x, forehead.y + 0.1f, forehead.z));
        hat.setScale(new Vector(0.15f, 0.15f, 0.15f));
        
        face.getAttachedNode().addChildNode(hat);
    }
    
    /**
     * Demonstrate Cloud Anchors
     */
    private void setupCloudAnchors() {
        // Enable cloud anchors
        mARSession.setCloudAnchorProvider(CloudAnchorProvider.GOOGLE);
        
        // Long press to create cloud anchor
        mARScene.setLongClickListener(new LongClickListener() {
            @Override
            public void onLongClick(int source, Node node, Vector clickLocation) {
                createCloudAnchor(clickLocation);
            }
        });
    }
    
    private void createCloudAnchor(Vector location) {
        // Create local anchor first
        ARAnchor localAnchor = mARSession.createAnchor(location);
        
        // Add visual marker
        Box marker = new Box(0.1f, 0.1f, 0.1f);
        Material markerMaterial = new Material();
        markerMaterial.setDiffuseColor(Color.BLUE);
        marker.setMaterials(Arrays.asList(markerMaterial));
        
        Node anchorNode = new Node();
        anchorNode.setGeometry(marker);
        anchorNode.setPosition(location);
        mARScene.getRootNode().addChildNode(anchorNode);
        
        // Host in cloud
        mARSession.hostCloudAnchor(localAnchor, new CloudAnchorListener() {
            @Override
            public void onHosted(String cloudAnchorId, CloudAnchorState state) {
                if (state == CloudAnchorState.SUCCESS) {
                    // Save cloud anchor ID
                    saveCloudAnchorId(cloudAnchorId);
                    Toast.makeText(MainActivity.this, 
                        "Cloud Anchor created: " + cloudAnchorId, 
                        Toast.LENGTH_LONG).show();
                } else {
                    Log.e(TAG, "Failed to host cloud anchor: " + state);
                }
            }
            
            @Override
            public void onResolved(ARAnchor anchor, CloudAnchorState state) {
                // Not used for hosting
            }
        });
    }
    
    private void resolveCloudAnchor(String cloudAnchorId) {
        mARSession.resolveCloudAnchor(cloudAnchorId, new CloudAnchorListener() {
            @Override
            public void onHosted(String cloudAnchorId, CloudAnchorState state) {
                // Not used for resolving
            }
            
            @Override
            public void onResolved(ARAnchor anchor, CloudAnchorState state) {
                if (state == CloudAnchorState.SUCCESS) {
                    // Add content at resolved anchor
                    addContentAtAnchor(anchor);
                } else {
                    Log.e(TAG, "Failed to resolve cloud anchor: " + state);
                }
            }
        });
    }
    
    /**
     * Setup Memory Management
     */
    private void setupMemoryManagement() {
        // Get memory manager instance
        ARMemoryManager memoryManager = ARMemoryManager.getInstance();
        
        // Enable automatic optimization
        memoryManager.setAutoOptimizationEnabled(true);
        
        // Set memory limit (MB)
        memoryManager.setMemoryLimit(150);
        
        // Set garbage collection threshold
        memoryManager.setGCThreshold(100); // Trigger GC when 100+ objects
        
        // Monitor memory usage
        memoryManager.setMemoryListener(new ARMemoryListener() {
            @Override
            public void onMemoryWarning(int currentUsageMB, int limitMB) {
                Log.w(TAG, "Memory warning: " + currentUsageMB + "/" + limitMB + " MB");
                // Clean up non-essential objects
                cleanupNonEssentialObjects();
            }
            
            @Override
            public void onMemoryCritical(int currentUsageMB, int limitMB) {
                Log.e(TAG, "Memory critical: " + currentUsageMB + "/" + limitMB + " MB");
                // Force cleanup
                forceCleanup();
            }
        });
        
        // Setup performance optimizer
        ARPerformanceOptimizer optimizer = ARPerformanceOptimizer.getInstance();
        optimizer.setAdaptiveQualityEnabled(true);
        optimizer.setTargetFPS(30);
        optimizer.setQualityPreset(ARPerformanceOptimizer.QualityPreset.BALANCED);
    }
    
    private void cleanupNonEssentialObjects() {
        // Remove particles, animations, etc.
        mARScene.getRootNode().removeAllParticleEmitters();
    }
    
    private void forceCleanup() {
        // Force garbage collection
        ARMemoryManager.getInstance().forceGarbageCollection();
    }
    
    // Helper methods
    private void saveCloudAnchorId(String cloudAnchorId) {
        getSharedPreferences("ARAnchors", MODE_PRIVATE)
            .edit()
            .putString("latest_anchor", cloudAnchorId)
            .apply();
    }
    
    private void showWinkEffect() {
        // Show wink animation/effect
    }
    
    private void showSurpriseEffect() {
        // Show surprise animation/effect
    }
    
    private void addContentAtAnchor(ARAnchor anchor) {
        // Add 3D content at anchor location
    }
    
    @Override
    protected void onPause() {
        super.onPause();
        mViroView.onActivityPaused(this);
    }
    
    @Override
    protected void onResume() {
        super.onResume();
        mViroView.onActivityResumed(this);
    }
    
    @Override
    protected void onDestroy() {
        super.onDestroy();
        mViroView.onActivityDestroyed(this);
    }
}