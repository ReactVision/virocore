// VROARSessionOpenXR.h
// ViroRenderer
//
// Mixed-Reality AR session for Meta Quest via OpenXR. Mirrors the shape of
// VROARSessionARCore / VROARSessionARKit, but is driven by OpenXR vendor /
// cross-vendor MR extensions instead of an ARCore/ARKit frame.
//
// Two plane sources are supported, whichever the runtime exposes:
//   1. XR_EXT_plane_detection  — cross-vendor real-time plane detection.
//   2. XR_FB_scene + XR_FB_spatial_entity(_query) — Meta's room model captured
//      once via Space Setup (floor/walls/ceiling/desk as 2D bounded entities).
//      This is what Quest's runtime actually implements today.
//
// Either way each plane becomes a VROARPlaneAnchor fed through the standard
// VROARSession anchor → delegate pipeline (anchorWasDetected / anchorWillUpdate /
// anchorDidUpdate / anchorWasRemoved), so the existing JNI → ARScene.Listener →
// JS (onAnchorFound / ViroARPlane) path is reused unchanged.
//
// The renderer (VROSceneRendererOpenXR) owns one instance, initialises it once
// the XrSession + reference space exist, calls updateFrame() each frame, and
// forwards FB spatial-query events via onSpatialEvent().
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#ifndef ANDROID_VROARSESSIONOPENXR_H
#define ANDROID_VROARSESSIONOPENXR_H

#include <map>
#include <memory>
#include <chrono>
#include <vector>

#include "VROARSession.h"
#include "VROARPlaneAnchor.h"

#include <openxr/openxr.h>

class VROARFrame;

