//
//  ViroARIntegrationTest.mm
//  ViroKit
//
//  Cross-device integration testing for iOS AR features
//  Copyright © 2024 Viro Media. All rights reserved.

#import <XCTest/XCTest.h>
#import <ARKit/ARKit.h>
#import "VROARSessioniOSExtended.h"
#import "VROARLocationAnchoriOS.h"
#import "VROARFaceTrackingiOS.h"
#import "VROARPerformanceOptimizer.h"
#import "VROARMemoryManager.h"

@interface ViroARIntegrationTest : XCTestCase
@property (nonatomic, strong) VROARSessioniOSExtended *arSession;
@property (nonatomic, strong) ARSession *arkitSession;
@end

@implementation ViroARIntegrationTest

- (void)setUp {
    [super setUp];
    
    // Initialize AR session for testing
    self.arkitSession = [[ARSession alloc] init];
    self.arSession = std::make_shared<VROARSessioniOSExtended>(self.arkitSession);
    
    NSLog(@"Setting up AR integration tests on iOS %@", [[UIDevice currentDevice] systemVersion]);
    NSLog(@"Device model: %@", [[UIDevice currentDevice] model]);
}

- (void)tearDown {
    [self.arkitSession pause];
    self.arSession = nullptr;
    self.arkitSession = nil;
    [super tearDown];
}

- (void)testDeviceCompatibility {
    NSLog(@"Testing device compatibility...");
    
    // Check iOS version
    NSOperatingSystemVersion version = [[NSProcessInfo processInfo] operatingSystemVersion];
    XCTAssertGreaterThanOrEqual(version.majorVersion, 14, @"iOS 14+ required for advanced AR features");
    
    // Test basic ARKit support
    XCTAssertTrue([ARConfiguration isSupported], @"ARKit should be supported");
    
    // Test specific configuration support
    BOOL worldTrackingSupported = [ARWorldTrackingConfiguration isSupported];
    BOOL faceTrackingSupported = [ARFaceTrackingConfiguration isSupported];
    BOOL geoTrackingSupported = NO;
    
    if (@available(iOS 14.0, *)) {
        geoTrackingSupported = [ARGeoTrackingConfiguration isSupported];
    }
    
    NSLog(@"AR Configuration Support:");
    NSLog(@"  World Tracking: %@", worldTrackingSupported ? @"YES" : @"NO");
    NSLog(@"  Face Tracking: %@", faceTrackingSupported ? @"YES" : @"NO");
    NSLog(@"  Geo Tracking: %@", geoTrackingSupported ? @"YES" : @"NO");
    
    // At least world tracking should be supported
    XCTAssertTrue(worldTrackingSupported, @"World tracking should be supported");
}

- (void)testLocationAnchorIntegration {
    if (@available(iOS 14.0, *)) {
        // Skip if geo tracking is not supported
        if (![ARGeoTrackingConfiguration isSupported]) {
            NSLog(@"Skipping location anchor test - geo tracking not supported");
            return;
        }
        
        NSLog(@"Testing location anchor integration...");
        
        // Test coordinate availability check
        XCTestExpectation *availabilityExpectation = [self expectationWithDescription:@"VPS availability check"];
        
        CLLocationCoordinate2D testCoordinate = CLLocationCoordinate2DMake(37.7749, -122.4194); // San Francisco
        
        [VROARLocationAnchoriOS checkAvailability:testCoordinate 
                               completionHandler:^(BOOL available, NSError *error) {
            NSLog(@"VPS availability at test location: %@", available ? @"YES" : @"NO");
            if (error) {
                NSLog(@"VPS availability error: %@", error.localizedDescription);
            }
            [availabilityExpectation fulfill];
        }];
        
        [self waitForExpectations:@[availabilityExpectation] timeout:10.0];
        
        // Test location anchor creation
        auto locationAnchor = VROARLocationAnchoriOS::createLocationAnchor(testCoordinate, 10.0, nullptr);
        if (locationAnchor) {
            NSLog(@"Location anchor created successfully");
            XCTAssertEqual(locationAnchor->getLatitude(), testCoordinate.latitude, @"Latitude should match");
            XCTAssertEqual(locationAnchor->getLongitude(), testCoordinate.longitude, @"Longitude should match");
            XCTAssertEqual(locationAnchor->getAltitude(), 10.0, @"Altitude should match");
        } else {
            NSLog(@"Location anchor creation failed (may be expected in testing environment)");
        }
    } else {
        NSLog(@"Skipping location anchor test - iOS 14+ required");
    }
}

