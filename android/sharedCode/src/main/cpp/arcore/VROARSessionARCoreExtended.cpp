//
//  VROARSessionARCoreExtended.cpp
//  ViroKit
//
//  Extended ARCore session implementation with new features
//  Copyright © 2024 Viro Media. All rights reserved.
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

#include "VROARSessionARCoreExtended.h"
#include "VROLog.h"
#include "VROTime.h"
#include "VROPlatformUtil.h"
#include <algorithm>

VROARSessionARCoreExtended::VROARSessionARCoreExtended(std::shared_ptr<VRODriverOpenGL> driver) :
    VROARSessionARCore(driver),
    _geospatialMode(VROARGeospatialMode::Disabled),
    _earth(nullptr),
    _semanticMode(VROARSemanticMode::Disabled),
    _sceneSemantics(std::make_shared<VROARSceneSemantics>()),
    _semanticImage(nullptr),
    _faceTrackingMode(VROARFaceTrackingMode::Disabled),
    _maxFacesToTrack(1),
    _faceList(nullptr),
    _depthEnabled(false),
    _depthImage(nullptr),
    _isRecording(false),
    _isPlayingBack(false),
    _recordingConfig(nullptr),
    _playbackDataset(nullptr) {
}

VROARSessionARCoreExtended::~VROARSessionARCoreExtended() {
    // Clean up ARCore objects
    if (_earth) {
        _earth->release();
    }
    if (_semanticImage) {
        _semanticImage->release();
    }
    if (_faceList) {
        _faceList->release();
    }
    if (_depthImage) {
        _depthImage->release();
    }
    if (_recordingConfig) {
        _recordingConfig->release();
    }
    if (_playbackDataset) {
        _playbackDataset->release();
    }
}

#pragma mark - Geospatial API

bool VROARSessionARCoreExtended::setGeospatialMode(VROARGeospatialMode mode) {
    if (_geospatialMode == mode) {
        return true;
    }
    
    if (!_session) {
        pwarn("Cannot set geospatial mode: ARCore session not initialized");
        return false;
    }
    
    arcore::Config* config = _session->getConfig();
    if (!config) {
        pwarn("Cannot get ARCore config for geospatial mode");
        return false;
    }
    
    arcore::GeospatialMode arcoreMode;
    switch (mode) {
        case VROARGeospatialMode::Disabled:
            arcoreMode = arcore::GeospatialMode::DISABLED;
            break;
        case VROARGeospatialMode::Enabled:
            arcoreMode = arcore::GeospatialMode::ENABLED;
            break;
        case VROARGeospatialMode::EnabledWithTerrainAnchors:
            // Check if terrain anchors are supported
            if (!arcore::Session::isGeospatialModeSupported(arcoreMode)) {
                pwarn("Terrain anchors not supported on this device");
                return false;
            }
            arcoreMode = arcore::GeospatialMode::ENABLED;
            break;
    }
    
    config->setGeospatialMode(arcoreMode);
    
    // Apply configuration
    arcore::Status status = _session->configure(config);
    if (status != arcore::Status::OK) {
        pinfo("Failed to configure geospatial mode: %d", static_cast<int>(status));
        config->release();
        return false;
    }
    
    _geospatialMode = mode;
    
    // Initialize Earth if enabled
    if (mode != VROARGeospatialMode::Disabled) {
        _earth = _session->getEarth();
        if (!_earth) {
            pwarn("Failed to get Earth object from ARCore");
            config->release();
            return false;
        }
    }
    
    config->release();
    pinfo("Geospatial mode set to: %d", static_cast<int>(mode));
    return true;
}

