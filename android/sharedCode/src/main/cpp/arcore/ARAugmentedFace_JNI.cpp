//
//  ARAugmentedFace_JNI.cpp
//  ViroCore
//
//  JNI bindings for Augmented Faces
//  Copyright © 2024 Viro Media. All rights reserved.

#include <jni.h>
#include <memory>
#include <functional>
#include "VROARSessionARCoreExtended.h"
#include "VROARAugmentedFace.h"
#include "VROLog.h"
#include "ArUtils_JNI.h"

static std::weak_ptr<VROARSessionARCoreExtended> getExtendedARSession(jlong sessionRef) {
    VROARSession *session = (VROARSession *) sessionRef;
    return std::dynamic_pointer_cast<VROARSessionARCoreExtended>(
        std::static_pointer_cast<VROARSession>(session->shared_from_this()));
}

extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_viro_core_ARAugmentedFace_nativeIsFaceTrackingSupported(JNIEnv *env,
                                                                 jclass clazz,
                                                                 jlong sessionRef) {
    std::shared_ptr<VROARSessionARCoreExtended> extendedSession = getExtendedARSession(sessionRef).lock();
    if (!extendedSession) {
        return JNI_FALSE;
    }
    
    try {
        return extendedSession->isAugmentedFacesModeSupported() ? JNI_TRUE : JNI_FALSE;
    } catch (const std::exception& e) {
        VROLogError("Exception checking face tracking support: %s", e.what());
    }
    
    return JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_com_viro_core_ARAugmentedFace_nativeGetMaxFaceCount(JNIEnv *env,
                                                         jclass clazz,
                                                         jlong sessionRef) {
    std::shared_ptr<VROARSessionARCoreExtended> extendedSession = getExtendedARSession(sessionRef).lock();
    if (!extendedSession) {
        return 1; // Default to single face tracking
    }
    
    try {
        return extendedSession->getMaxFaceTrackingCount();
    } catch (const std::exception& e) {
        VROLogError("Exception getting max face count: %s", e.what());
    }
    
    return 1;
}

JNIEXPORT jstring JNICALL
Java_com_viro_core_ARAugmentedFace_nativeGetFaceId(JNIEnv *env,
                                                   jobject thiz,
                                                   jlong faceRef) {
    if (faceRef == 0) {
        return nullptr;
    }
    
    try {
        std::shared_ptr<VROARAugmentedFace>* facePtr = 
            reinterpret_cast<std::shared_ptr<VROARAugmentedFace>*>(faceRef);
        
        if (facePtr && *facePtr) {
            std::string id = (*facePtr)->getId();
            if (!id.empty()) {
                return env->NewStringUTF(id.c_str());
            }
        }
    } catch (const std::exception& e) {
        VROLogError("Exception getting face ID: %s", e.what());
    }
    
    return nullptr;
}

JNIEXPORT jfloat JNICALL
Java_com_viro_core_ARAugmentedFace_nativeGetEyeBlinkLeft(JNIEnv *env,
                                                         jobject thiz,
                                                         jlong faceRef) {
    if (faceRef == 0) {
        return 0.0f;
    }
    
    try {
        std::shared_ptr<VROARAugmentedFace>* facePtr = 
            reinterpret_cast<std::shared_ptr<VROARAugmentedFace>*>(faceRef);
        
        if (facePtr && *facePtr) {
            return (*facePtr)->getEyeBlinkLeft();
        }
    } catch (const std::exception& e) {
        VROLogError("Exception getting eye blink left: %s", e.what());
    }
    
    return 0.0f;
}

JNIEXPORT jfloat JNICALL
Java_com_viro_core_ARAugmentedFace_nativeGetEyeBlinkRight(JNIEnv *env,
                                                          jobject thiz,
                                                          jlong faceRef) {
    if (faceRef == 0) {
        return 0.0f;
    }
    
    try {
        std::shared_ptr<VROARAugmentedFace>* facePtr = 
            reinterpret_cast<std::shared_ptr<VROARAugmentedFace>*>(faceRef);
        
        if (facePtr && *facePtr) {
            return (*facePtr)->getEyeBlinkRight();
        }
    } catch (const std::exception& e) {
        VROLogError("Exception getting eye blink right: %s", e.what());
    }
    
    return 0.0f;
}

JNIEXPORT jfloat JNICALL
Java_com_viro_core_ARAugmentedFace_nativeGetMouthSmileLeft(JNIEnv *env,
                                                           jobject thiz,
                                                           jlong faceRef) {
    if (faceRef == 0) {
        return 0.0f;
    }
    
    try {
        std::shared_ptr<VROARAugmentedFace>* facePtr = 
            reinterpret_cast<std::shared_ptr<VROARAugmentedFace>*>(faceRef);
        
        if (facePtr && *facePtr) {
            return (*facePtr)->getMouthSmileLeft();
        }
    } catch (const std::exception& e) {
        VROLogError("Exception getting mouth smile left: %s", e.what());
    }
    
    return 0.0f;
}

JNIEXPORT jfloat JNICALL
Java_com_viro_core_ARAugmentedFace_nativeGetMouthSmileRight(JNIEnv *env,
                                                            jobject thiz,
                                                            jlong faceRef) {
    if (faceRef == 0) {
        return 0.0f;
    }
    
    try {
        std::shared_ptr<VROARAugmentedFace>* facePtr = 
            reinterpret_cast<std::shared_ptr<VROARAugmentedFace>*>(faceRef);
        
        if (facePtr && *facePtr) {
            return (*facePtr)->getMouthSmileRight();
        }
    } catch (const std::exception& e) {
        VROLogError("Exception getting mouth smile right: %s", e.what());
    }
    
    return 0.0f;
}

JNIEXPORT jfloat JNICALL
Java_com_viro_core_ARAugmentedFace_nativeGetMouthOpen(JNIEnv *env,
                                                      jobject thiz,
                                                      jlong faceRef) {
    if (faceRef == 0) {
        return 0.0f;
    }
    
    try {
        std::shared_ptr<VROARAugmentedFace>* facePtr = 
            reinterpret_cast<std::shared_ptr<VROARAugmentedFace>*>(faceRef);
        
        if (facePtr && *facePtr) {
            return (*facePtr)->getMouthOpen();
        }
    } catch (const std::exception& e) {
        VROLogError("Exception getting mouth open: %s", e.what());
    }
    
    return 0.0f;
}

JNIEXPORT jfloat JNICALL
Java_com_viro_core_ARAugmentedFace_nativeGetBrowUpLeft(JNIEnv *env,
                                                       jobject thiz,
                                                       jlong faceRef) {
    if (faceRef == 0) {
        return 0.0f;
    }
    
    try {
        std::shared_ptr<VROARAugmentedFace>* facePtr = 
            reinterpret_cast<std::shared_ptr<VROARAugmentedFace>*>(faceRef);
        
        if (facePtr && *facePtr) {
            return (*facePtr)->getBrowUpLeft();
        }
    } catch (const std::exception& e) {
        VROLogError("Exception getting brow up left: %s", e.what());
    }
    
    return 0.0f;
}

JNIEXPORT jfloat JNICALL
Java_com_viro_core_ARAugmentedFace_nativeGetBrowUpRight(JNIEnv *env,
                                                        jobject thiz,
                                                        jlong faceRef) {
    if (faceRef == 0) {
        return 0.0f;
    }
    
    try {
        std::shared_ptr<VROARAugmentedFace>* facePtr = 
            reinterpret_cast<std::shared_ptr<VROARAugmentedFace>*>(faceRef);
        
        if (facePtr && *facePtr) {
            return (*facePtr)->getBrowUpRight();
        }
    } catch (const std::exception& e) {
        VROLogError("Exception getting brow up right: %s", e.what());
    }
    
    return 0.0f;
}

JNIEXPORT jfloat JNICALL
Java_com_viro_core_ARAugmentedFace_nativeGetBlendShapeCoefficient(JNIEnv *env,
                                                                  jobject thiz,
                                                                  jlong faceRef,
                                                                  jstring blendShapeName) {
    if (faceRef == 0 || !blendShapeName) {
        return 0.0f;
    }
    
    try {
        std::shared_ptr<VROARAugmentedFace>* facePtr = 
            reinterpret_cast<std::shared_ptr<VROARAugmentedFace>*>(faceRef);
        
        if (facePtr && *facePtr) {
            const char* nameCStr = env->GetStringUTFChars(blendShapeName, nullptr);
            std::string nameStr(nameCStr);
            env->ReleaseStringUTFChars(blendShapeName, nameCStr);
            
            return (*facePtr)->getBlendShapeCoefficient(nameStr);
        }
    } catch (const std::exception& e) {
        VROLogError("Exception getting blend shape coefficient: %s", e.what());
    }
    
    return 0.0f;
}

JNIEXPORT jfloat JNICALL
Java_com_viro_core_ARAugmentedFace_nativeGetTrackingConfidence(JNIEnv *env,
                                                               jobject thiz,
                                                               jlong faceRef) {
    if (faceRef == 0) {
        return 0.0f;
    }
    
    try {
        std::shared_ptr<VROARAugmentedFace>* facePtr = 
            reinterpret_cast<std::shared_ptr<VROARAugmentedFace>*>(faceRef);
        
        if (facePtr && *facePtr) {
            return (*facePtr)->getTrackingConfidence();
        }
    } catch (const std::exception& e) {
        VROLogError("Exception getting tracking confidence: %s", e.what());
    }
    
    return 0.0f;
}

JNIEXPORT void JNICALL
Java_com_viro_core_ARAugmentedFace_nativeDestroy(JNIEnv *env,
                                                 jobject thiz,
                                                 jlong faceRef) {
    if (faceRef == 0) {
        return;
    }
    
    try {
        std::shared_ptr<VROARAugmentedFace>* facePtr = 
            reinterpret_cast<std::shared_ptr<VROARAugmentedFace>*>(faceRef);
        
        if (facePtr) {
            delete facePtr;
        }
    } catch (const std::exception& e) {
        VROLogError("Exception destroying augmented face: %s", e.what());
    }
}

// Callback wrappers for face tracking events
class FaceTrackingCallbackWrapper {
public:
    FaceTrackingCallbackWrapper(JNIEnv* env, jobject callback) {
        _env = env;
        _callback = env->NewGlobalRef(callback);
        _class = (jclass) env->NewGlobalRef(env->GetObjectClass(callback));
        
        _onFaceAddedMethodId = env->GetMethodID(_class, "onFaceAdded", "(Lcom/viro/core/ARAugmentedFace;)V");
        _onFaceUpdatedMethodId = env->GetMethodID(_class, "onFaceUpdated", "(Lcom/viro/core/ARAugmentedFace;)V");
        _onFaceRemovedMethodId = env->GetMethodID(_class, "onFaceRemoved", "(Lcom/viro/core/ARAugmentedFace;)V");
    }
    
    ~FaceTrackingCallbackWrapper() {
        if (_env && _callback) {
            _env->DeleteGlobalRef(_callback);
            _env->DeleteGlobalRef(_class);
        }
    }
    
    void onFaceAdded(std::shared_ptr<VROARAugmentedFace> face) {
        if (_env && _callback && _onFaceAddedMethodId && face) {
            VROLogInfo("Face added: %s", face->getId().c_str());
            // Would need to create Java wrapper object for face
        }
    }
    
    void onFaceUpdated(std::shared_ptr<VROARAugmentedFace> face) {
        if (_env && _callback && _onFaceUpdatedMethodId && face) {
            // Face update - less verbose logging
        }
    }
    
    void onFaceRemoved(std::shared_ptr<VROARAugmentedFace> face) {
        if (_env && _callback && _onFaceRemovedMethodId && face) {
            VROLogInfo("Face removed: %s", face->getId().c_str());
        }
    }
    
private:
    JNIEnv* _env;
    jobject _callback;
    jclass _class;
    jmethodID _onFaceAddedMethodId;
    jmethodID _onFaceUpdatedMethodId;
    jmethodID _onFaceRemovedMethodId;
};

JNIEXPORT void JNICALL
Java_com_viro_core_ARAugmentedFace_nativeSetFaceTrackingListener(JNIEnv *env,
                                                                 jclass clazz,
                                                                 jlong sessionRef,
                                                                 jobject listener) {
    std::shared_ptr<VROARSessionARCoreExtended> extendedSession = getExtendedARSession(sessionRef).lock();
    if (!extendedSession) {
        VROLogError("Failed to get extended AR session for face tracking listener");
        return;
    }
    
    try {
        if (listener) {
            auto callbackWrapper = std::make_shared<FaceTrackingCallbackWrapper>(env, listener);
            
            extendedSession->setFaceTrackingCallback(
                [callbackWrapper](std::shared_ptr<VROARAugmentedFace> face, bool added, bool removed) {
                    if (added) {
                        callbackWrapper->onFaceAdded(face);
                    } else if (removed) {
                        callbackWrapper->onFaceRemoved(face);
                    } else {
                        callbackWrapper->onFaceUpdated(face);
                    }
                });
            
            VROLogInfo("Face tracking listener set");
        } else {
            // Clear the callback
            extendedSession->setFaceTrackingCallback(nullptr);
            VROLogInfo("Face tracking listener cleared");
        }
    } catch (const std::exception& e) {
        VROLogError("Exception setting face tracking listener: %s", e.what());
    }
}

} // extern "C"