class VROARSessionOpenXR : public VROARSession,
                           public std::enable_shared_from_this<VROARSessionOpenXR> {
public:

    VROARSessionOpenXR();
    virtual ~VROARSessionOpenXR();

    // ── OpenXR-specific lifecycle (called by VROSceneRendererOpenXR) ───────────
    /*
     Load XR_EXT_plane_detection entry points and create the plane detector.
     Returns false if the extension or detector is unavailable. `baseSpace` is the
     reference space plane poses are reported in (pass the renderer's stage space).
     */
    bool initPlaneDetection(XrInstance instance, XrSession session, XrSpace baseSpace);

    /*
     Load XR_FB_scene / XR_FB_spatial_entity(_query) entry points. Returns false if
     unavailable. Safe to call alongside initPlaneDetection(); whichever the runtime
     supports drives updateFrame().
     */
    bool initSceneDetection(XrInstance instance, XrSession session, XrSpace baseSpace);

    /* True if either plane source initialised. */
    bool hasPlaneSource() const {
        return _planeDetector != XR_NULL_HANDLE || _sceneAvailable;
    }

    /* Predicted display time for the current frame — set before updateFrame(). */
    void setDisplayTime(XrTime displayTime) { _displayTime = displayTime; }

    /* Forward FB spatial-query events (polled by the renderer's xrPollEvent loop). */
    void onSpatialEvent(const XrEventDataBuffer &event);

    /* Tear down detectors. Call from the renderer's destroySession(). */
    void destroyPlaneDetector();

    // ── VROARSession: implemented ──────────────────────────────────────────────
    void addAnchor(std::shared_ptr<VROARAnchor> anchor) override;
    void removeAnchor(std::shared_ptr<VROARAnchor> anchor) override;
    void updateAnchor(std::shared_ptr<VROARAnchor> anchor) override;
    std::unique_ptr<VROARFrame> &updateFrame() override;
    std::unique_ptr<VROARFrame> &getLastFrame() override { return _currentFrame; }
    bool isReady() const override { return hasPlaneSource(); }

    // ── VROARSession: no-ops / not supported at this phase ─────────────────────
    void setTrackingType(VROTrackingType type) override {}
    void run() override {}
    void pause() override {}
    void resetSession(bool resetTracking, bool removeAnchors) override {}
    bool setAnchorDetection(std::set<VROAnchorDetection> types) override { return true; }
    void setCloudAnchorProvider(VROCloudAnchorProvider provider) override {}
    void setAutofocus(bool enabled) override {}
    bool isCameraAutoFocusEnabled() override { return false; }
    void setNumberOfTrackedImages(int numImages) override {}
    void loadARImageDatabase(std::shared_ptr<VROARImageDatabase> db) override {}
    void unloadARImageDatabase() override {}
    void addARImageTarget(std::shared_ptr<VROARImageTarget> target) override {}
    void removeARImageTarget(std::shared_ptr<VROARImageTarget> target) override {}
    void addARObjectTarget(std::shared_ptr<VROARObjectTarget> target) override {}
    void removeARObjectTarget(std::shared_ptr<VROARObjectTarget> target) override {}
    void hostCloudAnchor(std::shared_ptr<VROARAnchor> anchor, int ttlDays,
                         std::function<void(std::shared_ptr<VROARAnchor>)> onSuccess,
                         std::function<void(std::string error)> onFailure) override {
        if (onFailure) onFailure("Cloud anchors not supported on OpenXR");
    }
    void resolveCloudAnchor(std::string cloudAnchorId,
                            std::function<void(std::shared_ptr<VROARAnchor>)> onSuccess,
                            std::function<void(std::string error)> onFailure) override {
        if (onFailure) onFailure("Cloud anchors not supported on OpenXR");
    }
    std::shared_ptr<VROTexture> getCameraBackgroundTexture() override { return nullptr; }
    void setViewport(VROViewport viewport) override {}
    void setOrientation(VROCameraOrientation orientation) override {}
    void setWorldOrigin(VROMatrix4f relativeTransform) override {}
    void setVideoQuality(VROVideoQuality quality) override {}
    void setVisionModel(std::shared_ptr<VROVisionModel> visionModel) override {}

private:

    // ── OpenXR handles (not owned — owned by VROSceneRendererOpenXR) ───────────
    XrInstance _instance    = XR_NULL_HANDLE;
    XrSession  _session     = XR_NULL_HANDLE;
    XrSpace    _baseSpace    = XR_NULL_HANDLE;
    XrTime     _displayTime  = 0;

    // ── XR_EXT_plane_detection ─────────────────────────────────────────────────
    XrPlaneDetectorEXT _planeDetector = XR_NULL_HANDLE;
    PFN_xrCreatePlaneDetectorEXT     _pfnCreatePlaneDetector     = nullptr;
    PFN_xrDestroyPlaneDetectorEXT    _pfnDestroyPlaneDetector    = nullptr;
    PFN_xrBeginPlaneDetectionEXT     _pfnBeginPlaneDetection     = nullptr;
    PFN_xrGetPlaneDetectionStateEXT  _pfnGetPlaneDetectionState  = nullptr;
    PFN_xrGetPlaneDetectionsEXT      _pfnGetPlaneDetections      = nullptr;
    PFN_xrGetPlanePolygonBufferEXT   _pfnGetPlanePolygonBuffer   = nullptr;
    bool _detectionInFlight = false;
    std::chrono::steady_clock::time_point _lastSweepStart;

    // ── XR_FB_scene / XR_FB_spatial_entity(_query) ─────────────────────────────
    bool _sceneAvailable = false;
    PFN_xrQuerySpacesFB                       _pfnQuerySpaces             = nullptr;
    PFN_xrRetrieveSpaceQueryResultsFB         _pfnRetrieveSpaceQueryResults = nullptr;
    PFN_xrGetSpaceComponentStatusFB           _pfnGetSpaceComponentStatus = nullptr;
    PFN_xrSetSpaceComponentStatusFB           _pfnSetSpaceComponentStatus = nullptr;
    PFN_xrGetSpaceBoundingBox2DFB             _pfnGetSpaceBoundingBox2D   = nullptr;
    PFN_xrGetSpaceSemanticLabelsFB            _pfnGetSpaceSemanticLabels  = nullptr;
    PFN_xrGetSpaceBoundary2DFB                _pfnGetSpaceBoundary2D      = nullptr;
    bool                _sceneQueryInFlight = false;
    XrAsyncRequestIdFB  _sceneQueryRequestId = 0;
    std::chrono::steady_clock::time_point _lastSceneQuery;
    bool                _sceneDiagLogged = false;  // one-shot per-space diagnostics
    std::set<uint64_t>  _locatableRequested;       // spaces we've asked to enable LOCATABLE on

    // planeId / space → Viro anchor, for tracking new / updated / removed planes.
    std::map<uint64_t, std::shared_ptr<VROARPlaneAnchor>> _planes;     // EXT path (planeId)
    std::map<uint64_t, std::shared_ptr<VROARPlaneAnchor>> _scenePlanes; // FB path (XrSpace handle)

    std::unique_ptr<VROARFrame> _currentFrame;

    // ── EXT helpers ─────────────────────────────────────────────────────────
    void beginDetectionSweep();
    void collectDetectedPlanes();
    void populatePlaneAnchor(const std::shared_ptr<VROARPlaneAnchor> &anchor,
                             const XrPlaneDetectorLocationEXT &loc);
    std::vector<VROVector3f> queryPolygon(uint64_t planeId);

    // ── FB scene helpers ───────────────────────────────────────────────────────
    void beginSceneQuery();
    void processSceneQueryResults(XrAsyncRequestIdFB requestId);
    std::shared_ptr<VROARPlaneAnchor> buildPlaneFromSpace(XrSpace space);
};

#endif  // ANDROID_VROARSESSIONOPENXR_H
