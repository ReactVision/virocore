//
//  VROARFrameARCore.cpp
//  ViroKit
//
//  Created by Raj Advani on 9/27/17.
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

#include "VROARFrameARCore.h"
#include "VROARSessionARCore.h"
#include "VROARCameraARCore.h"
#include "VROARHitTestResult.h"
#include "VROPlatformUtil.h"
#include "VROVector4f.h"
#include "VROLight.h"
#include "VROARHitTestResultARCore.h"
#include "VROTexture.h"
#include "VROData.h"
#include "VRODriver.h"
#include "VROLog.h"
#include "VROFieldOfView.h"
#include "VROARDepthMesh.h"

static const bool kDebugFrameLogs = false;

VROARFrameARCore::VROARFrameARCore(arcore::Frame *frame,
                                   VROViewport viewport,
                                   std::shared_ptr<VROARSessionARCore> session) :
    _session(session),
    _viewport(viewport) {

    _frame = frame;
    _camera = std::make_shared<VROARCameraARCore>(frame, session);
}

VROARFrameARCore::~VROARFrameARCore() {

}

double VROARFrameARCore::getTimestamp() const {
    std::shared_ptr<VROARSessionARCore> session = _session.lock();
    if (!session) {
        return 0;
    }
    return (double) _frame->getTimestampNs();
}

const std::shared_ptr<VROARCamera> &VROARFrameARCore::getCamera() const {
    return _camera;
}

// TODO: VIRO-1940 filter results based on types. Right now, devs can't set this, so don't use filtering.
std::vector<std::shared_ptr<VROARHitTestResult>> VROARFrameARCore::hitTest(int x, int y, std::set<VROARHitTestResultType> types) {
    std::shared_ptr<VROARSessionARCore> session = _session.lock();
    if (!session) {
        return {};
    }
    arcore::Session *session_arc = session->getSessionInternal();

    arcore::HitResultList *hitResultList = session_arc->createHitResultList();
    _frame->hitTest(x, y, hitResultList);

    int listSize = hitResultList->size();
    std::vector<std::shared_ptr<VROARHitTestResult>> toReturn;

    for (int i = 0; i < listSize; i++) {
        std::shared_ptr<arcore::HitResult> hitResult = std::shared_ptr<arcore::HitResult>(session_arc->createHitResult());
        hitResultList->getItem(i, hitResult.get());

        // Get the trackable associated with this hit result. Not all hit results have an
        // associated trackable. If a hit result does not have a trackable, we can still acquire
        // an anchor for it via hitResult->acquireAnchor(). This will create an anchor at the hit
        // result's pose. However, we don't immediately acquire this anchor because the user may
        // not even use the hit result. Instead we allow the user to manually acquire the anchor via
        // ARHitTestResult.createAnchoredNode().
        arcore::Trackable *trackable = hitResult->acquireTrackable();

        arcore::Pose *pose = session_arc->createPose();
        hitResult->getPose(pose);

        VROARHitTestResultType type;

        if (trackable != nullptr && trackable->getType() == arcore::TrackableType::Plane) {
            arcore::Plane *plane = (arcore::Plane *) trackable;
            bool inExtent  = plane->isPoseInExtents(pose);
            bool inPolygon = plane->isPoseInPolygon(pose);

            if (inExtent || inPolygon) {
                type = VROARHitTestResultType::ExistingPlaneUsingExtent;
            } else {
                type = VROARHitTestResultType::EstimatedHorizontalPlane;
            }
        } else if (trackable != nullptr && trackable->getType() == arcore::TrackableType::DepthPoint) {
            type = VROARHitTestResultType::DepthPoint;
        } else {
            type = VROARHitTestResultType::FeaturePoint;
        }

        // Get the distance from the camera to the HitResult.
        float distance = hitResult->getDistance();

        // Get the transform to the HitResult.
        float worldTransformMtx[16];
        pose->toMatrix(worldTransformMtx);
        VROMatrix4f worldTransform(worldTransformMtx);
        VROMatrix4f localTransform = VROMatrix4f::identity();

        std::shared_ptr<VROARHitTestResult> vResult = std::make_shared<VROARHitTestResultARCore>(type, distance, hitResult,
                                                                                                 worldTransform, localTransform,
                                                                                                 session);

        // For depth points, add depth data from the hit result
        if (type == VROARHitTestResultType::DepthPoint) {
            // ARCore depth points use the distance as the depth value
            // Confidence is not provided by ARCore depth API
            vResult->setDepthData(distance, -1.0f, "arcore");
        }

        toReturn.push_back(vResult);
        delete (pose);
        delete (trackable);
    }

    delete (hitResultList);
    return toReturn;
}

