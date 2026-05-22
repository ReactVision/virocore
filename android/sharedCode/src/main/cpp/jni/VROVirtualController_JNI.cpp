//
//  VROVirtualController_JNI.cpp
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

#include <memory>
#include <string>
#include "PersistentRef.h"
#include "VROInputState.h"
#include "VROVirtualControllerRegistry.h"
#include "VROPlatformUtil.h"
#include "VRODefines.h"
#include VRO_C_INCLUDE

#if VRO_PLATFORM_ANDROID
#define VRO_METHOD(return_type, method_name) \
  JNIEXPORT return_type JNICALL              \
      Java_com_viromedia_bridge_component_VRTVirtualJoystickView_##method_name
#endif

static inline jlong toRef(std::shared_ptr<VROInputState> state) {
    return reinterpret_cast<intptr_t>(new PersistentRef<VROInputState>(state));
}

static inline std::shared_ptr<VROInputState> fromRef(jlong ref) {
    return reinterpret_cast<PersistentRef<VROInputState> *>(ref)->get();
}

extern "C" {

VRO_METHOD(jlong, nativeAcquire)(VRO_ARGS jstring controllerId) {
    std::string id = VRO_STRING_STL(controllerId);
    auto state = VROVirtualControllerRegistry::instance().acquire(id);
    return toRef(state);
}

VRO_METHOD(void, nativeRelease)(VRO_ARGS jstring controllerId, jlong ref) {
    std::string id = VRO_STRING_STL(controllerId);
    VROVirtualControllerRegistry::instance().release(id);
    delete reinterpret_cast<PersistentRef<VROInputState> *>(ref);
}

VRO_METHOD(void, nativeSetStickL)(VRO_ARGS jlong ref, jfloat x, jfloat y) {
    fromRef(ref)->setStickL((float)x, (float)y);
}

VRO_METHOD(void, nativeSetStickR)(VRO_ARGS jlong ref, jfloat x, jfloat y) {
    fromRef(ref)->setStickR((float)x, (float)y);
}

VRO_METHOD(void, nativeSetButton)(VRO_ARGS jlong ref, jint buttonIndex, jboolean pressed) {
    fromRef(ref)->setButton((int)buttonIndex, (bool)pressed);
}

} // extern "C"
