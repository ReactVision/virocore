//
//  VROAVCaptureController.cpp
//  ViroRenderer
//
//  Created by Raj Advani on 3/12/19.
//  Copyright © 2019 Viro Media. All rights reserved.
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

#include "VROAVCaptureController.h"
#include "VRORenderContext.h"
#include "VROFrameSynchronizer.h"
#include "VROTextureSubstrate.h"
#include "VROLog.h"
#include "VROVideoTextureCache.h"
#include "VRODriver.h"
#include "VROConvert.h"
#include "VROImagePreprocessor.h"
#include "VRODriverOpenGLiOS.h"
#include "VROByteBuffer.h"

VROAVCaptureController::VROAVCaptureController() :
    _paused(true),
    _isRecording(false),
    _photoOutput(nil),
    _movieOutput(nil),
    _recordingPath(nil),
    _lastSampleBuffer(nil) {
}

void VROAVCaptureController::initCapture(VROCameraPosition position, VROCameraOrientation orientation,
                                         bool renderPreview,
                                         std::shared_ptr<VRODriver> driver) {
    pause();
    std::shared_ptr<VROAVCaptureController> shared = std::dynamic_pointer_cast<VROAVCaptureController>(shared_from_this());
    
    _delegate = [[VROCameraCaptureDelegate alloc] initWithCaptureController:shared];
    
    // Create a capture session
    _captureSession = [[AVCaptureSession alloc] init];
    if (!_captureSession) {
        pinfo("Error: Could not create a capture session");
        return;
    }
    
    [_captureSession beginConfiguration];
    
    AVCaptureSessionPreset preset;
    NSNumber *pixelFormat;
    
    // If we're rendering a preview, then we don't need a high quality output since we won't be
    // displaying the texture; we'll just be using it for computer vision. Additionally, we'll be
    // able to use YCbCr, which integrates better with CoreML.
    //
    // Note: we're still using high quality output since our ML algorithm isn't correctly
    // transforming coordinate systems when we start with a low quality image.
    if (renderPreview) {
        preset = AVCaptureSessionPresetHigh;
        pixelFormat = [NSNumber numberWithInt:kCVPixelFormatType_420YpCbCr8BiPlanarFullRange];
    } else {
        preset = AVCaptureSessionPresetHigh;
        pixelFormat = [NSNumber numberWithInt:kCVPixelFormatType_32BGRA];
    }
    [_captureSession setSessionPreset:preset];

    // Get the a video device with the requested camera
    AVCaptureDevicePosition avPosition = (position == VROCameraPosition::Front) ? AVCaptureDevicePositionFront : AVCaptureDevicePositionBack;
    AVCaptureDevice *videoDevice = [[[AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera]
                                                                                            mediaType:AVMediaTypeVideo
                                                                                             position:avPosition] devices] firstObject];

    NSError *error;
    
    // Device input
    AVCaptureDeviceInput *deviceInput = [AVCaptureDeviceInput deviceInputWithDevice:videoDevice error:&error];
    if (error) {
        pinfo("Error: could not create AVCaptureDeviceInput");
        return;
    }
    
    [_captureSession addInput:deviceInput];
    
    // Create the output for the capture session.
    AVCaptureVideoDataOutput *dataOutput = [[AVCaptureVideoDataOutput alloc] init];
    [dataOutput setAlwaysDiscardsLateVideoFrames:YES];
    [dataOutput setVideoSettings:[NSDictionary dictionaryWithObject:pixelFormat forKey:(id)kCVPixelBufferPixelFormatTypeKey]];
    
    // Set dispatch to be on the main thread to create the texture in memory
    // and allow OpenGL to use it for rendering
    [dataOutput setSampleBufferDelegate:_delegate queue:dispatch_get_main_queue()];
    
    [_captureSession addOutput:dataOutput];

    // Add AVCapturePhotoOutput so still captures are always available.
    _photoOutput = [[AVCapturePhotoOutput alloc] init];
    if ([_captureSession canAddOutput:_photoOutput]) {
        [_captureSession addOutput:_photoOutput];
    }

    updateOrientation(orientation);
    [_captureSession commitConfiguration];
    
    if (dataOutput.connections.firstObject.cameraIntrinsicMatrixDeliverySupported) {
        dataOutput.connections.firstObject.cameraIntrinsicMatrixDeliveryEnabled = true;
    }
    
    _orientationListener = [[VROCameraOrientationListener alloc] initWithCaptureController:shared];
    [[NSNotificationCenter defaultCenter] addObserver:_orientationListener
                                             selector:@selector(orientationDidChange:)
                                                 name:UIApplicationDidChangeStatusBarOrientationNotification
                                               object:nil];
    
    // Render a preview layer
    if (renderPreview) {
        AVCaptureVideoPreviewLayer *previewLayer = [[AVCaptureVideoPreviewLayer alloc] initWithSession:_captureSession];
        previewLayer.videoGravity = AVLayerVideoGravityResizeAspectFill;
        
        std::shared_ptr<VRODriverOpenGLiOS> driveriOS = std::dynamic_pointer_cast<VRODriverOpenGLiOS>(driver);
        UIView *previewView = driveriOS->getView();
        CALayer *rootLayer = previewView.layer;
        previewLayer.frame = rootLayer.bounds;
        previewLayer.opacity = 1.0;
        
        [rootLayer addSublayer:previewLayer];
    }
}

