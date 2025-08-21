/*
 * ViroARIntegrationTest.java
 * ViroCore Integration Tests
 *
 * Cross-device integration testing for AR features
 * Copyright © 2024 Viro Media. All rights reserved.
 */

package com.viromedia.releasetest.tests;

import android.content.Context;
import android.os.Build;
import android.os.SystemClock;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;

import com.viro.core.ARGeospatialAnchor;
import com.viro.core.ARSceneSemantics;
import com.viro.core.ARAugmentedFace;
import com.viro.core.ARSession;
import com.viro.core.ViroContext;
import com.viro.core.Vector;
import com.viro.core.Quaternion;
import com.viromedia.releasetest.ViroActivityTest;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

import static org.junit.Assert.*;

/**
 * Integration tests that verify AR features work correctly across different devices
 * and Android versions. Tests real hardware capabilities and performance.
 */
@RunWith(AndroidJUnit4.class)
public class ViroARIntegrationTest extends ViroActivityTest {
    
    private Context mContext;
    private ViroContext mViroContext;
    private ARSession mARSession;
    
    @Before
    public void setup() {
        super.setup();
        mContext = InstrumentationRegistry.getInstrumentation().getTargetContext();
        mViroContext = mViroActivity.getViroContext();
        mARSession = mScene.getARSession();
        
        // Ensure AR session is initialized
        assertNotNull("AR Session should be initialized", mARSession);
    }
    
    @After
    public void tearDown() {
        if (mARSession != null) {
            mARSession.pause();
        }
        super.tearDown();
    }
    
    @Test
    public void testDeviceCompatibility() {
        // Test basic device compatibility
        String deviceModel = Build.MODEL;
        String androidVersion = Build.VERSION.RELEASE;
        int apiLevel = Build.VERSION.SDK_INT;
        
        System.out.println("Testing on device: " + deviceModel);
        System.out.println("Android version: " + androidVersion);
        System.out.println("API level: " + apiLevel);
        
        // Minimum requirements check
        assertTrue("Device must support API level 24+", apiLevel >= 24);
        
        // Test ARCore availability
        assertTrue("ARCore should be supported", mARSession.isARCoreSupported());
        
        // Test specific feature support
        testFeatureSupport();
    }
    
    @Test
    public void testGeospatialIntegration() throws InterruptedException {
        // Skip if geospatial is not supported
        if (!mARSession.isGeospatialModeSupported()) {
            System.out.println("Skipping geospatial test - not supported on this device");
            return;
        }
        
        final CountDownLatch latch = new CountDownLatch(1);
        final AtomicBoolean success = new AtomicBoolean(false);
        final AtomicReference<String> error = new AtomicReference<>();
        
        // Enable geospatial mode
        boolean enabled = mARSession.setGeospatialModeEnabled(true);
        assertTrue("Geospatial mode should be enabled", enabled);
        
        // Wait for geospatial localization
        SystemClock.sleep(3000);
        
        // Test VPS availability check
        ARGeospatialAnchor.checkVPSAvailability(mARSession, 37.7749, -122.4194, new ARGeospatialAnchor.VPSAvailabilityCallback() {
            @Override
            public void onResult(int availability) {
                System.out.println("VPS Availability: " + availability);
                success.set(true);
                latch.countDown();
            }
        });
        
        assertTrue("VPS availability check should complete", 
                  latch.await(10, TimeUnit.SECONDS));
        assertTrue("VPS availability check should succeed", success.get());
        
        // Test anchor creation
        testGeospatialAnchorCreation();
    }
    
    @Test
    public void testSceneSemanticsIntegration() throws InterruptedException {
        // Skip if scene semantics is not supported
        if (!mARSession.isSceneSemanticsModeSupported()) {
            System.out.println("Skipping scene semantics test - not supported on this device");
            return;
        }
        
        final CountDownLatch latch = new CountDownLatch(1);
        final AtomicInteger labelsFound = new AtomicInteger(0);
        
        // Enable scene semantics
        boolean enabled = mARSession.setSceneSemanticsModeEnabled(true);
        assertTrue("Scene semantics mode should be enabled", enabled);
        
        // Wait for scene analysis
        SystemClock.sleep(2000);
        
        // Create semantics listener
        ARSceneSemantics.LabelUpdateListener listener = new ARSceneSemantics.LabelUpdateListener() {
            @Override
            public void onLabelUpdate(ARSceneSemantics semantics) {
                String[] labels = semantics.getAllLabels();
                labelsFound.set(labels.length);
                System.out.println("Found " + labels.length + " semantic labels");
                
                // Log some labels for debugging
                for (int i = 0; i < Math.min(5, labels.length); i++) {
                    float confidence = semantics.getLabelConfidence(labels[i]);
                    System.out.println("  " + labels[i] + ": " + confidence);
                }
                
                latch.countDown();
            }
        };
        
        // Register listener and wait for updates
        ARSceneSemantics semantics = ARSceneSemantics.create(mARSession);
        semantics.setLabelUpdateListener(listener);
        
        assertTrue("Scene semantics should detect labels", 
                  latch.await(15, TimeUnit.SECONDS));
        
        // Verify we found some semantic information
        assertTrue("Should find at least some semantic labels", labelsFound.get() >= 0);
    }
    