std::vector<std::shared_ptr<VROARHitTestResult>> VROARFrameARCore::hitTestRay(VROVector3f *origin, VROVector3f *destination , std::set<VROARHitTestResultType> types) {
    std::shared_ptr<VROARSessionARCore> session = _session.lock();
    if (!session) {
        return {};
    }
    arcore::Session *session_arc = session->getSessionInternal();

    arcore::HitResultList *hitResultList = session_arc->createHitResultList();
    _frame->hitTest(origin->x, origin->y, origin->z, destination->x, destination->y, destination->z, hitResultList);

    int listSize = hitResultList->size();
    std::vector<std::shared_ptr<VROARHitTestResult>> toReturn;

    for (int i = 0; i < listSize; i++) {
        std::shared_ptr<arcore::HitResult> hitResult = std::shared_ptr<arcore::HitResult>(session_arc->createHitResult());
        hitResultList->getItem(i, hitResult.get());

        // Get the trackable associated with this hit result. Not all hit results have an
        // associated trackable. If a hit result does not have a trackable, we can still acquire
        // an anchor for it via hitResult->acquireAnchor(). This will create an anchor at the hit
        // result's pose. However, we don't immediately acquire this anchor because the user may
        // not even use the hit result. Instead we allow the user to manually acquire the anchor via
        // ARHitTestResult.createAnchoredNode().
        arcore::Trackable *trackable = hitResult->acquireTrackable();

        arcore::Pose *pose = session_arc->createPose();
        hitResult->getPose(pose);

        VROARHitTestResultType type;

        if (trackable != nullptr && trackable->getType() == arcore::TrackableType::Plane) {
            arcore::Plane *plane = (arcore::Plane *) trackable;
            bool inExtent  = plane->isPoseInExtents(pose);
            bool inPolygon = plane->isPoseInPolygon(pose);

            if (inExtent || inPolygon) {
                type = VROARHitTestResultType::ExistingPlaneUsingExtent;
            } else {
                type = VROARHitTestResultType::EstimatedHorizontalPlane;
            }
        } else if (trackable != nullptr && trackable->getType() == arcore::TrackableType::DepthPoint) {
            type = VROARHitTestResultType::DepthPoint;
        } else {
            type = VROARHitTestResultType::FeaturePoint;
        }

        // Get the distance from the camera to the HitResult.
        float distance = hitResult->getDistance();

        // Get the transform to the HitResult.
        float worldTransformMtx[16];
        pose->toMatrix(worldTransformMtx);
        VROMatrix4f worldTransform(worldTransformMtx);
        VROMatrix4f localTransform = VROMatrix4f::identity();

        std::shared_ptr<VROARHitTestResult> vResult = std::make_shared<VROARHitTestResultARCore>(type, distance, hitResult,
                                                                                                 worldTransform, localTransform,
                                                                                                 session);

        // For depth points, add depth data from the hit result
        if (type == VROARHitTestResultType::DepthPoint) {
            // ARCore depth points use the distance as the depth value
            // Confidence is not provided by ARCore depth API
            vResult->setDepthData(distance, -1.0f, "arcore");
        }

        toReturn.push_back(vResult);
        delete (pose);
        delete (trackable);
    }

    delete (hitResultList);
    return toReturn;
}
VROMatrix4f VROARFrameARCore::getViewportToCameraImageTransform() const {
    std::shared_ptr<VROARSessionARCore> session = _session.lock();
    if (!session) {
        return VROMatrix4f();  // identity
    }

    // ARCore background texcoords: camera-image UVs at 4 viewport corners.
    // Layout from arcore::Frame::getBackgroundTexcoords:
    //   [0,1] = BL (screen 0,0),  [2,3] = TL (screen 0,1),
    //   [4,5] = BR (screen 1,0),  [6,7] = TR (screen 1,1)
    float tc[8] = {};
    _frame->getBackgroundTexcoords(tc);

    float blX = tc[0], blY = tc[1];
    float tlX = tc[2], tlY = tc[3];
    float brX = tc[4], brY = tc[5];

    // Build affine transform M : viewport-UV [0,1]^2  ->  camera-image UV
    //   M * (0,0,0,1)^T = (blX, blY, 0, 1)  (translation)
    //   M * (1,0,0,1)^T = (brX, brY, 0, 1)  (x-basis = BR - BL)
    //   M * (0,1,0,1)^T = (tlX, tlY, 0, 1)  (y-basis = TL - BL)
    //
    // VROMatrix4f uses column-major storage (same as iOS / OpenGL convention):
    //   indices [0..3]  = col 0,  [4..7] = col 1,  [12..15] = col 3
    VROMatrix4f matrix;          // initialised to identity by default ctor
    matrix[0]  = brX - blX;     // col0 row0 — cam-u per screen-x
    matrix[1]  = brY - blY;     // col0 row1 — cam-v per screen-x
    matrix[4]  = tlX - blX;     // col1 row0 — cam-u per screen-y
    matrix[5]  = tlY - blY;     // col1 row1 — cam-v per screen-y
    matrix[12] = blX;            // col3 row0 — cam-u at screen origin
    matrix[13] = blY;            // col3 row1 — cam-v at screen origin

    // Front camera (Augmented Faces): the camera V-axis is inverted relative to
    // the back camera. Apply v' = 1 - v by negating the V components of the matrix
    // and offsetting the translation: M'_v_row = -M_v_row, tx13 = 1 - tx13.
    // This is mathematically equivalent to composing M with a V-flip, without
    // swapping raw texcoords (which distorts aspect ratio).
    {
        std::shared_ptr<VROARSessionARCore> sess = _session.lock();
        if (sess && sess->isFrontCameraEnabled()) {
            matrix[1]  = -matrix[1];        // negate cam-v per screen-x
            matrix[5]  = -matrix[5];        // negate cam-v per screen-y
            matrix[13] = 1.0f - matrix[13]; // flip V translation
            static bool logged = false;
            if (!logged) {
                logged = true;
                __android_log_print(ANDROID_LOG_INFO, "ViroARCore",
                    "Front camera V-flip applied: m1=%.3f m5=%.3f m13=%.3f",
                    matrix[1], matrix[5], matrix[13]);
            }
        }
    }

    return matrix;
}

