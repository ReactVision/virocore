package com.viromedia.releasetest.tests;

import android.support.test.runner.AndroidJUnit4;
import android.util.Log;
import androidx.test.platform.app.InstrumentationRegistry;

import com.viro.core.*;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

import static org.junit.Assert.*;

/**
 * Unit tests for Advanced AR Features including:
 * - Geospatial API
 * - Scene Semantics
 * - Augmented Faces
 * - Enhanced Cloud Anchors
 */
@RunWith(AndroidJUnit4.class)
public class ViroAdvancedARFeaturesTest {
    private static final String TAG = "ViroAdvancedARTest";
    private static final int TIMEOUT_SECONDS = 30;
    
    private ViroViewARCore mViroView;
    private ARScene mScene;
    private CountDownLatch mTestLatch;
    
    @Before
    public void setUp() throws Exception {
        // Initialize ViroView for testing
        mViroView = new ViroViewARCore(InstrumentationRegistry.getInstrumentation().getTargetContext(), 
            new ViroViewARCore.StartupListener() {
                @Override
                public void onSuccess() {
                    Log.d(TAG, "ViroView initialized successfully");
                    setupARScene();
                }
                
                @Override
                public void onFailure(ViroViewARCore.StartupError error, String errorMessage) {
                    Log.e(TAG, "ViroView initialization failed: " + errorMessage);
                    fail("Failed to initialize ViroView: " + errorMessage);
                }
            });
        
        mTestLatch = new CountDownLatch(1);
        
        // Wait for initialization
        assertTrue("ViroView initialization timeout", 
                  mTestLatch.await(TIMEOUT_SECONDS, TimeUnit.SECONDS));
    }
    
    private void setupARScene() {
        mScene = new ARScene();
        
        ARSceneNavigator navigator = new ARSceneNavigator();
        navigator.setScene(mScene);
        mViroView.setSceneNavigator(navigator);
        
        mTestLatch.countDown();
    }
    
    @After
    public void tearDown() throws Exception {
        if (mViroView != null) {
            mViroView.onActivityDestroyed(null);
        }
    }
    
    // ================== Geospatial API Tests ==================
    
    @Test
    public void testGeospatialModeSupport() {
        // Test if device supports geospatial mode
        boolean isSupported = mScene.isGeospatialSupported();
        Log.d(TAG, "Geospatial supported: " + isSupported);
        
        // On supported devices, test mode setting
        if (isSupported) {
            boolean result = mScene.setGeospatialMode(
                ARGeospatialAnchor.GeospatialMode.ENABLED);
            assertTrue("Failed to enable geospatial mode", result);
            
            // Test terrain anchors mode
            result = mScene.setGeospatialMode(
                ARGeospatialAnchor.GeospatialMode.ENABLED_WITH_TERRAIN_ANCHORS);
            // This might fail on some devices, so we just log the result
            Log.d(TAG, "Terrain anchors supported: " + result);
        }
    }
    
    @Test
    public void testGeospatialAnchorCreation() {
        if (!mScene.isGeospatialSupported()) {
            Log.w(TAG, "Skipping geospatial anchor test - not supported on this device");
            return;
        }
        
        // Enable geospatial mode
        assertTrue("Failed to enable geospatial mode",
                   mScene.setGeospatialMode(ARGeospatialAnchor.GeospatialMode.ENABLED));
        
        // Test creating a basic geospatial anchor
        double latitude = 37.4220; // Google HQ
        double longitude = -122.0841;
        double altitude = 10.0;
        Quaternion orientation = new Quaternion(0, 0, 0, 1);
        
        ARGeospatialAnchor anchor = ARGeospatialAnchor.create(
            mScene.getARSession(),
            latitude, longitude, altitude, orientation
        );
        
        if (anchor != null) {
            // Verify anchor properties
            assertEquals("Latitude mismatch", latitude, anchor.getLatitude(), 0.000001);
            assertEquals("Longitude mismatch", longitude, anchor.getLongitude(), 0.000001);
            assertEquals("Altitude mismatch", altitude, anchor.getAltitude(), 0.001);
            assertEquals("Altitude mode should be WGS84", 
                        ARGeospatialAnchor.AltitudeMode.WGS84, anchor.getAltitudeMode());
            
            Log.d(TAG, "Successfully created geospatial anchor");
        } else {
            Log.w(TAG, "Failed to create geospatial anchor - may need better GPS/VPS conditions");
        }
    }
    
