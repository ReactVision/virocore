//
//  VROViewAR.m
//  ViroRenderer
//
//  Created by Raj Advani on 5/31/17.
//  Copyright © 2017 Viro Media. All rights reserved.
//
//  Permission is hereby granted, free of charge, to any person obtaining
//  a copy of this software and associated documentation files (the
//  "Software"), to deal in the Software without restriction, including
//  without limitation the rights to use, copy, modify, merge, publish,
//  distribute, sublicense, and/or sell copies of the Software, and to
//  permit persons to whom the Software is furnished to do so, subject to
//  the following conditions:
//
//  The above copyright notice and this permission notice shall be included
//  in all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
//  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#import "VROViewAR.h"
#import "VRORenderer.h"
#include "VROARFrameiOS.h"
#import "VROARSceneController.h"
#import "VRORenderDelegateiOS.h"
#import "VROTime.h"
#import "VROEye.h"
#import "VRODriverOpenGLiOS.h"
#import "VROARSessioniOS.h" 
#import "VROARSessionInertial.h"
#import "VROARCamera.h"
#import "VROARAnchor.h"
#import "VROARFrame.h"
#import "VROConvert.h"
#import "VROARHitTestResult.h"
#import "VRONodeCamera.h"
#import "vr/gvr/capi/include/gvr_audio.h"
#import "VROARScene.h"
#import "VROChoreographer.h"
#import "VROInputControllerAR.h"
#import "VROProjector.h"
#import "VROWeakProxy.h"
#import "VROViewRecorder.h"
#import "VROARCameraInertial.h"
#import "VRODeviceUtil.h"
#import "VROCameraTexture.h"
#import "VROShaderFactory.h"
#import "VROShaderModifier.h"
#import "VROUniform.h"

static VROVector3f const kZeroVector = VROVector3f();

@interface VROViewAR () {
    std::shared_ptr<VRORenderer> _renderer;
    std::shared_ptr<VROARSceneController> _sceneController;
    std::shared_ptr<VRORenderDelegateiOS> _renderDelegateWrapper;
    std::shared_ptr<VRODriverOpenGL> _driver;
    std::shared_ptr<VROSurface> _cameraBackground;
    std::shared_ptr<VROARSession> _arSession;
    std::shared_ptr<VRONode> _pointOfView;
    std::shared_ptr<VROInputControllerAR> _inputController;
    VROViewport _viewport;

    CADisplayLink *_displayLink;
    int _frame;
    VROWorldAlignment _worldAlignment;

    // Depth/occlusion shader modifiers and their state
    std::shared_ptr<VROShaderModifier> _depthDebugModifier;
    std::shared_ptr<VROShaderModifier> _occlusionModifier;
    bool _occlusionModifierAdded;
    bool _depthDebugModifierAdded;
    bool _depthDebugEnabled;
    float _depthDebugOpacity;
    VROMatrix4f _depthTextureTransform;
    VROViewport _currentViewport;
}

@property (readwrite, nonatomic) VROTrackingType trackingType;

// Image Tracking Output
@property (readwrite, nonatomic) UITextView *trackerStatusText;
@property (readwrite, nonatomic) UIImageView *trackerOutputView;
@property (readwrite, nonatomic) UITextView *trackerOutputText;
@property (readwrite, nonatomic) CGFloat trackerViewScale;
@property (readwrite, nonatomic) CGFloat textHeight;

// Debug view for drawing with CoreGraphics
@property (readwrite, nonatomic) UIView *glassView;

@end

@implementation VROViewAR

@dynamic renderDelegate;
@dynamic sceneController;

#pragma mark - Initialization

- (instancetype)initWithCoder:(NSCoder *)coder {
    self = [super initWithCoder:coder];
    if (self) {
        _worldAlignment = VROWorldAlignment::Gravity;
        _viewport = VROViewport(-1, -1, -1, -1);
        VRORendererConfiguration config;
        [self initRenderer:config];
    }
    return self;
}

- (instancetype)initWithFrame:(CGRect)frame
                       config:(VRORendererConfiguration)config
                      context:(EAGLContext *)context
               worldAlignment:(VROWorldAlignment)worldAlignment {
    return [self initWithFrame:frame config:config context:context worldAlignment:worldAlignment trackingType:VROTrackingType::DOF6];
}

- (instancetype)initWithFrame:(CGRect)frame
                       config:(VRORendererConfiguration)config
                      context:(EAGLContext *)context
               worldAlignment:(VROWorldAlignment)worldAlignment
                 trackingType:(VROTrackingType)trackingType {
    self = [super initWithFrame:frame context:context];
    if (self) {
        _worldAlignment = worldAlignment;
        _trackingType = trackingType;
        _viewport = VROViewport(-1, -1, -1, -1);
        if (_trackingType == VROTrackingType::Front) {
            _cameraPosition = VROCameraPosition::Front;
        } else {
            _cameraPosition = VROCameraPosition::Back;
        }
        [self initRenderer:config];
    }
    return self;
}

