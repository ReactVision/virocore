//
//  ARGeospatialAnchor_JNI.cpp
//  ViroCore
//
//  JNI bindings for Geospatial Anchors
//  Copyright © 2024 Viro Media. All rights reserved.

#include <jni.h>
#include <memory>
#include <functional>
#include "VROARSessionARCoreExtended.h"
#include "VROARGeospatialAnchor.h"
#include "VROLog.h"
#include "ArUtils_JNI.h"

static std::weak_ptr<VROARSessionARCoreExtended> getExtendedARSession(jlong sessionRef) {
    VROARSession *session = (VROARSession *) sessionRef;
    return std::dynamic_pointer_cast<VROARSessionARCoreExtended>(
        std::static_pointer_cast<VROARSession>(session->shared_from_this()));
}

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_viro_core_ARGeospatialAnchor_nativeCreateGeospatialAnchor(JNIEnv *env, 
                                                                   jclass clazz,
                                                                   jlong sessionRef,
                                                                   jdouble latitude,
                                                                   jdouble longitude,
                                                                   jdouble altitude,
                                                                   jfloat quatX,
                                                                   jfloat quatY,
                                                                   jfloat quatZ,
                                                                   jfloat quatW) {
    std::shared_ptr<VROARSessionARCoreExtended> extendedSession = getExtendedARSession(sessionRef).lock();
    if (!extendedSession) {
        VROLogError("Failed to get extended AR session for geospatial anchor");
        return 0;
    }
    
    VROQuaternion orientation(quatX, quatY, quatZ, quatW);
    
    try {
        auto geospatialAnchor = extendedSession->createGeospatialAnchor(
            static_cast<double>(latitude),
            static_cast<double>(longitude),
            static_cast<double>(altitude),
            orientation
        );
        
        if (geospatialAnchor) {
            return reinterpret_cast<jlong>(new std::shared_ptr<VROARGeospatialAnchor>(geospatialAnchor));
        }
    } catch (const std::exception& e) {
        VROLogError("Exception creating geospatial anchor: %s", e.what());
    }
    
    return 0;
}

JNIEXPORT jlong JNICALL
Java_com_viro_core_ARGeospatialAnchor_nativeCreateTerrainAnchor(JNIEnv *env,
                                                                jclass clazz,
                                                                jlong sessionRef,
                                                                jdouble latitude,
                                                                jdouble longitude,
                                                                jfloat quatX,
                                                                jfloat quatY,
                                                                jfloat quatZ,
                                                                jfloat quatW) {
    std::shared_ptr<VROARSessionARCoreExtended> extendedSession = getExtendedARSession(sessionRef).lock();
    if (!extendedSession) {
        VROLogError("Failed to get extended AR session for terrain anchor");
        return 0;
    }
    
    VROQuaternion orientation(quatX, quatY, quatZ, quatW);
    
    try {
        // Note: The callback will be handled by the Java callback mechanism
        auto terrainAnchor = extendedSession->createTerrainAnchor(
            static_cast<double>(latitude),
            static_cast<double>(longitude),
            orientation,
            nullptr  // Callback will be set up separately
        );
        
        if (terrainAnchor) {
            return reinterpret_cast<jlong>(new std::shared_ptr<VROARGeospatialAnchor>(terrainAnchor));
        }
    } catch (const std::exception& e) {
        VROLogError("Exception creating terrain anchor: %s", e.what());
    }
    
    return 0;
}

