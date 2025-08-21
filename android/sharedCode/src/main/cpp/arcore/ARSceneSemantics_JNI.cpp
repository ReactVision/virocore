//
//  ARSceneSemantics_JNI.cpp
//  ViroCore
//
//  JNI bindings for Scene Semantics
//  Copyright © 2024 Viro Media. All rights reserved.

#include <jni.h>
#include <memory>
#include <functional>
#include "VROARSessionARCoreExtended.h"
#include "VROARSceneSemantics.h"
#include "VROLog.h"
#include "ArUtils_JNI.h"

static std::weak_ptr<VROARSessionARCoreExtended> getExtendedARSession(jlong sessionRef) {
    VROARSession *session = (VROARSession *) sessionRef;
    return std::dynamic_pointer_cast<VROARSessionARCoreExtended>(
        std::static_pointer_cast<VROARSession>(session->shared_from_this()));
}

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_viro_core_ARSceneSemantics_nativeCreate(JNIEnv *env,
                                                  jclass clazz,
                                                  jlong sessionRef) {
    std::shared_ptr<VROARSessionARCoreExtended> extendedSession = getExtendedARSession(sessionRef).lock();
    if (!extendedSession) {
        VROLogError("Failed to get extended AR session for scene semantics");
        return 0;
    }
    
    try {
        auto semantics = extendedSession->createSceneSemantics();
        if (semantics) {
            return reinterpret_cast<jlong>(new std::shared_ptr<VROARSceneSemantics>(semantics));
        }
    } catch (const std::exception& e) {
        VROLogError("Exception creating scene semantics: %s", e.what());
    }
    
    return 0;
}

JNIEXPORT jobjectArray JNICALL
Java_com_viro_core_ARSceneSemantics_nativeGetAllLabels(JNIEnv *env,
                                                        jobject thiz,
                                                        jlong semanticsRef) {
    if (semanticsRef == 0) {
        return env->NewObjectArray(0, env->FindClass("java/lang/String"), nullptr);
    }
    
    try {
        std::shared_ptr<VROARSceneSemantics>* semanticsPtr = 
            reinterpret_cast<std::shared_ptr<VROARSceneSemantics>*>(semanticsRef);
        
        if (semanticsPtr && *semanticsPtr) {
            std::vector<std::string> labels = (*semanticsPtr)->getAllLabels();
            
            jobjectArray result = env->NewObjectArray(labels.size(), 
                                                     env->FindClass("java/lang/String"), 
                                                     nullptr);
            
            for (size_t i = 0; i < labels.size(); i++) {
                jstring labelStr = env->NewStringUTF(labels[i].c_str());
                env->SetObjectArrayElement(result, i, labelStr);
                env->DeleteLocalRef(labelStr);
            }
            
            return result;
        }
    } catch (const std::exception& e) {
        VROLogError("Exception getting all labels: %s", e.what());
    }
    
    return env->NewObjectArray(0, env->FindClass("java/lang/String"), nullptr);
}

JNIEXPORT jfloat JNICALL
Java_com_viro_core_ARSceneSemantics_nativeGetLabelConfidence(JNIEnv *env,
                                                             jobject thiz,
                                                             jlong semanticsRef,
                                                             jstring label) {
    if (semanticsRef == 0 || !label) {
        return 0.0f;
    }
    
    try {
        std::shared_ptr<VROARSceneSemantics>* semanticsPtr = 
            reinterpret_cast<std::shared_ptr<VROARSceneSemantics>*>(semanticsRef);
        
        if (semanticsPtr && *semanticsPtr) {
            const char* labelCStr = env->GetStringUTFChars(label, nullptr);
            std::string labelStr(labelCStr);
            env->ReleaseStringUTFChars(label, labelCStr);
            
            return (*semanticsPtr)->getLabelConfidence(labelStr);
        }
    } catch (const std::exception& e) {
        VROLogError("Exception getting label confidence: %s", e.what());
    }
    
    return 0.0f;
}

JNIEXPORT jfloat JNICALL
Java_com_viro_core_ARSceneSemantics_nativeGetLabelCoverage(JNIEnv *env,
                                                           jobject thiz,
                                                           jlong semanticsRef,
                                                           jstring label) {
    if (semanticsRef == 0 || !label) {
        return 0.0f;
    }
    
    try {
        std::shared_ptr<VROARSceneSemantics>* semanticsPtr = 
            reinterpret_cast<std::shared_ptr<VROARSceneSemantics>*>(semanticsRef);
        
        if (semanticsPtr && *semanticsPtr) {
            const char* labelCStr = env->GetStringUTFChars(label, nullptr);
            std::string labelStr(labelCStr);
            env->ReleaseStringUTFChars(label, labelCStr);
            
            return (*semanticsPtr)->getLabelCoverage(labelStr);
        }
    } catch (const std::exception& e) {
        VROLogError("Exception getting label coverage: %s", e.what());
    }
    
    return 0.0f;
}