VROARSessionARCoreExtended::GeospatialPose VROARSessionARCoreExtended::getCurrentGeospatialPose() const {
    GeospatialPose pose = {};
    
    if (!_earth || _geospatialMode == VROARGeospatialMode::Disabled) {
        pose.isValid = false;
        return pose;
    }
    
    arcore::GeospatialPose* geoPose = _earth->getCameraGeospatialPose();
    if (!geoPose) {
        pose.isValid = false;
        return pose;
    }
    
    pose.latitude = geoPose->getLatitude();
    pose.longitude = geoPose->getLongitude();
    pose.altitude = geoPose->getAltitude();
    pose.heading = geoPose->getHeading();
    pose.headingAccuracy = geoPose->getHeadingAccuracy();
    pose.horizontalAccuracy = geoPose->getHorizontalAccuracy();
    pose.verticalAccuracy = geoPose->getVerticalAccuracy();
    pose.isValid = true;
    
    geoPose->release();
    return pose;
}

std::shared_ptr<VROARGeospatialAnchor> VROARSessionARCoreExtended::createGeospatialAnchor(
    double latitude, 
    double longitude, 
    double altitude,
    const VROQuaternion& eastUpSouthQuat) {
    
    if (!_earth || _geospatialMode == VROARGeospatialMode::Disabled) {
        pwarn("Cannot create geospatial anchor: Geospatial mode not enabled");
        return nullptr;
    }
    
    // Create ARCore anchor
    arcore::Anchor* anchor = _earth->createAnchor(latitude, longitude, altitude,
                                                  eastUpSouthQuat.x, eastUpSouthQuat.y,
                                                  eastUpSouthQuat.z, eastUpSouthQuat.w);
    if (!anchor) {
        pwarn("Failed to create ARCore geospatial anchor");
        return nullptr;
    }
    
    // Create VRO anchor
    auto vroAnchor = std::make_shared<VROARGeospatialAnchor>();
    vroAnchor->setId(VROStringUtil::toString((long) anchor));
    vroAnchor->setLatitude(latitude);
    vroAnchor->setLongitude(longitude);
    vroAnchor->setAltitude(altitude);
    vroAnchor->setEastUpSouthOrientation(eastUpSouthQuat);
    
    _geospatialAnchors.push_back(vroAnchor);
    
    pinfo("Created geospatial anchor at %.6f, %.6f, %.2f", latitude, longitude, altitude);
    return vroAnchor;
}

std::shared_ptr<VROARGeospatialAnchor> VROARSessionARCoreExtended::createTerrainAnchor(
    double latitude,
    double longitude,
    const VROQuaternion& eastUpSouthQuat,
    std::function<void(std::shared_ptr<VROARGeospatialAnchor>, double altitude)> onComplete) {
    
    if (!_earth || _geospatialMode != VROARGeospatialMode::EnabledWithTerrainAnchors) {
        pwarn("Cannot create terrain anchor: Terrain anchors not enabled");
        return nullptr;
    }
    
    // Create terrain anchor
    arcore::TerrainAnchor* terrainAnchor = _earth->createTerrainAnchor(
        latitude, longitude,
        eastUpSouthQuat.x, eastUpSouthQuat.y, eastUpSouthQuat.z, eastUpSouthQuat.w);
    
    if (!terrainAnchor) {
        pwarn("Failed to create ARCore terrain anchor");
        return nullptr;
    }
    
    // Create VRO anchor
    auto vroAnchor = std::make_shared<VROARGeospatialAnchor>();
    vroAnchor->setId(VROStringUtil::toString((long) terrainAnchor));
    vroAnchor->setLatitude(latitude);
    vroAnchor->setLongitude(longitude);
    vroAnchor->setEastUpSouthOrientation(eastUpSouthQuat);
    vroAnchor->setTerrainAnchor(true);
    
    // Set up async callback for terrain resolution
    terrainAnchor->setTerrainAnchorCallback([onComplete, vroAnchor](
        arcore::TerrainAnchorState state, double altitude) {
        if (state == arcore::TerrainAnchorState::SUCCESS) {
            vroAnchor->setAltitude(altitude);
            if (onComplete) {
                onComplete(vroAnchor, altitude);
            }
        }
    });
    
    _geospatialAnchors.push_back(vroAnchor);
    
    pinfo("Created terrain anchor at %.6f, %.6f", latitude, longitude);
    return vroAnchor;
}