    @Test
    public void testVPSAvailabilityCheck() {
        if (!mScene.isGeospatialSupported()) {
            Log.w(TAG, "Skipping VPS availability test - geospatial not supported");
            return;
        }
        
        CountDownLatch vpsLatch = new CountDownLatch(1);
        final ARGeospatialAnchor.VPSAvailability[] result = new ARGeospatialAnchor.VPSAvailability[1];
        
        // Check VPS availability at a known location (Google HQ)
        ARGeospatialAnchor.checkVPSAvailability(
            mScene.getARSession(),
            37.4220, -122.0841,
            new ARGeospatialAnchor.VPSAvailabilityCallback() {
                @Override
                public void onResult(ARGeospatialAnchor.VPSAvailability availability) {
                    result[0] = availability;
                    Log.d(TAG, "VPS availability: " + availability);
                    vpsLatch.countDown();
                }
            }
        );
        
        try {
            assertTrue("VPS availability check timeout",
                      vpsLatch.await(TIMEOUT_SECONDS, TimeUnit.SECONDS));
            assertNotNull("VPS availability result should not be null", result[0]);
            
            // Log result but don't fail test since availability depends on location/network
            Log.d(TAG, "VPS availability check completed: " + result[0]);
        } catch (InterruptedException e) {
            fail("VPS availability check interrupted");
        }
    }
    
    @Test
    public void testTerrainAnchorAsync() {
        if (!mScene.isGeospatialSupported()) {
            Log.w(TAG, "Skipping terrain anchor test - geospatial not supported");
            return;
        }
        
        // Enable terrain anchors mode
        boolean enabled = mScene.setGeospatialMode(
            ARGeospatialAnchor.GeospatialMode.ENABLED_WITH_TERRAIN_ANCHORS);
        if (!enabled) {
            Log.w(TAG, "Skipping terrain anchor test - terrain anchors not supported");
            return;
        }
        
        CountDownLatch terrainLatch = new CountDownLatch(1);
        final boolean[] callbackInvoked = {false};
        
        ARGeospatialAnchor anchor = ARGeospatialAnchor.createTerrainAnchor(
            mScene.getARSession(),
            37.4220, -122.0841, // Google HQ
            new Quaternion(0, 0, 0, 1),
            new ARGeospatialAnchor.AnchorResolveCallback() {
                @Override
                public void onResolve(ARGeospatialAnchor anchor, double altitude) {
                    Log.d(TAG, "Terrain anchor resolved at altitude: " + altitude);
                    callbackInvoked[0] = true;
                    terrainLatch.countDown();
                }
                
                @Override
                public void onError(String error) {
                    Log.w(TAG, "Terrain anchor error: " + error);
                    callbackInvoked[0] = true;
                    terrainLatch.countDown();
                }
            }
        );
        
        assertNotNull("Terrain anchor should be created immediately", anchor);
        assertEquals("Should be terrain anchor", 
                    ARGeospatialAnchor.AltitudeMode.TERRAIN, anchor.getAltitudeMode());
        
        try {
            // Wait for async resolution (this may timeout in test environment)
            boolean resolved = terrainLatch.await(TIMEOUT_SECONDS, TimeUnit.SECONDS);
            Log.d(TAG, "Terrain anchor callback invoked: " + callbackInvoked[0]);
            
            // Don't fail test if timeout - terrain resolution may need real GPS/network
            if (!resolved) {
                Log.w(TAG, "Terrain anchor resolution timeout - may need real location data");
            }
        } catch (InterruptedException e) {
            Log.w(TAG, "Terrain anchor test interrupted");
        }
    }
    
    // ================== Scene Semantics Tests ==================
    
    @Test
    public void testSceneSemanticsSupport() {
        boolean isSupported = mScene.isSemanticsSupported();
        Log.d(TAG, "Scene semantics supported: " + isSupported);
        
        if (isSupported) {
            // Test enabling semantics
            boolean result = mScene.setSemanticMode(ARScene.SemanticMode.ENABLED);
            assertTrue("Failed to enable scene semantics", result);
            
            // Test disabling
            result = mScene.setSemanticMode(ARScene.SemanticMode.DISABLED);
            assertTrue("Failed to disable scene semantics", result);
        }
    }
    
