//
//  VROARSessioniOSExtended.h
//  ViroKit
//
//  Extended ARKit session with support for new features in iOS 14.0+
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

#ifndef VROARSessioniOSExtended_h
#define VROARSessioniOSExtended_h

#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 140000
#include "VROARSessioniOS.h"
#include "VROARLocationAnchoriOS.h"
#include "VROARSceneUnderstandingiOS.h"
#include "VROARFaceTrackingiOS.h"
#include <CoreLocation/CoreLocation.h>
#include <functional>
#include <vector>

@class VROARKitExtendedDelegate;
@class VROLocationManager;

class API_AVAILABLE(ios(14.0)) VROARSessioniOSExtended : public VROARSessioniOS {
public:
    
    VROARSessioniOSExtended(VROTrackingType trackingType,
                           VROWorldAlignment worldAlignment,
                           std::shared_ptr<VRODriver> driver);
    virtual ~VROARSessioniOSExtended();
    
    // ================== Geo Tracking (Location Anchors) ==================
    
    /*
     Enable geo tracking for location-based AR.
     Requires location permissions and supported locations.
     */
    bool enableGeoTracking();
    bool isGeoTrackingSupported() const;
    
    /*
     Get current geo tracking state and accuracy.
     */
    ARGeoTrackingState getGeoTrackingState() const API_AVAILABLE(ios(14.0));
    ARGeoTrackingAccuracy getGeoTrackingAccuracy() const API_AVAILABLE(ios(14.0));
    ARGeoTrackingStateReason getGeoTrackingStateReason() const API_AVAILABLE(ios(14.0));
    
    /*
     Create a geo anchor at specified coordinates.
     */
    std::shared_ptr<VROARLocationAnchoriOS> createGeoAnchor(
        CLLocationCoordinate2D coordinate,
        CLLocationDistance altitude,
        float *orientation = nullptr) API_AVAILABLE(ios(14.0));
    
    /*
     Check geo tracking availability at location.
     */
    void checkGeoTrackingAvailability(
        CLLocationCoordinate2D coordinate,
        std::function<void(bool available, NSError* error)> callback) API_AVAILABLE(ios(14.0));
    
    /*
     Get coaching overlay guidance for geo tracking.
     */
    ARCoachingOverlayView* getGeoCoachingOverlay() const API_AVAILABLE(ios(14.0));
    
    // ================== Scene Understanding ==================
    
    /*
     Enable scene reconstruction and classification (requires LiDAR).
     */
    bool enableSceneUnderstanding(bool reconstruction = true,
                                 bool classification = true,
                                 bool personOcclusion = true);
    bool isSceneUnderstandingSupported() const;
    
    /*
     Get scene understanding manager.
     */
    std::shared_ptr<VROARSceneUnderstandingiOS> getSceneUnderstanding() const {
        return _sceneUnderstanding;
    }
    
    /*
     Enable depth data capture (LiDAR devices).
     */
    bool enableDepthData(bool smoothing = true) API_AVAILABLE(ios(14.0));
    
    /*
     Get current depth and confidence maps.
     */
    CVPixelBufferRef getDepthMap() const API_AVAILABLE(ios(14.0));
    CVPixelBufferRef getConfidenceMap() const API_AVAILABLE(ios(14.0));
    
    /*
     Ray cast against scene mesh.
     */
    std::vector<ARRaycastResult*> raycastSceneMesh(
        VROVector3f origin,
        VROVector3f direction,
        ARRaycastTarget target = ARRaycastTargetExistingPlaneGeometry) API_AVAILABLE(ios(13.4));
    
    // ================== Face Tracking ==================
    
    /*
     Enable face tracking (requires TrueDepth camera).
     */
    bool enableFaceTracking(bool multipleFaces = false);
    bool isFaceTrackingSupported() const;
    
    /*
     Get currently tracked faces.
     */
    std::vector<std::shared_ptr<VROARFaceTrackingiOS>> getTrackedFaces() const;
    
    /*
     Set face tracking delegate.
     */
    void setFaceTrackingDelegate(std::shared_ptr<VROARFaceTrackingDelegate> delegate) {
        _faceTrackingDelegate = delegate;
    }
    
    /*
     Configure simultaneous front and back camera (iOS 13.0+).
     */
    bool enableSimultaneousFrontAndBackCamera() API_AVAILABLE(ios(13.0));
    
    // ================== Body Tracking ==================
    
    /*
     Enable 3D body tracking (requires A12+ processor).
     */
    bool enableBodyTracking(bool enable3D = true, bool enable2D = true) API_AVAILABLE(ios(13.0));
    bool isBodyTrackingSupported() const API_AVAILABLE(ios(13.0));
    
    /*
     Get detected body anchor.
     */
    ARBodyAnchor* getBodyAnchor() const API_AVAILABLE(ios(13.0));
    ARBody2D* getBody2D() const API_AVAILABLE(ios(13.0));
    