void VROARSessionARCoreExtended::checkVPSAvailability(double latitude, double longitude,
                                                      std::function<void(VPSAvailability)> callback) {
    if (!_earth) {
        if (callback) {
            callback(VPSAvailability::Unknown);
        }
        return;
    }
    
    // Use ARCore's VPS availability check
    _earth->checkVpsAvailability(latitude, longitude, [callback](arcore::VpsAvailability availability) {
        VPSAvailability vroAvailability = VPSAvailability::Unknown;
        
        switch (availability) {
            case arcore::VpsAvailability::AVAILABLE:
                vroAvailability = VPSAvailability::Available;
                break;
            case arcore::VpsAvailability::UNAVAILABLE:
                vroAvailability = VPSAvailability::Unavailable;
                break;
            case arcore::VpsAvailability::ERROR_NETWORK_CONNECTION:
                vroAvailability = VPSAvailability::ErrorNetworkConnection;
                break;
            case arcore::VpsAvailability::ERROR_INTERNAL:
                vroAvailability = VPSAvailability::ErrorInternal;
                break;
            default:
                vroAvailability = VPSAvailability::Unknown;
                break;
        }
        
        if (callback) {
            callback(vroAvailability);
        }
    });
}

#pragma mark - Scene Semantics

bool VROARSessionARCoreExtended::setSemanticMode(VROARSemanticMode mode) {
    if (_semanticMode == mode) {
        return true;
    }
    
    if (!_session) {
        pwarn("Cannot set semantic mode: ARCore session not initialized");
        return false;
    }
    
    arcore::Config* config = _session->getConfig();
    if (!config) {
        pwarn("Cannot get ARCore config for semantic mode");
        return false;
    }
    
    bool enableSemantics = (mode == VROARSemanticMode::Enabled);
    config->setSemanticMode(enableSemantics ? 
                           arcore::SemanticMode::ENABLED : 
                           arcore::SemanticMode::DISABLED);
    
    arcore::Status status = _session->configure(config);
    if (status != arcore::Status::OK) {
        pinfo("Failed to configure semantic mode: %d", static_cast<int>(status));
        config->release();
        return false;
    }
    
    _semanticMode = mode;
    _sceneSemantics->setEnabled(enableSemantics);
    
    config->release();
    pinfo("Scene semantics %s", enableSemantics ? "enabled" : "disabled");
    return true;
}

bool VROARSessionARCoreExtended::isSemanticLabelSupported(VROARSemanticLabel label) const {
    if (!_session) {
        return false;
    }
    
    // Map VRO labels to ARCore labels and check support
    arcore::SemanticLabel arcoreLabel;
    switch (label) {
        case VROARSemanticLabel::Sky:
            arcoreLabel = arcore::SemanticLabel::SKY;
            break;
        case VROARSemanticLabel::Building:
            arcoreLabel = arcore::SemanticLabel::BUILDING;
            break;
        case VROARSemanticLabel::Tree:
            arcoreLabel = arcore::SemanticLabel::TREE;
            break;
        case VROARSemanticLabel::Road:
            arcoreLabel = arcore::SemanticLabel::ROAD;
            break;
        case VROARSemanticLabel::Person:
            arcoreLabel = arcore::SemanticLabel::PERSON;
            break;
        default:
            return false;
    }
    
    return _session->isSemanticLabelSupported(arcoreLabel);
}

#pragma mark - Frame Processing

void VROARSessionARCoreExtended::processFrame() {
    // Call parent processing
    VROARSessionARCore::processFrame();
    
    // Process extended features
    updateGeospatialAnchors();
    updateSemantics();
    updateFaces();
    updateDepth();
}

void VROARSessionARCoreExtended::updateGeospatialAnchors() {
    if (_geospatialMode == VROARGeospatialMode::Disabled || !_earth) {
        return;
    }
    
    // Update all geospatial anchors
    for (auto& anchor : _geospatialAnchors) {
        // Update anchor transform from ARCore
        // This would require mapping the anchor ID back to ARCore anchor
        // and getting its current pose
    }
}

