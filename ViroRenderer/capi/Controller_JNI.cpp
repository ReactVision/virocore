//
//  Controller_JNI.cpp
//  ViroRenderer
//
//  Copyright © 2016 Viro Media. All rights reserved.
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

#include <memory>
#include "ViroContext_JNI.h"
#include "EventDelegate_JNI.h"
#include "ViroUtils_JNI.h"
#include "VROInputControllerBase.h"
#include "VROLog.h"

#if VRO_PLATFORM_ANDROID
#define VRO_METHOD(return_type, method_name) \
  JNIEXPORT return_type JNICALL              \
      Java_com_viro_core_Controller_##method_name
#else
#define VRO_METHOD(return_type, method_name) \
    return_type Controller_##method_name
#endif

extern "C" {

VRO_METHOD(void, nativeSetEventDelegate)(VRO_ARGS
                                         VRO_REF(ViroContext) render_context_ref,
                                         VRO_REF(EventDelegate_JNI) native_delegate_ref) {
    // A ViroContext disposed by a renderer teardown hands us 0 (seen on Quest when a
    // scene remounts); dereferencing it crashed in VRO_REF_GET.
    if (render_context_ref == 0) {
        return;
    }
    std::weak_ptr<ViroContext> nativeContext_w = VRO_REF_GET(ViroContext,  render_context_ref);
    std::weak_ptr<EventDelegate_JNI> delegate_w = VRO_REF_GET(EventDelegate_JNI, native_delegate_ref);

    VROPlatformDispatchAsyncRenderer([nativeContext_w, delegate_w] {
        std::shared_ptr<ViroContext> nativeContext = nativeContext_w.lock();
        std::shared_ptr<EventDelegate_JNI> delegate = delegate_w.lock();

        if (!nativeContext || !delegate) {
            pinput("Controller.setEventDelegate dropped: %s was released before the renderer ran it",
                   !nativeContext ? "the context" : "the delegate");
            return;
        }
        std::shared_ptr<VROInputControllerBase> inputController = nativeContext->getInputController();
        std::shared_ptr<VROInputPresenter> controllerPresenter
                = inputController ? inputController->getPresenter() : nullptr;
        if (!controllerPresenter) {
            // No presenter until the input controller has a driver.
            pwarn("Controller.setEventDelegate dropped: the input controller has no presenter yet");
            return;
        }
        controllerPresenter->setEventDelegate(delegate);
        pinput("Controller.setEventDelegate: delegate %p set on presenter %p",
               (void *) delegate.get(), (void *) controllerPresenter.get());
    });
}

VRO_METHOD(void, nativeEnableReticle)(VRO_ARGS
                                      VRO_REF(ViroContext) render_context_ref,
                                      VRO_BOOL enable) {
    if (render_context_ref == 0) {
        return;
    }
    std::weak_ptr<ViroContext> nativeContext_w = VRO_REF_GET(ViroContext, render_context_ref);

    VROPlatformDispatchAsyncRenderer([nativeContext_w, enable] {
        std::shared_ptr<ViroContext> nativeContext = nativeContext_w.lock();
        if (!nativeContext) {
            return;
        }

        std::shared_ptr<VROInputPresenter> controllerPresenter = nativeContext->getInputController()->getPresenter();
        std::shared_ptr<VROReticle> reticle = controllerPresenter->getReticle();
        if (reticle != nullptr) {
            reticle->setEnabled(enable);
        }
    });
}

VRO_METHOD(void, nativeEnableController)(VRO_ARGS
                                         VRO_REF(ViroContext) render_context_ref,
                                         VRO_BOOL enable) {
    if (render_context_ref == 0) {
        return;
    }
    std::weak_ptr<ViroContext> nativeContext_w = VRO_REF_GET(ViroContext, render_context_ref);

    VROPlatformDispatchAsyncRenderer([nativeContext_w, enable] {
        std::shared_ptr<ViroContext> nativeContext = nativeContext_w.lock();
        if (!nativeContext) {
            return;
        }

        std::shared_ptr<VROInputPresenter> controllerPresenter = nativeContext->getInputController()->getPresenter();
        controllerPresenter->getRootNode()->setHidden(!enable);
    });
}

VRO_METHOD(VRO_FLOAT_ARRAY, nativeGetControllerForwardVector)(VRO_ARGS
                                                              VRO_REF(ViroContext) context_j) {
    if (context_j == 0) {
        return ARUtilsCreateFloatArrayFromVector3f(VROVector3f());
    }
    std::shared_ptr<ViroContext> context = VRO_REF_GET(ViroContext, context_j);
    VROVector3f position = context->getInputController()->getPresenter()->getLastKnownForward();
    return ARUtilsCreateFloatArrayFromVector3f(position);
}

/*
 The callbacks of the two asynchronous reads below are held through strong global
 references until they have been called. They were held weakly, and nothing else holds
 them on the Java side: a garbage collection between the request and the answer cleared
 the reference, and the answer was dropped without the caller ever hearing. Each path
 below calls the callback once and then releases it. A context that is gone answers
 like one with nothing to report (a zero vector, no ray) rather than not at all.
 */
VRO_METHOD(void, nativeGetControllerForwardVectorAsync)(VRO_ARGS
                                                        VRO_REF(ViroContext) native_render_context_ref,
                                                        VRO_OBJECT callback) {
    if (native_render_context_ref == 0) {
        return;
    }
    VRO_OBJECT callback_g = VRO_NEW_GLOBAL_REF(callback);
    std::weak_ptr<ViroContext> helperContext_w = VRO_REF_GET(ViroContext, native_render_context_ref);

    VROPlatformDispatchAsyncApplication([helperContext_w, callback_g] {
        VRO_ENV env = VROPlatformGetJNIEnv();
        VROVector3f position;
        std::shared_ptr<ViroContext> helperContext = helperContext_w.lock();
        if (helperContext) {
            position = helperContext->getInputController()->getPresenter()->getLastKnownForward();
        }
        VROPlatformCallHostFunction(callback_g,
                                    "onGetForwardVector", "(FFF)V", position.x, position.y, position.z);
        VRO_DELETE_GLOBAL_REF(callback_g);
    });
}

VRO_METHOD(void, nativeGetControllerRayAsync)(VRO_ARGS
                                              VRO_REF(ViroContext) native_render_context_ref,
                                              VRO_INT source,
                                              VRO_OBJECT callback) {
    if (native_render_context_ref == 0) {
        return;
    }
    VRO_OBJECT callback_g = VRO_NEW_GLOBAL_REF(callback);
    std::weak_ptr<ViroContext> helperContext_w = VRO_REF_GET(ViroContext, native_render_context_ref);

    // The poses are written on the render thread, so they are read there too.
    VROPlatformDispatchAsyncRenderer([helperContext_w, callback_g, source] {
        std::shared_ptr<ViroContext> helperContext = helperContext_w.lock();
        VROVector3f origin, forward;
        bool found = helperContext &&
                     helperContext->getInputController()->getSourceRay(source, &origin, &forward);

        VROPlatformDispatchAsyncApplication([callback_g, found, origin, forward] {
            VRO_ENV env = VROPlatformGetJNIEnv();
            VRO_FLOAT_ARRAY jOrigin = found ? ARUtilsCreateFloatArrayFromVector3f(origin) : nullptr;
            VRO_FLOAT_ARRAY jForward = found ? ARUtilsCreateFloatArrayFromVector3f(forward) : nullptr;
            VROPlatformCallHostFunction(callback_g, "onGetRay", "([F[F)V", jOrigin, jForward);
            VRO_DELETE_GLOBAL_REF(callback_g);
        });
    });
}
/**
 * TODO VIRO-704: Add APIs for custom controls - replacing Obj or adding tooltip support.
 */

}  // extern "C"