VROAVCaptureController::~VROAVCaptureController() {
    if (_lastSampleBuffer) {
        CFRelease(_lastSampleBuffer);
    }
    [[NSNotificationCenter defaultCenter] removeObserver:_orientationListener];
}

float VROAVCaptureController::getHorizontalFOV() const {
    AVCaptureDeviceInput *input = [_captureSession.inputs firstObject];
    return input.device.activeFormat.videoFieldOfView;
}

VROVector3f VROAVCaptureController::getImageSize() const {
    AVCaptureDeviceInput *input = [_captureSession.inputs firstObject];
    
    CMFormatDescriptionRef desc = input.device.activeFormat.formatDescription;
    CGSize dim = CMVideoFormatDescriptionGetPresentationDimensions(desc, true, true);
    
    UIInterfaceOrientation orientation = [[UIApplication sharedApplication] statusBarOrientation];
    if (UIInterfaceOrientationIsPortrait(orientation)) {
        return { (float)dim.height, (float)dim.width,  0 };
    }
    else {
        return { (float)dim.width,  (float)dim.height, 0 };
    }
}

void VROAVCaptureController::updateOrientation(VROCameraOrientation orientation) {
    AVCaptureVideoDataOutput *output = [[_captureSession outputs] objectAtIndex:0];
    
    AVCaptureConnection *connection = [output connectionWithMediaType:AVMediaTypeVideo];
    if (orientation == VROCameraOrientation::Portrait) {
        [connection setVideoOrientation:AVCaptureVideoOrientationPortrait];
    }
    else if (orientation == VROCameraOrientation::LandscapeLeft) {
        [connection setVideoOrientation:AVCaptureVideoOrientationLandscapeLeft];
    }
    else if (orientation == VROCameraOrientation::LandscapeRight) {
        [connection setVideoOrientation:AVCaptureVideoOrientationLandscapeRight];
    }
    else {
        [connection setVideoOrientation:AVCaptureVideoOrientationPortraitUpsideDown];
    }
}

void VROAVCaptureController::pause() {
    _paused = true;
    [_captureSession stopRunning];
}

void VROAVCaptureController::play() {
    _paused = false;
    [_captureSession startRunning];
}

bool VROAVCaptureController::isPaused() {
    return _paused;
}

void VROAVCaptureController::update(CMSampleBufferRef sampleBuffer, std::vector<float> intrinsics) {
    if (_lastSampleBuffer) {
        CFRelease(_lastSampleBuffer);
    }
    CFRetain(sampleBuffer);
    _lastSampleBuffer = sampleBuffer;
    _lastCameraIntrinsics = intrinsics;
    
    if (_updateListener) {
        _updateListener(sampleBuffer, intrinsics);
    }
}

#pragma mark - Photo capture

