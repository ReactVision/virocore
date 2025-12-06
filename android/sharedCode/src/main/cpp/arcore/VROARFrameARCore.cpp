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
        toReturn.push_back(vResult);
        delete (pose);
        delete (trackable);
    }

    delete (hitResultList);
    return toReturn;
}
VROMatrix4f VROARFrameARCore::getViewportToCameraImageTransform() const {
    pabort("Not supported on ARCore");
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
                if(pointsIdArray != NULL) {
                    int pointId = pointsIdArray[i];
                    points.push_back(point);
                    //TODO: Does android return negative points ids? If not, this cast to uint64 shouldn't
                    //be a problem.
                    identifiers.push_back((uint64_t) pointId);
                }
            }
        }
        delete (pointCloud);
    }
    _pointCloud = std::make_shared<VROARPointCloud>(points, identifiers);
    return _pointCloud;
}

#pragma mark - Depth Data

void VROARFrameARCore::acquireDepthData() {
    // Reset state
    _depthDataAvailable = false;
    _depthTexture = nullptr;
    _depthConfidenceTexture = nullptr;
    _depthWidth = 0;
    _depthHeight = 0;

    std::shared_ptr<VROARSessionARCore> session = _session.lock();
    std::shared_ptr<VRODriver> driver = _driver.lock();
    if (!session || !driver) {
        return;
    }

    // Acquire depth image from ARCore
    arcore::Image *depthImage = nullptr;
    arcore::ImageRetrievalStatus status = _frame->acquireDepthImage(&depthImage);

    if (status != arcore::ImageRetrievalStatus::Success || depthImage == nullptr) {
        // Depth not available for this frame
        return;
    }

    // Get depth image dimensions
    _depthWidth = depthImage->getWidth();
    _depthHeight = depthImage->getHeight();

    if (_depthWidth <= 0 || _depthHeight <= 0) {
        delete depthImage;
        return;
    }

    // Get depth data (16-bit depth in millimeters)
    const uint8_t *depthData = nullptr;
    int depthDataLength = 0;
    depthImage->getPlaneData(0, &depthData, &depthDataLength);

    if (depthData == nullptr || depthDataLength <= 0) {
        delete depthImage;
        return;
    }

    // Create depth texture from the raw data
    // ARCore provides depth as 16-bit unsigned integers in millimeters
    // Convert to 32-bit float (in meters) for consistency with iOS and shader usage
    int numPixels = _depthWidth * _depthHeight;
    std::vector<float> floatDepthData(numPixels);

    const uint16_t *depthData16 = reinterpret_cast<const uint16_t*>(depthData);
    for (int i = 0; i < numPixels; i++) {
        // Convert from millimeters (uint16) to meters (float)
        floatDepthData[i] = static_cast<float>(depthData16[i]) / 1000.0f;
    }

    // Create VROData from the float depth data
    std::shared_ptr<VROData> depthVROData = std::make_shared<VROData>(
        floatDepthData.data(),
        floatDepthData.size() * sizeof(float),
        VRODataOwnership::Copy);
    std::vector<std::shared_ptr<VROData>> dataVec = { depthVROData };

    // Create the depth texture using R32F format
    _depthTexture = std::make_shared<VROTexture>(VROTextureType::Texture2D,
                                                  VROTextureFormat::R32F,
                                                  VROTextureInternalFormat::R32F,
                                                  false, // not sRGB
                                                  VROMipmapMode::None,
                                                  dataVec,
                                                  _depthWidth, _depthHeight,
                                                  std::vector<uint32_t>());

    _depthDataAvailable = true;

    // Clean up
    delete depthImage;

    pinfo("VROARFrameARCore: Acquired depth data %dx%d", _depthWidth, _depthHeight);
}

std::shared_ptr<VROTexture> VROARFrameARCore::getDepthTexture() {
    if (!_depthDataAvailable) {
        acquireDepthData();
    }
    return _depthTexture;
}

std::shared_ptr<VROTexture> VROARFrameARCore::getDepthConfidenceTexture() {
    // Confidence texture acquisition can be implemented similarly
    // For now, return nullptr as it's optional
    return _depthConfidenceTexture;
}

bool VROARFrameARCore::hasDepthData() const {
    return _depthDataAvailable;
}

int VROARFrameARCore::getDepthImageWidth() const {
    return _depthWidth;
}

int VROARFrameARCore::getDepthImageHeight() const {
    return _depthHeight;
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
    return _frame->getSemanticLabelFraction(arcoreLabel);
}

int VROARFrameARCore::getSemanticImageWidth() const {
    acquireSemanticData();
    return _semanticWidth;
}

int VROARFrameARCore::getSemanticImageHeight() const {
    acquireSemanticData();
    return _semanticHeight;
}