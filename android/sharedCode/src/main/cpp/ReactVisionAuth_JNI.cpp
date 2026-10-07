//
//  ReactVisionAuth_JNI.cpp
//  ViroRenderer
//
//  Copyright © 2026 ReactVision. All rights reserved.
//

#include <jni.h>
#include <string>

#include "VROReactVisionAuth.h"

#define VRO_METHOD(return_type, method_name) \
  JNIEXPORT return_type JNICALL              \
      Java_com_viro_core_ReactVisionAuth_##method_name

namespace {

std::string toStd(JNIEnv *env, jstring s) {
    if (!s) return "";
    const char *c = env->GetStringUTFChars(s, nullptr);
    std::string out = c ? c : "";
    if (c) env->ReleaseStringUTFChars(s, c);
    return out;
}

} // namespace

extern "C" {

VRO_METHOD(void, nativeSetSession)(JNIEnv *env, jclass, jstring baseUrl_j,
                                   jstring accessToken_j, jstring clientTag_j,
                                   jstring functionRegion_j) {
    VROReactVisionAuth::get().setSession(toStd(env, baseUrl_j),
                                         toStd(env, accessToken_j),
                                         toStd(env, clientTag_j),
                                         toStd(env, functionRegion_j));
}

VRO_METHOD(void, nativeClearSession)(JNIEnv *, jclass) {
    VROReactVisionAuth::get().clearSession();
}

VRO_METHOD(void, nativeSetProjectId)(JNIEnv *env, jclass, jstring projectId_j) {
    VROReactVisionAuth::get().setProjectId(toStd(env, projectId_j));
}

} // extern "C"