void VROARSessionARCoreExtended::updateSemantics() {
    if (_semanticMode == VROARSemanticMode::Disabled || !_frame) {
        return;
    }
    
    // Get semantic image from current frame
    if (_semanticImage) {
        _semanticImage->release();
    }
    
    _semanticImage = _frame->acquireSemanticImage();
    if (!_semanticImage) {
        return;
    }
    
    // Update scene semantics with new data
    int width = _semanticImage->getWidth();
    int height = _semanticImage->getHeight();
    const uint8_t* labelData = _semanticImage->getPixelData();
    const float* confidenceData = _semanticImage->getConfidenceData();
    
    _sceneSemantics->updateFromFrame(labelData, confidenceData, width, height);
}

void VROARSessionARCoreExtended::updateFaces() {
    if (_faceTrackingMode == VROARFaceTrackingMode::Disabled || !_frame) {
        return;
    }
    
    if (_faceList) {
        _faceList->release();
    }
    
    _faceList = _frame->getUpdatedAugmentedFaces();
    if (!_faceList) {
        return;
    }
    
    // Clear old faces
    _trackedFaces.clear();
    
    // Process each detected face
    int faceCount = _faceList->getSize();
    for (int i = 0; i < faceCount && i < _maxFacesToTrack; i++) {
        arcore::AugmentedFace* face = _faceList->at(i);
        if (face && face->getTrackingState() == arcore::TrackingState::TRACKING) {
            auto vroFace = createFaceFromARCore(face);
            if (vroFace) {
                _trackedFaces.push_back(vroFace);
            }
        }
    }
}

void VROARSessionARCoreExtended::updateDepth() {
    if (!_depthEnabled || !_frame) {
        return;
    }
    
    if (_depthImage) {
        _depthImage->release();
    }
    
    _depthImage = _frame->acquireRawDepthImage();
    if (!_depthImage) {
        return;
    }
    
    // Process depth data if needed
    // This could be used for occlusion or scene understanding
}

std::shared_ptr<VROARAugmentedFace> VROARSessionARCoreExtended::createFaceFromARCore(
    arcore::AugmentedFace* face) {
    
    auto vroFace = std::make_shared<VROARAugmentedFace>();
    
    // Set basic properties
    vroFace->setId(VROStringUtil::toString((long) face));
    vroFace->setTrackingConfidence(1.0f); // ARCore doesn't provide confidence
    
    // Update face mesh
    updateFaceMesh(vroFace, face);
    
    // Get face center pose
    arcore::Pose* centerPose = face->getCenterPose();
    if (centerPose) {
        VROMatrix4f transform = convertPoseToMatrix(centerPose);
        vroFace->setCenterPose(transform);
        centerPose->release();
    }
    
    return vroFace;
}

void VROARSessionARCoreExtended::updateFaceMesh(std::shared_ptr<VROARAugmentedFace> face,
                                                arcore::AugmentedFace* arFace) {
    
    // Get mesh vertices
    int vertexCount = arFace->getMeshVertexCount();
    const float* vertices = arFace->getMeshVertices();
    
    std::vector<VROVector3f> vroVertices;
    for (int i = 0; i < vertexCount; i++) {
        vroVertices.push_back(VROVector3f(vertices[i*3], vertices[i*3+1], vertices[i*3+2]));
    }
    face->setMeshVertices(vroVertices);
    
    // Get mesh indices
    int indexCount = arFace->getMeshTriangleCount() * 3;
    const uint16_t* indices = arFace->getMeshTriangleIndices();
    
    std::vector<uint16_t> vroIndices(indices, indices + indexCount);
    face->setMeshIndices(vroIndices);
    
    // Get UV coordinates
    const float* uvs = arFace->getMeshUVs();
    std::vector<VROVector2f> vroUVs;
    for (int i = 0; i < vertexCount; i++) {
        vroUVs.push_back(VROVector2f(uvs[i*2], uvs[i*2+1]));
    }
    face->setMeshUVs(vroUVs);
    
    // Get normals
    const float* normals = arFace->getMeshNormals();
    std::vector<VROVector3f> vroNormals;
    for (int i = 0; i < vertexCount; i++) {
        vroNormals.push_back(VROVector3f(normals[i*3], normals[i*3+1], normals[i*3+2]));
    }
    face->setMeshNormals(vroNormals);
}

