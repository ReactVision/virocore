//
//  VROARAdvancedViewController.mm
//  ViroSample
//
//  Sample view controller demonstrating advanced AR features in iOS
//  Copyright © 2024 Viro Media. All rights reserved.

#import <UIKit/UIKit.h>
#import <ViroKit/ViroKit.h>
#import <CoreLocation/CoreLocation.h>
#import "VROARSessioniOSExtended.h"
#import "VROARLocationAnchoriOS.h"
#import "VROARSceneUnderstandingiOS.h"
#import "VROARFaceTrackingiOS.h"

@interface VROARAdvancedViewController : UIViewController <VROViewControllerPresenter>

@property (nonatomic, strong) VROViewAR *vroView;
@property (nonatomic, strong) VROARSceneController *arSceneController;
@property (nonatomic, assign) std::shared_ptr<VROARSessioniOSExtended> extendedSession;
@property (nonatomic, strong) CLLocationManager *locationManager;

// Feature flags
@property (nonatomic, assign) BOOL geoTrackingEnabled;
@property (nonatomic, assign) BOOL sceneUnderstandingEnabled;
@property (nonatomic, assign) BOOL faceTrackingEnabled;
@property (nonatomic, assign) BOOL bodyTrackingEnabled;

@end

@implementation VROARAdvancedViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    // Create extended AR view with advanced features
    [self setupARView];
    [self setupLocationServices];
    [self setupUI];
}

- (void)setupARView {
    // Create AR view with extended configuration
    VRORendererConfiguration config;
    config.enableHDR = YES;
    config.enablePBR = YES;
    config.enableBloom = YES;
    config.enableShadows = YES;
    
    EAGLContext *context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
    
    _vroView = [[VROViewAR alloc] initWithFrame:self.view.bounds
                                          config:config
                                         context:context
                                  worldAlignment:VROWorldAlignment::Gravity
                                    trackingType:VROTrackingType::DOF6];
    
    _vroView.presenter = self;
    [self.view addSubview:_vroView];
    
    // Get extended session
    if (@available(iOS 14.0, *)) {
        auto renderer = std::dynamic_pointer_cast<VRORendererAR>(_vroView.renderer);
        if (renderer) {
            _extendedSession = std::make_shared<VROARSessioniOSExtended>(
                VROTrackingType::DOF6,
                VROWorldAlignment::Gravity,
                renderer->getDriver()
            );
            renderer->setARSession(_extendedSession);
        }
    }
}

- (void)setupLocationServices {
    _locationManager = [[CLLocationManager alloc] init];
    _locationManager.delegate = self;
    [_locationManager requestWhenInUseAuthorization];
}

- (void)setupUI {
    // Add UI buttons for features
    UIStackView *stackView = [[UIStackView alloc] init];
    stackView.axis = UILayoutConstraintAxisVertical;
    stackView.spacing = 10;
    stackView.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:stackView];
    
    [NSLayoutConstraint activateConstraints:@[
        [stackView.topAnchor constraintEqualToAnchor:self.view.safeAreaLayoutGuide.topAnchor constant:20],
        [stackView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:20]
    ]];
    
    // Add feature buttons
    [stackView addArrangedSubview:[self createButton:@"Geo Tracking" action:@selector(toggleGeoTracking)]];
    [stackView addArrangedSubview:[self createButton:@"Scene Understanding" action:@selector(toggleSceneUnderstanding)]];
    [stackView addArrangedSubview:[self createButton:@"Face Tracking" action:@selector(toggleFaceTracking)]];
    [stackView addArrangedSubview:[self createButton:@"Body Tracking" action:@selector(toggleBodyTracking)]];
    [stackView addArrangedSubview:[self createButton:@"Place Geo Anchor" action:@selector(placeGeoAnchor)]];
    [stackView addArrangedSubview:[self createButton:@"Start Recording" action:@selector(toggleRecording)]];
}

