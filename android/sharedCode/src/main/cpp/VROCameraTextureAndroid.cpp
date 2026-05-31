//
//  VROCameraTextureAndroid.cpp
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

#include "VROCameraTextureAndroid.h"
#include "VROTextureSubstrateOpenGL.h"
#include "VRODriverOpenGL.h"
#include "VROFrameSynchronizer.h"
#include "VROPlatformUtil.h"
#include "VROLog.h"
#include <GLES2/gl2ext.h>

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

VROCameraTextureAndroid::VROCameraTextureAndroid(bool isFront, jobject javaCameraTexture)
    : VROCameraTexture(VROTextureType::TextureEGLImage),
      _isFront(isFront),
      _glTextureId(0),
      _paused(true),
      _initialized(false),
      _javaCameraTexture(nullptr) {

    // Retain the Java CameraTexture object for the lifetime of this C++ object.
    JNIEnv *env = VROPlatformGetJNIEnv();
    if (env && javaCameraTexture) {
        _javaCameraTexture = env->NewGlobalRef(javaCameraTexture);
    }
}

VROCameraTextureAndroid::~VROCameraTextureAndroid() {
    if (_javaCameraTexture) {
        JNIEnv *env = VROPlatformGetJNIEnv();
        if (env) {
            env->DeleteGlobalRef(_javaCameraTexture);
        }
        _javaCameraTexture = nullptr;
    }
}

// ---------------------------------------------------------------------------
// VROCameraTexture — initCamera
// Dispatched to the GL thread from nativeInit() in CameraTexture_JNI.cpp.
// ---------------------------------------------------------------------------

bool VROCameraTextureAndroid::initCamera(VROCameraPosition position,
                                         VROCameraOrientation orientation,
                                         std::shared_ptr<VRODriver> driver) {
    _driver = std::dynamic_pointer_cast<VRODriverOpenGL>(driver);

    // 1. Generate OES texture (must be on the GL thread).
    glGenTextures(1, &_glTextureId);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, _glTextureId);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 2. Wrap the OES texture in a VROTextureSubstrate so the renderer can sample it.
    std::unique_ptr<VROTextureSubstrate> substrate(
        new VROTextureSubstrateOpenGL(GL_TEXTURE_EXTERNAL_OES, _glTextureId, driver, true));
    setSubstrate(0, std::move(substrate));

    // 3. Register as a VROFrameListener so onFrameWillRender() runs each GL frame.
    driver->getFrameSynchronizer()->addFrameListener(
        std::dynamic_pointer_cast<VROFrameListener>(shared_from_this()));

    _initialized = true;

    // 4. Call back to Java so it can create SurfaceTexture(glTextureId) and open Camera2.
    javaOnGLReady((int)_glTextureId);

    return true;
}

// ---------------------------------------------------------------------------
// Play / Pause
// ---------------------------------------------------------------------------

void VROCameraTextureAndroid::play() {
    _paused = false;
}

void VROCameraTextureAndroid::pause() {
    _paused = true;
}

bool VROCameraTextureAndroid::isPaused() {
    return _paused;
}

// ---------------------------------------------------------------------------
// VROFrameListener — push the latest camera frame into the OES texture
// ---------------------------------------------------------------------------

void VROCameraTextureAndroid::onFrameWillRender(const VRORenderContext &context) {
    if (!_paused && _initialized) {
        javaUpdateTexImage();
    }
}

// ---------------------------------------------------------------------------
// Camera properties
// ---------------------------------------------------------------------------

float VROCameraTextureAndroid::getHorizontalFOV() const {
    // Typical smartphone horizontal FOV ~65°.
    // Can be refined via CameraCharacteristics.LENS_INFO_AVAILABLE_FOCAL_LENGTHS.
    return 65.0f;
}

VROVector3f VROCameraTextureAndroid::getImageSize() const {
    // Default HD preview size; real size is set by the Camera2 session on the Java side.
    return VROVector3f(1280, 720, 0);
}

std::vector<float> VROCameraTextureAndroid::getCameraIntrinsics() const {
    return {};
}

// ---------------------------------------------------------------------------
// JNI helpers
// ---------------------------------------------------------------------------

/*
 * Calls Java: CameraTexture.onGLReady(int glTextureId)
 * Java will create SurfaceTexture(glTextureId) and open the Camera2 session.
 */
void VROCameraTextureAndroid::javaOnGLReady(int glTextureId) {
    JNIEnv *env = VROPlatformGetJNIEnv();
    if (!env || !_javaCameraTexture) return;

    jclass cls = env->GetObjectClass(_javaCameraTexture);
    if (!cls) return;

    jmethodID m = env->GetMethodID(cls, "onGLReady", "(I)V");
    if (m) {
        env->CallVoidMethod(_javaCameraTexture, m, (jint)glTextureId);
    }
    env->DeleteLocalRef(cls);
}

/*
 * Calls Java: CameraTexture.updateTexImage()
 * Java delegates to SurfaceTexture.updateTexImage(), pulling the latest camera
 * frame into the OES texture on the current (GL) thread.
 */
void VROCameraTextureAndroid::javaUpdateTexImage() {
    JNIEnv *env = VROPlatformGetJNIEnv();
    if (!env || !_javaCameraTexture) return;

    jclass cls = env->GetObjectClass(_javaCameraTexture);
    if (!cls) return;

    jmethodID m = env->GetMethodID(cls, "updateTexImage", "()V");
    if (m) {
        env->CallVoidMethod(_javaCameraTexture, m);
    }
    env->DeleteLocalRef(cls);
}