void VROAVCaptureController::capturePhoto(NSString *outputPath,
                                          std::function<void(bool, NSString *, NSString *)> onComplete) {
    if (!_captureSession || !_photoOutput) {
        onComplete(false, nil, @"Camera not initialised");
        return;
    }
    if (outputPath == nil || outputPath.length == 0) {
        NSString *ts = [NSString stringWithFormat:@"viro_photo_%lld.jpg",
                        (long long)([[NSDate date] timeIntervalSince1970] * 1000)];
        outputPath = [NSTemporaryDirectory() stringByAppendingPathComponent:ts];
    }

    _photoCompletion = onComplete;

    AVCapturePhotoSettings *settings = [AVCapturePhotoSettings photoSettingsWithFormat:
        @{AVVideoCodecKey: AVVideoCodecTypeJPEG}];
    VROCameraPhotoDelegate *delegate = [[VROCameraPhotoDelegate alloc]
                                        initWithOutputPath:outputPath
                                                completion:onComplete];
    [_photoOutput capturePhotoWithSettings:settings delegate:delegate];
}

#pragma mark - Video recording

void VROAVCaptureController::startRecording(NSString *outputPath,
                                            std::function<void(bool, NSString *, NSString *)> onStarted) {
    if (_isRecording) {
        onStarted(false, nil, @"Already recording");
        return;
    }
    if (!_captureSession) {
        onStarted(false, nil, @"Camera not initialised");
        return;
    }
    if (outputPath == nil || outputPath.length == 0) {
        NSString *ts = [NSString stringWithFormat:@"viro_video_%lld.mp4",
                        (long long)([[NSDate date] timeIntervalSince1970] * 1000)];
        outputPath = [NSTemporaryDirectory() stringByAppendingPathComponent:ts];
    }

    _recordingPath = outputPath;

    [_captureSession beginConfiguration];
    _movieOutput = [[AVCaptureMovieFileOutput alloc] init];
    if ([_captureSession canAddOutput:_movieOutput]) {
        [_captureSession addOutput:_movieOutput];
    }
    [_captureSession commitConfiguration];

    NSURL *url = [NSURL fileURLWithPath:outputPath];
    VROCameraMovieDelegate *delegate = [[VROCameraMovieDelegate alloc]
                                        initWithCompletion:onStarted];
    [_movieOutput startRecordingToOutputFileURL:url recordingDelegate:delegate];
    _isRecording = true;
}

void VROAVCaptureController::stopRecording(std::function<void(bool, NSString *, NSString *)> onStopped) {
    if (!_isRecording || !_movieOutput) {
        onStopped(false, nil, @"Not recording");
        return;
    }
    _recordingCompletion = onStopped;
    NSString *path = _recordingPath;
    [_movieOutput stopRecording];
    _isRecording = false;

    // Remove the movie output from the session to free resources.
    // C++ class method: capture 'this' explicitly — 'self' is Obj-C only.
    VROAVCaptureController *controller = this;
    dispatch_async(dispatch_get_main_queue(), ^{
        [controller->_captureSession beginConfiguration];
        [controller->_captureSession removeOutput:controller->_movieOutput];
        [controller->_captureSession commitConfiguration];
        controller->_movieOutput = nil;
        controller->_recordingPath = nil;
        if (controller->_recordingCompletion) {
            controller->_recordingCompletion(true, path, nil);
            controller->_recordingCompletion = nullptr;
        }
    });
}

#pragma mark - VROCameraCaptureDelegate

@interface VROCameraCaptureDelegate ()

@property (readwrite, nonatomic) std::weak_ptr<VROAVCaptureController> controller;

@end

@implementation VROCameraCaptureDelegate

- (id)initWithCaptureController:(std::shared_ptr<VROAVCaptureController>)controller {
    self = [super init];
    if (self) {
        self.controller = controller;
    }
    return self;
}

