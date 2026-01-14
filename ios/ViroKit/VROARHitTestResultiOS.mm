//
//  VROARHitTestResultiOS.mm
//  ViroKit
//
//  Copyright © 2026 Viro Media. All rights reserved.
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

#include "VROARHitTestResultiOS.h"

#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110000

#include "VROARSessioniOS.h"
#include "VROARAnchor.h"
#include "VROARNode.h"
#include "VROConvert.h"
#include "VROPlatformUtil.h"
#include "VROLog.h"
#include <simd/simd.h>

VROARHitTestResultiOS::VROARHitTestResultiOS(
    VROARHitTestResultType type,
    std::shared_ptr<VROARAnchor> anchor,
    float distance,
    VROMatrix4f worldTransform,
    VROMatrix4f localTransform,
    ARHitTestResult *nativeResult,
    std::shared_ptr<VROARSessioniOS> session)
    : VROARHitTestResult(type, anchor, distance, worldTransform, localTransform),
      _nativeResult(nativeResult),
      _session(session) {
}

VROARHitTestResultiOS::~VROARHitTestResultiOS() {
    _nativeResult = nil;
}

std::shared_ptr<VROARNode> VROARHitTestResultiOS::createAnchoredNodeAtHitLocation() {
    std::shared_ptr<VROARSessioniOS> session = _session.lock();
    if (!session) {
        pwarn("Cannot create anchor: AR session is no longer available");
        return nullptr;
    }

    if (!_nativeResult) {
        pwarn("Cannot create anchor: native hit test result is null");
        return nullptr;
    }

    // Create VROARNode immediately with current transform
    // This allows the node to be used on the application thread
    std::shared_ptr<VROARNode> node = std::make_shared<VROARNode>();

    VROMatrix4f transform = getWorldTransform();
    VROVector3f position = transform.extractTranslation();
    VROVector3f scale = transform.extractScale();
    VROQuaternion rotation = transform.extractRotation(scale);

    node->setPositionAtomic(position);
    node->setRotationAtomic(rotation);
    node->setScaleAtomic(scale);
    node->computeTransformsAtomic({}, {});

    // Create ARAnchor from hit result
    // Use hit result's world transform, or if the hit already has an anchor, use that
    ARAnchor *nativeAnchor = nil;

    if (_nativeResult.anchor) {
        // Hit result already has an anchor (e.g., from a detected plane)
        nativeAnchor = _nativeResult.anchor;
        pinfo("Using existing anchor from hit result: %s",
              [[nativeAnchor.identifier UUIDString] UTF8String]);
    } else {
        // Create new anchor at hit location
        nativeAnchor = [[ARAnchor alloc] initWithTransform:_nativeResult.worldTransform];
        pinfo("Created new anchor at hit location: %s",
              [[nativeAnchor.identifier UUIDString] UTF8String]);
    }

    if (!nativeAnchor) {
        pwarn("Failed to create ARAnchor");
        return nullptr;
    }

    // Create Viro anchor wrapper
    std::string anchorId = [[nativeAnchor.identifier UUIDString] UTF8String];
    std::shared_ptr<VROARAnchor> viroAnchor = std::make_shared<VROARAnchor>();
    viroAnchor->setId(anchorId);
    viroAnchor->setTransform(VROConvert::toMatrix4f(nativeAnchor.transform));
    viroAnchor->setARNode(node);
    node->setAnchor(viroAnchor);

    // Add anchor to ARKit session on main thread
    std::weak_ptr<VROARSessioniOS> session_w = session;
    std::weak_ptr<VROARAnchor> anchor_w = viroAnchor;
    bool shouldAddToARSession = (_nativeResult.anchor == nil);  // Only add if we created it

    dispatch_async(dispatch_get_main_queue(), ^{
        std::shared_ptr<VROARSessioniOS> session_s = session_w.lock();
        if (!session_s) {
            return;
        }

        ARSession *arSession = session_s->getARSession();
        if (!arSession) {
            pwarn("ARSession is null, cannot add anchor");
            return;
        }

        // If we created a new anchor, add it to the session
        if (shouldAddToARSession) {
            [arSession addAnchor:nativeAnchor];
            // ARC will handle memory management automatically
        }

        // Add to Viro's anchor tracking (on renderer thread)
        VROPlatformDispatchAsyncRenderer([session_w, anchor_w] {
            std::shared_ptr<VROARSessioniOS> session_s = session_w.lock();
            std::shared_ptr<VROARAnchor> anchor_s = anchor_w.lock();

            if (session_s && anchor_s) {
                session_s->addAnchor(anchor_s);
            }
        });
    });

    return node;
}

#endif // __IPHONE_OS_VERSION_MAX_ALLOWED >= 110000
