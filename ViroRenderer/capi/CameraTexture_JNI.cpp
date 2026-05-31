//
//  CameraTexture_JNI.cpp
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

/*
 * JNI bridge between Java com.viro.core.CameraTexture and C++ VROCameraTextureAndroid.
 *
 * Flow:
 *   Java CameraTexture(ViroContext, Position)
 *     → nativeCreate(isFront)        : creates VROCameraTextureAndroid, returns PersistentRef
 *     → nativeInit(ref, ctxRef)      : dispatches initCamera() to GL thread via
 *                                      VROPlatformDispatchAsyncRenderer; initCamera() calls
 *                                      back Java onGLReady(glTextureId) when OES tex is ready
 *     → nativePlay / nativePause     : toggle the _paused flag on the C++ object
 *     → nativeDispose                : deletes PersistentRef, releasing the shared_ptr
 */

#if VRO_PLATFORM_ANDROID

#include <memory>
#include "VROCameraTextureAndroid.h"
#include "ViroContext_JNI.h"
#include "VROPlatformUtil.h"
#include "VRODriverOpenGL.h"
#include "VROLog.h"

// Bring in the VRO_REF / VRO_ARGS / PersistentRef machinery
#include "VROAndroidCAPI.h"
#include "PersistentRef.h"

#define VRO_METHOD(return_type, method_name) \
    JNIEXPORT return_type JNICALL            \
        Java_com_viro_core_CameraTexture_##method_name

extern "C" {

/*
 * nativeCreate — called from Java CameraTexture constructor.
 *
 * Creates a VROCameraTextureAndroid, storing a global JNI ref to the Java
 * CameraTexture instance (obj) so C++ can call onGLReady() and updateTexImage()
 * back on it later.
 *
 * Returns: jlong handle (PersistentRef<VROCameraTextureAndroid>*)
 */
VRO_METHOD(VRO_REF(VROCameraTextureAndroid), nativeCreate)(VRO_ARGS
                                                            VRO_BOOL isFront) {
    VRO_METHOD_PREAMBLE;

    std::shared_ptr<VROCameraTextureAndroid> cameraTexture =
        std::make_shared<VROCameraTextureAndroid>((bool)isFront, obj);

    return VRO_REF_NEW(VROCameraTextureAndroid, cameraTexture);
}

/*
 * nativeInit — dispatches initCamera() to the GL thread.
 *
 * Uses VROPlatformDispatchAsyncRenderer (same mechanism as VideoTexture_JNI).
 * initCamera() generates the OES texture and calls back Java onGLReady(glTextureId).
 */
VRO_METHOD(void, nativeInit)(VRO_ARGS
                              VRO_REF(VROCameraTextureAndroid) nativeRef,
                              VRO_REF(ViroContext) viroContextRef) {
    VRO_METHOD_PREAMBLE;

    // Read isFrontFacing() before the async dispatch — the shared_ptr is alive here.
    std::shared_ptr<VROCameraTextureAndroid> texture =
        VRO_REF_GET(VROCameraTextureAndroid, nativeRef);
    if (!texture) return;

    VROCameraPosition position = texture->isFrontFacing()
        ? VROCameraPosition::Front
        : VROCameraPosition::Back;

    std::weak_ptr<VROCameraTextureAndroid> texture_w = texture;
    std::weak_ptr<ViroContext> context_w =
        VRO_REF_GET(ViroContext, viroContextRef);

    VROPlatformDispatchAsyncRenderer([texture_w, context_w, position] {
        std::shared_ptr<VROCameraTextureAndroid> tex = texture_w.lock();
        if (!tex) return;

        std::shared_ptr<ViroContext> context = context_w.lock();
        if (!context) return;

        std::shared_ptr<VRODriver> driver = context->getDriver();
        if (!driver) return;

        tex->initCamera(position, VROCameraOrientation::Portrait, driver);
    });
}

/*
 * nativePlay — unpauses frame-by-frame texture updates.
 */
VRO_METHOD(void, nativePlay)(VRO_ARGS
                              VRO_REF(VROCameraTextureAndroid) nativeRef) {
    VRO_METHOD_PREAMBLE;
    std::weak_ptr<VROCameraTextureAndroid> texture_w =
        VRO_REF_GET(VROCameraTextureAndroid, nativeRef);

    VROPlatformDispatchAsyncRenderer([texture_w] {
        std::shared_ptr<VROCameraTextureAndroid> texture = texture_w.lock();
        if (texture) texture->play();
    });
}

/*
 * nativePause — pauses frame-by-frame texture updates.
 */
VRO_METHOD(void, nativePause)(VRO_ARGS
                               VRO_REF(VROCameraTextureAndroid) nativeRef) {
    VRO_METHOD_PREAMBLE;
    std::weak_ptr<VROCameraTextureAndroid> texture_w =
        VRO_REF_GET(VROCameraTextureAndroid, nativeRef);

    VROPlatformDispatchAsyncRenderer([texture_w] {
        std::shared_ptr<VROCameraTextureAndroid> texture = texture_w.lock();
        if (texture) texture->pause();
    });
}

/*
 * nativeDispose — releases the C++ object.
 *
 * Deletes the PersistentRef, decrementing the shared_ptr's refcount. The
 * VROCameraTextureAndroid destructor releases the Java global ref and any
 * GL resources.
 */
VRO_METHOD(void, nativeDispose)(VRO_ARGS
                                 VRO_REF(VROCameraTextureAndroid) nativeRef) {
    VRO_METHOD_PREAMBLE;
    VRO_REF_DELETE(VROCameraTextureAndroid, nativeRef);
}

} // extern "C"

#endif // VRO_PLATFORM_ANDROID