- (void)captureOutput:(AVCaptureOutput *)captureOutput didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
fromConnection:(AVCaptureConnection *)connection {
    
    std::shared_ptr<VROAVCaptureController> controller = self.controller.lock();
    if (controller && captureOutput.connections.firstObject.cameraIntrinsicMatrixDeliveryEnabled) {
        CFDataRef cameraIntrinsics = (CFDataRef) CMGetAttachment(sampleBuffer, kCMSampleBufferAttachmentKey_CameraIntrinsicMatrix, nil);
        
        CFIndex length = CFDataGetLength(cameraIntrinsics);
        uint8_t bytes[length];
        CFDataGetBytes(cameraIntrinsics, CFRangeMake(0, length), bytes);
        
        VROByteBuffer buffer(bytes, (int) length, false);
        std::vector<float> intrinsics;
        for (int i = 0; i < 12; i++) {
            intrinsics.push_back(buffer.readFloat());
        }
        
        controller->update(sampleBuffer, intrinsics);
    } else {
        controller->update(sampleBuffer, {});
    }
}

- (void)captureOutput:(AVCaptureOutput *)captureOutput didDropSampleBuffer:(CMSampleBufferRef)sampleBuffer
fromConnection:(AVCaptureConnection *)connection {
    
}

@end

@interface VROCameraOrientationListener ()

@property (readwrite, nonatomic) std::weak_ptr<VROAVCaptureController> controller;

@end

@implementation VROCameraOrientationListener

- (id)initWithCaptureController:(std::shared_ptr<VROAVCaptureController>)controller {
    self = [super init];;
    if (self) {
        self.controller = controller;
    }
    return self;
}

- (void)orientationDidChange:(NSNotification *)notification {
    std::shared_ptr<VROAVCaptureController> controller = self.controller.lock();
    if (!controller) {
        return;
    }

    UIInterfaceOrientation orientation = [[UIApplication sharedApplication] statusBarOrientation];
    controller->updateOrientation(VROConvert::toCameraOrientation(orientation));
}

@end

#pragma mark - VROCameraPhotoDelegate

@interface VROCameraPhotoDelegate ()
@property (nonatomic, copy) NSString *outputPath;
@property (nonatomic, assign) std::function<void(bool, NSString *, NSString *)> completion;
@end

@implementation VROCameraPhotoDelegate

- (id)initWithOutputPath:(NSString *)path
              completion:(std::function<void(bool, NSString *, NSString *)>)completion {
    self = [super init];
    if (self) {
        _outputPath = path;
        _completion = completion;
    }
    return self;
}

- (void)captureOutput:(AVCapturePhotoOutput *)output
didFinishProcessingPhoto:(AVCapturePhoto *)photo
                error:(NSError *)error {
    if (error) {
        _completion(false, nil, error.localizedDescription);
        return;
    }
    NSData *data = [photo fileDataRepresentation];
    if (!data) {
        _completion(false, nil, @"No image data");
        return;
    }
    NSError *writeError = nil;
    [data writeToFile:_outputPath options:NSDataWritingAtomic error:&writeError];
    if (writeError) {
        _completion(false, nil, writeError.localizedDescription);
    } else {
        _completion(true, _outputPath, nil);
    }
}

@end

#pragma mark - VROCameraMovieDelegate

@interface VROCameraMovieDelegate ()
@property (nonatomic, assign) std::function<void(bool, NSString *, NSString *)> completion;
@end

@implementation VROCameraMovieDelegate

- (id)initWithCompletion:(std::function<void(bool, NSString *, NSString *)>)completion {
    self = [super init];
    if (self) {
        _completion = completion;
    }
    return self;
}

// Called when recording successfully starts — fire the onStarted callback.
- (void)fileOutput:(AVCaptureFileOutput *)output
didStartRecordingToOutputFileAtURL:(NSURL *)fileURL
   fromConnections:(NSArray<AVCaptureConnection *> *)connections {
    if (_completion) {
        _completion(true, fileURL.path, nil);
        _completion = nullptr;  // only fire once for "started"
    }
}

// Called when recording finishes (after stopRecording). Errors are surfaced here.
- (void)fileOutput:(AVCaptureFileOutput *)output
didFinishRecordingToOutputFileAtURL:(NSURL *)outputFileURL
   fromConnections:(NSArray<AVCaptureConnection *> *)connections
             error:(NSError *)error {
    // The completion for stop is handled synchronously in stopRecording(); nothing to do here.
    // If recording was interrupted by an error before stop, surface it.
    if (error && _completion) {
        _completion(false, nil, error.localizedDescription);
        _completion = nullptr;
    }
}

@end
