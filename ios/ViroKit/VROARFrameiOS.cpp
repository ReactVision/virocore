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
#include "VROARHitTestResultiOS.h"
#include "VROLight.h"
#include "VROCameraTexture.h"
#include "VROTexture.h"
#include "VROData.h"
#include "VROFieldOfView.h"
#include "VROARDepthMesh.h"
#include "VROMonocularDepthEstimator.h"

VROARFrameiOS::VROARFrameiOS(ARFrame *frame, VROViewport viewport, VROCameraOrientation orientation,
                             std::shared_ptr<VROARSessioniOS> session) :
    _frame(frame),
    _viewport(viewport),
    _orientation(orientation),
    _session(session) {

    _camera = std::make_shared<VROARCameraiOS>(frame.camera, orientation);
    for (ARAnchor *anchor in frame.anchors) {
        // Try to get the existing anchor from the session's anchor map first
        // This ensures we use anchors with properly set IDs that can be used for cloud anchors
        std::shared_ptr<VROARAnchor> vAnchor = session->getAnchorForNative(anchor);
        if (vAnchor) {
            _anchors.push_back(vAnchor);
        } else {
            // Fallback: create a new anchor (this shouldn't normally happen for tracked anchors)
            auto newAnchor = std::make_shared<VROARAnchoriOS>(anchor);
            newAnchor->setId(std::string([anchor.identifier.UUIDString UTF8String]));
            _anchors.push_back(newAnchor);
        }
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

        std::shared_ptr<VROARHitTestResult> vResult = std::make_shared<VROARHitTestResultiOS>(convertResultType(result.type), vAnchor, result.distance,
                                   VROConvert::toMatrix4f(result.worldTransform),
                                   VROConvert::toMatrix4f(result.localTransform),
                                   result,
                                   session);
        vResults.push_back(vResult);
    }

    // Enhance all hit test results with depth data if available
    std::shared_ptr<VROTexture> depthTexture = getDepthTexture();
    if (depthTexture && hasDepthData()) {
        pinfo("Depth texture available, enhancing %zu hit results with depth data", vResults.size());
        // Determine depth source
        std::string depthSource = "none";
        bool preferMonocular = session && session->isPreferMonocularDepth();

        if (preferMonocular) {
            depthSource = "monocular";
        } else if (hasLiDARDepth()) {
            depthSource = "lidar";
        } else {
            depthSource = "monocular";
        }

        // Get depth transform and confidence texture
        VROMatrix4f depthTransform = getDepthTextureTransform();
        std::shared_ptr<VROTexture> confidenceTexture = getDepthConfidenceTexture();

        // Sample depth at hit point for each result
        for (auto& vResult : vResults) {
            // Transform screen point to depth texture UV
            // pointViewport is already in normalized screen space (0-1)
            VROVector3f screenPoint(pointViewport.x, pointViewport.y, 0.0f);
            VROVector3f depthUV = depthTransform.multiply(screenPoint);

            // Sample depth texture at UV
            float depthValue = sampleDepthTextureAtUV(depthTexture, depthUV.x, depthUV.y);

            // Sample confidence if available
            float confidence = -1.0f;
            if (confidenceTexture && depthSource == "lidar") {
                confidence = sampleDepthTextureAtUV(confidenceTexture, depthUV.x, depthUV.y);
            }

            // Set depth data on result if valid
            if (depthValue > 0.0f) {
                vResult->setDepthData(depthValue, confidence, depthSource);

                // Upgrade result type to DepthPoint when depth data is available
                // For LiDAR: Use confidence threshold of > 0.3 (lowered for better detection)
                // For Monocular: Always upgrade when depth available
                bool shouldUpgradeToDepthPoint = false;
                if (depthSource == "lidar") {
                    // LiDAR: Upgrade if confidence is unavailable or > 0.3
                    shouldUpgradeToDepthPoint = (confidence < 0.0f || confidence > 0.3f);
                    pinfo("LiDAR depth: %.2fm, confidence: %.2f, upgrading: %s",
                          depthValue, confidence, shouldUpgradeToDepthPoint ? "YES" : "NO");
                } else if (depthSource == "monocular") {
                    // Monocular: Always use depth data when available
                    shouldUpgradeToDepthPoint = true;
                    pinfo("Monocular depth: %.2fm, upgrading to DepthPoint", depthValue);
                }

                if (shouldUpgradeToDepthPoint) {
                    pinfo("Upgrading hit result to DepthPoint type");
                    vResult->setType(VROARHitTestResultType::DepthPoint);
                } else {
                    pinfo("Keeping original type, depth confidence too low");
                }
            }
        }
    } else {
        if (!depthTexture) {
            pwarn("No depth texture available for hit test enhancement");
        } else if (!hasDepthData()) {
            pwarn("Frame reports no depth data available");
        }
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
    // Check for LiDAR depth first
    if (hasLiDARDepth()) {
        return true;
    }

    // Check for monocular depth estimation
    std::shared_ptr<VROARSessioniOS> session = _session.lock();
    if (session) {
        auto depthEstimator = session->getMonocularDepthEstimator();
        if (depthEstimator && depthEstimator->isAvailable()) {
            return depthEstimator->getDepthTexture() != nullptr;
        }
    }

    return false;
}

bool VROARFrameiOS::hasLiDARDepth() const {
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

    std::shared_ptr<VROARSessioniOS> session = _session.lock();
    bool preferMonocular = session && session->isPreferMonocularDepth();

    // If preferMonocular is true, skip LiDAR and go directly to monocular depth
    if (!preferMonocular) {
        // Priority 1: LiDAR depth from ARKit (highest quality)
        if (@available(iOS 14.0, *)) {
            ARDepthData *sceneDepth = _frame.sceneDepth;
            if (sceneDepth != nil) {
                CVPixelBufferRef depthMap = sceneDepth.depthMap;
                if (depthMap != nil) {
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

                    if (_depthTexture) {
                        return _depthTexture;
                    }
                }
            }
        }
    }

    // Priority 2: Monocular depth estimation (fallback for non-LiDAR devices, or when preferred)
    if (session) {
        auto depthEstimator = session->getMonocularDepthEstimator();
        if (depthEstimator && depthEstimator->isAvailable()) {
            _depthTexture = depthEstimator->getDepthTexture();
            return _depthTexture;
        }
    }

    return nullptr;
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
     ARKit's displayTransformForOrientation maps FROM camera image (normalized) TO screen.
     We need the inverse: FROM screen UV TO depth texture UV.

     For LiDAR: depth map matches camera image space, so the ARKit inverse is sufficient.
     For monocular: depth texture is 518x518 (ScaleFill crop of camera image), so we
     need to compose the ARKit inverse with a crop correction.
     */
    UIInterfaceOrientation orientation = VROConvert::toDeviceOrientation(_orientation);
    CGSize viewportSize = CGSizeMake(_viewport.getWidth()  / _viewport.getContentScaleFactor(),
                                     _viewport.getHeight() / _viewport.getContentScaleFactor());

    CGAffineTransform arkitInverse = CGAffineTransformInvert(
        [_frame displayTransformForOrientation:orientation viewportSize:viewportSize]);

    // Check if monocular depth is active
    std::shared_ptr<VROARSessioniOS> session = _session.lock();
    bool isMonocular = session && session->isPreferMonocularDepth();
    if (!isMonocular) {
        // Also check: no LiDAR but monocular available
        if (@available(iOS 14.0, *)) {
            isMonocular = (_frame.sceneDepth == nil) && session &&
                          session->getMonocularDepthEstimator() != nullptr;
        }
    }

    CGAffineTransform finalTransform = arkitInverse;

    if (isMonocular) {
        /*
         Monocular depth texture transform: screenUV → depth texture UV.

         The monocular depth buffer is in PORTRAIT orientation because Vision applies
         kCGImagePropertyOrientationRight (90° CW rotation) before feeding the model.
         LiDAR depth is in landscape camera space.

         Chain: screenUV → arkitInverse → landscape camera UV → portrait UV → ScaleFill crop → depth texture UV

         Step 1: arkitInverse gives landscape camera UV (same as LiDAR)
         Step 2: Landscape → Portrait (90° CW rotation): portrait_u = 1 - landscape_v, portrait_v = landscape_u
         Step 3: ScaleFill center-crop correction on portrait_v (model crops tall portrait to square)
         */

        // Get arkitInverse components
        float ai_a = arkitInverse.a, ai_b = arkitInverse.b;
        float ai_c = arkitInverse.c, ai_d = arkitInverse.d;
        float ai_tx = arkitInverse.tx, ai_ty = arkitInverse.ty;
        // landscape_u = ai_a * sx + ai_c * sy + ai_tx
        // landscape_v = ai_b * sx + ai_d * sy + ai_ty

        // Step 2: portrait_u = 1 - landscape_v, portrait_v = landscape_u
        // portrait_u = -ai_b * sx - ai_d * sy + (1 - ai_ty)
        // portrait_v = ai_a * sx + ai_c * sy + ai_tx

        // Step 3: ScaleFill crop correction
        // Portrait image: camLandscapeH x camLandscapeW (e.g. 2160 x 3840)
        // Model input: depthW x depthH (e.g. 518 x 518)
        CGSize imageRes = _frame.camera.imageResolution;
        float portraitW = imageRes.height;  // 2160
        float portraitH = imageRes.width;   // 3840

        int depthW = 518, depthH = 518;
        if (session) {
            auto est = session->getMonocularDepthEstimator();
            if (est) {
                depthW = est->getDepthBufferWidth();
                depthH = est->getDepthBufferHeight();
                if (depthW <= 0 || depthH <= 0) { depthW = 518; depthH = 518; }
            }
        }

        float scale = std::max((float)depthW / portraitW, (float)depthH / portraitH);
        float scaledW = portraitW * scale;
        float scaledH = portraitH * scale;
        float cropU = (scaledW - depthW) / (2.0f * scaledW);  // crop in portrait u (should be ~0)
        float cropV = (scaledH - depthH) / (2.0f * scaledH);  // crop in portrait v (~0.219)
        float visU = 1.0f - 2.0f * cropU;
        float visV = 1.0f - 2.0f * cropV;

        // depth_u = (portrait_u - cropU) / visU
        // depth_v = (portrait_v - cropV) / visV
        float du_sx = -ai_b / visU;
        float du_sy = -ai_d / visU;
        float du_c  = (1.0f - ai_ty - cropU) / visU;

        float dv_sx = ai_a / visV;
        float dv_sy = ai_c / visV;
        float dv_c  = (ai_tx - cropV) / visV;

        VROMatrix4f matrix;
        matrix[0]  = du_sx;    // col 0, row 0
        matrix[1]  = dv_sx;    // col 0, row 1
        matrix[4]  = du_sy;    // col 1, row 0
        matrix[5]  = dv_sy;    // col 1, row 1
        matrix[12] = du_c;     // tx
        matrix[13] = dv_c;     // ty
        return matrix;
    }

    VROMatrix4f matrix;
    matrix[0] = finalTransform.a;
    matrix[1] = finalTransform.b;
    matrix[4] = finalTransform.c;
    matrix[5] = finalTransform.d;
    matrix[12] = finalTransform.tx;
    matrix[13] = finalTransform.ty;
    return matrix;
}

float VROARFrameiOS::getSemanticLabelFraction(VROSemanticLabel label) {
    // Scene Semantics on iOS is provided through the ARCore SDK
    // Delegate to the session which has access to the ARCore provider
    std::shared_ptr<VROARSessioniOS> session = _session.lock();
    if (session) {
        return session->getSemanticLabelFraction(label);
    }
    return 0.0f;
}

#pragma mark - Depth Mesh Generation

std::shared_ptr<VROARDepthMesh> VROARFrameiOS::generateDepthMesh(
    int stride,
    float minConfidence,
    float maxDepth)
{
    if (@available(iOS 14.0, *)) {
        // Prefer smoothed depth if available (temporal smoothing reduces noise)
        ARDepthData *depthData = _frame.smoothedSceneDepth;
        if (!depthData) {
            depthData = _frame.sceneDepth;
        }
        if (!depthData) {
            pinfo("VROARFrameiOS: No depth data available");
            return nullptr;
        }

        CVPixelBufferRef depthMap = depthData.depthMap;
        CVPixelBufferRef confidenceMap = depthData.confidenceMap;

        if (!depthMap) {
            pinfo("VROARFrameiOS: Depth map is null");
            return nullptr;
        }

        CVPixelBufferLockBaseAddress(depthMap, kCVPixelBufferLock_ReadOnly);
        if (confidenceMap) {
            CVPixelBufferLockBaseAddress(confidenceMap, kCVPixelBufferLock_ReadOnly);
        }

        int depthWidth = (int)CVPixelBufferGetWidth(depthMap);
        int depthHeight = (int)CVPixelBufferGetHeight(depthMap);

        pinfo("VROARFrameiOS: Depth map size: %dx%d", depthWidth, depthHeight);

        if (depthWidth <= 0 || depthHeight <= 0) {
            CVPixelBufferUnlockBaseAddress(depthMap, kCVPixelBufferLock_ReadOnly);
            if (confidenceMap) {
                CVPixelBufferUnlockBaseAddress(confidenceMap, kCVPixelBufferLock_ReadOnly);
            }
            return nullptr;
        }

        float *depthValues = (float *)CVPixelBufferGetBaseAddress(depthMap);
        uint8_t *confidenceValues = confidenceMap ?
            (uint8_t *)CVPixelBufferGetBaseAddress(confidenceMap) : nullptr;

        // Get camera intrinsics for proper unprojection
        // ARKit intrinsics are:
        //   fx  0  cx
        //   0  fy  cy
        //   0   0   1
        // where (fx, fy) are focal lengths in pixels and (cx, cy) is principal point
        matrix_float3x3 intrinsics = _frame.camera.intrinsics;
        float fx = intrinsics.columns[0][0];
        float fy = intrinsics.columns[1][1];
        float cx = intrinsics.columns[2][0];
        float cy = intrinsics.columns[2][1];

        // Get camera image resolution (intrinsics are relative to this)
        CGSize imageRes = _frame.camera.imageResolution;
        float imageWidth = imageRes.width;
        float imageHeight = imageRes.height;

        // Scale intrinsics to depth map resolution
        float scaleX = (float)depthWidth / imageWidth;
        float scaleY = (float)depthHeight / imageHeight;
        fx *= scaleX;
        fy *= scaleY;
        cx *= scaleX;
        cy *= scaleY;

        pinfo("VROARFrameiOS: Scaled intrinsics fx=%.2f fy=%.2f cx=%.2f cy=%.2f", fx, fy, cx, cy);

        // Get camera transform (camera-to-world)
        VROMatrix4f cameraToWorld = VROConvert::toMatrix4f(_frame.camera.transform);

        // Calculate grid dimensions based on stride
        int gridWidth = (depthWidth + stride - 1) / stride;
        int gridHeight = (depthHeight + stride - 1) / stride;

        // Prepare output buffers
        std::vector<VROVector3f> vertices;
        std::vector<float> confidences;
        std::vector<int> indices;
        std::vector<float> depthsAtVertices; // Store original depths for discontinuity check

        vertices.reserve(gridWidth * gridHeight);
        confidences.reserve(gridWidth * gridHeight);
        depthsAtVertices.reserve(gridWidth * gridHeight);
        indices.reserve(gridWidth * gridHeight * 6);

        // Map from grid position to vertex index (-1 if invalid)
        std::vector<int> vertexMap(gridWidth * gridHeight, -1);

        int skippedInvalid = 0;
        int skippedConfidence = 0;

        // Generate vertices by sampling depth at stride intervals
        int vertexIndex = 0;
        for (int gy = 0; gy < gridHeight; gy++) {
            for (int gx = 0; gx < gridWidth; gx++) {
                int px = gx * stride;
                int py = gy * stride;

                if (px >= depthWidth || py >= depthHeight) continue;

                int pixelIndex = py * depthWidth + px;
                float depthMeters = depthValues[pixelIndex];

                // Skip invalid depth
                if (depthMeters <= 0 || depthMeters > maxDepth || std::isnan(depthMeters) || std::isinf(depthMeters)) {
                    skippedInvalid++;
                    continue;
                }

                // Check confidence if available
                // ARKit confidence: 0=low, 1=medium, 2=high
                float confidence = 1.0f;
                if (confidenceValues) {
                    uint8_t confLevel = confidenceValues[pixelIndex];
                    confidence = confLevel / 2.0f;  // Normalize to 0-1
                }
                if (confidence < minConfidence) {
                    skippedConfidence++;
                    continue;
                }

                // Unproject from depth image coordinates to camera space using intrinsics
                // In depth image: (0,0) is top-left, Y increases downward
                // camX: positive to the right of principal point
                // camY: positive below the principal point (depth image convention)
                // camZ: depth in meters (positive into the scene)
                float camX = (px - cx) * depthMeters / fx;
                float camY = (py - cy) * depthMeters / fy;
                float camZ = depthMeters;

                // Convert to ARKit camera space:
                // ARKit: X-right, Y-up, Z-backward (camera looks along -Z)
                // - X stays the same (right is positive in both)
                // - Y is negated (depth image has Y-down, ARKit has Y-up)
                // - Z is negated (depth is forward-positive, ARKit has Z-backward)
                VROVector4f camPos(camX, -camY, -camZ, 1.0f);

                // Transform to world space
                VROVector4f worldPos = cameraToWorld.multiply(camPos);

                vertexMap[gy * gridWidth + gx] = vertexIndex++;
                vertices.push_back(VROVector3f(worldPos.x, worldPos.y, worldPos.z));
                confidences.push_back(confidence);
                depthsAtVertices.push_back(depthMeters);
            }
        }

        pinfo("VROARFrameiOS: Generated %d vertices (skipped %d invalid, %d low confidence)",
              (int)vertices.size(), skippedInvalid, skippedConfidence);

        // Debug: Print bounding box of vertices
        if (!vertices.empty()) {
            VROVector3f minPt = vertices[0];
            VROVector3f maxPt = vertices[0];
            for (const auto& v : vertices) {
                minPt.x = std::min(minPt.x, v.x);
                minPt.y = std::min(minPt.y, v.y);
                minPt.z = std::min(minPt.z, v.z);
                maxPt.x = std::max(maxPt.x, v.x);
                maxPt.y = std::max(maxPt.y, v.y);
                maxPt.z = std::max(maxPt.z, v.z);
            }
            pinfo("VROARFrameiOS: Mesh bounds min=(%.2f, %.2f, %.2f) max=(%.2f, %.2f, %.2f)",
                  minPt.x, minPt.y, minPt.z, maxPt.x, maxPt.y, maxPt.z);
        }

        // Generate triangle indices, skipping triangles that span depth discontinuities
        const float maxDepthDiff = 0.3f; // 30cm threshold
        int skippedDiscontinuity = 0;
        int triangleCount = 0;

        for (int gy = 0; gy < gridHeight - 1; gy++) {
            for (int gx = 0; gx < gridWidth - 1; gx++) {
                int i00 = vertexMap[gy * gridWidth + gx];
                int i10 = vertexMap[gy * gridWidth + (gx + 1)];
                int i01 = vertexMap[(gy + 1) * gridWidth + gx];
                int i11 = vertexMap[(gy + 1) * gridWidth + (gx + 1)];

                // All four corners must have valid vertices
                if (i00 >= 0 && i10 >= 0 && i01 >= 0 && i11 >= 0) {
                    // Check for depth discontinuities using original depth values
                    float d00 = depthsAtVertices[i00];
                    float d10 = depthsAtVertices[i10];
                    float d01 = depthsAtVertices[i01];
                    float d11 = depthsAtVertices[i11];

                    float diff1 = std::abs(d00 - d10);
                    float diff2 = std::abs(d00 - d01);
                    float diff3 = std::abs(d10 - d11);
                    float diff4 = std::abs(d01 - d11);
                    float maxDiffVal = std::max(std::max(diff1, diff2), std::max(diff3, diff4));

                    if (maxDiffVal < maxDepthDiff) {
                        // Triangle 1
                        indices.push_back(i00);
                        indices.push_back(i10);
                        indices.push_back(i01);

                        // Triangle 2
                        indices.push_back(i10);
                        indices.push_back(i11);
                        indices.push_back(i01);

                        triangleCount += 2;
                    } else {
                        skippedDiscontinuity++;
                    }
                }
            }
        }

        pinfo("VROARFrameiOS: Generated %d triangles (skipped %d quads due to discontinuity)",
              triangleCount, skippedDiscontinuity);

        CVPixelBufferUnlockBaseAddress(depthMap, kCVPixelBufferLock_ReadOnly);
        if (confidenceMap) {
            CVPixelBufferUnlockBaseAddress(confidenceMap, kCVPixelBufferLock_ReadOnly);
        }

        if (vertices.empty() || indices.empty()) {
            pinfo("VROARFrameiOS: No valid mesh generated (vertices=%d, indices=%d)",
                  (int)vertices.size(), (int)indices.size());
            return nullptr;
        }

        return std::make_shared<VROARDepthMesh>(
            std::move(vertices),
            std::move(indices),
            std::move(confidences)
        );
    }

    return nullptr;
}

float VROARFrameiOS::sampleDepthTextureAtUV(std::shared_ptr<VROTexture> texture, float u, float v) const {
    if (!texture) {
        return 0.0f;
    }

    // Clamp UV coordinates to valid range
    u = std::max(0.0f, std::min(1.0f, u));
    v = std::max(0.0f, std::min(1.0f, v));

    // For iOS depth textures, we need to access the underlying CVPixelBuffer
    // Check if this is a LiDAR depth texture
    if (@available(iOS 14.0, *)) {
        if (_frame.sceneDepth && _frame.sceneDepth.depthMap) {
            CVPixelBufferRef depthMap = _frame.sceneDepth.depthMap;
            CVPixelBufferLockBaseAddress(depthMap, kCVPixelBufferLock_ReadOnly);

            size_t width = CVPixelBufferGetWidth(depthMap);
            size_t height = CVPixelBufferGetHeight(depthMap);
            Float32 *depthData = (Float32 *)CVPixelBufferGetBaseAddress(depthMap);

            if (depthData) {
                // Convert UV to pixel coordinates
                int x = static_cast<int>(u * (width - 1));
                int y = static_cast<int>(v * (height - 1));

                // Clamp to valid pixel range
                x = std::max(0, std::min(static_cast<int>(width - 1), x));
                y = std::max(0, std::min(static_cast<int>(height - 1), y));

                float depth = depthData[y * width + x];

                CVPixelBufferUnlockBaseAddress(depthMap, kCVPixelBufferLock_ReadOnly);
                return depth;
            }

            CVPixelBufferUnlockBaseAddress(depthMap, kCVPixelBufferLock_ReadOnly);
        }
    }

    // Fallback: Try to sample from monocular depth texture
    std::shared_ptr<VROARSessioniOS> session = _session.lock();
    if (session) {
        auto depthEstimator = session->getMonocularDepthEstimator();
        if (depthEstimator && depthEstimator->isAvailable()) {
            std::shared_ptr<VROTexture> monoDepth = depthEstimator->getDepthTexture();
            if (monoDepth && monoDepth == texture) {
                // Access CPU-side depth buffer
                const float* depthData = depthEstimator->getDepthBufferData();
                int width = depthEstimator->getDepthBufferWidth();
                int height = depthEstimator->getDepthBufferHeight();

                if (depthData && width > 0 && height > 0) {
                    // Convert UV to pixel coordinates
                    int x = static_cast<int>(u * (width - 1));
                    int y = static_cast<int>(v * (height - 1));

                    // Clamp to valid pixel range
                    x = std::max(0, std::min(width - 1, x));
                    y = std::max(0, std::min(height - 1, y));

                    // Sample depth buffer (row-major storage)
                    return depthData[y * width + x];
                }
            }
        }
    }

    return 0.0f;
}

#endif
