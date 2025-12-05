//
//  VROARFrameiOS.cpp
//  ViroKit
//
//  Created by Raj Advani on 6/6/17.
//  Copyright © 2017 Viro Media. All rights reserved.
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

#include "Availability.h"
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 110000
#include "VROARFrameiOS.h"
#include "VROARSessioniOS.h"
#include "VROARAnchoriOS.h"
#include "VROARCameraiOS.h"
#include "VROVideoTextureCache.h"
#include "VROTextureSubstrate.h"
#include "VROConvert.h"
#include "VROVector4f.h"
#include "VROARHitTestResult.h"
#include "VROLight.h"
#include "VROCameraTexture.h"
#include "VROTexture.h"
#include "VROData.h"

VROARFrameiOS::VROARFrameiOS(ARFrame *frame, VROViewport viewport, VROCameraOrientation orientation,
                             std::shared_ptr<VROARSessioniOS> session) :
    _frame(frame),
    _viewport(viewport),
    _orientation(orientation),
    _session(session) {
        
    _camera = std::make_shared<VROARCameraiOS>(frame.camera, orientation);
    for (ARAnchor *anchor in frame.anchors) {
        _anchors.push_back(std::make_shared<VROARAnchoriOS>(anchor));
    }
}

VROARFrameiOS::~VROARFrameiOS() {
    
}

CVPixelBufferRef VROARFrameiOS::getImage() const {
    return _frame.capturedImage;
}

CGImagePropertyOrientation VROARFrameiOS::getImageOrientation() const {
    // Image orientation is determined by ARKit and is based on the camera orientation.
    // When in portrait mode, for example, ARKit returns its image rotated to the right.
    if (_orientation == VROCameraOrientation::Portrait) {
        return kCGImagePropertyOrientationRight;
    }
    else {
        // TODO Fill in proper image orientation types
        return kCGImagePropertyOrientationRight;
    }
}

double VROARFrameiOS::getTimestamp() const {
    return _frame.timestamp;
}

const std::shared_ptr<VROARCamera> &VROARFrameiOS::getCamera() const {
    return _camera;
}

ARHitTestResultType convertResultTypes(std::set<VROARHitTestResultType> types) {
    ARHitTestResultType type = 0;
    for (VROARHitTestResultType t : types) {
        if (t == VROARHitTestResultType::ExistingPlaneUsingExtent) {
            type |= ARHitTestResultTypeExistingPlaneUsingExtent;
        }
        else if (t == VROARHitTestResultType::ExistingPlane) {
            type |= ARHitTestResultTypeExistingPlane;
        }
        else if (t == VROARHitTestResultType::EstimatedHorizontalPlane) {
            type |= ARHitTestResultTypeEstimatedHorizontalPlane;
        }
        else if (t == VROARHitTestResultType::FeaturePoint) {
            type |= ARHitTestResultTypeFeaturePoint;
        }
    }
    
    return type;
}

VROARHitTestResultType convertResultType(ARHitTestResultType type) {
    if (type == ARHitTestResultTypeExistingPlaneUsingExtent) {
        return VROARHitTestResultType::ExistingPlaneUsingExtent;
    }
    else if (type == ARHitTestResultTypeExistingPlane) {
        return VROARHitTestResultType::ExistingPlane;
    }
    else if (type == ARHitTestResultTypeEstimatedHorizontalPlane) {
        return VROARHitTestResultType::EstimatedHorizontalPlane;
    }
    else if (type == ARHitTestResultTypeFeaturePoint) {
        return VROARHitTestResultType::FeaturePoint;
    }
    else {
        pabort();
    }
}

std::vector<std::shared_ptr<VROARHitTestResult>> VROARFrameiOS::hitTest(int x, int y, std::set<VROARHitTestResultType> types) {
    /*
     Convert from viewport space to camera image space.
     */
    VROMatrix4f viewportSpaceToCameraImageSpace = getViewportToCameraImageTransform();
    VROVector3f pointViewport(x / (float)_viewport.getWidth(), y / (float)_viewport.getHeight());
    VROVector3f pointCameraImage = viewportSpaceToCameraImageSpace.multiply(pointViewport);
    
    /*
     Perform the ARKit hit test.
     */
    CGPoint point = CGPointMake(pointCameraImage.x, pointCameraImage.y);
    ARHitTestResultType typesAR = convertResultTypes(types);
    NSArray<ARHitTestResult *> *results = [_frame hitTest:point types:typesAR];
    
    /*
     Convert the results to VROARHitTestResult objects.
     */
    std::shared_ptr<VROARSessioniOS> session = _session.lock();
    std::vector<std::shared_ptr<VROARHitTestResult>> vResults;
    for (ARHitTestResult *result in results) {
        std::shared_ptr<VROARAnchor> vAnchor;
        if (session && result.anchor) {
            vAnchor = session->getAnchorForNative(result.anchor);
        }
        
        std::shared_ptr<VROARHitTestResult> vResult = std::make_shared<VROARHitTestResult>(convertResultType(result.type), vAnchor, result.distance,
                                   VROConvert::toMatrix4f(result.worldTransform),
                                   VROConvert::toMatrix4f(result.localTransform));
        vResults.push_back(vResult);
    }
    
    return vResults;
}

