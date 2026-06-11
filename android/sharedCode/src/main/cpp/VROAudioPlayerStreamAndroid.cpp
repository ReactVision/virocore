//
//  VROAudioPlayerStreamAndroid.cpp
//  ViroKit
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

#include "VROAudioPlayerStreamAndroid.h"
#include "VROPlatformUtil.h"
#include "VROLog.h"

static const char *kClass = "com/viro/core/internal/StreamingAudioPlayer";

VROAudioPlayerStreamAndroid::VROAudioPlayerStreamAndroid()
    : _jPlayer(nullptr), _streaming(false) {

    JNIEnv *env = VROPlatformGetJNIEnv();
    jclass cls = env->FindClass(kClass);
    if (!cls) {
        pinfo("VROAudioPlayerStreamAndroid: StreamingAudioPlayer class not found");
        return;
    }
    jmethodID init = env->GetMethodID(cls, "<init>", "()V");
    jobject obj = env->NewObject(cls, init);
    _jPlayer = env->NewGlobalRef(obj);
    env->DeleteLocalRef(obj);
    env->DeleteLocalRef(cls);
}

VROAudioPlayerStreamAndroid::~VROAudioPlayerStreamAndroid() {
    if (!_jPlayer) return;
    JNIEnv *env = VROPlatformGetJNIEnv();
    jclass cls = env->GetObjectClass(_jPlayer);
    jmethodID destroy = env->GetMethodID(cls, "destroy", "()V");
    env->CallVoidMethod(_jPlayer, destroy);
    env->DeleteLocalRef(cls);
    env->DeleteGlobalRef(_jPlayer);
    _jPlayer = nullptr;
}

void VROAudioPlayerStreamAndroid::beginStreaming(int sampleRate, int channels) {
    if (!_jPlayer) return;
    JNIEnv *env = VROPlatformGetJNIEnv();
    jclass cls = env->GetObjectClass(_jPlayer);
    jmethodID m = env->GetMethodID(cls, "beginStreaming", "(II)V");
    env->CallVoidMethod(_jPlayer, m, (jint)sampleRate, (jint)channels);
    env->DeleteLocalRef(cls);
    _streaming = true;
    if (_delegate) {
        _delegate->soundIsReady();
    }
}

void VROAudioPlayerStreamAndroid::play() {
    if (!_jPlayer) return;
    JNIEnv *env = VROPlatformGetJNIEnv();
    jclass cls = env->GetObjectClass(_jPlayer);
    env->CallVoidMethod(_jPlayer, env->GetMethodID(cls, "play", "()V"));
    env->DeleteLocalRef(cls);
}

void VROAudioPlayerStreamAndroid::pause() {
    if (!_jPlayer) return;
    JNIEnv *env = VROPlatformGetJNIEnv();
    jclass cls = env->GetObjectClass(_jPlayer);
    env->CallVoidMethod(_jPlayer, env->GetMethodID(cls, "pause", "()V"));
    env->DeleteLocalRef(cls);
}

void VROAudioPlayerStreamAndroid::setVolume(float volume) {
    if (!_jPlayer) return;
    JNIEnv *env = VROPlatformGetJNIEnv();
    jclass cls = env->GetObjectClass(_jPlayer);
    jmethodID m = env->GetMethodID(cls, "setVolume", "(F)V");
    env->CallVoidMethod(_jPlayer, m, (jfloat)volume);
    env->DeleteLocalRef(cls);
}

void VROAudioPlayerStreamAndroid::setMuted(bool muted) {
    if (!_jPlayer) return;
    JNIEnv *env = VROPlatformGetJNIEnv();
    jclass cls = env->GetObjectClass(_jPlayer);
    jmethodID m = env->GetMethodID(cls, "setMuted", "(Z)V");
    env->CallVoidMethod(_jPlayer, m, (jboolean)muted);
    env->DeleteLocalRef(cls);
}

size_t VROAudioPlayerStreamAndroid::pushSamples(const float *data, size_t count) {
    if (!_jPlayer || !_streaming || count == 0) return 0;
    JNIEnv *env = VROPlatformGetJNIEnv();

    jfloatArray arr = env->NewFloatArray((jsize)count);
    env->SetFloatArrayRegion(arr, 0, (jsize)count, data);

    jclass cls = env->GetObjectClass(_jPlayer);
    jmethodID m = env->GetMethodID(cls, "write", "([F)I");
    jint written = env->CallIntMethod(_jPlayer, m, arr);

    env->DeleteLocalRef(arr);
    env->DeleteLocalRef(cls);
    return (written > 0) ? (size_t)written : 0;
}