bool VROARFrameARCore::hasDisplayGeometryChanged() {
    std::shared_ptr<VROARSessionARCore> session = _session.lock();
    if (!session) {
        return false;
    }
    return _frame->hasDisplayGeometryChanged();
}

void VROARFrameARCore::getBackgroundTexcoords(VROVector3f *BL, VROVector3f *BR, VROVector3f *TL, VROVector3f *TR) {
    std::shared_ptr<VROARSessionARCore> session = _session.lock();
    if (!session) {
        return;
    }

    float texcoords[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    _frame->getBackgroundTexcoords(texcoords);
    BL->x = texcoords[0];
    BL->y = texcoords[1];
    TL->x = texcoords[2];
    TL->y = texcoords[3];
    BR->x = texcoords[4];
    BR->y = texcoords[5];
    TR->x = texcoords[6];
    TR->y = texcoords[7];

}

const std::vector<std::shared_ptr<VROARAnchor>> &VROARFrameARCore::getAnchors() const {
    return _anchors; // Always empty; unused in ARCore
}

float VROARFrameARCore::getAmbientLightIntensity() const {
    std::shared_ptr<VROARSessionARCore> session = _session.lock();
    if (!session) {
        return 1.0;
    }

    float intensity = 0;
    arcore::LightEstimate *estimate = session->getSessionInternal()->createLightEstimate();

    _frame->getLightEstimate(estimate);
    if (estimate->isValid()) {
        intensity = estimate->getPixelIntensity();
    } else {
        intensity = 1.0;
    }
    delete (estimate);

    // Multiply by 1000 to get into lumen range
    return intensity * 1000;
}

VROVector3f VROARFrameARCore::getAmbientLightColor() const {
    VROVector3f color = { 1, 1, 1 };

    std::shared_ptr<VROARSessionARCore> session = _session.lock();
    if (!session) {
        return color;
    }

    arcore::LightEstimate *estimate = session->getSessionInternal()->createLightEstimate();
    _frame->getLightEstimate(estimate);

    float correction[4];
    if (estimate->isValid()) {
        estimate->getColorCorrection(correction);
    }
    delete (estimate);

    VROVector3f gammaColor = { correction[0], correction[1], correction[2] };
    return VROLight::convertGammaToLinear(gammaColor);
}

bool VROARFrameARCore::getCameraImageDimensions(int *outWidth, int *outHeight) {
    if (_cameraImageW > 0 && _cameraImageH > 0) {
        *outWidth = _cameraImageW; *outHeight = _cameraImageH;
        return true;
    }
    // getCameraImageY may already have paid for the acquire this frame.
    if (_lumaW > 0 && _lumaH > 0) {
        _cameraImageW = _lumaW; _cameraImageH = _lumaH;
        *outWidth = _cameraImageW; *outHeight = _cameraImageH;
        return true;
    }
    arcore::Image *img = nullptr;
    if (_frame->acquireCameraImage(&img) != arcore::ImageRetrievalStatus::Success || !img) {
        return false;
    }
    _cameraImageW = img->getWidth();
    _cameraImageH = img->getHeight();
    delete img;
    if (_cameraImageW <= 0 || _cameraImageH <= 0) return false;
    *outWidth = _cameraImageW; *outHeight = _cameraImageH;
    return true;
}

bool VROARFrameARCore::getCameraImageY(const uint8_t** data, int* width, int* height) {
    if (!_lumaData.empty()) {
        *data = _lumaData.data(); *width = _lumaW; *height = _lumaH;
        return true;
    }
    arcore::Image* img = nullptr;
    auto status = _frame->acquireCameraImage(&img);
    if (status != arcore::ImageRetrievalStatus::Success || !img) return false;
    int w = img->getWidth(), h = img->getHeight();
    int stride = img->getPlaneRowStride(0);
    const uint8_t* src = nullptr; int len = 0;
    img->getPlaneData(0, &src, &len);
    if (src && w > 0 && h > 0) {
        _lumaW = w; _lumaH = h;
        _lumaData.resize(w * h);
        for (int row = 0; row < h; ++row)
            memcpy(_lumaData.data() + row * w, src + row * stride, w);
    }
    delete img;
    if (_lumaData.empty()) return false;
    *data = _lumaData.data(); *width = _lumaW; *height = _lumaH;
    return true;
}

std::shared_ptr<VROARPointCloud> VROARFrameARCore::getPointCloud() {
    if (_pointCloud) {
        return _pointCloud;
    }
    std::shared_ptr<VROARSessionARCore> session = _session.lock();
    if (!session) {
        return _pointCloud;
    }

    JNIEnv* env = VROPlatformGetJNIEnv();
    std::vector<VROVector4f> points;
    std::vector<uint64_t> identifiers; // Android doesn't have any identifiers with their point cloud!

    arcore::PointCloud *pointCloud = _frame->acquirePointCloud();
    if (pointCloud != NULL) {
        const float *pointsArray = pointCloud->getPoints();
        const int *pointsIdArray = pointCloud->getPointIds();
        int numPoints = pointCloud->getNumPoints();

        for (int i = 0; i < numPoints; i++) {
            // Only use points with > 0.1. This is just meant to make the display of the points
            // look good (if low confidence points are used, we may end up with points very close
            // to the camera).
            if (pointsArray[i * 4 + 3] > .1) {
                VROVector4f point = VROVector4f(pointsArray[i * 4 + 0], pointsArray[i * 4 + 1],
                                                pointsArray[i * 4 + 2], pointsArray[i * 4 + 3]);
                points.push_back(point);
                // Use the ARCore point ID when available; fall back to loop index so the
                // identifiers vector always stays in sync with points (required by VROARPointCloud).
                identifiers.push_back(pointsIdArray ? (uint64_t) pointsIdArray[i] : (uint64_t) i);
            }
        }
        delete (pointCloud);
    }
    _pointCloud = std::make_shared<VROARPointCloud>(points, identifiers);
    return _pointCloud;
}

#pragma mark - Depth Data

void VROARFrameARCore::acquireDepthData() const {
    // Deprecated: Depth data is now managed by VROARSessionARCore
}

std::shared_ptr<VROTexture> VROARFrameARCore::getDepthTexture() {
    std::shared_ptr<VROARSessionARCore> session = _session.lock();
    if (session) {
        return session->getDepthTexture();
    }
    return nullptr;
}

std::shared_ptr<VROTexture> VROARFrameARCore::getDepthConfidenceTexture() {
    return nullptr;
}

bool VROARFrameARCore::hasDepthData() const {
    std::shared_ptr<VROARSessionARCore> session = _session.lock();
    if (session) {
        return session->getDepthTexture() != nullptr;
    }
    return false;
}

int VROARFrameARCore::getDepthImageWidth() const {
    std::shared_ptr<VROARSessionARCore> session = _session.lock();
    if (session && session->getDepthTexture()) {
        return session->getDepthTexture()->getWidth();
    }
    return 0;
}

int VROARFrameARCore::getDepthImageHeight() const {
    std::shared_ptr<VROARSessionARCore> session = _session.lock();
    if (session && session->getDepthTexture()) {
        return session->getDepthTexture()->getHeight();
    }
    return 0;
}

#pragma mark - Scene Semantics

void VROARFrameARCore::acquireSemanticData() const {
    // Only check once per frame
    if (_semanticDataChecked) {
        return;
    }
    _semanticDataChecked = true;

    // Reset state
    _semanticDataAvailable = false;
    _semanticImage = VROSemanticImage();
    _semanticConfidenceImage = VROSemanticConfidenceImage();
    _semanticWidth = 0;
    _semanticHeight = 0;

    std::shared_ptr<VROARSessionARCore> session = _session.lock();
    if (!session) {
        return;
    }

    // Check if semantic mode is enabled
    if (!session->isSemanticModeEnabled()) {
        return;
    }

    // Try to acquire semantic image
    arcore::Image *semanticImage = nullptr;
    arcore::ImageRetrievalStatus status = _frame->acquireSemanticImage(&semanticImage);

    if (status != arcore::ImageRetrievalStatus::Success || semanticImage == nullptr) {
        // Semantic data not yet available (normal during first few frames)
        return;
    }

    // Get image dimensions
    _semanticWidth = semanticImage->getWidth();
    _semanticHeight = semanticImage->getHeight();

    if (_semanticWidth <= 0 || _semanticHeight <= 0) {
        delete semanticImage;
        return;
    }

    // Copy semantic label data
    const uint8_t *data = nullptr;
    int dataLength = 0;
    semanticImage->getPlaneData(0, &data, &dataLength);

    if (data != nullptr && dataLength > 0) {
        _semanticImage.width = _semanticWidth;
        _semanticImage.height = _semanticHeight;
        _semanticImage.data.assign(data, data + dataLength);
        _semanticDataAvailable = true;
    }

    delete semanticImage;

    // Optionally acquire confidence image
    arcore::Image *confidenceImage = nullptr;
    status = _frame->acquireSemanticConfidenceImage(&confidenceImage);

    if (status == arcore::ImageRetrievalStatus::Success && confidenceImage != nullptr) {
        const uint8_t *confData = nullptr;
        int confDataLength = 0;
        confidenceImage->getPlaneData(0, &confData, &confDataLength);

        if (confData != nullptr && confDataLength > 0) {
            _semanticConfidenceImage.width = confidenceImage->getWidth();
            _semanticConfidenceImage.height = confidenceImage->getHeight();
            _semanticConfidenceImage.data.assign(confData, confData + confDataLength);
        }

        delete confidenceImage;
    }
}

bool VROARFrameARCore::hasSemanticData() const {
    acquireSemanticData();
    return _semanticDataAvailable;
}

VROSemanticImage VROARFrameARCore::getSemanticImage() {
    acquireSemanticData();
    return _semanticImage;
}

VROSemanticConfidenceImage VROARFrameARCore::getSemanticConfidenceImage() {
    acquireSemanticData();
    return _semanticConfidenceImage;
}

float VROARFrameARCore::getSemanticLabelFraction(VROSemanticLabel label) {
    // Query ARCore directly for fraction (more efficient than parsing image)
    arcore::SemanticLabel arcoreLabel = static_cast<arcore::SemanticLabel>(static_cast<int>(label));
    float result = _frame->getSemanticLabelFraction(arcoreLabel);

    // Debug: Log non-zero results and periodic status for label 0
    if (result > 0.0f) {
        if (kDebugFrameLogs) pinfo("Semantic label %d fraction: %.4f", static_cast<int>(label), result);
    }
    return result;
}

int VROARFrameARCore::getSemanticImageWidth() const {
    acquireSemanticData();
    return _semanticWidth;
}

int VROARFrameARCore::getSemanticImageHeight() const {
    acquireSemanticData();
    return _semanticHeight;
}

#pragma mark - Depth Mesh Generation

std::shared_ptr<VROARDepthMesh> VROARFrameARCore::generateDepthMesh(
    int stride,
    float minConfidence,
    float maxDepth)
{
    std::shared_ptr<VROARSessionARCore> session = _session.lock();
    if (!session) {
        return nullptr;
    }

    // Acquire depth image from ARCore
    arcore::Image *depthImage = nullptr;
    arcore::ImageRetrievalStatus status = _frame->acquireDepthImage(&depthImage);

    if (status != arcore::ImageRetrievalStatus::Success || depthImage == nullptr) {
        return nullptr;
    }

    int depthWidth = depthImage->getWidth();
    int depthHeight = depthImage->getHeight();

    if (depthWidth <= 0 || depthHeight <= 0) {
        delete depthImage;
        return nullptr;
    }

    // Get depth data (16-bit depth in millimeters)
    const uint8_t *rawData = nullptr;
    int dataLength = 0;
    depthImage->getPlaneData(0, &rawData, &dataLength);

    if (rawData == nullptr || dataLength <= 0) {
        delete depthImage;
        return nullptr;
    }

    const uint16_t *depthData = reinterpret_cast<const uint16_t*>(rawData);

    // Try to get confidence data
    arcore::Image *confidenceImage = nullptr;
    const uint8_t *confidenceData = nullptr;
    status = _frame->acquireDepthConfidenceImage(&confidenceImage);
    if (status == arcore::ImageRetrievalStatus::Success && confidenceImage != nullptr) {
        int confLength = 0;
        confidenceImage->getPlaneData(0, &confidenceData, &confLength);
    }

    // Unprojection, by pinhole intrinsics rather than by the view-projection matrix.
    //
    // This used to build clipPos = (ndcX*d, ndcY*d, -d, d) and run it through the inverse
    // view-projection. That is d * (ndcX, ndcY, -1, 1), and since the multiply is a plain linear
    // product, dividing the result by its own w cancels d exactly: every sample landed on the near
    // plane, a centimetre from the camera, whatever its depth. The depth only ever reached the
    // maxDepth filter. iOS (VROARFrameiOS::generateDepthMesh) always did this the right way.
    float fx = 0, fy = 0, cx = 0, cy = 0;
    _frame->getImageIntrinsics(&fx, &fy, &cx, &cy);
    if (fx <= 0 || fy <= 0) {
        delete depthImage;
        if (confidenceImage) delete confidenceImage;
        pinfo("VROARFrameARCore: no camera intrinsics, cannot build depth mesh");
        return nullptr;
    }

    // Intrinsics are relative to the CPU camera image; scale them to the depth image, which is
    // much smaller (ARCore gives 160x120) but covers the same field of view.
    int imageWidth = 0, imageHeight = 0;
    if (!getCameraImageDimensions(&imageWidth, &imageHeight)) {
        delete depthImage;
        if (confidenceImage) delete confidenceImage;
        pinfo("VROARFrameARCore: no camera image size, cannot scale intrinsics");
        return nullptr;
    }
    const float sx = (float)depthWidth  / (float)imageWidth;
    const float sy = (float)depthHeight / (float)imageHeight;
    fx *= sx; cx *= sx;
    fy *= sy; cy *= sy;

    // The physical camera pose, not the view matrix: the depth image is aligned with the CPU
    // image (+X along a readout row, +Y up the image, -Z forward), while the view matrix inverts
    // the display-oriented pose, which differs by the display rotation.
    float poseMtx[16];
    _frame->getCameraPose(poseMtx);
    VROMatrix4f cameraToWorld(poseMtx);

    // Calculate grid dimensions based on stride
    int gridWidth = (depthWidth + stride - 1) / stride;
    int gridHeight = (depthHeight + stride - 1) / stride;

    // Prepare output buffers
    std::vector<VROVector3f> vertices;
    std::vector<float> confidences;
    std::vector<int> indices;
    // Camera-space depth per emitted vertex. The discontinuity check below needs the distance
    // from the camera, and a world-space coordinate is not that.
    std::vector<float> depthsAtVertices;

    vertices.reserve(gridWidth * gridHeight);
    confidences.reserve(gridWidth * gridHeight);
    depthsAtVertices.reserve(gridWidth * gridHeight);
    indices.reserve(gridWidth * gridHeight * 6);

    // Map from grid position to vertex index (-1 if invalid)
    std::vector<int> vertexMap(gridWidth * gridHeight, -1);

    // Generate vertices by sampling depth at stride intervals
    int vertexIndex = 0;
    for (int gy = 0; gy < gridHeight; gy++) {
        for (int gx = 0; gx < gridWidth; gx++) {
            int px = gx * stride;
            int py = gy * stride;

            if (px >= depthWidth || py >= depthHeight) continue;

            int pixelIndex = py * depthWidth + px;
            uint16_t depthMm = depthData[pixelIndex];

            // Skip invalid depth (0 means no depth data)
            if (depthMm == 0) continue;

            float depthMeters = depthMm / 1000.0f;
            if (depthMeters > maxDepth) continue;

            // Check confidence if available
            float confidence = 1.0f;
            if (confidenceData) {
                confidence = confidenceData[pixelIndex] / 255.0f;
            }
            if (confidence < minConfidence) continue;

            // Pinhole unprojection into camera space, then into the world by the camera pose.
            // Image rows run downward and the camera's +Y is up, hence the negated Y; forward
            // is -Z, hence the negated depth.
            float camX =  ((float)px - cx) * depthMeters / fx;
            float camY = -((float)py - cy) * depthMeters / fy;
            VROVector4f worldPos = cameraToWorld.multiply(
                VROVector4f(camX, camY, -depthMeters, 1.0f));

            vertexMap[gy * gridWidth + gx] = vertexIndex++;
            vertices.push_back(VROVector3f(worldPos.x, worldPos.y, worldPos.z));
            confidences.push_back(confidence);
            depthsAtVertices.push_back(depthMeters);
        }
    }

    // Generate triangle indices, skipping triangles that span depth discontinuities
    const float maxDepthDiff = 0.3f; // 30cm threshold
    for (int gy = 0; gy < gridHeight - 1; gy++) {
        for (int gx = 0; gx < gridWidth - 1; gx++) {
            int i00 = vertexMap[gy * gridWidth + gx];
            int i10 = vertexMap[gy * gridWidth + (gx + 1)];
            int i01 = vertexMap[(gy + 1) * gridWidth + gx];
            int i11 = vertexMap[(gy + 1) * gridWidth + (gx + 1)];

            // All four corners must have valid vertices
            if (i00 >= 0 && i10 >= 0 && i01 >= 0 && i11 >= 0) {
                // Check for depth discontinuities (to avoid connecting walls to floors, etc.).
                // Distance from the camera, not a world coordinate: -vertices[i].z is a world Z,
                // so two samples on the same flat wall could differ by metres, or not at all,
                // depending only on which way the phone was pointing.
                float d00 = depthsAtVertices[i00];
                float d10 = depthsAtVertices[i10];
                float d01 = depthsAtVertices[i01];
                float d11 = depthsAtVertices[i11];

                float diff1 = std::abs(d00 - d10);
                float diff2 = std::abs(d00 - d01);
                float diff3 = std::abs(d10 - d11);
                float diff4 = std::abs(d01 - d11);
                float maxDiff = std::max(std::max(diff1, diff2), std::max(diff3, diff4));

                if (maxDiff < maxDepthDiff) {
                    // Triangle 1: top-left, top-right, bottom-left
                    indices.push_back(i00);
                    indices.push_back(i10);
                    indices.push_back(i01);

                    // Triangle 2: top-right, bottom-right, bottom-left
                    indices.push_back(i10);
                    indices.push_back(i11);
                    indices.push_back(i01);
                }
            }
        }
    }

    // Cleanup
    delete depthImage;
    if (confidenceImage) {
        delete confidenceImage;
    }

    if (vertices.empty() || indices.empty()) {
        return nullptr;
    }

    if (kDebugFrameLogs) pinfo("VROARFrameARCore: Generated depth mesh with %zu vertices, %zu triangles",
          vertices.size(), indices.size() / 3);

    return std::make_shared<VROARDepthMesh>(
        std::move(vertices),
        std::move(indices),
        std::move(confidences),
        "lidar"
    );
}

std::shared_ptr<VROARDepthMesh> VROARFrameARCore::generatePlaneMesh() {
    std::shared_ptr<VROARSessionARCore> session = _session.lock();
    if (!session) {
        return nullptr;
    }

    std::vector<VROVector3f> vertices;
    std::vector<int> indices;
    session->collectPlaneMeshData(vertices, indices);

    if (vertices.empty() || indices.empty()) {
        return nullptr;
    }

    std::vector<float> confidences(vertices.size(), 1.0f);

    if (kDebugFrameLogs) pinfo("VROARFrameARCore: Generated plane mesh with %zu vertices, %zu triangles",
          vertices.size(), indices.size() / 3);

    return std::make_shared<VROARDepthMesh>(
        std::move(vertices),
        std::move(indices),
        std::move(confidences),
        "plane"
    );
}