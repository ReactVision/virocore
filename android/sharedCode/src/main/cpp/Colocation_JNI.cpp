//
//  Colocation_JNI.cpp
//  ViroRenderer
//
//  Copyright © 2026 ReactVision. All rights reserved.
//
//  JNI surface for the co-location frame channel.
//
//  Bound to a plain Java class rather than a scene controller: the channel
//  needs no AR session, only a room id and poses, so tying it to one would make
//  it unreachable on the platforms that have no VROARSession.
//

#include <jni.h>
#include <string>
#include <memory>

#include "VROColocationSession.h"
#include "VROMatrix4f.h"
#include "VROPlatformUtil.h"

#define VRO_METHOD(return_type, method_name) \
  JNIEXPORT return_type JNICALL              \
      Java_com_viro_core_ColocationSession_##method_name

namespace {

std::string toStd(JNIEnv *env, jstring s) {
    if (!s) return "";
    const char *c = env->GetStringUTFChars(s, nullptr);
    std::string out = c ? c : "";
    if (c) env->ReleaseStringUTFChars(s, c);
    return out;
}

/** 16 comma-separated floats, column-major — the encoding every layer uses. */
bool parseMatrixCsv(const std::string &csv, VROMatrix4f *out) {
    float v[16];
    size_t pos = 0;
    for (int i = 0; i < 16; i++) {
        size_t comma = csv.find(',', pos);
        std::string tok = csv.substr(pos, comma == std::string::npos
                                            ? std::string::npos : comma - pos);
        try { v[i] = std::stof(tok); } catch (...) { return false; }
        if (comma == std::string::npos) { if (i != 15) return false; break; }
        pos = comma + 1;
    }
    *out = VROMatrix4f(v);
    return true;
}

} // namespace

extern "C" {

VRO_METHOD(jboolean, nativeIsAvailable)(JNIEnv *, jobject) {
    return VROColocationSession::get().isAvailable() ? JNI_TRUE : JNI_FALSE;
}

VRO_METHOD(void, nativeJoin)(JNIEnv *env, jobject obj, jstring key_j,
                             jstring roomId_j, jstring apiKey_j,
                             jstring projectId_j, jstring endpoint_j) {
    std::string keyStr    = toStd(env, key_j);
    std::string roomId    = toStd(env, roomId_j);
    std::string apiKey    = toStd(env, apiKey_j);
    std::string projectId = toStd(env, projectId_j);
    std::string endpoint  = toStd(env, endpoint_j);

    jobject weakObj = env->NewWeakGlobalRef(obj);

    VROColocationSession::get().join(roomId, apiKey, projectId, endpoint,
        [weakObj, keyStr](bool success, std::string error) {
            VROPlatformDispatchAsyncApplication([weakObj, keyStr, success, error] {
                JNIEnv *env = VROPlatformGetJNIEnv();
                jobject local = env->NewLocalRef(weakObj);
                if (local == nullptr) {
                    env->DeleteWeakGlobalRef(weakObj);
                    return;
                }
                jstring jKey = env->NewStringUTF(keyStr.c_str());
                jstring jErr = env->NewStringUTF(error.c_str());
                VROPlatformCallHostFunction(local, "onJoinResult",
                    "(Ljava/lang/String;ZLjava/lang/String;)V",
                    jKey, (jboolean)success, jErr);
                env->DeleteLocalRef(local);
                env->DeleteWeakGlobalRef(weakObj);
            });
        });
}

VRO_METHOD(void, nativeLeave)(JNIEnv *, jobject) {
    VROColocationSession::get().leave();
}

VRO_METHOD(void, nativeSetLocalPose)(JNIEnv *env, jobject, jstring csv_j) {
    VROMatrix4f m;
    if (!parseMatrixCsv(toStd(env, csv_j), &m)) return;
    VROColocationSession::get().setLocalPose(m);
}

VRO_METHOD(jint, nativeGetState)(JNIEnv *, jobject) {
    return (jint)VROColocationSession::get().state();
}

VRO_METHOD(jstring, nativeGetLocalPeerId)(JNIEnv *env, jobject) {
    return env->NewStringUTF(VROColocationSession::get().localPeerId().c_str());
}

/**
 * Peers as one flat string rather than an object array.
 *
 * Building a String[][] or a parcelable list across JNI costs a local ref per
 * field and is polled several times a second; one string parsed on the Java
 * side is cheaper and far less code. Format, semicolon-separated rows:
 *   peerId,px,py,pz,qx,qy,qz,qw,timestampMs,localized
 */
VRO_METHOD(jstring, nativeGetPeers)(JNIEnv *env, jobject) {
    std::string out;
    char buf[256];
    for (const auto &p : VROColocationSession::get().peers()) {
        if (!out.empty()) out += ';';
        snprintf(buf, sizeof(buf), "%s,%g,%g,%g,%g,%g,%g,%g,%.0f,%d",
                 p.peerId.c_str(),
                 p.position[0], p.position[1], p.position[2],
                 p.rotation[0], p.rotation[1], p.rotation[2], p.rotation[3],
                 p.timestampMs, p.localized ? 1 : 0);
        out += buf;
    }
    return env->NewStringUTF(out.c_str());
}

} // extern "C"