- (void)setFrame:(CGRect)frame {
    [super setFrame:frame];
    
    // If the frame changes, then update the _camera background to match
    if (_cameraBackground) {
        _cameraBackground->setX(self.frame.size.width * self.contentScaleFactor / 2.0);
        _cameraBackground->setY(self.frame.size.height * self.contentScaleFactor / 2.0);
        _cameraBackground->setWidth(self.frame.size.width * self.contentScaleFactor);
        _cameraBackground->setHeight(self.frame.size.height * self.contentScaleFactor);
    }

    if (_inputController) {
        _inputController->setViewportSize(self.frame.size.width * self.contentScaleFactor,
                                          self.frame.size.height * self.contentScaleFactor);
    }

    if (_trackerOutputView) {
        float width = self.frame.size.width * _trackerViewScale;
        float height = self.frame.size.height * _trackerViewScale;
        _trackerOutputView.frame = CGRectMake(self.frame.size.width - width, self.frame.size.height - height, width, height);
    }
    
    if (_trackerOutputText) {
        _trackerOutputText.frame = CGRectMake(0, self.frame.size.height - _textHeight, self.frame.size.width, _textHeight);

    }
}

- (void)initRenderer:(VRORendererConfiguration)config {
    VROPlatformSetType(VROPlatformType::iOSARKit);
    if (!self.context) {
        EAGLContext *context = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
        self.context = context;
    }
    VROPlatformSetEAGLContext(self.context);
    VROThreadRestricted::setThread(VROThreadName::Renderer);
    
    /*
     Setup the GLKView.
     */
    self.enableSetNeedsDisplay = NO;
    self.drawableColorFormat = GLKViewDrawableColorFormatSRGBA8888;
    self.drawableStencilFormat = GLKViewDrawableStencilFormat8;
    self.drawableDepthFormat = GLKViewDrawableDepthFormat16;
    if (config.enableMultisampling) {
        self.drawableMultisample = GLKViewDrawableMultisample4X;
    }
    
    [EAGLContext setCurrentContext:self.context];

    /*
     Setup the animation loop for the GLKView.
     */
    VROWeakProxy *proxy = [VROWeakProxy weakProxyForObject:self];
    _displayLink = [CADisplayLink displayLinkWithTarget:proxy selector:@selector(display)];
    [_displayLink addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSRunLoopCommonModes];

#if VRO_POSEMOJI
    VRODeviceUtil *device = [[VRODeviceUtil alloc] init];
    if (![device isBionicA12]) {
        _displayLink.preferredFramesPerSecond = 30;
    }
#endif

    /*
     Disable going to sleep, and setup notifications.
     */
    [UIApplication sharedApplication].idleTimerDisabled = YES;
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(orientationDidChange:)
                                                 name:UIApplicationDidChangeStatusBarOrientationNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationWillResignActive:)
                                                 name:UIApplicationWillResignActiveNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(applicationDidBecomeActive:)
                                                 name:UIApplicationDidBecomeActiveNotification
                                               object:nil];
    
    /*
     Create Viro renderer objects.
     */
    _driver = std::make_shared<VRODriverOpenGLiOS>(self, self.context);
    _frame = 0;
    _occlusionModifierAdded = false;
    _depthDebugModifierAdded = false;
    _depthDebugEnabled = false;
    _depthDebugOpacity = 0.5f;

    _inputController = std::make_shared<VROInputControllerAR>(self.frame.size.width * self.contentScaleFactor,
                                                              self.frame.size.height * self.contentScaleFactor,
                                                              _driver);

    _renderer = std::make_shared<VRORenderer>(config, _inputController);
    
    /*
     Set up the Audio Session properly for recording and playing back audio. We need
     to do this *AFTER* we init _gvrAudio (in driver construction), because it resets
     some setting, else audio recording won't work.
     */
#if !VRO_POSEMOJI
    AVAudioSession *session = [AVAudioSession sharedInstance];
    [session setCategory:AVAudioSessionCategoryPlayAndRecord
             withOptions:AVAudioSessionCategoryOptionDefaultToSpeaker
                   error:nil];
#endif
    /*
     Create AR session checking if an ARKit class and one of our classes have been defined. If not, then load VROARSessionInertial,
     otherwise create a VROARSessioniOS w/ 6DOF tracking.
     */
    if (NSClassFromString(@"ARSession") == nil) {
        _arSession = std::make_shared<VROARSessionInertial>(VROTrackingType::DOF3, _driver);
    } else if (_trackingType == VROTrackingType::Front || _trackingType == VROTrackingType::PrerecordedVideo) {
        _arSession = std::make_shared<VROARSessionInertial>(_trackingType, _driver);
    } else {
        _arSession = std::make_shared<VROARSessioniOS>(_trackingType, _worldAlignment, _driver);
    }

    _arSession->setOrientation(VROConvert::toCameraOrientation([[UIApplication sharedApplication] statusBarOrientation]));
    _inputController->setSession(std::dynamic_pointer_cast<VROARSession>(_arSession));
    
    /*
     Set the point of view to a special node that will follow the user's
     real position.
     */
    _pointOfView = std::make_shared<VRONode>();
    _pointOfView->setCamera(std::make_shared<VRONodeCamera>());
    _renderer->setPointOfView(_pointOfView);
    
    UIRotationGestureRecognizer *rotateGesture = [[UIRotationGestureRecognizer alloc] initWithTarget:self action:@selector(handleRotate:)];
    [rotateGesture setDelegate:self];
    [self addGestureRecognizer:rotateGesture];

    UIPinchGestureRecognizer *pinchGesture = [[UIPinchGestureRecognizer alloc] initWithTarget:self action:@selector(handlePinch:)];
    [rotateGesture setDelegate:self];
    [self addGestureRecognizer:pinchGesture];

    /*
     Use a pan gesture instead of a 0 second long press gesture recoginizer because
     it seems to play better with the other two recoginizers
     */
    UIPanGestureRecognizer *panGesture = [[UIPanGestureRecognizer alloc] initWithTarget:self
                                                                                 action:@selector(handleLongPress:)];
    [panGesture setDelegate:self];
    [self addGestureRecognizer:panGesture];

    UITapGestureRecognizer *tapGesture = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(handleTap:)];
    [tapGesture setDelegate:self];
    [self addGestureRecognizer:tapGesture];
    
    self.viewRecorder = [[VROViewRecorder alloc] initWithView:self renderer:_renderer driver:_driver];
}