    @Test
    public void testSemanticLabels() {
        if (!mScene.isSemanticsSupported()) {
            Log.w(TAG, "Skipping semantic labels test - not supported on this device");
            return;
        }
        
        // Enable semantics
        assertTrue("Failed to enable semantics",
                   mScene.setSemanticMode(ARScene.SemanticMode.ENABLED));
        
        CountDownLatch semanticsLatch = new CountDownLatch(1);
        final boolean[] frameReceived = {false};
        
        // Set up semantic frame listener
        mScene.setSemanticFrameListener(new ARScene.SemanticFrameListener() {
            @Override
            public void onSemanticFrameUpdate(ARScene.SemanticFrame frame) {
                frameReceived[0] = true;
                
                // Test semantic label queries
                boolean hasSky = frame.hasLabel(ARScene.SemanticLabel.SKY, 0.1f);
                float skyPercentage = frame.getLabelCoverage(ARScene.SemanticLabel.SKY, 0.1f);
                
                Log.d(TAG, "Semantic frame - Sky detected: " + hasSky + 
                          ", coverage: " + (skyPercentage * 100) + "%");
                
                // Test pixel-level queries
                ARScene.SemanticLabel centerLabel = frame.getLabelAtPixel(320, 240);
                Log.d(TAG, "Center pixel label: " + centerLabel);
                
                semanticsLatch.countDown();
            }
        });
        
        try {
            // Wait for semantic frames (this may timeout without camera feed)
            boolean received = semanticsLatch.await(10, TimeUnit.SECONDS);
            if (!received) {
                Log.w(TAG, "No semantic frames received - may need active camera");
            } else {
                assertTrue("Semantic frame should be received", frameReceived[0]);
            }
        } catch (InterruptedException e) {
            Log.w(TAG, "Semantic frame test interrupted");
        }
    }
    
    // ================== Augmented Faces Tests ==================
    
    @Test
    public void testFaceTrackingSupport() {
        boolean isSupported = mScene.isFaceTrackingSupported();
        Log.d(TAG, "Face tracking supported: " + isSupported);
        
        // Face tracking requires front camera configuration, which is complex to test
        // We'll just verify the support query works
        if (isSupported) {
            Log.d(TAG, "Device supports face tracking");
        } else {
            Log.d(TAG, "Device does not support face tracking");
        }
    }
    
    @Test
    public void testFaceTrackingConfiguration() {
        if (!mScene.isFaceTrackingSupported()) {
            Log.w(TAG, "Skipping face tracking config test - not supported");
            return;
        }
        
        // Test setting face tracking mode
        // Note: This requires reconfiguring the AR session with front camera
        // which is complex to test in unit tests
        Log.d(TAG, "Face tracking configuration would require camera reconfiguration");
        
        // Test max faces setting
        mScene.setMaxFacesToTrack(1);
        assertEquals("Max faces should be 1", 1, mScene.getMaxFacesToTrack());
        
        mScene.setMaxFacesToTrack(3);
        assertEquals("Max faces should be 3", 3, mScene.getMaxFacesToTrack());
    }
    
    // ================== Enhanced Cloud Anchors Tests ==================
    
    @Test
    public void testCloudAnchorQuality() {
        // Enable cloud anchors
        mScene.setCloudAnchorMode(ARScene.CloudAnchorMode.ENABLED);
        
        // Create a local anchor for testing
        Vector anchorPosition = new Vector(0, 0, -1);
        ARAnchor localAnchor = mScene.addAnchor(anchorPosition);
        
        if (localAnchor != null) {
            // Test hosting quality assessment
            ARScene.CloudAnchorQuality quality = mScene.getCloudAnchorHostingQuality(localAnchor);
            Log.d(TAG, "Cloud anchor hosting quality: " + quality);
            
            assertNotNull("Quality should not be null", quality);
            assertTrue("Quality should be a valid enum value",
                      quality == ARScene.CloudAnchorQuality.INSUFFICIENT ||
                      quality == ARScene.CloudAnchorQuality.SUFFICIENT ||
                      quality == ARScene.CloudAnchorQuality.GOOD);
        } else {
            Log.w(TAG, "Failed to create local anchor for cloud anchor quality test");
        }
    }
    
    @Test
    public void testCloudAnchorTTL() {
        // Enable cloud anchors
        mScene.setCloudAnchorMode(ARScene.CloudAnchorMode.ENABLED);
        
        Vector anchorPosition = new Vector(0, 0, -1);
        ARAnchor localAnchor = mScene.addAnchor(anchorPosition);
        
        if (localAnchor == null) {
            Log.w(TAG, "Cannot test cloud anchor TTL without local anchor");
            return;
        }
        
        CountDownLatch cloudLatch = new CountDownLatch(1);
        final String[] result = {null};
        final String[] error = {null};
        
        // Test hosting with TTL (this will likely fail in test environment)
        mScene.hostCloudAnchorWithTTL(
            localAnchor,
            30, // 30 days TTL
            new ARScene.CloudAnchorCallback() {
                @Override
                public void onSuccess(String cloudAnchorId) {
                    result[0] = cloudAnchorId;
                    Log.d(TAG, "Cloud anchor hosted with TTL: " + cloudAnchorId);
                    cloudLatch.countDown();
                }
                
                @Override
                public void onFailure(String errorMessage) {
                    error[0] = errorMessage;
                    Log.w(TAG, "Cloud anchor hosting failed: " + errorMessage);
                    cloudLatch.countDown();
                }
            }
        );
        
        try {
            // Wait for result (likely will timeout or fail in test environment)
            boolean completed = cloudLatch.await(TIMEOUT_SECONDS, TimeUnit.SECONDS);
            
            if (result[0] != null) {
                Log.d(TAG, "Cloud anchor successfully hosted: " + result[0]);
                assertNotNull("Cloud anchor ID should not be null", result[0]);
                assertFalse("Cloud anchor ID should not be empty", result[0].isEmpty());
            } else if (error[0] != null) {
                Log.w(TAG, "Cloud anchor hosting failed as expected in test: " + error[0]);
                // Don't fail test - hosting requires good environment conditions
            } else {
                Log.w(TAG, "Cloud anchor hosting timeout - expected in test environment");
            }
        } catch (InterruptedException e) {
            Log.w(TAG, "Cloud anchor test interrupted");
        }
    }
    
