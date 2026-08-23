//
//  VROARWeb.h
//  ViroRenderer — Web (WASM) AR backend
//
//  A minimal VROARSession backend for the web. Pose, camera background, and
//  anchors are injected from JS (which runs slam-wasm) via the web C API; this
//  just surfaces them to the renderer as VROARFrame/VROARCamera. Modeled on
//  VROARSessionInertial (the lightweight native backend). Most VROARSession
//  methods are no-ops/unsupported on web.
//

#ifndef VROARWeb_h
#define VROARWeb_h

#include <memory>
#include <vector>
#include <set>
#include "VROARSession.h"
#include "VROARFrame.h"
#include "VROARCamera.h"
#include "VROMatrix4f.h"
#include "VROVector3f.h"
#include "VROViewport.h"
#include "VROCameraTexture.h" // VROCameraOrientation

class VROARPointCloud;
class VROTexture;
class VROARAnchor;

#pragma mark - Camera

class VROARCameraWeb : public VROARCamera {
public:
    VROARCameraWeb()
        : _trackingState(VROARTrackingState::Normal), _width(0), _height(0) {}
    virtual ~VROARCameraWeb() {}

    void setPose(VROMatrix4f rotation, VROVector3f position) {
        _rotation = rotation;
        _position = position;
    }
    void setTrackingState(VROARTrackingState state) { _trackingState = state; }
    void setImageSize(float w, float h) { _width = w; _height = h; }

    VROARTrackingState getTrackingState() const override { return _trackingState; }
    VROARTrackingStateReason getLimitedTrackingStateReason() const override {
        return VROARTrackingStateReason::None;
    }
    VROMatrix4f getRotation() const override { return _rotation; }
    VROVector3f getPosition() const override { return _position; }
    VROMatrix4f getProjection(VROViewport viewport, float near, float far,
                              VROFieldOfView *outFOV) override;
    VROVector3f getImageSize() override { return { _width, _height, 0 }; }

    /**
     Real camera intrinsics, when the host has them.

     Without these the projection falls back to a fixed 60-degree vertical
     field of view, which is very unlikely to be the field of view of the
     camera that produced the image behind the scene. The background is drawn
     as a screen-space surface and fills the viewport regardless, so it looks
     right while the 3-D content is projected through a different camera --
     content near the optical axis lands close to correct and content toward
     the edges is off by hundreds of pixels.
     */
    void setIntrinsics(float fx, float fy, float cx, float cy) {
        _fx = fx; _fy = fy; _cx = cx; _cy = cy;
    }
    bool hasIntrinsics() const { return _fx > 0 && _fy > 0; }

private:
    VROMatrix4f _rotation;
    VROVector3f _position;
    VROARTrackingState _trackingState;
    float _width, _height;
    float _fx = 0, _fy = 0, _cx = 0, _cy = 0;
};

#pragma mark - Frame

class VROARFrameWeb : public VROARFrame {
public:
    VROARFrameWeb(std::shared_ptr<VROARCamera> camera, double timestamp,
                  VROCameraOrientation orientation,
                  std::vector<std::shared_ptr<VROARAnchor>> anchors,
                  std::shared_ptr<VROARPointCloud> pointCloud)
        : _camera(camera), _timestamp(timestamp), _orientation(orientation),
          _anchors(std::move(anchors)), _pointCloud(pointCloud) {}
    virtual ~VROARFrameWeb() {}