VROMatrix4f VROARFrameiOS::getViewportToCameraImageTransform() const {
    CGSize viewportSize = CGSizeMake(_viewport.getWidth()  / _viewport.getContentScaleFactor(),
                                     _viewport.getHeight() / _viewport.getContentScaleFactor());
    UIInterfaceOrientation orientation = VROConvert::toDeviceOrientation(_orientation);
    
    /*
     The display transform converts from the camera's image space (normalized image space) to
     viewport space for the given orientation and viewport. We can either apply this transform
     to *vertices* of the camera background (by modifying its projection matrix) or apply the
     *inverse* of this transform to the *texture coordinates* of the camera background. The
     two are equivalent. We do the latter, since our camera background uses a fixed orthographic
     projection.
     */
    CGAffineTransform transform = CGAffineTransformInvert([_frame displayTransformForOrientation:orientation viewportSize:viewportSize]);
    
    VROMatrix4f matrix;
    matrix[0] = transform.a;
    matrix[1] = transform.b;
    matrix[4] = transform.c;
    matrix[5] = transform.d;
    matrix[12] = transform.tx;
    matrix[13] = transform.ty;
    return matrix;
}

const std::vector<std::shared_ptr<VROARAnchor>> &VROARFrameiOS::getAnchors() const {
    return _anchors;
}

float VROARFrameiOS::getAmbientLightIntensity() const {
    return _frame.lightEstimate.ambientIntensity;
}

VROVector3f VROARFrameiOS::getAmbientLightColor() const {
    return VROLight::deriveRGBFromTemperature(_frame.lightEstimate.ambientColorTemperature);
}

std::shared_ptr<VROARPointCloud> VROARFrameiOS::getPointCloud() {
    if (_pointCloud) {
        return _pointCloud;
    }
    std::vector<VROVector4f> points;
    ARPointCloud *arPointCloud = _frame.rawFeaturePoints;
    for (int i = 0; i < arPointCloud.count; i++) {
        vector_float3 arPoint = arPointCloud.points[i];
        // the last value in the point is a "confidence" value from 0 to 1. Since
        // ARKit doesn't provide this, set it to 1.
        points.push_back(VROVector4f(arPoint.x, arPoint.y, arPoint.z, 1));
    }

    std::vector<uint64_t> identifiers(arPointCloud.identifiers, arPointCloud.identifiers + arPointCloud.count);

    _pointCloud = std::make_shared<VROARPointCloud>(points, identifiers);
    return _pointCloud;
}

bool VROARFrameiOS::hasDepthData() const {
    if (@available(iOS 14.0, *)) {
        return _frame.sceneDepth != nil;
    }
    return false;
}

int VROARFrameiOS::getDepthImageWidth() const {
    if (@available(iOS 14.0, *)) {
        if (_frame.sceneDepth != nil) {
            return (int)CVPixelBufferGetWidth(_frame.sceneDepth.depthMap);
        }
    }
    return 0;
}

int VROARFrameiOS::getDepthImageHeight() const {
    if (@available(iOS 14.0, *)) {
        if (_frame.sceneDepth != nil) {
            return (int)CVPixelBufferGetHeight(_frame.sceneDepth.depthMap);
        }
    }
    return 0;
}