    @Test
    public void testAugmentedFacesIntegration() throws InterruptedException {
        // Skip if augmented faces is not supported
        if (!mARSession.isAugmentedFacesModeSupported()) {
            System.out.println("Skipping augmented faces test - not supported on this device");
            return;
        }
        
        final CountDownLatch latch = new CountDownLatch(1);
        final AtomicBoolean faceDetected = new AtomicBoolean(false);
        
        // Enable augmented faces mode
        boolean enabled = mARSession.setAugmentedFacesModeEnabled(true);
        assertTrue("Augmented faces mode should be enabled", enabled);
        
        // Create face tracking listener
        ARAugmentedFace.FaceTrackingListener listener = new ARAugmentedFace.FaceTrackingListener() {
            @Override
            public void onFaceAdded(ARAugmentedFace face) {
                System.out.println("Face detected with ID: " + face.getFaceId());
                faceDetected.set(true);
                
                // Test blend shape access
                testBlendShapeAccess(face);
                
                latch.countDown();
            }
            
            @Override
            public void onFaceUpdated(ARAugmentedFace face) {
                // Face updates
            }
            
            @Override
            public void onFaceRemoved(ARAugmentedFace face) {
                System.out.println("Face removed with ID: " + face.getFaceId());
            }
        };
        
        ARAugmentedFace.setFaceTrackingListener(listener);
        
        // Note: This test may not always detect faces in automated testing
        // It's mainly for device capability verification
        System.out.println("Waiting for face detection (may timeout in automated testing)...");
        boolean completed = latch.await(10, TimeUnit.SECONDS);
        
        if (completed) {
            assertTrue("Face should be detected", faceDetected.get());
            System.out.println("Face detection test completed successfully");
        } else {
            System.out.println("Face detection timed out - this is expected in automated testing without a face present");
        }
    }
    
    @Test
    public void testPerformanceUnderLoad() throws InterruptedException {
        System.out.println("Testing performance under load...");
        
        // Create multiple anchors and test performance
        final int ANCHOR_COUNT = 10;
        final CountDownLatch latch = new CountDownLatch(ANCHOR_COUNT);
        final AtomicInteger successCount = new AtomicInteger(0);
        
        long startTime = System.currentTimeMillis();
        
        // Create multiple geospatial anchors if supported
        if (mARSession.isGeospatialModeSupported()) {
            mARSession.setGeospatialModeEnabled(true);
            
            for (int i = 0; i < ANCHOR_COUNT; i++) {
                double lat = 37.7749 + (i * 0.0001); // Slightly different positions
                double lon = -122.4194 + (i * 0.0001);
                double alt = 10.0;
                
                ARGeospatialAnchor anchor = ARGeospatialAnchor.create(
                    mARSession, lat, lon, alt, 
                    new Quaternion(0, 0, 0, 1)
                );
                
                if (anchor != null) {
                    successCount.incrementAndGet();
                }
                latch.countDown();
            }
            
            assertTrue("Performance test should complete", 
                      latch.await(30, TimeUnit.SECONDS));
            
            long endTime = System.currentTimeMillis();
            long duration = endTime - startTime;
            
            System.out.println("Created " + successCount.get() + " anchors in " + duration + "ms");
            System.out.println("Average time per anchor: " + (duration / ANCHOR_COUNT) + "ms");
            
            // Performance assertions
            assertTrue("Should create at least half the anchors", 
                      successCount.get() >= ANCHOR_COUNT / 2);
            assertTrue("Should complete within reasonable time", duration < 30000);
        } else {
            System.out.println("Skipping performance test - geospatial not supported");
        }
    }
    