#pragma mark Gesture handlers

- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gestureRecognizer shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer *)otherGestureRecognizer {
    if ([gestureRecognizer isKindOfClass:[UIPanGestureRecognizer class]] || [otherGestureRecognizer isKindOfClass:[UIPanGestureRecognizer class]]) {
        return NO;
    }
    return YES;
}

- (void)handleRotate:(UIRotationGestureRecognizer *)recognizer {
    // locationInView was `recognizer.self` but if view is created after app initialization, then it location x and y is 0
    CGPoint location = [recognizer locationInView:nil];
    VROVector3f viewportTouchPos = VROVector3f(location.x * self.contentScaleFactor, location.y * self.contentScaleFactor);
    
    if(recognizer.state == UIGestureRecognizerStateBegan) {
        _inputController->onRotateStart(viewportTouchPos);
    } else if(recognizer.state == UIGestureRecognizerStateChanged) {
        // Note: we need to "negate" the rotation because the value returned is "opposite" of our platform.
        _inputController->onRotate(-recognizer.rotation); // already in radians
    } else if(recognizer.state == UIGestureRecognizerStateEnded) {
        _inputController->onRotateEnd();
    }
}

- (void)handlePinch:(UIPinchGestureRecognizer *)recognizer {
    // locationInView was `recognizer.self` but if view is created after app initialization, then it location x and y is 0
    CGPoint location = [recognizer locationInView:nil];
    VROVector3f viewportTouchPos = VROVector3f(location.x * self.contentScaleFactor, location.y * self.contentScaleFactor);
  
    if(recognizer.state == UIGestureRecognizerStateBegan) {
        _inputController->onPinchStart(viewportTouchPos);
    } else if(recognizer.state == UIGestureRecognizerStateChanged) {
        _inputController->onPinchScale(recognizer.scale);
    } else if(recognizer.state == UIGestureRecognizerStateEnded) {
        _inputController->onPinchEnd();
    }
}

- (void)handleLongPress:(UIPanGestureRecognizer *)recognizer {
    // locationInView was `recognizer.self` but if view is created after app initialization, then it location x and y is 0
    CGPoint location = [recognizer locationInView:nil];
    
    VROVector3f viewportTouchPos = VROVector3f(location.x * self.contentScaleFactor, location.y * self.contentScaleFactor);
    
    if (recognizer.state == UIGestureRecognizerStateBegan) {
        _inputController->onScreenTouchDown(viewportTouchPos);
    } else if (recognizer.state == UIGestureRecognizerStateEnded) {
        _inputController->onScreenTouchUp(viewportTouchPos);
    } else {
        _inputController->onScreenTouchMove(viewportTouchPos);
    }
}

- (void)handleTap:(UITapGestureRecognizer *)recognizer {
    // locationInView was `recognizer.self` but if view is created after app initialization, then it location x and y is 0
    CGPoint location = [recognizer locationInView:nil];
    
    VROVector3f viewportTouchPos = VROVector3f(location.x * self.contentScaleFactor, location.y * self.contentScaleFactor);
    
    if (recognizer.state == UIGestureRecognizerStateRecognized) {
        _inputController->onScreenTouchDown(viewportTouchPos);
        _inputController->onScreenTouchUp(viewportTouchPos);
    }
}