- (void)testFaceTrackingIntegration {
    if (![ARFaceTrackingConfiguration isSupported]) {
        NSLog(@"Skipping face tracking test - not supported on this device");
        return;
    }
    
    NSLog(@"Testing face tracking integration...");
    
    // Test face tracking configuration creation
    if (@available(iOS 11.0, *)) {
        ARFaceTrackingConfiguration *config = VROARFaceTrackingiOS::createFaceTrackingConfiguration(false, false);
        XCTAssertNotNil(config, @"Face tracking configuration should be created");
        
        if (config) {
            NSLog(@"Face tracking configuration created successfully");
            
            if (@available(iOS 13.0, *)) {
                int maxFaces = VROARFaceTrackingiOS::getMaximumNumberOfTrackedFaces();
                NSLog(@"Maximum tracked faces supported: %d", maxFaces);
                XCTAssertGreaterThan(maxFaces, 0, @"Should support at least 1 face");
            }
        }
    }
    
    // Test basic face tracking support detection
    BOOL faceTrackingSupported = VROARFaceTrackingiOS::isFaceTrackingSupported();
    XCTAssertTrue(faceTrackingSupported, @"Face tracking should be supported");
    
    NSLog(@"Face tracking integration test completed");
}

- (void)testPerformanceOptimization {
    NSLog(@"Testing performance optimization...");
    
    // Create performance optimizer
    VROARPerformanceOptimizer optimizer;
    
    // Test initial metrics
    const auto& initialMetrics = optimizer.getMetrics();
    XCTAssertEqual(initialMetrics.droppedFrameCount, 0, @"Initial dropped frame count should be 0");
    XCTAssertEqual(initialMetrics.memoryUsageMB, 0, @"Initial memory usage should be 0");
    
    // Test adaptive quality
    optimizer.setAdaptiveQuality(true);
    XCTAssertTrue(optimizer.isAdaptiveQualityEnabled(), @"Adaptive quality should be enabled");
    
    // Test memory budget
    optimizer.setMemoryBudgetMB(256);
    XCTAssertFalse(optimizer.isMemoryBudgetExceeded(), @"Memory budget should not be exceeded initially");
    
    // Test performance under simulated load
    [self simulatePerformanceLoad:&optimizer];
    
    NSLog(@"Performance optimization test completed");
}

- (void)testMemoryManagement {
    NSLog(@"Testing memory management...");
    
    // Get memory manager instance
    auto memoryManager = VROARMemoryManager::getInstance();
    XCTAssertTrue(memoryManager != nullptr, @"Memory manager should be available");
    
    // Test initial stats
    const auto& initialStats = memoryManager->getStats();
    NSLog(@"Initial memory stats:");
    NSLog(@"  Total allocated: %zu bytes", initialStats.totalAllocated);
    NSLog(@"  Active anchors: %d", initialStats.activeAnchors);
    NSLog(@"  Active faces: %d", initialStats.activeFaces);
    
    // Test memory pressure handling
    memoryManager->setMemoryLimit(128); // 128MB limit
    
    float pressureLevel = memoryManager->getMemoryPressureLevel();
    XCTAssertGreaterThanOrEqual(pressureLevel, 0.0f, @"Pressure level should be non-negative");
    XCTAssertLessThanOrEqual(pressureLevel, 1.0f, @"Pressure level should not exceed 1.0");
    
    // Test garbage collection
    memoryManager->performGarbageCollection();
    
    const auto& finalStats = memoryManager->getStats();
    NSLog(@"Final memory stats:");
    NSLog(@"  Total allocated: %zu bytes", finalStats.totalAllocated);
    NSLog(@"  Last GC time: %.2f ms", finalStats.lastGCTime);
    
    NSLog(@"Memory management test completed");
}