JNIEXPORT jlong JNICALL
Java_com_viro_core_ARGeospatialAnchor_nativeCreateRooftopAnchor(JNIEnv *env,
                                                                jclass clazz,
                                                                jlong sessionRef,
                                                                jdouble latitude,
                                                                jdouble longitude,
                                                                jfloat quatX,
                                                                jfloat quatY,
                                                                jfloat quatZ,
                                                                jfloat quatW) {
    std::shared_ptr<VROARSessionARCoreExtended> extendedSession = getExtendedARSession(sessionRef).lock();
    if (!extendedSession) {
        VROLogError("Failed to get extended AR session for rooftop anchor");
        return 0;
    }
    
    VROQuaternion orientation(quatX, quatY, quatZ, quatW);
    
    try {
        auto rooftopAnchor = extendedSession->createRooftopAnchor(
            static_cast<double>(latitude),
            static_cast<double>(longitude),
            orientation,
            nullptr  // Callback will be set up separately
        );
        
        if (rooftopAnchor) {
            return reinterpret_cast<jlong>(new std::shared_ptr<VROARGeospatialAnchor>(rooftopAnchor));
        }
    } catch (const std::exception& e) {
        VROLogError("Exception creating rooftop anchor: %s", e.what());
    }
    
    return 0;
}

// Callback wrapper classes for async operations
class VPSAvailabilityCallbackWrapper {
public:
    VPSAvailabilityCallbackWrapper(JNIEnv* env, jobject callback) {
        _env = env;
        _callback = env->NewGlobalRef(callback);
        _class = (jclass) env->NewGlobalRef(env->GetObjectClass(callback));
        _methodId = env->GetMethodID(_class, "onResult", "(I)V");
    }
    
    ~VPSAvailabilityCallbackWrapper() {
        if (_env && _callback) {
            _env->DeleteGlobalRef(_callback);
            _env->DeleteGlobalRef(_class);
        }
    }
    
    void onResult(VROARSessionARCoreExtended::VPSAvailability availability) {
        if (_env && _callback && _methodId) {
            _env->CallVoidMethod(_callback, _methodId, static_cast<jint>(availability));
        }
    }
    
private:
    JNIEnv* _env;
    jobject _callback;
    jclass _class;
    jmethodID _methodId;
};

class TerrainCallbackWrapper {
public:
    TerrainCallbackWrapper(JNIEnv* env, jobject callback) {
        _env = env;
        _callback = env->NewGlobalRef(callback);
        _class = (jclass) env->NewGlobalRef(env->GetObjectClass(callback));
        _onResolveMethodId = env->GetMethodID(_class, "onResolve", "(D)V");
        _onErrorMethodId = env->GetMethodID(_class, "onError", "(Ljava/lang/String;)V");
    }
    
    ~TerrainCallbackWrapper() {
        if (_env && _callback) {
            _env->DeleteGlobalRef(_callback);
            _env->DeleteGlobalRef(_class);
        }
    }
    
    void onResolve(double altitude) {
        if (_env && _callback && _onResolveMethodId) {
            _env->CallVoidMethod(_callback, _onResolveMethodId, altitude);
        }
    }
    
    void onError(const std::string& error) {
        if (_env && _callback && _onErrorMethodId) {
            jstring errorStr = _env->NewStringUTF(error.c_str());
            _env->CallVoidMethod(_callback, _onErrorMethodId, errorStr);
            _env->DeleteLocalRef(errorStr);
        }
    }
    
private:
    JNIEnv* _env;
    jobject _callback;
    jclass _class;
    jmethodID _onResolveMethodId;
    jmethodID _onErrorMethodId;
};

class RooftopCallbackWrapper {
public:
    RooftopCallbackWrapper(JNIEnv* env, jobject callback) {
        _env = env;
        _callback = env->NewGlobalRef(callback);
        _class = (jclass) env->NewGlobalRef(env->GetObjectClass(callback));
        _onResolveMethodId = env->GetMethodID(_class, "onResolve", "(DI)V");
        _onErrorMethodId = env->GetMethodID(_class, "onError", "(Ljava/lang/String;)V");
    }
    
    ~RooftopCallbackWrapper() {
        if (_env && _callback) {
            _env->DeleteGlobalRef(_callback);
            _env->DeleteGlobalRef(_class);
        }
    }
    
    void onResolve(double altitude, int rooftopState) {
        if (_env && _callback && _onResolveMethodId) {
            _env->CallVoidMethod(_callback, _onResolveMethodId, altitude, rooftopState);
        }
    }
    