- (void)dealloc {
    // Invalidate display link first to stop render loop
    if (_displayLink) {
        [_displayLink invalidate];
        _displayLink = nil;
    }

    // Ensure all GL resources are cleaned up if not already done
    // This acts as a safety net in case deleteGL wasn't called
    if (_renderer || _driver || _arSession) {
        [self deleteGL];
    }

    // Clear thread restriction
    VROThreadRestricted::unsetThread();

    // Remove notification observers
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

- (void)deleteGL {
    // Clean up view recorder first
    [self.viewRecorder deleteGL];

    // Reset shader modifiers to release GPU shader resources
    _depthDebugModifier.reset();
    _occlusionModifier.reset();
    _occlusionModifierAdded = false;
    _depthDebugModifierAdded = false;

    // Reset camera background surface (holds camera texture)
    _cameraBackground.reset();

    // Clean up scene controller and its node tree
    if (_sceneController) {
        if (_sceneController->getScene()) {
            // CRITICAL: Remove all children FIRST to release their shared_ptrs
            // This must happen BEFORE deleteGL to actually destroy the objects
            _sceneController->getScene()->getRootNode()->removeAllChildren();

            // Now call deleteGL to release GPU resources
            _sceneController->getScene()->getRootNode()->deleteGL();
        }
        _sceneController.reset();
    }

    // Reset render delegate wrapper
    _renderDelegateWrapper.reset();

    // Reset input controller
    _inputController.reset();

    // Reset point of view node
    _pointOfView.reset();

    // Reset renderer (holds significant GPU state including frame synchronizer,
    // choreographer, and cached rendering state)
    _renderer.reset();

    // Pause and reset AR session
    if (_arSession) {
        _arSession->pause();
        _arSession.reset();
    }

    // Reset driver LAST as other objects may have dependencies on it
    // The driver holds the OpenGL context state, texture caches, and GPU resources
    _driver.reset();
}

- (void)setPaused:(BOOL)paused {
    [_displayLink setPaused:paused];
}

- (BOOL)setShadowsEnabled:(BOOL)enabled {
    return _renderer->setShadowsEnabled(enabled);
}

- (BOOL)setHDREnabled:(BOOL)enabled {
    return _renderer->setHDREnabled(enabled);
}

- (BOOL)setPBREnabled:(BOOL)enabled {
    return _renderer->setPBREnabled(enabled);
}

- (BOOL)setBloomEnabled:(BOOL)enabled {
    return _renderer->setBloomEnabled(enabled);
}

- (VROVector3f)unprojectPoint:(VROVector3f)point {
    return _renderer->unprojectPoint(point);
}

- (VROVector3f)projectPoint:(VROVector3f)point {
    return _renderer->projectPoint(point);
}

#pragma mark - Recording and Screenshots

- (void)startVideoRecording:(NSString *)fileName
           saveToCameraRoll:(BOOL)saveToCamera
                 errorBlock:(VROViewRecordingErrorBlock)errorBlock {
    [self.viewRecorder startVideoRecording:fileName saveToCameraRoll:saveToCamera errorBlock:errorBlock];
}

- (void)startVideoRecording:(NSString *)fileName
              withWatermark:(UIImage *)watermarkImage
                  withFrame:(CGRect)watermarkFrame
           saveToCameraRoll:(BOOL)saveToCamera
                 errorBlock:(VROViewRecordingErrorBlock)errorBlock {
    [self.viewRecorder startVideoRecording:fileName withWatermark:watermarkImage withFrame:watermarkFrame saveToCameraRoll:saveToCamera errorBlock:errorBlock];
}

- (void)startVideoRecording:(NSString *)fileName
                    gifFile:(NSString *)gifFile
              withWatermark:(UIImage *)watermarkImage
                  withFrame:(CGRect)watermarkFrame
           saveToCameraRoll:(BOOL)saveToCamera
                 errorBlock:(VROViewRecordingErrorBlock)errorBlock {
    [self.viewRecorder startVideoRecording:fileName gifFile:gifFile withWatermark:watermarkImage withFrame:watermarkFrame saveToCameraRoll:saveToCamera errorBlock:errorBlock];
}

- (void)stopVideoRecordingWithHandler:(VROViewWriteMediaFinishBlock)completionHandler {
    [self.viewRecorder stopVideoRecordingWithHandler:completionHandler];
}

- (void)stopVideoRecordingWithHandler:(VROViewWriteMediaFinishBlock)completionHandler mergeAudioTrack:(NSURL *)audioPath {
    [self.viewRecorder stopVideoRecordingWithHandler:completionHandler mergeAudioTrack:audioPath];
}

- (void)takeScreenshot:(NSString *)fileName
      saveToCameraRoll:(BOOL)saveToCamera
 withCompletionHandler:(VROViewWriteMediaFinishBlock)completionHandler {
    [self.viewRecorder takeScreenshot:fileName saveToCameraRoll:saveToCamera withCompletionHandler:completionHandler];
}

#pragma mark - AR Functions

- (std::vector<std::shared_ptr<VROARHitTestResult>>)performARHitTest:(VROVector3f)ray {
    // check that the ray is in front of the camera
    VROVector3f cameraForward = _renderer->getCamera().getForward();
    if (cameraForward.dot(ray) <= 0) {
        return {};
    }
    
    VROVector3f worldPoint = _renderer->getCamera().getPosition() + ray.normalize();
    VROVector3f screenPoint = _renderer->projectPoint(worldPoint);

    return [self performARHitTestWithPoint:screenPoint.x y:screenPoint.y];
}

- (std::vector<std::shared_ptr<VROARHitTestResult>>)performARHitTestWithPoint:(int)x y:(int)y {
    int viewportArr[4] = {0, 0,
        (int) (self.bounds.size.width  * self.contentScaleFactor),
        (int) (self.bounds.size.height * self.contentScaleFactor)};

    // check the 2D point, perform and return the results from the AR hit test
    std::unique_ptr<VROARFrame> &frame = _arSession->getLastFrame();
    if (frame && x >= 0 && x <= viewportArr[2] && y >= 0 && y <= viewportArr[3]) {
        std::vector<std::shared_ptr<VROARHitTestResult>> results = frame->hitTest(x, y,
                                                                 { VROARHitTestResultType::ExistingPlaneUsingExtent,
                                                                     VROARHitTestResultType::ExistingPlane,
                                                                     VROARHitTestResultType::EstimatedHorizontalPlane,
                                                                     VROARHitTestResultType::FeaturePoint });
        return results;
    }

    return {};
}

- (std::shared_ptr<VROARSession>)getARSession {
    return _arSession;
}

#pragma mark - Settings and Notifications

- (void)orientationDidChange:(NSNotification *)notification {
    if (_arSession) {
        // the _cameraBackground will be updated if/when the frame is actually set.
        _arSession->setOrientation(VROConvert::toCameraOrientation([[UIApplication sharedApplication] statusBarOrientation]));
    }
}

- (void)applicationWillResignActive:(NSNotification *)notification {
    if (_displayLink) { _displayLink.paused = YES; }
    if (_arSession) { _arSession->pause(); }
}

- (void)applicationDidBecomeActive:(NSNotification *)notification {
    if (_displayLink) { _displayLink.paused = NO; }
    if (_arSession) { _arSession->run(); }
}

- (void)setRenderDelegate:(id<VRORenderDelegate>)renderDelegate {
    _renderDelegateWrapper = std::make_shared<VRORenderDelegateiOS>(renderDelegate);
    _renderer->setDelegate(_renderDelegateWrapper);
}

- (void)setARSessionDelegate:(std::shared_ptr<VROARSessionDelegate>)delegate {
    _arSession->setDelegate(delegate);
}

- (void)setVrMode:(BOOL)enabled {
    // No-op in AR mode
}

- (void)setDebugHUDEnabled:(BOOL)enabled {
    _renderer->setDebugHUDEnabled(enabled);
}

- (NSString *)getPlatform {
    return @"ar";
}

- (NSString *)getHeadset {
    return [NSString stringWithUTF8String:_inputController->getHeadset().c_str()];
}

- (NSString *)getController {
    return [NSString stringWithUTF8String:_inputController->getController().c_str()];
}

- (void)setDebugDrawDelegate:(NSObject<VRODebugDrawDelegate> *)debugDrawDelegate {
    self.glassView = [[VROGlassView alloc] initWithFrame:self.bounds delegate:debugDrawDelegate];
    [self addSubview:self.glassView];
}

#pragma mark - Camera

- (void)setPointOfView:(std::shared_ptr<VRONode>)node {
    self.renderer->setPointOfView(node);
}

#pragma mark - Scene Loading

- (void)setSceneController:(std::shared_ptr<VROSceneController>)sceneController {
    [self setSceneController:sceneController duration:0 timingFunction:VROTimingFunctionType::EaseIn];
}

- (void)setSceneController:(std::shared_ptr<VROSceneController>)sceneController
                  duration:(float)seconds
            timingFunction:(VROTimingFunctionType)timingFunctionType {
    _sceneController = std::dynamic_pointer_cast<VROARSceneController>(sceneController);
    passert_msg (_sceneController != nullptr, "AR View requires an AR Scene Controller!");

    _renderer->setSceneController(sceneController, seconds, timingFunctionType, _driver);

    std::shared_ptr<VROARScene> arScene = std::dynamic_pointer_cast<VROARScene>(_sceneController->getScene());
    passert_msg (arScene != nullptr, "AR View requires an AR Scene!");
    
    _arSession->setScene(arScene);
    _arSession->setDelegate(arScene->getSessionDelegate());
    arScene->setARSession(_arSession);
    arScene->addNode(_pointOfView);
    arScene->setDriver(_driver);

    // Reset the camera background for the new scene
    _cameraBackground.reset();
}

#pragma mark - Getters

- (std::shared_ptr<VROFrameSynchronizer>)frameSynchronizer {
    return _renderer->getFrameSynchronizer();
}

- (std::shared_ptr<VRORenderer>)renderer {
    return _renderer;
}

- (std::shared_ptr<VROChoreographer>)choreographer {
    return _renderer->getChoreographer();
}

#pragma mark - Rendering

- (void)drawRect:(CGRect)rect {
    // Clear error state
    GL ();
    
    @autoreleasepool {
        [self renderFrame];
    }

    if (_glassView) {
        [_glassView setNeedsDisplay];
    }
    
    ++_frame;
    ALLOCATION_TRACKER_PRINT();
}

- (void)renderFrame {
    if (!_arSession) {
        return;
    }
    
    /*
     Setup GL state.
     */
    glEnable(GL_DEPTH_TEST);    
    _driver->setCullMode(VROCullMode::Back);
    
    VROViewport viewport;
    if (_viewport.getWidth() == -1) {
        viewport = VROViewport(0, 0,
                               self.bounds.size.width  * self.contentScaleFactor,
                               self.bounds.size.height * self.contentScaleFactor);
    } else {
        viewport = _viewport;
    }
    _currentViewport = viewport;
    
    /*
     Attempt to initialize the ARSession if we have not yet done so.
     */
    if (_sceneController) {
        if (!_cameraBackground) {
            [self initARSessionWithViewport:viewport scene:_sceneController->getScene()];
        }
    }

    /*
     The viewport can be 0, if say in React Native, the user accidentally messes up their
     styles and React Native lays the view out with 0 width or height. No use rendering
     in this case.
     */
    if (viewport.getWidth() == 0 || viewport.getHeight() == 0) {
        return;
    }

    /*
     If the ARSession is not yet ready, render black.
     */
    if (!_arSession->isReady()) {
        [self renderWithoutTracking:viewport];
        return;
    }
    
    /*
     ARSession is ready (meaning at least one frame has been produced).
     */
    _arSession->setViewport(viewport);
    
    /*
     When using the front-facing camera with preview layer, we don't use our own background;
     instead the camera is rendered via an AVCaptureVideoPreviewLayer.
     */
    if (!_sceneController->getScene()->getRootNode()->getBackground() &&
        (_trackingType != VROTrackingType::Front || !kInertialRenderCameraUsingPreviewLayer)) {
        _sceneController->getScene()->getRootNode()->setBackground(_cameraBackground);
    }

    /*
     Retrieve transforms from the AR session.
     */
    const std::unique_ptr<VROARFrame> &frame = _arSession->updateFrame();
    const std::shared_ptr<VROARCamera> camera = frame->getCamera();

    /*
     Update the AR camera background transform (maps the camera background to our scene).
     If we're in front-facing mode, we also have to mirror the camera background.
     */
    VROMatrix4f backgroundTransform = frame->getViewportToCameraImageTransform();
    if (_trackingType == VROTrackingType::Front) {
        VROMatrix4f mirrorX;
        mirrorX.scale(-1, 1, 1);
        backgroundTransform = mirrorX * backgroundTransform;
    }
    _cameraBackground->setTexcoordTransform(backgroundTransform);

    /*
     Update occlusion settings on the camera background.
     */
    [self updateBackgroundOcclusionWithFrame:frame];

    /*
     Notify the current ARScene with the ARCamera's tracking state.
     */
    if (_sceneController) {
        std::shared_ptr<VROARScene> arScene = std::dynamic_pointer_cast<VROARScene>(_sceneController->getScene());
        passert_msg (arScene != nullptr, "AR View requires an AR Scene!");
        arScene->setTrackingState(camera->getTrackingState(), camera->getLimitedTrackingStateReason(), false);
    }

    /*
     Render the scene.
     */
    [self renderWithTracking:camera withFrame:frame withViewport:viewport];

    /*
     Notify scene of the updated ambient light estimates.
     */
    std::shared_ptr<VROARScene> scene = std::dynamic_pointer_cast<VROARScene>(_arSession->getScene());
    scene->updateAmbientLight(frame->getAmbientLightIntensity(), frame->getAmbientLightColor());
}

- (void)renderWithTracking:(const std::shared_ptr<VROARCamera>) camera
                 withFrame:(const std::unique_ptr<VROARFrame> &) frame
              withViewport:(VROViewport)viewport {
    VROFieldOfView fov;
    VROMatrix4f projection = camera->getProjection(viewport, kZNear, _renderer->getFarClippingPlane(), &fov);
    VROMatrix4f rotation = camera->getRotation();
    VROVector3f position = camera->getPosition();

    // Set up occlusion depth texture if occlusion is enabled
    VROOcclusionMode occlusionMode = _arSession->getOcclusionMode();
    std::shared_ptr<VROTexture> depthTexture = nullptr;
    VROMatrix4f depthTextureTransform = VROMatrix4f::identity();

    // Check if we have any depth texture (LiDAR or Monocular)
    // Note: frame->hasDepthData() only checks for LiDAR, so we explicitly try getDepthTexture()
    if (occlusionMode != VROOcclusionMode::Disabled) {
        depthTexture = frame->getDepthTexture();
        if (depthTexture) {
            depthTextureTransform = frame->getDepthTextureTransform();

            // Ensure the depth texture is uploaded to GPU before rendering
            if (!depthTexture->isHydrated()) {
                depthTexture->prewarm(_driver);
            }

        }
    }

    // Store for debug modifier uniform binders
    _depthTextureTransform = depthTextureTransform;

    // CRITICAL: Set occlusion info on the render context BEFORE prepareFrame
    // so that shader capability keys include arOcclusion during scene traversal.
    _renderer->setOcclusionMode(occlusionMode);
    _renderer->setDepthTexture(depthTexture);
    _renderer->setDepthTextureTransform(depthTextureTransform);

    _pointOfView->getCamera()->setPosition(position);
    _renderer->prepareFrame(_frame, viewport, fov, rotation, projection, _driver);

    // DEBUG: Occlusion debug logging (every 60 frames)
    // Uncomment for debugging: occlusionMode, depthTexture, transform, viewport, camera image dimensions
    /*
    static int occDebugCounter = 0;
    if (occDebugCounter++ % 60 == 0) {
        NSLog(@"DEBUG occlusionMode=%d, depthTexture=%p, hydrated=%d",
              (int)occlusionMode, depthTexture.get(),
              depthTexture ? depthTexture->isHydrated() : -1);
        if (depthTexture) {
            NSLog(@"DEBUG depthTexture size=%dx%d", depthTexture->getWidth(), depthTexture->getHeight());
            NSLog(@"DEBUG transform: [%.3f, %.3f, 0, 0]", depthTextureTransform[0], depthTextureTransform[1]);
            NSLog(@"DEBUG           [%.3f, %.3f, 0, 0]", depthTextureTransform[4], depthTextureTransform[5]);
            NSLog(@"DEBUG           tx=%.3f, ty=%.3f", depthTextureTransform[12], depthTextureTransform[13]);
            NSLog(@"DEBUG viewport=%dx%d", viewport.getWidth(), viewport.getHeight());
            const VROARFrameiOS *frameiOS = dynamic_cast<const VROARFrameiOS *>(frame.get());
            if (frameiOS) {
                CVPixelBufferRef camImg = frameiOS->getImage();
                if (camImg) {
                    NSLog(@"DEBUG camImage=%zux%zu", CVPixelBufferGetWidth(camImg), CVPixelBufferGetHeight(camImg));
                }
            }
        }
    }
    */

    _renderer->renderEye(VROEyeType::Monocular, _renderer->getLookAtMatrix(), projection, viewport, _driver);
    _renderer->renderHUD(VROEyeType::Monocular, VROMatrix4f::identity(), projection, _driver);
    _renderer->endFrame(_driver);
}

/*
 Render black while waiting for the AR session to initialize.
 */
- (void)renderWithoutTracking:(VROViewport)viewport {
    VROFieldOfView fov = _renderer->computeUserFieldOfView(viewport.getWidth(), viewport.getHeight());
    VROMatrix4f projection = fov.toPerspectiveProjection(kZNear, _renderer->getFarClippingPlane());

    _renderer->prepareFrame(_frame, viewport, fov, VROMatrix4f::identity(), projection, _driver);
    _renderer->renderEye(VROEyeType::Monocular, _renderer->getLookAtMatrix(), projection, viewport, _driver);
    _renderer->renderHUD(VROEyeType::Monocular, VROMatrix4f::identity(), projection, _driver);
    _renderer->endFrame(_driver);
}

- (void)updateBackgroundOcclusionWithFrame:(const std::unique_ptr<VROARFrame> &)frame {
    std::shared_ptr<VROMaterial> material = _cameraBackground->getMaterials()[0];

    // Check if we have depth data available for debug visualization
    // NOTE: Occlusion is now handled on 3D objects via the shader factory, not on the background.
    // The background camera image should NOT write to the depth buffer.
    std::shared_ptr<VROTexture> depthTexture = frame->getDepthTexture();
    bool hasDepth = (depthTexture != nullptr);

    // Only add depth debug visualization if enabled and depth data is available
    bool needsDebugVisualization = _depthDebugEnabled && depthTexture;

    if (needsDebugVisualization) {
        // Hydrate the depth texture so it's uploaded to the GPU
        if (!depthTexture->isHydrated()) {
            depthTexture->prewarm(_driver);
        }

        // Debug modifier now uses ar_occlusion_depth_texture (global ARDepthMap binding)
        // No need to set depth texture on the material's AO slot.

        // Add the debug modifier if not already added
        if (!_depthDebugModifierAdded) {
            _depthDebugModifier = VROShaderFactory::createDepthDebugModifier();

            // Bind the opacity uniform dynamically
            __weak VROViewAR *weakSelf = self;
            _depthDebugModifier->setUniformBinder("depth_debug_opacity", VROShaderProperty::Float,
                [weakSelf](VROUniform *uniform, const VROGeometry *geometry, const VROMaterial *material) {
                    VROViewAR *strongSelf = weakSelf;
                    if (strongSelf) {
                        uniform->setFloat(strongSelf->_depthDebugEnabled ? strongSelf->_depthDebugOpacity : 0.0f);
                    } else {
                        uniform->setFloat(0.0f);
                    }
                });

            // Bind viewport size and depth transform directly on the modifier.
            // These are normally bound by bindOcclusionUniforms via the render context,
            // but the camera background may not go through that path reliably.
            _depthDebugModifier->setUniformBinder("ar_viewport_size", VROShaderProperty::Vec3,
                [weakSelf](VROUniform *uniform, const VROGeometry *geometry, const VROMaterial *material) {
                    VROViewAR *strongSelf = weakSelf;
                    if (strongSelf) {
                        VROVector3f size((float)strongSelf->_currentViewport.getWidth(),
                                        (float)strongSelf->_currentViewport.getHeight(), 0.0f);
                        uniform->setVec3(size);
                    }
                });

            _depthDebugModifier->setUniformBinder("ar_depth_texture_transform", VROShaderProperty::Mat4,
                [weakSelf](VROUniform *uniform, const VROGeometry *geometry, const VROMaterial *material) {
                    VROViewAR *strongSelf = weakSelf;
                    if (strongSelf) {
                        uniform->setMat4(strongSelf->_depthTextureTransform);
                    }
                });

            material->addShaderModifier(_depthDebugModifier);
            _depthDebugModifierAdded = true;
        }
    } else {
        // Remove depth debug modifier if it was added
        if (_depthDebugModifierAdded && _depthDebugModifier) {
            material->removeShaderModifier(_depthDebugModifier);
            _depthDebugModifier.reset();
            _depthDebugModifierAdded = false;
            material->getAmbientOcclusion().setTexture(nullptr);
        }
    }

    // Background camera should NEVER write to depth buffer.
    // Occlusion is handled per-3D-object in the fragment shader via the occlusion mask modifier
    // which is automatically added by VROShaderFactory when arOcclusion capability is enabled.
    material->setWritesToDepthBuffer(false);
}

- (void)initARSessionWithViewport:(VROViewport)viewport scene:(std::shared_ptr<VROScene>)scene {
    _cameraBackground = VROSurface::createSurface(viewport.getX() + viewport.getWidth()  / 2.0,
                                                  viewport.getY() + viewport.getHeight() / 2.0,
                                                  viewport.getWidth(), viewport.getHeight(),
                                                  0, 0, 1, 1);
    _cameraBackground->setScreenSpace(true);
    _cameraBackground->setName("Camera");

    std::shared_ptr<VROMaterial> material = _cameraBackground->getMaterials()[0];
    material->setLightingModel(VROLightingModel::Constant);
    material->getDiffuse().setTexture(_arSession->getCameraBackgroundTexture());
    material->setWritesToDepthBuffer(false);
    material->setNeedsToneMapping(false);
    material->setCullMode(VROCullMode::None); // Required for mirrored mode when using front-facing camera

    // NOTE: We do NOT add the occlusion modifier here because we don't have the depth texture yet.
    // The modifier will be added in updateBackgroundOcclusionWithFrame when depth data is available.
    // This ensures the ao_map uniform is properly declared in the shader.

    _arSession->setViewport(viewport);
    _arSession->run();
}

- (void)setRenderedFrameViewPort:(VROViewport)viewport{
    _viewport = viewport;
    [self.viewRecorder setRecorderWidth:viewport.getWidth() height:viewport.getHeight()];
}

+ (BOOL) isARSupported {
  if (@available(iOS 11, *)) {
    return [ARConfiguration isSupported];
  }
  return false;
}

- (void)recenterTracking {
    // TODO Implement this, try to share code with VROSceneRendererCardboardOpenGL; maybe
    //      move the functionality into VRORenderer
}

- (void)setDepthDebugEnabled:(BOOL)enabled opacity:(float)opacity {
    _depthDebugEnabled = enabled;
    _depthDebugOpacity = opacity;

    // The uniform binder will dynamically read _depthDebugEnabled and _depthDebugOpacity,
    // so changes take effect on the next frame automatically.
    //
    // When disabled, the uniform binder sets opacity to 0, making the debug overlay invisible.
    // If depth features are completely disabled (no occlusion and no debug), the modifiers
    // will be removed in updateBackgroundOcclusionWithFrame on the next frame.
}

#pragma mark - Monocular Depth Estimation

- (void)setMonocularDepthEnabled:(BOOL)enabled {
    if (self.trackingType == VROTrackingType::DOF6) {
        std::shared_ptr<VROARSessioniOS> sessioniOS =
            std::dynamic_pointer_cast<VROARSessioniOS>(_arSession);
        if (sessioniOS) {
            sessioniOS->setMonocularDepthEnabled(enabled);
        }
    }
}

- (BOOL)isMonocularDepthSupported {
    if (self.trackingType == VROTrackingType::DOF6) {
        std::shared_ptr<VROARSessioniOS> sessioniOS =
            std::dynamic_pointer_cast<VROARSessioniOS>(_arSession);
        if (sessioniOS) {
            return sessioniOS->isMonocularDepthSupported();
        }
    }
    return NO;
}

- (BOOL)isMonocularDepthModelAvailable {
    // Check framework bundle first (model bundled in ViroKit)
    NSBundle *frameworkBundle = [NSBundle bundleForClass:[VROViewAR class]];
    NSString *bundledPath = [frameworkBundle pathForResource:@"DepthPro" ofType:@"mlmodelc"];
    
    // Fallback to main app bundle (for custom deployments)
    if (!bundledPath) {
        bundledPath = [[NSBundle mainBundle] pathForResource:@"DepthPro" ofType:@"mlmodelc"];
    }
    
    return bundledPath != nil;
}

- (void)setPreferMonocularDepth:(BOOL)prefer {
    if (self.trackingType == VROTrackingType::DOF6) {
        std::shared_ptr<VROARSessioniOS> session = std::dynamic_pointer_cast<VROARSessioniOS>([self getARSession]);
        if (session) {
            session->setPreferMonocularDepth(prefer);
        }
    }
}

- (BOOL)isPreferMonocularDepth {
    if (self.trackingType == VROTrackingType::DOF6) {
        std::shared_ptr<VROARSessioniOS> session = std::dynamic_pointer_cast<VROARSessioniOS>([self getARSession]);
        if (session) {
            return session->isPreferMonocularDepth();
        }
    }
    return NO;
}

@end

@implementation VROGlassView

- (id)initWithFrame:(CGRect)frame delegate:(NSObject<VRODebugDrawDelegate> *)delegate {
    self = [super initWithFrame:frame];
    if (self) {
        _debugDrawDelegate = delegate;
        self.backgroundColor = [UIColor clearColor];
    }
    return self;
}

- (void)drawRect:(CGRect)rect {
    if (self.debugDrawDelegate) {
        [self.debugDrawDelegate drawRect];
    }
}

@end