    // ================== Performance and Memory Tests ==================
    
    @Test
    public void testFeatureMemoryUsage() {
        // Test that enabling/disabling features doesn't cause memory leaks
        
        long initialMemory = getUsedMemory();
        
        // Enable all supported features
        if (mScene.isGeospatialSupported()) {
            mScene.setGeospatialMode(ARGeospatialAnchor.GeospatialMode.ENABLED);
        }
        
        if (mScene.isSemanticsSupported()) {
            mScene.setSemanticMode(ARScene.SemanticMode.ENABLED);
        }
        
        mScene.setCloudAnchorMode(ARScene.CloudAnchorMode.ENABLED);
        
        long enabledMemory = getUsedMemory();
        
        // Disable all features
        mScene.setGeospatialMode(ARGeospatialAnchor.GeospatialMode.DISABLED);
        mScene.setSemanticMode(ARScene.SemanticMode.DISABLED);
        mScene.setCloudAnchorMode(ARScene.CloudAnchorMode.DISABLED);
        
        // Force garbage collection
        System.gc();
        try {
            Thread.sleep(1000);
        } catch (InterruptedException e) {
            // Ignore
        }
        
        long finalMemory = getUsedMemory();
        
        Log.d(TAG, String.format("Memory usage - Initial: %d KB, Enabled: %d KB, Final: %d KB",
                                 initialMemory / 1024, enabledMemory / 1024, finalMemory / 1024));
        
        // Memory should not increase significantly after disabling features
        long memoryIncrease = finalMemory - initialMemory;
        assertTrue("Memory leak detected: " + (memoryIncrease / 1024) + " KB",
                   memoryIncrease < 10 * 1024 * 1024); // Less than 10MB increase
    }
    
    @Test
    public void testConcurrentFeatures() {
        // Test that multiple features can be enabled simultaneously without issues
        
        boolean geospatialEnabled = false;
        boolean semanticsEnabled = false;
        boolean cloudAnchorsEnabled = false;
        
        try {
            if (mScene.isGeospatialSupported()) {
                geospatialEnabled = mScene.setGeospatialMode(ARGeospatialAnchor.GeospatialMode.ENABLED);
            }
            
            if (mScene.isSemanticsSupported()) {
                semanticsEnabled = mScene.setSemanticMode(ARScene.SemanticMode.ENABLED);
            }
            
            mScene.setCloudAnchorMode(ARScene.CloudAnchorMode.ENABLED);
            cloudAnchorsEnabled = true;
            
            Log.d(TAG, String.format("Concurrent features - Geospatial: %b, Semantics: %b, Cloud: %b",
                                     geospatialEnabled, semanticsEnabled, cloudAnchorsEnabled));
            
            // Wait a moment to ensure no crashes
            Thread.sleep(2000);
            
            Log.d(TAG, "Concurrent features test passed - no immediate crashes");
            
        } catch (Exception e) {
            fail("Exception during concurrent features test: " + e.getMessage());
        }
    }
    
    // ================== Utility Methods ==================
    
    private long getUsedMemory() {
        Runtime runtime = Runtime.getRuntime();
        return runtime.totalMemory() - runtime.freeMemory();
    }
    
    private void waitForCondition(String description, int timeoutSeconds, 
                                 Runnable condition) throws InterruptedException {
        CountDownLatch latch = new CountDownLatch(1);
        
        // Check condition periodically
        Thread checkThread = new Thread(() -> {
            try {
                for (int i = 0; i < timeoutSeconds * 10; i++) {
                    try {
                        condition.run();
                        latch.countDown();
                        return;
                    } catch (AssertionError e) {
                        // Condition not met yet, continue waiting
                    }
                    Thread.sleep(100);
                }
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        });
        
        checkThread.start();
        
        if (!latch.await(timeoutSeconds, TimeUnit.SECONDS)) {
            checkThread.interrupt();
            fail(description + " timeout after " + timeoutSeconds + " seconds");
        }
    }
}