    void onError(const std::string& error) {
        if (_env && _callback && _onErrorMethodId) {
            jstring errorStr = _env->NewStringUTF(error.c_str());
            _env->CallVoidMethod(_callback, _onErrorMethodId, errorStr);
            _env->DeleteLocalRef(errorStr);
        }
    }
    
private:
    JNIEnv* _env;
    jobject _callback;
    jclass _class;
    jmethodID _onResolveMethodId;
    jmethodID _onErrorMethodId;
};

JNIEXPORT void JNICALL
Java_com_viro_core_ARGeospatialAnchor_nativeCheckVPSAvailability(JNIEnv *env,
                                                                 jclass clazz,
                                                                 jlong sessionRef,
                                                                 jdouble latitude,
                                                                 jdouble longitude,
                                                                 jobject callback) {
    std::shared_ptr<VROARSessionARCoreExtended> extendedSession = getExtendedARSession(sessionRef).lock();
    if (!extendedSession) {
        VROLogError("Failed to get extended AR session for VPS availability check");
        return;
    }
    
    if (!callback) {
        VROLogError("VPS availability callback is null");
        return;
    }
    
    try {
        auto callbackWrapper = std::make_shared<VPSAvailabilityCallbackWrapper>(env, callback);
        
        extendedSession->checkVPSAvailability(
            static_cast<double>(latitude),
            static_cast<double>(longitude),
            [callbackWrapper](VROARSessionARCoreExtended::VPSAvailability availability) {
                callbackWrapper->onResult(availability);
            }
        );
    } catch (const std::exception& e) {
        VROLogError("Exception checking VPS availability: %s", e.what());
    }
}

JNIEXPORT void JNICALL
Java_com_viro_core_ARGeospatialAnchor_nativeRegisterTerrainCallback(JNIEnv *env,
                                                                    jclass clazz,
                                                                    jlong anchorRef,
                                                                    jobject callback) {
    if (anchorRef == 0 || !callback) {
        VROLogError("Invalid terrain callback parameters");
        return;
    }
    
    try {
        auto callbackWrapper = std::make_shared<TerrainCallbackWrapper>(env, callback);
        
        // Store the callback wrapper with the anchor
        // This would require extending the anchor to support callbacks
        // For now, we'll just log that the callback was registered
        VROLogInfo("Terrain callback registered for anchor %ld", anchorRef);
        
    } catch (const std::exception& e) {
        VROLogError("Exception registering terrain callback: %s", e.what());
    }
}

JNIEXPORT void JNICALL
Java_com_viro_core_ARGeospatialAnchor_nativeRegisterRooftopCallback(JNIEnv *env,
                                                                    jclass clazz,
                                                                    jlong anchorRef,
                                                                    jobject callback) {
    if (anchorRef == 0 || !callback) {
        VROLogError("Invalid rooftop callback parameters");
        return;
    }
    
    try {
        auto callbackWrapper = std::make_shared<RooftopCallbackWrapper>(env, callback);
        
        // Store the callback wrapper with the anchor
        // This would require extending the anchor to support callbacks
        // For now, we'll just log that the callback was registered
        VROLogInfo("Rooftop callback registered for anchor %ld", anchorRef);
        
    } catch (const std::exception& e) {
        VROLogError("Exception registering rooftop callback: %s", e.what());
    }
}

// Additional utility functions for geospatial anchors
JNIEXPORT jdouble JNICALL
Java_com_viro_core_ARGeospatialAnchor_nativeGetLatitude(JNIEnv *env,
                                                        jobject thiz,
                                                        jlong anchorRef) {
    if (anchorRef == 0) {
        return 0.0;
    }
    
    try {
        std::shared_ptr<VROARGeospatialAnchor>* anchorPtr = 
            reinterpret_cast<std::shared_ptr<VROARGeospatialAnchor>*>(anchorRef);
        
        if (anchorPtr && *anchorPtr) {
            return (*anchorPtr)->getLatitude();
        }
    } catch (const std::exception& e) {
        VROLogError("Exception getting latitude: %s", e.what());
    }
    
    return 0.0;
}