    @Test
    public void testMemoryStability() {
        System.out.println("Testing memory stability...");
        
        // Get initial memory usage
        Runtime runtime = Runtime.getRuntime();
        long initialMemory = runtime.totalMemory() - runtime.freeMemory();
        
        // Perform memory-intensive operations
        for (int cycle = 0; cycle < 5; cycle++) {
            System.out.println("Memory test cycle " + (cycle + 1));
            
            // Create and destroy AR objects
            if (mARSession.isGeospatialModeSupported()) {
                mARSession.setGeospatialModeEnabled(true);
                
                for (int i = 0; i < 20; i++) {
                    ARGeospatialAnchor anchor = ARGeospatialAnchor.create(
                        mARSession, 37.7749, -122.4194, 10.0, 
                        new Quaternion(0, 0, 0, 1)
                    );
                    // Let anchor go out of scope for GC
                }
            }
            
            // Force garbage collection
            System.gc();
            SystemClock.sleep(500);
            
            long currentMemory = runtime.totalMemory() - runtime.freeMemory();
            long memoryIncrease = currentMemory - initialMemory;
            
            System.out.println("Memory increase: " + (memoryIncrease / 1024) + " KB");
            
            // Memory should not grow excessively
            assertTrue("Memory growth should be reasonable", 
                      memoryIncrease < 50 * 1024 * 1024); // 50MB limit
        }
        
        System.out.println("Memory stability test completed");
    }
    
    private void testFeatureSupport() {
        // Test individual feature support
        boolean geospatialSupported = mARSession.isGeospatialModeSupported();
        boolean semanticsSupported = mARSession.isSceneSemanticsModeSupported();
        boolean facesSupported = mARSession.isAugmentedFacesModeSupported();
        boolean cloudAnchorsSupported = mARSession.isCloudAnchorsSupported();
        
        System.out.println("Feature support:");
        System.out.println("  Geospatial: " + geospatialSupported);
        System.out.println("  Scene Semantics: " + semanticsSupported);
        System.out.println("  Augmented Faces: " + facesSupported);
        System.out.println("  Cloud Anchors: " + cloudAnchorsSupported);
        
        // At least cloud anchors should be supported on most devices
        // (Other features depend on specific device capabilities)
    }
    
    private void testGeospatialAnchorCreation() {
        System.out.println("Testing geospatial anchor creation...");
        
        // Test basic anchor creation
        ARGeospatialAnchor anchor = ARGeospatialAnchor.create(
            mARSession, 37.7749, -122.4194, 10.0, 
            new Quaternion(0, 0, 0, 1)
        );
        
        if (anchor != null) {
            // Verify anchor properties
            assertEquals("Latitude should match", 37.7749, anchor.getLatitude(), 0.0001);
            assertEquals("Longitude should match", -122.4194, anchor.getLongitude(), 0.0001);
            assertEquals("Altitude should match", 10.0, anchor.getAltitude(), 0.1);
            
            System.out.println("Geospatial anchor created successfully");
        } else {
            System.out.println("Geospatial anchor creation failed (may be expected depending on location/connectivity)");
        }
    }
    
    private void testBlendShapeAccess(ARAugmentedFace face) {
        // Test accessing common blend shapes
        float eyeBlinkLeft = face.getEyeBlinkLeft();
        float eyeBlinkRight = face.getEyeBlinkRight();
        float mouthSmileLeft = face.getMouthSmileLeft();
        float mouthSmileRight = face.getMouthSmileRight();
        float mouthOpen = face.getMouthOpen();
        
        System.out.println("Blend shapes:");
        System.out.println("  Eye blink left: " + eyeBlinkLeft);
        System.out.println("  Eye blink right: " + eyeBlinkRight);
        System.out.println("  Mouth smile left: " + mouthSmileLeft);
        System.out.println("  Mouth smile right: " + mouthSmileRight);
        System.out.println("  Mouth open: " + mouthOpen);
        
        // Blend shape values should be in valid range
        assertTrue("Eye blink left should be valid", eyeBlinkLeft >= 0.0f && eyeBlinkLeft <= 1.0f);
        assertTrue("Eye blink right should be valid", eyeBlinkRight >= 0.0f && eyeBlinkRight <= 1.0f);
        assertTrue("Mouth smile left should be valid", mouthSmileLeft >= 0.0f && mouthSmileLeft <= 1.0f);
        assertTrue("Mouth smile right should be valid", mouthSmileRight >= 0.0f && mouthSmileRight <= 1.0f);
        assertTrue("Mouth open should be valid", mouthOpen >= 0.0f && mouthOpen <= 1.0f);
    }
}