- (UIButton *)createButton:(NSString *)title action:(SEL)action {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    [button setTitle:title forState:UIControlStateNormal];
    [button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    button.backgroundColor = [UIColor colorWithWhite:0.9 alpha:0.9];
    button.layer.cornerRadius = 5;
    button.contentEdgeInsets = UIEdgeInsetsMake(10, 20, 10, 20);
    return button;
}

#pragma mark - Feature Toggles

- (void)toggleGeoTracking {
    if (@available(iOS 14.0, *)) {
        if (!_geoTrackingEnabled) {
            if (_extendedSession->enableGeoTracking()) {
                _geoTrackingEnabled = YES;
                NSLog(@"Geo tracking enabled");
                
                // Monitor geo tracking state
                [self monitorGeoTrackingState];
            } else {
                [self showAlert:@"Geo tracking not supported at this location"];
            }
        } else {
            _geoTrackingEnabled = NO;
            NSLog(@"Geo tracking disabled");
        }
    }
}

- (void)toggleSceneUnderstanding {
    if (@available(iOS 13.4, *)) {
        if (!_sceneUnderstandingEnabled) {
            if (_extendedSession->enableSceneUnderstanding(true, true, true)) {
                _sceneUnderstandingEnabled = YES;
                NSLog(@"Scene understanding enabled");
                
                // Set up scene semantics processing
                [self processSceneSemantics];
            } else {
                [self showAlert:@"Scene understanding requires LiDAR sensor"];
            }
        } else {
            _sceneUnderstandingEnabled = NO;
            _extendedSession->enableSceneUnderstanding(false, false, false);
            NSLog(@"Scene understanding disabled");
        }
    }
}

- (void)toggleFaceTracking {
    if (@available(iOS 11.0, *)) {
        if (!_faceTrackingEnabled) {
            if (_extendedSession->enableFaceTracking(true)) {
                _faceTrackingEnabled = YES;
                NSLog(@"Face tracking enabled");
                
                // Set up face tracking delegate
                [self setupFaceTrackingDelegate];
            } else {
                [self showAlert:@"Face tracking requires TrueDepth camera"];
            }
        } else {
            _faceTrackingEnabled = NO;
            _extendedSession->enableFaceTracking(false);
            NSLog(@"Face tracking disabled");
        }
    }
}

- (void)toggleBodyTracking {
    if (@available(iOS 13.0, *)) {
        if (!_bodyTrackingEnabled) {
            if (_extendedSession->enableBodyTracking(true, true)) {
                _bodyTrackingEnabled = YES;
                NSLog(@"Body tracking enabled");
                
                // Process body tracking data
                [self processBodyTracking];
            } else {
                [self showAlert:@"Body tracking requires A12+ processor"];
            }
        } else {
            _bodyTrackingEnabled = NO;
            _extendedSession->enableBodyTracking(false, false);
            NSLog(@"Body tracking disabled");
        }
    }
}

#pragma mark - Geo Anchors

- (void)placeGeoAnchor {
    if (@available(iOS 14.0, *)) {
        if (!_geoTrackingEnabled) {
            [self showAlert:@"Enable geo tracking first"];
            return;
        }
        
        // Get current location
        CLLocation *currentLocation = _locationManager.location;
        if (!currentLocation) {
            [self showAlert:@"Location not available"];
            return;
        }
        
        // Create geo anchor 10 meters north
        CLLocationCoordinate2D anchorCoordinate = currentLocation.coordinate;
        anchorCoordinate.latitude += 0.00009; // ~10 meters north
        
        auto geoAnchor = _extendedSession->createGeoAnchor(
            anchorCoordinate,
            currentLocation.altitude + 1.0, // 1 meter above ground
            nullptr // Default orientation
        );
        
        if (geoAnchor) {
            // Create 3D content at the anchor
            [self addContentToGeoAnchor:geoAnchor];
            NSLog(@"Geo anchor placed at %.6f, %.6f", 
                  anchorCoordinate.latitude, anchorCoordinate.longitude);
        }
    }
}

- (void)addContentToGeoAnchor:(std::shared_ptr<VROARLocationAnchoriOS>)geoAnchor {
    // Create AR node for the anchor
    std::shared_ptr<VROARNode> arNode = std::make_shared<VROARNode>();
    arNode->setAnchor(geoAnchor);
    
    // Create 3D box as marker
    std::shared_ptr<VROBox> box = VROBox::createBox(0.2, 0.2, 0.2);
    
    // Create material
    std::shared_ptr<VROMaterial> material = std::make_shared<VROMaterial>();
    material->getDiffuse().setColor(VROVector4f(0, 0, 1, 1)); // Blue
    box->setMaterials({material});
    
    // Add to scene
    arNode->setGeometry(box);
    _arSceneController->getScene()->getRootNode()->addChildNode(arNode);
}

- (void)monitorGeoTrackingState {
    if (@available(iOS 14.0, *)) {
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            while (self.geoTrackingEnabled) {
                ARGeoTrackingState state = self.extendedSession->getGeoTrackingState();
                ARGeoTrackingAccuracy accuracy = self.extendedSession->getGeoTrackingAccuracy();
                
                dispatch_async(dispatch_get_main_queue(), ^{
                    [self updateGeoTrackingUI:state accuracy:accuracy];
                });
                
                [NSThread sleepForTimeInterval:1.0];
            }
        });
    }
}