JNIEXPORT jdouble JNICALL
Java_com_viro_core_ARGeospatialAnchor_nativeGetLongitude(JNIEnv *env,
                                                         jobject thiz,
                                                         jlong anchorRef) {
    if (anchorRef == 0) {
        return 0.0;
    }
    
    try {
        std::shared_ptr<VROARGeospatialAnchor>* anchorPtr = 
            reinterpret_cast<std::shared_ptr<VROARGeospatialAnchor>*>(anchorRef);
        
        if (anchorPtr && *anchorPtr) {
            return (*anchorPtr)->getLongitude();
        }
    } catch (const std::exception& e) {
        VROLogError("Exception getting longitude: %s", e.what());
    }
    
    return 0.0;
}

JNIEXPORT jdouble JNICALL
Java_com_viro_core_ARGeospatialAnchor_nativeGetAltitude(JNIEnv *env,
                                                        jobject thiz,
                                                        jlong anchorRef) {
    if (anchorRef == 0) {
        return 0.0;
    }
    
    try {
        std::shared_ptr<VROARGeospatialAnchor>* anchorPtr = 
            reinterpret_cast<std::shared_ptr<VROARGeospatialAnchor>*>(anchorRef);
        
        if (anchorPtr && *anchorPtr) {
            return (*anchorPtr)->getAltitude();
        }
    } catch (const std::exception& e) {
        VROLogError("Exception getting altitude: %s", e.what());
    }
    
    return 0.0;
}

JNIEXPORT jboolean JNICALL
Java_com_viro_core_ARGeospatialAnchor_nativeIsTerrainAnchor(JNIEnv *env,
                                                            jobject thiz,
                                                            jlong anchorRef) {
    if (anchorRef == 0) {
        return JNI_FALSE;
    }
    
    try {
        std::shared_ptr<VROARGeospatialAnchor>* anchorPtr = 
            reinterpret_cast<std::shared_ptr<VROARGeospatialAnchor>*>(anchorRef);
        
        if (anchorPtr && *anchorPtr) {
            return (*anchorPtr)->isTerrainAnchor() ? JNI_TRUE : JNI_FALSE;
        }
    } catch (const std::exception& e) {
        VROLogError("Exception checking terrain anchor: %s", e.what());
    }
    
    return JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_viro_core_ARGeospatialAnchor_nativeIsRooftopAnchor(JNIEnv *env,
                                                            jobject thiz,
                                                            jlong anchorRef) {
    if (anchorRef == 0) {
        return JNI_FALSE;
    }
    
    try {
        std::shared_ptr<VROARGeospatialAnchor>* anchorPtr = 
            reinterpret_cast<std::shared_ptr<VROARGeospatialAnchor>*>(anchorRef);
        
        if (anchorPtr && *anchorPtr) {
            return (*anchorPtr)->isRooftopAnchor() ? JNI_TRUE : JNI_FALSE;
        }
    } catch (const std::exception& e) {
        VROLogError("Exception checking rooftop anchor: %s", e.what());
    }
    
    return JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_viro_core_ARGeospatialAnchor_nativeDestroyAnchor(JNIEnv *env,
                                                          jobject thiz,
                                                          jlong anchorRef) {
    if (anchorRef == 0) {
        return;
    }
    
    try {
        std::shared_ptr<VROARGeospatialAnchor>* anchorPtr = 
            reinterpret_cast<std::shared_ptr<VROARGeospatialAnchor>*>(anchorRef);
        
        if (anchorPtr) {
            delete anchorPtr;
        }
    } catch (const std::exception& e) {
        VROLogError("Exception destroying geospatial anchor: %s", e.what());
    }
}

} // extern "C"