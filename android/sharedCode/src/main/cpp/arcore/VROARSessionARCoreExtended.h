//
//  VROARSessionARCoreExtended.h
//  ViroKit
//
//  Extended ARCore session with support for new features in ARCore 1.50.0
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

#ifndef VROARSessionARCoreExtended_h
#define VROARSessionARCoreExtended_h

#include "VROARSessionARCore.h"
#include "VROARGeospatialAnchor.h"
#include "VROARSceneSemantics.h"
#include "VROARAugmentedFace.h"
#include <functional>

class VROARSessionARCoreExtended : public VROARSessionARCore {
public:
    
    VROARSessionARCoreExtended(std::shared_ptr<VRODriverOpenGL> driver);
    virtual ~VROARSessionARCoreExtended();
    
    // ================== Geospatial API ==================
    
    /*
     Configure Geospatial mode for the session.
     */
    bool setGeospatialMode(VROARGeospatialMode mode);
    VROARGeospatialMode getGeospatialMode() const { return _geospatialMode; }
    
    /*
     Get the current geospatial pose of the device.
     Returns latitude, longitude, altitude, and heading.
     */
    struct GeospatialPose {
        double latitude;
        double longitude;
        double altitude;
        float heading;
        float headingAccuracy;
        float horizontalAccuracy;
        float verticalAccuracy;
        bool isValid;
    };
    GeospatialPose getCurrentGeospatialPose() const;
    
    /*
     Create a geospatial anchor at the specified coordinates.
     */
    std::shared_ptr<VROARGeospatialAnchor> createGeospatialAnchor(
        double latitude, 
        double longitude, 
        double altitude,
        const VROQuaternion& eastUpSouthQuat);
    
    /*
     Create a terrain anchor (altitude relative to terrain).
     Requires Geospatial mode with terrain anchors enabled.
     */
    std::shared_ptr<VROARGeospatialAnchor> createTerrainAnchor(
        double latitude,
        double longitude,
        const VROQuaternion& eastUpSouthQuat,
        std::function<void(std::shared_ptr<VROARGeospatialAnchor>, double altitude)> onComplete);
    
    /*
     Create a rooftop anchor (altitude relative to building rooftop).
     Requires Geospatial mode with terrain anchors enabled.
     */
    std::shared_ptr<VROARGeospatialAnchor> createRooftopAnchor(
        double latitude,
        double longitude,
        const VROQuaternion& eastUpSouthQuat,
        std::function<void(std::shared_ptr<VROARGeospatialAnchor>, double altitude)> onComplete);
    
    /*
     Check VPS (Visual Positioning Service) availability at current location.
     */
    enum class VPSAvailability {
        Unknown,
        Available,
        Unavailable,
        ErrorNetworkConnection,
        ErrorInternal
    };
    void checkVPSAvailability(double latitude, double longitude,
                              std::function<void(VPSAvailability)> callback);
    
    // ================== Scene Semantics ==================
    
    /*
     Enable or disable Scene Semantics.
     */
    bool setSemanticMode(VROARSemanticMode mode);
    VROARSemanticMode getSemanticMode() const { return _semanticMode; }
    
    /*
     Get the current scene semantics data.
     */
    std::shared_ptr<VROARSceneSemantics> getSceneSemantics() const { return _sceneSemantics; }
    
    /*
     Query if a specific semantic label is supported on this device.
     */
    bool isSemanticLabelSupported(VROARSemanticLabel label) const;
    
    // ================== Augmented Faces ==================
    
    /*
     Configure face tracking mode.
     Note: Front camera face tracking requires different session configuration.
     */
    bool setFaceTrackingMode(VROARFaceTrackingMode mode);
    VROARFaceTrackingMode getFaceTrackingMode() const { return _faceTrackingMode; }
    
    /*
     Get all currently tracked augmented faces.
     */
    std::vector<std::shared_ptr<VROARAugmentedFace>> getTrackedFaces() const;
    
    /*
     Set maximum number of faces to track simultaneously (default: 1).
     */
    void setMaxFacesToTrack(int maxFaces);
    int getMaxFacesToTrack() const { return _maxFacesToTrack; }
    
    // ================== Enhanced Cloud Anchors ==================
    
    /*
     Host a cloud anchor with quality feedback.
     */
    enum class CloudAnchorQuality {
        Insufficient,
        Sufficient,
        Good
    };
    CloudAnchorQuality getCloudAnchorHostingQuality(std::shared_ptr<VROARAnchor> anchor) const;
    
    /*
     Host a cloud anchor with TTL (time-to-live) in days.
     Maximum TTL is 365 days.
     */
    void hostCloudAnchorWithTTL(std::shared_ptr<VROARAnchor> anchor,
                                int ttlDays,
                                std::function<void(std::shared_ptr<VROARAnchor>)> onSuccess,
                                std::function<void(std::string error)> onFailure);
    
    // ================== Depth API ==================
    
    /*
     Enable raw depth API for dense depth maps.
     */
    bool setDepthMode(bool enabled);
    bool isDepthModeSupported() const;
    
    /*
     Get raw depth image data.
     */
    struct DepthImage {
        std::vector<uint16_t> depthData;  // Depth in millimeters
        int width;
        int height;
        VROMatrix4f depthToViewTransform;
        bool isValid;
    };
    DepthImage getRawDepthImage() const;
    
    /*
     Get depth at a specific screen point.
     */
    float getDepthAtScreenPoint(float x, float y) const;
    
    // ================== Recording & Playback ==================
    
    /*
     Start recording the AR session for playback.
     */
    bool startRecording(const std::string& mp4FilePath);
    void stopRecording();
    bool isRecording() const { return _isRecording; }
    
    /*
     Playback a previously recorded AR session.
     */
    bool startPlayback(const std::string& mp4FilePath);
    void stopPlayback();
    bool isPlayingBack() const { return _isPlayingBack; }
    
protected:
    
    // Override parent class methods to add new feature processing
    virtual void processFrame() override;
    virtual void updateAnchors() override;
    
private:
    
    // Geospatial API
    VROARGeospatialMode _geospatialMode;
    arcore::Earth* _earth;
    std::vector<std::shared_ptr<VROARGeospatialAnchor>> _geospatialAnchors;
    
    // Scene Semantics
    VROARSemanticMode _semanticMode;
    std::shared_ptr<VROARSceneSemantics> _sceneSemantics;
    arcore::SemanticImage* _semanticImage;
    
    // Augmented Faces
    VROARFaceTrackingMode _faceTrackingMode;
    std::vector<std::shared_ptr<VROARAugmentedFace>> _trackedFaces;
    int _maxFacesToTrack;
    arcore::AugmentedFaceList* _faceList;
    
    // Depth API
    bool _depthEnabled;
    arcore::DepthImage* _depthImage;
    
    // Recording & Playback
    bool _isRecording;
    bool _isPlayingBack;
    arcore::RecordingConfig* _recordingConfig;
    arcore::PlaybackDataset* _playbackDataset;
    
    // Helper methods
    void updateGeospatialAnchors();
    void updateSemantics();
    void updateFaces();
    void updateDepth();
    
    std::shared_ptr<VROARAugmentedFace> createFaceFromARCore(arcore::AugmentedFace* face);
    void updateFaceMesh(std::shared_ptr<VROARAugmentedFace> face, arcore::AugmentedFace* arFace);
};

#endif /* VROARSessionARCoreExtended_h */