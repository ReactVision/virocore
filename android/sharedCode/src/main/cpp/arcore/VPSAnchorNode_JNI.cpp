//
//  VPSAnchorNode_JNI.cpp
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

#include <memory>
#include <VROPlatformUtil.h>
#include <VROVPSAnchorNode.h>
#include "ViroUtils_JNI.h"

#if VRO_PLATFORM_ANDROID
#define VRO_METHOD(return_type, method_name) \
  JNIEXPORT return_type JNICALL              \
      Java_com_viro_core_internal_VPSAnchorNode_##method_name
#else
#define VRO_METHOD(return_type, method_name) \
    return_type VPSAnchorNode_##method_name
#endif

namespace {

// Owns the weak global ref to the Java node for as long as the listener lives.
class VPSAnchorNodeJavaRef {
public:
    VPSAnchorNodeJavaRef(VRO_OBJECT obj, VRO_ENV env) : _object(VRO_NEW_WEAK_GLOBAL_REF(obj)) {}
    ~VPSAnchorNodeJavaRef() {
        VRO_ENV env = VROPlatformGetJNIEnv();
        VRO_DELETE_WEAK_GLOBAL_REF(_object);
    }
    VRO_OBJECT get() const { return _object; }
private:
    VRO_OBJECT _object;
};

}

extern "C" {

VRO_METHOD(VRO_REF(VROVPSAnchorNode), nativeCreateVPSAnchorNode)(VRO_NO_ARGS) {
    std::shared_ptr<VROVPSAnchorNode> node = std::make_shared<VROVPSAnchorNode>();
    return VRO_REF_NEW(VROVPSAnchorNode, node);
}

VRO_METHOD(void, nativeSetPlacedListener)(VRO_ARGS
                                          VRO_REF(VROVPSAnchorNode) node_j) {
    VRO_METHOD_PREAMBLE;
    std::shared_ptr<VROVPSAnchorNode> node = VRO_REF_GET(VROVPSAnchorNode, node_j);
    std::shared_ptr<VPSAnchorNodeJavaRef> ref = std::make_shared<VPSAnchorNodeJavaRef>(obj, env);
    node->setPlacedListener([ref](bool placed) {
        VRO_ENV env = VROPlatformGetJNIEnv();
        VRO_WEAK jObject_w = VRO_NEW_WEAK_GLOBAL_REF(ref->get());
        VROPlatformDispatchAsyncApplication([jObject_w, placed] {
            VRO_ENV env = VROPlatformGetJNIEnv();
            VRO_OBJECT localObj = VRO_NEW_LOCAL_REF(jObject_w);
            if (!VRO_IS_OBJECT_NULL(localObj)) {
                VROPlatformCallHostFunction(localObj, "onPlacedChanged", "(Z)V", placed);
                VRO_DELETE_LOCAL_REF(localObj);
            }
            VRO_DELETE_WEAK_GLOBAL_REF(jObject_w);
        });
    });
}

} // extern "C"