JNIEXPORT jboolean JNICALL
Java_com_viro_core_ARSceneSemantics_nativeHasLabel(JNIEnv *env,
                                                   jobject thiz,
                                                   jlong semanticsRef,
                                                   jstring label) {
    if (semanticsRef == 0 || !label) {
        return JNI_FALSE;
    }
    
    try {
        std::shared_ptr<VROARSceneSemantics>* semanticsPtr = 
            reinterpret_cast<std::shared_ptr<VROARSceneSemantics>*>(semanticsRef);
        
        if (semanticsPtr && *semanticsPtr) {
            const char* labelCStr = env->GetStringUTFChars(label, nullptr);
            std::string labelStr(labelCStr);
            env->ReleaseStringUTFChars(label, labelCStr);
            
            return (*semanticsPtr)->hasLabel(labelStr) ? JNI_TRUE : JNI_FALSE;
        }
    } catch (const std::exception& e) {
        VROLogError("Exception checking has label: %s", e.what());
    }
    
    return JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_com_viro_core_ARSceneSemantics_nativeGetMostConfidentLabel(JNIEnv *env,
                                                                jobject thiz,
                                                                jlong semanticsRef) {
    if (semanticsRef == 0) {
        return nullptr;
    }
    
    try {
        std::shared_ptr<VROARSceneSemantics>* semanticsPtr = 
            reinterpret_cast<std::shared_ptr<VROARSceneSemantics>*>(semanticsRef);
        
        if (semanticsPtr && *semanticsPtr) {
            std::string mostConfident = (*semanticsPtr)->getMostConfidentLabel();
            if (!mostConfident.empty()) {
                return env->NewStringUTF(mostConfident.c_str());
            }
        }
    } catch (const std::exception& e) {
        VROLogError("Exception getting most confident label: %s", e.what());
    }
    
    return nullptr;
}

JNIEXPORT jfloat JNICALL
Java_com_viro_core_ARSceneSemantics_nativeGetOverallConfidence(JNIEnv *env,
                                                               jobject thiz,
                                                               jlong semanticsRef) {
    if (semanticsRef == 0) {
        return 0.0f;
    }
    
    try {
        std::shared_ptr<VROARSceneSemantics>* semanticsPtr = 
            reinterpret_cast<std::shared_ptr<VROARSceneSemantics>*>(semanticsRef);
        
        if (semanticsPtr && *semanticsPtr) {
            return (*semanticsPtr)->getOverallConfidence();
        }
    } catch (const std::exception& e) {
        VROLogError("Exception getting overall confidence: %s", e.what());
    }
    
    return 0.0f;
}

JNIEXPORT jstring JNICALL
Java_com_viro_core_ARSceneSemantics_nativeGetId(JNIEnv *env,
                                                jobject thiz,
                                                jlong semanticsRef) {
    if (semanticsRef == 0) {
        return nullptr;
    }
    
    try {
        std::shared_ptr<VROARSceneSemantics>* semanticsPtr = 
            reinterpret_cast<std::shared_ptr<VROARSceneSemantics>*>(semanticsRef);
        
        if (semanticsPtr && *semanticsPtr) {
            std::string id = (*semanticsPtr)->getId();
            if (!id.empty()) {
                return env->NewStringUTF(id.c_str());
            }
        }
    } catch (const std::exception& e) {
        VROLogError("Exception getting semantics ID: %s", e.what());
    }
    
    return nullptr;
}

JNIEXPORT void JNICALL
Java_com_viro_core_ARSceneSemantics_nativeDestroy(JNIEnv *env,
                                                  jobject thiz,
                                                  jlong semanticsRef) {
    if (semanticsRef == 0) {
        return;
    }
    
    try {
        std::shared_ptr<VROARSceneSemantics>* semanticsPtr = 
            reinterpret_cast<std::shared_ptr<VROARSceneSemantics>*>(semanticsRef);
        
        if (semanticsPtr) {
            delete semanticsPtr;
        }
    } catch (const std::exception& e) {
        VROLogError("Exception destroying scene semantics: %s", e.what());
    }
}

// Callback wrapper for label updates
class LabelUpdateCallbackWrapper {
public:
    LabelUpdateCallbackWrapper(JNIEnv* env, jobject callback) {
        _env = env;
        _callback = env->NewGlobalRef(callback);
        _class = (jclass) env->NewGlobalRef(env->GetObjectClass(callback));
        _methodId = env->GetMethodID(_class, "onLabelUpdate", "(Lcom/viro/core/ARSceneSemantics;)V");
    }
    
    ~LabelUpdateCallbackWrapper() {
        if (_env && _callback) {
            _env->DeleteGlobalRef(_callback);
            _env->DeleteGlobalRef(_class);
        }
    }
    
    void onLabelUpdate(std::shared_ptr<VROARSceneSemantics> semantics) {
        if (_env && _callback && _methodId && semantics) {
            // Create Java object wrapper for semantics
            // This would require additional JNI setup for the semantics object
            // For now, we'll just log the update
            VROLogInfo("Scene semantics label update received");
        }
    }
    
private:
    JNIEnv* _env;
    jobject _callback;
    jclass _class;
    jmethodID _methodId;
};

JNIEXPORT void JNICALL
Java_com_viro_core_ARSceneSemantics_nativeSetLabelUpdateListener(JNIEnv *env,
                                                                 jobject thiz,
                                                                 jlong semanticsRef,
                                                                 jobject listener) {
    if (semanticsRef == 0) {
        return;
    }
    
    try {
        std::shared_ptr<VROARSceneSemantics>* semanticsPtr = 
            reinterpret_cast<std::shared_ptr<VROARSceneSemantics>*>(semanticsRef);
        
        if (semanticsPtr && *semanticsPtr) {
            if (listener) {
                auto callbackWrapper = std::make_shared<LabelUpdateCallbackWrapper>(env, listener);
                
                (*semanticsPtr)->setLabelUpdateCallback([callbackWrapper](std::shared_ptr<VROARSceneSemantics> semantics) {
                    callbackWrapper->onLabelUpdate(semantics);
                });
                
                VROLogInfo("Scene semantics label update listener set");
            } else {
                // Clear the callback
                (*semanticsPtr)->setLabelUpdateCallback(nullptr);
                VROLogInfo("Scene semantics label update listener cleared");
            }
        }
    } catch (const std::exception& e) {
        VROLogError("Exception setting label update listener: %s", e.what());
    }
}

} // extern "C"