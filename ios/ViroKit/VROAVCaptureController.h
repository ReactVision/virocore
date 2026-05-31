//
//  VROAVCaptureController.h
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

#ifndef VROAVCaptureController_h
#define VROAVCaptureController_h

#include "VROCameraTexture.h"
#include <vector>
#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>

@class VROCameraCaptureDelegate;
@class VROCameraOrientationListener;
class VROVideoTextureCache;

class VROAVCaptureController : public std::enable_shared_from_this<VROAVCaptureController> {
    
public:
    
    VROAVCaptureController();
    virtual ~VROAVCaptureController();
    
    void initCapture(VROCameraPosition position, VROCameraOrientation orientation,
                     bool renderPreview, std::shared_ptr<VRODriver> driver);
    void pause();
    void play();
    bool isPaused();
    
    float getHorizontalFOV() const;
    VROVector3f getImageSize() const;
    
    void update(CMSampleBufferRef sampleBuffer, std::vector<float> intrinsics);
    void updateOrientation(VROCameraOrientation orientation);
    
    void setUpdateListener(std::function<void(CMSampleBufferRef, std::vector<float>)> listener) {
        _updateListener = listener;
    }

    /*
     Get the CMSampleBufferRef that corresponds to the last image produced by this
     controller.
     */
    CMSampleBufferRef getSampleBuffer() const {
        return _lastSampleBuffer;
    }

    /*
     Get the camera intrinsics that correspond to the last image produced by this
     controller.
     */
    std::vector<float> getCameraIntrinsics() const {
        return _lastCameraIntrinsics;
    }

    // -----------------------------------------------------------------------
    // Photo capture
    // -----------------------------------------------------------------------

    /*
     Capture a single JPEG still and write it to outputPath.
     onComplete(success, path, errorMessage) is called on the main queue.
     */
    void capturePhoto(NSString *outputPath,
                      std::function<void(bool, NSString *, NSString *)> onComplete);

    // -----------------------------------------------------------------------
    // Video recording
    // -----------------------------------------------------------------------

    /*
     Start recording to outputPath (MP4). onStarted fires when recording begins.
     */
    void startRecording(NSString *outputPath,
                        std::function<void(bool, NSString *, NSString *)> onStarted);

    /*
     Stop an in-progress recording. onStopped(success, path, error) fires on main queue.
     */
    void stopRecording(std::function<void(bool, NSString *, NSString *)> onStopped);

    bool isRecording() const { return _isRecording; }

private:
    
    /*
     Capture session and delegates used for live video playback.
     */
    AVCaptureSession *_captureSession;
    VROCameraCaptureDelegate *_delegate;
    VROCameraOrientationListener *_orientationListener;

    /*
     Photo output — added to the session during initCapture.
     */
    AVCapturePhotoOutput *_photoOutput;

    /*
     Movie file output — added during startRecording, removed after stopRecording.
     */
    AVCaptureMovieFileOutput *_movieOutput;
    NSString *_recordingPath;
    bool _isRecording;

    /*
     Pending completion callbacks (held as ObjC blocks via __block to avoid
     capturing C++ lambdas across ARC boundaries).
     */
    std::function<void(bool, NSString *, NSString *)> _photoCompletion;
    std::function<void(bool, NSString *, NSString *)> _recordingCompletion;

    /*
     True if paused.
     */
    bool _paused;
    
    /*
     The last received CMSampleBufferRef, which represents the current contents of the
     camera texture.
     */
    CMSampleBufferRef _lastSampleBuffer;
    
    /*
     The camera intrinsics for _lastSampleBuffer.
     */
    std::vector<float> _lastCameraIntrinsics;
    
    /*
     Set a listener (optional) to respond to the sample buffer being updated.
     */
    std::function<void(CMSampleBufferRef, std::vector<float>)> _updateListener;
    
};

/*
 Delegate for capturing video from cameras.
 */
@interface VROCameraCaptureDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
- (id)initWithCaptureController:(std::shared_ptr<VROAVCaptureController>)controller;
@end

/*
 Delegate for listening to orientation changes.
 */
@interface VROCameraOrientationListener : NSObject

- (id)initWithCaptureController:(std::shared_ptr<VROAVCaptureController>)controller;
- (void)orientationDidChange:(NSNotification *)notification;

@end

/*
 Delegate for AVCapturePhotoOutput still captures.
 */
@interface VROCameraPhotoDelegate : NSObject <AVCapturePhotoCaptureDelegate>
- (id)initWithOutputPath:(NSString *)path
              completion:(std::function<void(bool, NSString *, NSString *)>)completion;
@end

/*
 Delegate for AVCaptureMovieFileOutput recording.
 */
@interface VROCameraMovieDelegate : NSObject <AVCaptureFileOutputRecordingDelegate>
- (id)initWithCompletion:(std::function<void(bool, NSString *, NSString *)>)completion;
@end

#endif /* VROAVCaptureController_h */
