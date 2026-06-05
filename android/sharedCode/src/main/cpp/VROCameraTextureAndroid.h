//
//  VROCameraTextureAndroid.h
//  ViroRenderer
//
//  Copyright © 2026 ReactVision. All rights reserved.
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

#ifndef VROCameraTextureAndroid_h
#define VROCameraTextureAndroid_h

#include "VROCameraTexture.h"
#include "VROFrameListener.h"
#include "VROFrameSynchronizer.h"
#include "VROOpenGL.h"
#include <jni.h>
#include <atomic>

class VRODriverOpenGL;

/*
 * VROCameraTextureAndroid streams live camera frames into an OpenGL OES texture
 * using the Android Camera2 API via a Java SurfaceTexture bridge.
 *
 * Architecture (mirrors VROVideoTextureAVP / bindSurface pattern):
 *   1. nativeCreate() constructs this object and retains a global JNI ref to the
 *      Java CameraTexture instance.
 *   2. nativeInit() dispatches initCamera() to the GL thread via
 *      VROPlatformDispatchAsyncRenderer.
 *   3. initCamera() generates the OES texture, creates VROTextureSubstrateOpenGL,
 *      registers as a VROFrameListener, then calls javaOnGLReady(glTextureId).
 *   4. Java CameraTexture.onGLReady() creates SurfaceTexture(glTextureId) and
 *      opens the Camera2 session.
 *   5. onFrameWillRender() calls javaUpdateTexImage() each GL frame so that the
 *      latest camera frame is pushed into the OES texture before rendering.
 */
class VROCameraTextureAndroid : public VROCameraTexture, public VROFrameListener {

public:
    /*
     * @param isFront           true = front-facing camera, false = rear camera.
     * @param javaCameraTexture Local JNI ref to Java CameraTexture. A global ref
     *                          is retained internally; caller need not keep it alive.
     */
    VROCameraTextureAndroid(bool isFront, jobject javaCameraTexture);
    virtual ~VROCameraTextureAndroid();

    // VROCameraTexture interface
    // Satisfies VROCameraTexture base — not used on Android (frameSynchronizer overload below).
    bool initCamera(VROCameraPosition position, VROCameraOrientation orientation,
                    std::shared_ptr<VRODriver> driver) override { return false; }

    // Android-specific init that receives the frame synchronizer from ViroContext.
    bool initCamera(VROCameraPosition position, VROCameraOrientation orientation,
                    std::shared_ptr<VRODriver> driver,
                    std::shared_ptr<VROFrameSynchronizer> frameSynchronizer);
    void play()     override;
    void pause()    override;
    bool isPaused() override;

    float getHorizontalFOV()             const override;
    VROVector3f getImageSize()            const override;
    std::vector<float> getCameraIntrinsics() const override;

    // VROFrameListener — called on the GL thread before each render
    void onFrameWillRender(const VRORenderContext &context) override;
    void onFrameDidRender(const VRORenderContext &context)  override {}

private:
    bool                           _isFront;
    GLuint                         _glTextureId;
    std::atomic<bool>              _paused;
    std::atomic<bool>              _initialized;
    std::weak_ptr<VRODriverOpenGL> _driver;

    // Global JNI ref to Java CameraTexture — released in destructor
    jobject                        _javaCameraTexture;

    // Used by nativeInit JNI to read back the stored position before GL dispatch
    bool isFrontFacing() const { return _isFront; }

    // JNI callbacks into Java CameraTexture
    void javaOnGLReady(int glTextureId);  // → CameraTexture.onGLReady(int)
    void javaUpdateTexImage();             // → CameraTexture.updateTexImage() → SurfaceTexture
};

#endif /* VROCameraTextureAndroid_h */