VROMatrix4f VROARSessionARCoreExtended::convertPoseToMatrix(arcore::Pose* pose) {
    float translation[3];
    float rotation[4];
    
    pose->getTranslation(translation);
    pose->getRotation(rotation);
    
    // Convert to VRO matrix
    VROQuaternion quat(rotation[0], rotation[1], rotation[2], rotation[3]);
    VROVector3f trans(translation[0], translation[1], translation[2]);
    
    return VROMatrix4f(trans, quat, VROVector3f(1, 1, 1));
}

#pragma mark - Cloud Anchors Enhanced

VROARSessionARCoreExtended::CloudAnchorQuality 
VROARSessionARCoreExtended::getCloudAnchorHostingQuality(std::shared_ptr<VROARAnchor> anchor) const {
    
    if (!_session || !anchor) {
        return CloudAnchorQuality::Insufficient;
    }
    
    // Get ARCore anchor from VRO anchor
    // This would require maintaining a mapping between VRO and ARCore anchors
    // For now, we'll use the session's estimate based on current conditions
    
    arcore::FeatureMapQuality quality = _session->estimateFeatureMapQualityForHosting();
    
    switch (quality) {
        case arcore::FeatureMapQuality::GOOD:
            return CloudAnchorQuality::Good;
        case arcore::FeatureMapQuality::SUFFICIENT:
            return CloudAnchorQuality::Sufficient;
        case arcore::FeatureMapQuality::INSUFFICIENT:
        default:
            return CloudAnchorQuality::Insufficient;
    }
}

void VROARSessionARCoreExtended::hostCloudAnchorWithTTL(
    std::shared_ptr<VROARAnchor> anchor,
    int ttlDays,
    std::function<void(std::shared_ptr<VROARAnchor>)> onSuccess,
    std::function<void(std::string error)> onFailure) {
    
    if (!_session) {
        if (onFailure) {
            onFailure("ARCore session not initialized");
        }
        return;
    }
    
    // Clamp TTL to valid range (1-365 days)
    ttlDays = std::max(1, std::min(365, ttlDays));
    
    // This would require getting the ARCore anchor from the VRO anchor
    // and calling the ARCore hosting API with TTL
    // For now, we'll call the parent method
    hostCloudAnchor(anchor, onSuccess, onFailure);
}

#pragma mark - Depth API

bool VROARSessionARCoreExtended::setDepthMode(bool enabled) {
    if (_depthEnabled == enabled) {
        return true;
    }
    
    if (!_session) {
        pwarn("Cannot set depth mode: ARCore session not initialized");
        return false;
    }
    
    if (enabled && !isDepthModeSupported()) {
        pwarn("Depth mode not supported on this device");
        return false;
    }
    
    arcore::Config* config = _session->getConfig();
    if (!config) {
        pwarn("Cannot get ARCore config for depth mode");
        return false;
    }
    
    config->setDepthMode(enabled ? arcore::DepthMode::AUTOMATIC : arcore::DepthMode::DISABLED);
    
    arcore::Status status = _session->configure(config);
    if (status != arcore::Status::OK) {
        pinfo("Failed to configure depth mode: %d", static_cast<int>(status));
        config->release();
        return false;
    }
    
    _depthEnabled = enabled;
    config->release();
    
    pinfo("Depth mode %s", enabled ? "enabled" : "disabled");
    return true;
}

bool VROARSessionARCoreExtended::isDepthModeSupported() const {
    if (!_session) {
        return false;
    }
    
    return _session->isDepthModeSupported(arcore::DepthMode::AUTOMATIC);
}