    double getTimestamp() const override { return _timestamp; }
    const std::shared_ptr<VROARCamera> &getCamera() const override { return _camera; }
    VROCameraOrientation getOrientation() const override { return _orientation; }
    std::vector<std::shared_ptr<VROARHitTestResult>> hitTest(
        int x, int y, std::set<VROARHitTestResultType> types) override;
    std::vector<std::shared_ptr<VROARHitTestResult>> hitTestRay(
        VROVector3f *origin, VROVector3f *destination,
        std::set<VROARHitTestResultType> types) override;
    VROMatrix4f getViewportToCameraImageTransform() const override {
        return VROMatrix4f::identity();
    }
    float getAmbientLightIntensity() const override { return 1000.0f; }
    VROVector3f getAmbientLightColor() const override { return { 1, 1, 1 }; }
    const std::vector<std::shared_ptr<VROARAnchor>> &getAnchors() const override {
        return _anchors;
    }
    std::shared_ptr<VROARPointCloud> getPointCloud() override { return _pointCloud; }

private:
    std::shared_ptr<VROARCamera> _camera;
    double _timestamp;
    VROCameraOrientation _orientation;
    std::vector<std::shared_ptr<VROARAnchor>> _anchors;
    std::shared_ptr<VROARPointCloud> _pointCloud;
};

#pragma mark - Session

class VROARSessionWeb : public VROARSession,
                        public std::enable_shared_from_this<VROARSessionWeb> {
public:
    VROARSessionWeb();
    virtual ~VROARSessionWeb();

    // --- Injected from JS (slam-wasm) via the web C API ---
    // Pose is already in virocore convention (Y-up); rotation from a quaternion.
    void setPose(VROMatrix4f rotation, VROVector3f position, VROARTrackingState state);
    void setCameraBackground(std::shared_ptr<VROTexture> texture);
    void setCameraImageSize(float w, float h);
    /** Real intrinsics for the projection; see VROARCameraWeb::setIntrinsics. */
    void setCameraIntrinsics(float fx, float fy, float cx, float cy,
                             float w, float h);

    // --- VROARSession ---
    void run() override { _running = true; }
    void pause() override { _running = false; }
    void resetSession(bool resetTracking, bool removeAnchors) override;
    bool isReady() const override { return true; }
    void setTrackingType(VROTrackingType trackingType) override {}
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
    void addAnchor(std::shared_ptr<VROARAnchor> anchor) override;
    void removeAnchor(std::shared_ptr<VROARAnchor> anchor) override;
    void updateAnchor(std::shared_ptr<VROARAnchor> anchor) override;
    void hostCloudAnchor(std::shared_ptr<VROARAnchor> anchor, int ttlDays,
                         std::function<void(std::shared_ptr<VROARAnchor>)> onSuccess,
                         std::function<void(std::string error)> onFailure) override {
        if (onFailure) onFailure("cloud anchors not supported on web");
    }
    void resolveCloudAnchor(std::string cloudAnchorId,
                            std::function<void(std::shared_ptr<VROARAnchor> anchor)> onSuccess,
                            std::function<void(std::string error)> onFailure) override {
        if (onFailure) onFailure("cloud anchors not supported on web");
    }
    std::unique_ptr<VROARFrame> &updateFrame() override;
    std::unique_ptr<VROARFrame> &getLastFrame() override { return _currentFrame; }
    std::shared_ptr<VROTexture> getCameraBackgroundTexture() override { return _background; }
    void setViewport(VROViewport viewport) override { _viewport = viewport; }
    void setOrientation(VROCameraOrientation orientation) override { _orientation = orientation; }
    void setWorldOrigin(VROMatrix4f relativeTransform) override {}
    void setVideoQuality(VROVideoQuality quality) override {}
    void setVisionModel(std::shared_ptr<VROVisionModel> visionModel) override {}

    const std::vector<std::shared_ptr<VROARAnchor>> &getAnchors() const { return _anchors; }

private:
    std::unique_ptr<VROARFrame> _currentFrame;
    std::shared_ptr<VROARCameraWeb> _camera;
    std::shared_ptr<VROTexture> _background;
    std::vector<std::shared_ptr<VROARAnchor>> _anchors;
    VROViewport _viewport;
    VROCameraOrientation _orientation;
    bool _running;
    double _frameCounter;
};

#endif /* VROARWeb_h */