    // ================== Collaboration ==================
    
    /*
     Enable collaborative session for multi-user AR.
     */
    bool enableCollaborativeSession() API_AVAILABLE(ios(13.0));
    
    /*
     Send/receive collaboration data.
     */
    void sendCollaborationData(NSData* data) API_AVAILABLE(ios(13.0));
    void setCollaborationDataHandler(
        std::function<void(NSData* data, ARSession* peer)> handler) API_AVAILABLE(ios(13.0));
    
    // ================== App Clip Code Detection ==================
    
    /*
     Enable App Clip Code detection (iOS 14.3+).
     */
    bool enableAppClipCodeTracking() API_AVAILABLE(ios(14.3));
    
    /*
     Get detected App Clip Codes.
     */
    std::vector<ARAppClipCodeAnchor*> getAppClipCodeAnchors() const API_AVAILABLE(ios(14.3));
    
    // ================== Recording & Replay ==================
    
    /*
     Start recording AR session.
     */
    bool startRecording(NSURL* fileURL) API_AVAILABLE(ios(13.0));
    void stopRecording(std::function<void(NSURL* url, NSError* error)> completion) API_AVAILABLE(ios(13.0));
    
    /*
     Replay recorded AR session.
     */
    bool startReplay(NSURL* fileURL) API_AVAILABLE(ios(13.0));
    
    // ================== Configuration ==================
    
    /*
     Create configuration with all supported features.
     */
    ARConfiguration* createExtendedConfiguration();
    
    /*
     Update configuration with new features.
     */
    void updateConfiguration(ARConfiguration* config);
    
protected:
    
    // Override parent methods
    virtual void run() override;
    virtual void pause() override;
    virtual std::unique_ptr<VROARFrame> &updateFrame() override;
    
private:
    
    // Geo tracking
    bool _geoTrackingEnabled;
    ARGeoTrackingConfiguration* _geoConfig API_AVAILABLE(ios(14.0));
    std::vector<std::shared_ptr<VROARLocationAnchoriOS>> _geoAnchors;
    VROLocationManager* _locationManager;
    
    // Scene understanding
    bool _sceneUnderstandingEnabled;
    std::shared_ptr<VROARSceneUnderstandingiOS> _sceneUnderstanding API_AVAILABLE(ios(13.4));
    
    // Face tracking
    bool _faceTrackingEnabled;
    ARFaceTrackingConfiguration* _faceConfig API_AVAILABLE(ios(11.0));
    std::vector<std::shared_ptr<VROARFaceTrackingiOS>> _trackedFaces;
    std::shared_ptr<VROARFaceTrackingDelegate> _faceTrackingDelegate;
    
    // Body tracking
    bool _bodyTrackingEnabled;
    ARBodyTrackingConfiguration* _bodyConfig API_AVAILABLE(ios(13.0));
    ARBodyAnchor* _currentBodyAnchor API_AVAILABLE(ios(13.0));
    ARBody2D* _currentBody2D API_AVAILABLE(ios(13.0));
    
    // Collaboration
    bool _collaborativeSessionEnabled;
    std::function<void(NSData*, ARSession*)> _collaborationDataHandler;
    
    // App Clip Codes
    bool _appClipCodeTrackingEnabled;
    std::vector<ARAppClipCodeAnchor*> _appClipCodeAnchors API_AVAILABLE(ios(14.3));
    
    // Recording
    bool _isRecording;
    
    // Extended delegate
    VROARKitExtendedDelegate* _extendedDelegate;
    
    // Helper methods
    void processGeoAnchors(NSArray<ARAnchor*>* anchors);
    void processFaceAnchors(NSArray<ARAnchor*>* anchors);
    void processBodyAnchors(NSArray<ARAnchor*>* anchors);
    void processAppClipCodeAnchors(NSArray<ARAnchor*>* anchors) API_AVAILABLE(ios(14.3));
    void updateSceneUnderstanding(ARFrame* frame);
    
    ARConfiguration* selectBestConfiguration();
    bool supportsConfiguration(ARConfiguration* config);
};

// Location manager helper
API_AVAILABLE(ios(14.0))
@interface VROLocationManager : NSObject <CLLocationManagerDelegate>
@property (nonatomic, strong) CLLocationManager* locationManager;
@property (nonatomic, copy) void (^authorizationHandler)(CLAuthorizationStatus);
@property (nonatomic, copy) void (^locationUpdateHandler)(CLLocation*);

- (void)requestLocationAuthorization;
- (void)startLocationUpdates;
- (void)stopLocationUpdates;
@end

// Extended ARKit delegate
API_AVAILABLE(ios(14.0))
@interface VROARKitExtendedDelegate : NSObject <ARSessionDelegate, ARSessionObserver>
- (instancetype)initWithSession:(std::shared_ptr<VROARSessioniOSExtended>)session;
@end

#endif // iOS 14.0+
#endif /* VROARSessioniOSExtended_h */