- (void)updateGeoTrackingUI:(ARGeoTrackingState)state accuracy:(ARGeoTrackingAccuracy)accuracy {
    NSString *stateStr = @"Unknown";
    NSString *accuracyStr = @"Unknown";
    
    if (@available(iOS 14.0, *)) {
        switch (state) {
            case ARGeoTrackingStateNotAvailable:
                stateStr = @"Not Available";
                break;
            case ARGeoTrackingStateInitializing:
                stateStr = @"Initializing";
                break;
            case ARGeoTrackingStateLocalizing:
                stateStr = @"Localizing";
                break;
            case ARGeoTrackingStateLocalized:
                stateStr = @"Localized";
                break;
        }
        
        switch (accuracy) {
            case ARGeoTrackingAccuracyUndetermined:
                accuracyStr = @"Undetermined";
                break;
            case ARGeoTrackingAccuracyLow:
                accuracyStr = @"Low";
                break;
            case ARGeoTrackingAccuracyMedium:
                accuracyStr = @"Medium";
                break;
            case ARGeoTrackingAccuracyHigh:
                accuracyStr = @"High";
                break;
        }
    }
    
    NSLog(@"Geo Tracking - State: %@, Accuracy: %@", stateStr, accuracyStr);
}

#pragma mark - Scene Understanding

- (void)processSceneSemantics {
    if (@available(iOS 13.4, *)) {
        auto sceneUnderstanding = _extendedSession->getSceneUnderstanding();
        if (!sceneUnderstanding) return;
        
        // Enable different features
        sceneUnderstanding->setSceneReconstructionEnabled(true);
        sceneUnderstanding->setClassificationEnabled(true);
        sceneUnderstanding->setDepthEnabled(true);
        
        // Process depth data
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            while (self.sceneUnderstandingEnabled) {
                [self processDepthAndSemantics:sceneUnderstanding];
                [NSThread sleepForTimeInterval:0.1]; // 10 Hz
            }
        });
    }
}

- (void)processDepthAndSemantics:(std::shared_ptr<VROARSceneUnderstandingiOS>)sceneUnderstanding {
    if (@available(iOS 14.0, *)) {
        // Get depth map
        CVPixelBufferRef depthMap = sceneUnderstanding->getDepthMap();
        if (depthMap) {
            // Process depth data
            [self processDepthMap:depthMap];
        }
        
        // Check for person segmentation
        if (@available(iOS 13.0, *)) {
            if (sceneUnderstanding->isPersonAtPoint(CGPointMake(0.5, 0.5))) {
                NSLog(@"Person detected in center of view");
            }
        }
    }
}

- (void)processDepthMap:(CVPixelBufferRef)depthMap {
    // Example: Get depth at center point
    size_t width = CVPixelBufferGetWidth(depthMap);
    size_t height = CVPixelBufferGetHeight(depthMap);
    
    CVPixelBufferLockBaseAddress(depthMap, kCVPixelBufferLock_ReadOnly);
    Float32 *depthData = (Float32 *)CVPixelBufferGetBaseAddress(depthMap);
    
    if (depthData) {
        Float32 centerDepth = depthData[(height/2) * width + (width/2)];
        NSLog(@"Depth at center: %.2f meters", centerDepth);
    }
    
    CVPixelBufferUnlockBaseAddress(depthMap, kCVPixelBufferLock_ReadOnly);
}

#pragma mark - Face Tracking

- (void)setupFaceTrackingDelegate {
    class FaceDelegate : public VROARFaceTrackingDelegate {
    public:
        VROARAdvancedViewController *controller;
        
        FaceDelegate(VROARAdvancedViewController *vc) : controller(vc) {}
        
        virtual void onFaceDetected(std::shared_ptr<VROARFaceTrackingiOS> face) override {
            NSLog(@"Face detected");
            [controller addFaceEffects:face];
        }
        
        virtual void onFaceUpdated(std::shared_ptr<VROARFaceTrackingiOS> face) override {
            // Update face effects based on expressions
            float smile = (face->getMouthSmileLeft() + face->getMouthSmileRight()) / 2.0f;
            if (smile > 0.5f) {
                NSLog(@"Smile detected: %.2f", smile);
            }
        }
        
        virtual void onFaceLost(std::shared_ptr<VROARFaceTrackingiOS> face) override {
            NSLog(@"Face lost");
        }
    };
    
    auto delegate = std::make_shared<FaceDelegate>(self);
    _extendedSession->setFaceTrackingDelegate(delegate);
}