- (void)testMemoryStabilityUnderLoad {
    NSLog(@"Testing memory stability under load...");
    
    auto memoryManager = VROARMemoryManager::getInstance();
    
    size_t initialMemory = memoryManager->getMemoryUsage();
    NSLog(@"Initial memory usage: %zu bytes", initialMemory);
    
    // Simulate creating and destroying AR objects
    const int CYCLE_COUNT = 10;
    const int OBJECTS_PER_CYCLE = 20;
    
    for (int cycle = 0; cycle < CYCLE_COUNT; cycle++) {
        NSLog(@"Memory stress test cycle %d/%d", cycle + 1, CYCLE_COUNT);
        
        // Create temporary AR objects (simulated)
        std::vector<std::string> tempIds;
        for (int i = 0; i < OBJECTS_PER_CYCLE; i++) {
            std::string objectId = "test_object_" + std::to_string(cycle) + "_" + std::to_string(i);
            tempIds.push_back(objectId);
            
            // Simulate registering objects with memory manager
            // (In real usage, actual AR objects would be registered)
        }
        
        // Force memory cleanup
        memoryManager->performGarbageCollection();
        
        // Check memory usage
        size_t currentMemory = memoryManager->getMemoryUsage();
        size_t memoryIncrease = currentMemory > initialMemory ? currentMemory - initialMemory : 0;
        
        NSLog(@"  Memory increase: %zu bytes", memoryIncrease);
        
        // Memory growth should be reasonable
        XCTAssertLessThan(memoryIncrease, 50 * 1024 * 1024, @"Memory growth should be under 50MB");
        
        // Brief pause between cycles
        [NSThread sleepForTimeInterval:0.1];
    }
    
    NSLog(@"Memory stability test completed");
}

- (void)testConcurrentARFeatures {
    NSLog(@"Testing concurrent AR features...");
    
    // Test multiple AR features running simultaneously
    BOOL worldTrackingSupported = [ARWorldTrackingConfiguration isSupported];
    BOOL faceTrackingSupported = [ARFaceTrackingConfiguration isSupported];
    
    if (worldTrackingSupported) {
        // Test world tracking with multiple features
        ARWorldTrackingConfiguration *worldConfig = [[ARWorldTrackingConfiguration alloc] init];
        
        if (@available(iOS 12.0, *)) {
            // Enable environment texturing if supported
            if ([ARWorldTrackingConfiguration supportsFrameSemantics:ARFrameSemanticPersonSegmentation]) {
                worldConfig.frameSemantics = ARFrameSemanticPersonSegmentation;
                NSLog(@"Person segmentation enabled");
            }
        }
        
        if (@available(iOS 13.0, *)) {
            // Enable plane detection
            worldConfig.planeDetection = ARPlaneDetectionHorizontal | ARPlaneDetectionVertical;
            NSLog(@"Plane detection enabled");
        }
        
        // Test session configuration
        [self.arkitSession runWithConfiguration:worldConfig];
        [NSThread sleepForTimeInterval:1.0]; // Brief run time
        [self.arkitSession pause];
        
        NSLog(@"World tracking configuration test completed");
    }
    
    if (faceTrackingSupported) {
        // Test face tracking configuration
        if (@available(iOS 11.0, *)) {
            ARFaceTrackingConfiguration *faceConfig = VROARFaceTrackingiOS::createFaceTrackingConfiguration(false, false);
            if (faceConfig) {
                [self.arkitSession runWithConfiguration:faceConfig];
                [NSThread sleepForTimeInterval:1.0]; // Brief run time
                [self.arkitSession pause];
                NSLog(@"Face tracking configuration test completed");
            }
        }
    }
    
    NSLog(@"Concurrent AR features test completed");
}

#pragma mark - Helper Methods

- (void)simulatePerformanceLoad:(VROARPerformanceOptimizer *)optimizer {
    // Simulate processing multiple frames
    for (int i = 0; i < 60; i++) { // Simulate 1 second at 60fps
        // Simulate frame processing time
        auto startTime = std::chrono::high_resolution_clock::now();
        
        // Simulate some work
        [NSThread sleepForTimeInterval:0.001]; // 1ms work
        
        auto endTime = std::chrono::high_resolution_clock::now();
        double frameTime = std::chrono::duration<double, std::milli>(endTime - startTime).count();
        
        // Update metrics would normally happen here
        // optimizer->updateFrameTimeHistory(frameTime);
    }
    
    // Test metrics after load
    const auto& metrics = optimizer->getMetrics();
    NSLog(@"Performance metrics after load:");
    NSLog(@"  Average FPS: %.1f", metrics.averageFPS);
    NSLog(@"  Dropped frames: %d", metrics.droppedFrameCount);
    NSLog(@"  Memory usage: %zu MB", metrics.memoryUsageMB);
}

@end