std::shared_ptr<VROTexture> VROARFrameiOS::getDepthTexture() {
    if (_depthTexture) {
        return _depthTexture;
    }

    if (@available(iOS 14.0, *)) {
        ARDepthData *sceneDepth = _frame.sceneDepth;
        if (sceneDepth == nil) {
            return nullptr;
        }

        CVPixelBufferRef depthMap = sceneDepth.depthMap;
        if (depthMap == nil) {
            return nullptr;
        }

        // Get the depth map dimensions
        size_t width = CVPixelBufferGetWidth(depthMap);
        size_t height = CVPixelBufferGetHeight(depthMap);

        // Lock the buffer to access the data
        CVPixelBufferLockBaseAddress(depthMap, kCVPixelBufferLock_ReadOnly);
        void *baseAddress = CVPixelBufferGetBaseAddress(depthMap);

        // The depth map is Float32 format
        OSType formatType = CVPixelBufferGetPixelFormatType(depthMap);
        if (formatType == kCVPixelFormatType_DepthFloat32 && baseAddress != nullptr) {
            // Copy the depth data (we need to copy since the CVPixelBuffer will be released)
            size_t dataSize = width * height * sizeof(float);
            std::shared_ptr<VROData> depthData = std::make_shared<VROData>(baseAddress, dataSize, VRODataOwnership::Copy);
            std::vector<std::shared_ptr<VROData>> dataVec = { depthData };

            // DEBUG: Log some sample depth values from the texture
            float *depthValues = (float *)baseAddress;
            size_t centerIdx = (height / 2) * width + (width / 2);
            size_t topLeftIdx = 0;
            size_t bottomRightIdx = width * height - 1;
            static int logCounter = 0;
            if (logCounter++ % 60 == 0) {  // Log once per second (assuming 60fps)
                float z = depthValues[centerIdx];
                float n = 0.01f;  // kZNear
                float f = 50.0f;  // kZFar
                float depthNDC = 1.0f;
                if (z > 0.0f && z < f) {
                    depthNDC = (f * (z - n)) / (z * (f - n));
                }
                pinfo("Depth texture: %zux%zu, center z=%.3fm -> NDC=%.6f",
                      width, height, z, depthNDC);
            }

            _depthTexture = std::make_shared<VROTexture>(VROTextureType::Texture2D,
                                                          VROTextureFormat::R32F,
                                                          VROTextureInternalFormat::R32F,
                                                          false, // not sRGB
                                                          VROMipmapMode::None,
                                                          dataVec,
                                                          (int)width, (int)height,
                                                          std::vector<uint32_t>());
        }

        CVPixelBufferUnlockBaseAddress(depthMap, kCVPixelBufferLock_ReadOnly);
    }

    return _depthTexture;
}

std::shared_ptr<VROTexture> VROARFrameiOS::getDepthConfidenceTexture() {
    if (_depthConfidenceTexture) {
        return _depthConfidenceTexture;
    }

    if (@available(iOS 14.0, *)) {
        ARDepthData *sceneDepth = _frame.sceneDepth;
        if (sceneDepth == nil || sceneDepth.confidenceMap == nil) {
            return nullptr;
        }

        CVPixelBufferRef confidenceMap = sceneDepth.confidenceMap;
        size_t width = CVPixelBufferGetWidth(confidenceMap);
        size_t height = CVPixelBufferGetHeight(confidenceMap);

        CVPixelBufferLockBaseAddress(confidenceMap, kCVPixelBufferLock_ReadOnly);
        void *baseAddress = CVPixelBufferGetBaseAddress(confidenceMap);

        if (baseAddress != nullptr) {
            // Copy the confidence data
            size_t dataSize = width * height;
            std::shared_ptr<VROData> confidenceData = std::make_shared<VROData>(baseAddress, dataSize, VRODataOwnership::Copy);
            std::vector<std::shared_ptr<VROData>> dataVec = { confidenceData };

            _depthConfidenceTexture = std::make_shared<VROTexture>(VROTextureType::Texture2D,
                                                                    VROTextureFormat::R8,
                                                                    VROTextureInternalFormat::R8,
                                                                    false, // not sRGB
                                                                    VROMipmapMode::None,
                                                                    dataVec,
                                                                    (int)width, (int)height,
                                                                    std::vector<uint32_t>());
        }

        CVPixelBufferUnlockBaseAddress(confidenceMap, kCVPixelBufferLock_ReadOnly);
    }

    return _depthConfidenceTexture;
}

VROMatrix4f VROARFrameiOS::getDepthTextureTransform() const {
    /*
     For 3D objects, we calculate screen-space UV from gl_FragCoord.xy / viewport_size.
     This gives us normalized screen coordinates (0,0 at bottom-left, 1,1 at top-right).

     The depth texture from ARKit's sceneDepth is in camera sensor space, which may be
     rotated/flipped relative to the screen based on device orientation.

     ARKit's displayTransformForOrientation maps FROM camera sensor coordinates TO screen
     display coordinates. We need the inverse: mapping FROM screen UV TO depth texture UV.

     This is the same transform we use for the camera background texture coordinates
     (getTexcoordTransform), since both the camera image and depth texture are in the
     same sensor coordinate space.
     */
    UIInterfaceOrientation orientation = VROConvert::toDeviceOrientation(_orientation);
    CGSize viewportSize = CGSizeMake(_viewport.getWidth(), _viewport.getHeight());

    // Get the inverse of displayTransform to map from screen coordinates to texture coordinates
    CGAffineTransform transform = CGAffineTransformInvert([_frame displayTransformForOrientation:orientation viewportSize:viewportSize]);

    VROMatrix4f matrix;
    matrix[0] = transform.a;
    matrix[1] = transform.b;
    matrix[4] = transform.c;
    matrix[5] = transform.d;
    matrix[12] = transform.tx;
    matrix[13] = transform.ty;
    return matrix;
}

#endif