- (void)addFaceEffects:(std::shared_ptr<VROARFaceTrackingiOS>)face {
    // Create face mesh node
    std::shared_ptr<VRONode> faceNode = std::make_shared<VRONode>();
    
    // Get face geometry
    auto faceGeometry = face->createFaceMeshGeometry();
    if (faceGeometry) {
        // Apply material with transparency
        std::shared_ptr<VROMaterial> material = std::make_shared<VROMaterial>();
        material->getDiffuse().setColor(VROVector4f(1, 0.8, 0.8, 0.5));
        material->setTransparency(0.5);
        material->setLightingModel(VROLightingModel::Blinn);
        
        faceGeometry->setMaterials({material});
        faceNode->setGeometry(faceGeometry);
        
        _arSceneController->getScene()->getRootNode()->addChildNode(faceNode);
    }
}

#pragma mark - Body Tracking

- (void)processBodyTracking {
    if (@available(iOS 13.0, *)) {
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            while (self.bodyTrackingEnabled) {
                ARBodyAnchor *bodyAnchor = self.extendedSession->getBodyAnchor();
                if (bodyAnchor) {
                    [self processBodyAnchor:bodyAnchor];
                }
                
                ARBody2D *body2D = self.extendedSession->getBody2D();
                if (body2D) {
                    [self processBody2D:body2D];
                }
                
                [NSThread sleepForTimeInterval:0.033]; // 30 Hz
            }
        });
    }
}

- (void)processBodyAnchor:(ARBodyAnchor *)bodyAnchor API_AVAILABLE(ios(13.0)) {
    // Get skeleton joint transforms
    ARSkeleton3D *skeleton = bodyAnchor.skeleton;
    NSArray<NSString *> *jointNames = skeleton.jointNames;
    
    for (NSString *jointName in jointNames) {
        simd_float4x4 transform = [skeleton modelTransformForJointName:jointName];
        // Process joint transform
        VROMatrix4f vroTransform = [self convertTransform:transform];
        // Create visualization at joint position
    }
}

- (void)processBody2D:(ARBody2D *)body2D API_AVAILABLE(ios(13.0)) {
    // Get 2D skeleton joints
    ARSkeleton2D *skeleton = body2D.skeleton;
    NSArray<NSString *> *jointNames = skeleton.jointNames;
    
    for (NSString *jointName in jointNames) {
        simd_float2 point = [skeleton landmarkForJointNamed:jointName];
        NSLog(@"Joint %@: (%.2f, %.2f)", jointName, point.x, point.y);
    }
}

#pragma mark - Recording

- (void)toggleRecording {
    if (@available(iOS 13.0, *)) {
        static BOOL isRecording = NO;
        
        if (!isRecording) {
            NSString *documentsPath = NSSearchPathForDirectoriesInDomains(
                NSDocumentDirectory, NSUserDomainMask, YES)[0];
            NSString *filePath = [documentsPath stringByAppendingPathComponent:@"ar_recording.mp4"];
            NSURL *fileURL = [NSURL fileURLWithPath:filePath];
            
            if (_extendedSession->startRecording(fileURL)) {
                isRecording = YES;
                NSLog(@"Recording started: %@", filePath);
            }
        } else {
            _extendedSession->stopRecording([](NSURL *url, NSError *error) {
                if (error) {
                    NSLog(@"Recording error: %@", error);
                } else {
                    NSLog(@"Recording saved: %@", url);
                }
            });
            isRecording = NO;
        }
    }
}

#pragma mark - Utilities

- (VROMatrix4f)convertTransform:(simd_float4x4)transform {
    VROMatrix4f result;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result[i * 4 + j] = transform.columns[j][i];
        }
    }
    return result;
}

- (void)showAlert:(NSString *)message {
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"AR Features"
                                                                  message:message
                                                           preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"OK" 
                                              style:UIAlertActionStyleDefault 
                                            handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
}

#pragma mark - VROViewControllerPresenter

- (void)startPresenting {
    // Start AR session
    dispatch_async(dispatch_get_main_queue(), ^{
        [self.vroView startPresenting];
    });
}

- (void)stopPresenting {
    [_vroView stopPresenting];
}

#pragma mark - CLLocationManagerDelegate

- (void)locationManager:(CLLocationManager *)manager 
     didUpdateLocations:(NSArray<CLLocation *> *)locations {
    CLLocation *location = locations.lastObject;
    NSLog(@"Location updated: %.6f, %.6f", location.coordinate.latitude, location.coordinate.longitude);
}

- (void)locationManager:(CLLocationManager *)manager 
       didFailWithError:(NSError *)error {
    NSLog(@"Location error: %@", error);
}

@end