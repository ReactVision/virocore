//  VROGameLoop_JNI.cpp — JNI bridge for VROGameLoopListener
//  Copyright © 2026 ReactVision. All rights reserved.

#include "VROGameLoopListener.h"
#include "PersistentRef.h"
#include "VRODefines.h"
#include VRO_C_INCLUDE
#include "ViroContextAndroid_JNI.h"

#define VRO_METHOD(return_type, method_name) \
  JNIEXPORT return_type JNICALL              \
      Java_com_viromedia_bridge_component_VRTGameLoopView_##method_name

static inline jlong toListenerRef(std::shared_ptr<VROGameLoopListener> l) {
    return reinterpret_cast<jlong>(new PersistentRef<VROGameLoopListener>(l));
}
static inline std::shared_ptr<VROGameLoopListener> fromListenerRef(jlong ref) {
    return reinterpret_cast<PersistentRef<VROGameLoopListener>*>(ref)->get();
}

// Holds the JavaVM + GlobalRef to the VRTGameLoopView for callbacks.
struct GameLoopCallbackCtx {
    JavaVM*  jvm;
    jobject  viewRef; // GlobalRef

    ~GameLoopCallbackCtx() {
        JNIEnv *e = nullptr;
        if (jvm->AttachCurrentThread(&e, nullptr) == 0 && e) {
            e->DeleteGlobalRef(viewRef);
        }
    }

    void emit(const char *method, const char *sig, ...) const {
        JNIEnv *e = nullptr;
        if (jvm->AttachCurrentThread(&e, nullptr) != 0 || !e) return;
        jclass cls = e->GetObjectClass(viewRef);
        jmethodID mid = e->GetMethodID(cls, method, sig);
        if (mid) {
            va_list args; va_start(args, sig);
            e->CallVoidMethodV(viewRef, mid, args);
            va_end(args);
        }
        e->DeleteLocalRef(cls);
    }
};

extern "C" {

VRO_METHOD(jlong, nativeCreate)(VRO_ARGS jlong contextRef) {
    VRO_METHOD_PREAMBLE;
    auto context = VRO_REF_GET(ViroContext, contextRef);
    auto ctxAndroid = std::dynamic_pointer_cast<ViroContextAndroid>(context);
    if (!ctxAndroid) return 0;

    auto frameSynchronizer = ctxAndroid->getFrameSynchronizer();
    if (!frameSynchronizer) return 0;

    auto listener = std::make_shared<VROGameLoopListener>();

    auto ctx = std::make_shared<GameLoopCallbackCtx>();
    env->GetJavaVM(&ctx->jvm);
    ctx->viewRef = env->NewGlobalRef(obj);

    listener->setOnFrameWillRender([ctx](float dt, float elapsed) {
        ctx->emit("notifyUpdate", "(FF)V", (jfloat)dt, (jfloat)elapsed);
    });

    listener->setOnFrameDidRender([ctx](float dt, float elapsed) {
        ctx->emit("notifyLateUpdate", "(FF)V", (jfloat)dt, (jfloat)elapsed);
    });

    listener->setOnFixedStep([ctx](float dt) {
        ctx->emit("notifyFixedUpdate", "(F)V", (jfloat)dt);
    });

    frameSynchronizer->addFrameListener(listener);
    return toListenerRef(listener);
}

VRO_METHOD(void, nativeDestroy)(VRO_ARGS jlong listenerRef, jlong contextRef) {
    VRO_METHOD_PREAMBLE;
    auto listener = fromListenerRef(listenerRef);
    if (!listener) return;

    auto context = VRO_REF_GET(ViroContext, contextRef);
    auto ctxAndroid = std::dynamic_pointer_cast<ViroContextAndroid>(context);
    if (ctxAndroid) {
        auto sync = ctxAndroid->getFrameSynchronizer();
        if (sync) sync->removeFrameListener(listener);
    }
    delete reinterpret_cast<PersistentRef<VROGameLoopListener>*>(listenerRef);
}

VRO_METHOD(void, nativeSetFixedHz)(VRO_ARGS jlong listenerRef, jfloat hz) {
    VRO_METHOD_PREAMBLE;
    auto listener = fromListenerRef(listenerRef);
    if (listener) listener->setFixedHz((float)hz);
}

} // extern "C"