VROARSessionARCoreExtended::DepthImage VROARSessionARCoreExtended::getRawDepthImage() const {
    DepthImage depthImage = {};
    
    if (!_depthImage || !_depthEnabled) {
        depthImage.isValid = false;
        return depthImage;
    }
    
    depthImage.width = _depthImage->getWidth();
    depthImage.height = _depthImage->getHeight();
    
    // Copy depth data
    const uint16_t* depthData = reinterpret_cast<const uint16_t*>(_depthImage->getData());
    int pixelCount = depthImage.width * depthImage.height;
    depthImage.depthData.assign(depthData, depthData + pixelCount);
    
    // Get transform matrix
    float transformData[16];
    _depthImage->getDisplayGeometryTransform(transformData);
    depthImage.depthToViewTransform = VROMatrix4f(transformData);
    
    depthImage.isValid = true;
    return depthImage;
}

float VROARSessionARCoreExtended::getDepthAtScreenPoint(float x, float y) const {
    if (!_depthImage || !_depthEnabled) {
        return 0.0f;
    }
    
    int width = _depthImage->getWidth();
    int height = _depthImage->getHeight();
    
    // Convert normalized coordinates to pixel coordinates
    int pixelX = static_cast<int>(x * width);
    int pixelY = static_cast<int>(y * height);
    
    // Bounds check
    if (pixelX < 0 || pixelX >= width || pixelY < 0 || pixelY >= height) {
        return 0.0f;
    }
    
    // Get depth value at pixel
    const uint16_t* depthData = reinterpret_cast<const uint16_t*>(_depthImage->getData());
    uint16_t depthMM = depthData[pixelY * width + pixelX];
    
    // Convert from millimeters to meters
    return static_cast<float>(depthMM) / 1000.0f;
}

#pragma mark - Recording & Playback

bool VROARSessionARCoreExtended::startRecording(const std::string& mp4FilePath) {
    if (!_session) {
        pwarn("Cannot start recording: ARCore session not initialized");
        return false;
    }
    
    if (_isRecording) {
        pwarn("Recording already in progress");
        return false;
    }
    
    _recordingConfig = arcore::RecordingConfig::create();
    _recordingConfig->setMp4DatasetFilePath(mp4FilePath.c_str());
    _recordingConfig->setAutoStopOnPause(true);
    
    arcore::Status status = _session->startRecording(_recordingConfig);
    if (status != arcore::Status::OK) {
        pinfo("Failed to start recording: %d", static_cast<int>(status));
        return false;
    }
    
    _isRecording = true;
    pinfo("Recording started: %s", mp4FilePath.c_str());
    return true;
}

void VROARSessionARCoreExtended::stopRecording() {
    if (!_isRecording || !_session) {
        return;
    }
    
    arcore::Status status = _session->stopRecording();
    if (status != arcore::Status::OK) {
        pinfo("Error stopping recording: %d", static_cast<int>(status));
    }
    
    _isRecording = false;
    pinfo("Recording stopped");
}

bool VROARSessionARCoreExtended::startPlayback(const std::string& mp4FilePath) {
    if (!_session) {
        pwarn("Cannot start playback: ARCore session not initialized");
        return false;
    }
    
    if (_isPlayingBack) {
        pwarn("Playback already in progress");
        return false;
    }
    
    _playbackDataset = arcore::PlaybackDataset::create(mp4FilePath.c_str());
    if (!_playbackDataset) {
        pwarn("Failed to create playback dataset from: %s", mp4FilePath.c_str());
        return false;
    }
    
    arcore::Status status = _session->setPlaybackDataset(_playbackDataset);
    if (status != arcore::Status::OK) {
        pinfo("Failed to set playback dataset: %d", static_cast<int>(status));
        return false;
    }
    
    _isPlayingBack = true;
    pinfo("Playback started: %s", mp4FilePath.c_str());
    return true;
}

void VROARSessionARCoreExtended::stopPlayback() {
    if (!_isPlayingBack || !_session) {
        return;
    }
    
    _session->setPlaybackDataset(nullptr);
    _isPlayingBack = false;
    
    pinfo("Playback stopped");
}