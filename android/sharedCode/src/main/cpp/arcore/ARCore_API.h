//
//  ARCore_API.h
//  Viro
//
//  Created by Raj Advani on 2/20/18.
//  Copyright © 2018 Viro Media. All rights reserved.
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

#ifndef ARCORE_API_h
#define ARCORE_API_h

#include <stdint.h>
#include <arcore_c_api.h>
#include <functional>
#include <string>
#include <memory>

typedef struct AImage AImage;

namespace arcore {

    class Anchor;
    class Trackable;
    class HitResultList;
    class PointCloud;
    class HitResult;
    class AugmentedImageDatabase;

    enum class AnchorAcquireStatus {
        Success,
        ErrorNotTracking,
        ErrorSessionPaused,
        ErrorResourceExhausted,
        ErrorDeadlineExceeded,
        ErrorCloudAnchorsNotConfigured,
        ErrorAnchorNotSupportedForHosting,
        ErrorUnknown
    };

    enum class ConfigStatus {
        Success,
        UnsupportedConfiguration,
        SessionNotPaused,
    };

    enum class ImageRetrievalStatus {
        Success,
        InvalidArgument,
        DeadlineExceeded,
        ResourceExhausted,
        NotYetAvailable,
        UnknownError
    };

    enum class AugmentedImageDatabaseStatus {
        Success,
        ImageInsufficientQuality
    };

    enum class CloudAnchorMode {
        Disabled,
        Enabled
    };

    enum class CloudAnchorState {
        None,
        TaskInProgress,
        Success,
        ErrorInternal,
        ErrorNotAuthorized,
        ErrorServiceUnavailable,
        ErrorResourceExhausted,
        ErrorDatasetProcessingFailed,
        ErrorCloudIDNotFound,
        ErrorResolvingLocalizationNoMatch,
        ErrorResolvingSDKVersionTooOld,
        ErrorResolvingSDKVersionTooNew
    };

    enum class TrackingState {
        NotTracking,
        Tracking,
        Paused,
        Stopped
    };

    enum class TrackingFailureReason {
        None,
        BadState = 1,
        InsufficientLight = 2,
        ExcessiveMotion = 3,
        InsufficientFeatures = 4
    };

    enum class TrackingMethod {
        NotTracking,
        Tracking,
        LastKnownPose
    };

    enum class TrackableType {
        Image,
        Plane,
        Point
    };

    enum class PlaneType {
        HorizontalUpward,
        HorizontalDownward,
        Vertical,
    };

    enum class LightingMode {
        Disabled,
        AmbientIntensity,
        EnvironmentalHDR
    };
    enum class PlaneFindingMode {
        Disabled,
        Horizontal,
        HorizontalAndVertical,
        Vertical
    };
    enum class UpdateMode {
        Blocking,
        LatestCameraImage
    };

    enum class FocusMode {
        FIXED_FOCUS,
        AUTO_FOCUS
    };

    enum class DepthMode {
        Disabled,
        Automatic,
        RawDepthOnly
    };

    enum class SemanticMode {
        Disabled,
        Enabled
    };

    /*
     * Semantic labels for scene understanding.
     * These labels classify pixels in outdoor scenes into semantic categories.
     * Values match ARCore's ArSemanticLabel enum.
     */
    enum class SemanticLabel {
        Unlabeled = 0,   // Pixel could not be classified
        Sky = 1,         // Sky regions
        Building = 2,    // Building structures
        Tree = 3,        // Trees and large vegetation
        Road = 4,        // Road surfaces
        Sidewalk = 5,    // Pedestrian sidewalks
        Terrain = 6,     // Natural terrain/ground
        Structure = 7,   // General man-made structures
        Object = 8,      // Generic objects
        Vehicle = 9,     // Vehicles (cars, trucks, etc.)
        Person = 10,     // Human figures
        Water = 11       // Water bodies
    };

    // Total number of semantic labels
    static const int SEMANTIC_LABEL_COUNT = 12;

    enum class GeospatialMode {
        Disabled,
        Enabled
    };

    enum class VPSAvailability {
        Unknown,
        Available,
        Unavailable,
        ErrorInternal,
        ErrorNetwork,
        ErrorResourceExhausted
    };

    struct GeospatialPoseData {
        double latitude;
        double longitude;
        double altitude;
        double heading;
        double horizontalAccuracy;
        double verticalAccuracy;
        double orientationYawAccuracy;
        float quaternion[4];
    };

    class Config {
    public:
        virtual ~Config() {}
        virtual void setAugmentedImageDatabase(AugmentedImageDatabase *database) = 0;
    };

    class AugmentedImageDatabase {
    public:
        virtual ~AugmentedImageDatabase() {}
        // The guidance from ARCore is that this function be called on the background thread!
        virtual AugmentedImageDatabaseStatus addImageWithPhysicalSize(const char *image_name, const uint8_t *image_grayscale_pixels,
                                                                      int32_t image_width_in_pixels, int32_t image_height_in_pixels,
                                                                      int32_t image_stride_in_pixels, float image_width_in_meters,
                                                                      int32_t *out_index) = 0;

    };

    class Pose {
    public:
        virtual ~Pose() {}
        virtual void toMatrix(float *outMatrix) = 0;
    };

    class AnchorList {
    public:
        virtual ~AnchorList() {}
        virtual Anchor *acquireItem(int index) = 0;
        virtual int size() = 0;
    };

    class Anchor {
    public:
        virtual ~Anchor() {}
        virtual uint64_t getHashCode() = 0;
        virtual uint64_t getId() = 0;
        virtual void getPose(Pose *outPose) = 0;
        virtual void getTransform(float *outTransform) = 0;
        virtual TrackingState getTrackingState() = 0;
        virtual void acquireCloudAnchorId(char **outCloudAnchorId) = 0;
        virtual CloudAnchorState getCloudAnchorState() = 0;
        virtual void detach() = 0;
    };

    class TrackableList {
    public:
        virtual ~TrackableList() {}
        virtual Trackable *acquireItem(int index) = 0;
        virtual int size() = 0;
    };

    class Trackable {
    public:
        virtual ~Trackable() {}
        virtual Anchor *acquireAnchor(Pose *pose) = 0;
        virtual TrackingState getTrackingState() = 0;
        virtual TrackableType getType() = 0;
    };

    class Plane : public Trackable {
    public:
        virtual ~Plane() {}
        virtual uint64_t getHashCode() = 0;
        virtual void getCenterPose(Pose *outPose) = 0;
        virtual float getExtentX() = 0;
        virtual float getExtentZ() = 0;
        virtual Plane *acquireSubsumedBy() = 0;
        virtual PlaneType getPlaneType() = 0;
        virtual bool isPoseInExtents(const Pose *pose) = 0;
        virtual bool isPoseInPolygon(const Pose *pose) = 0;
        virtual float *getPolygon() = 0;
        virtual int getPolygonSize() = 0;
    };

    class AugmentedImage : public Trackable {
    public:
        virtual ~AugmentedImage() {}
        virtual char *getName() = 0;
        virtual TrackingMethod getTrackingMethod() = 0;
        virtual void getCenterPose(Pose *outPose) = 0;
        virtual float getExtentX() = 0;
        virtual float getExtentZ() = 0;
        virtual int32_t getIndex() = 0;
    };

    class LightEstimate {
    public:
        virtual ~LightEstimate() {}
        virtual float getPixelIntensity() = 0;
        virtual void getColorCorrection(float *outColorCorrection) = 0;
        virtual bool isValid() = 0;
    };

    class Image {
    public:
        virtual ~Image() {}
        virtual int32_t getWidth() = 0;
        virtual int32_t getHeight() = 0;
        virtual int32_t getFormat() = 0;
        virtual void getCropRect(int *outLeft, int *outRight, int *outBottom, int *outTop) = 0;
        virtual int32_t getNumberOfPlanes() = 0;
        virtual int32_t getPlanePixelStride(int planeIdx) = 0;
        virtual int32_t getPlaneRowStride(int planeIdx) = 0;
        virtual void getPlaneData(int planeIdx, const uint8_t **outData, int *outDataLength) = 0;
    };

    class Frame {
    public:
        virtual ~Frame() {}
        virtual void getViewMatrix(float *outMatrix) = 0;
        virtual void getProjectionMatrix(float near, float far, float *outMatrix) = 0;
        virtual void getImageIntrinsics(float *outFx, float *outFy, float *outCx, float *outCy) = 0;
        virtual TrackingState getTrackingState() = 0;
        virtual TrackingFailureReason getTrackingFailureReason() = 0;
        virtual void getLightEstimate(LightEstimate *outLightEstimate) = 0;
        virtual bool hasDisplayGeometryChanged() = 0;
        virtual void hitTest(float x, float y, HitResultList *outList) = 0;
        virtual void hitTest(float px, float py, float pz, float qx, float qy, float qz, HitResultList *outList) = 0;
        virtual int64_t getTimestampNs() = 0;
        virtual void getUpdatedAnchors(AnchorList *outList) = 0;
        virtual void getUpdatedTrackables(TrackableList *outList, TrackableType type) = 0;
        virtual void getBackgroundTexcoords(float *outTexcoords) = 0;
        virtual PointCloud *acquirePointCloud() = 0;
        virtual ImageRetrievalStatus acquireCameraImage(Image **outImage) = 0;

        /*
         * Acquire the depth image for this frame.
         * Returns Success if depth image was acquired, otherwise returns an error status.
         * The depth image contains 16-bit unsigned integers representing depth in millimeters.
         * Caller must delete the returned Image when done.
         */
        virtual ImageRetrievalStatus acquireDepthImage(Image **outImage) = 0;

        /*
         * Acquire the depth confidence image for this frame.
         * Returns Success if confidence image was acquired, otherwise returns an error status.
         * The confidence image contains 8-bit unsigned integers (0-255).
         * Caller must delete the returned Image when done.
         */
        virtual ImageRetrievalStatus acquireDepthConfidenceImage(Image **outImage) = 0;

        /*
         * Acquire the semantic image for this frame.
         * Returns Success if semantic image was acquired, otherwise returns an error status.
         * The semantic image contains 8-bit unsigned integers representing semantic labels (0-11).
         * Each pixel value corresponds to a SemanticLabel enum value.
         * Caller must delete the returned Image when done.
         */
        virtual ImageRetrievalStatus acquireSemanticImage(Image **outImage) = 0;

        /*
         * Acquire the semantic confidence image for this frame.
         * Returns Success if confidence image was acquired, otherwise returns an error status.
         * The confidence image contains 8-bit unsigned integers (0-255) representing
         * the confidence of the semantic label for each pixel.
         * Caller must delete the returned Image when done.
         */
        virtual ImageRetrievalStatus acquireSemanticConfidenceImage(Image **outImage) = 0;

        /*
         * Get the fraction of pixels with the specified semantic label.
         * Returns a value in the range [0.0, 1.0] representing the percentage
         * of pixels classified with the given label.
         * Returns 0.0 if semantic data is not available.
         */
        virtual float getSemanticLabelFraction(SemanticLabel label) = 0;
    };

    class PointCloud {
    public:
        virtual ~PointCloud() {}
        virtual const float *getPoints() = 0;
        virtual int getNumPoints() = 0;
        virtual const int *getPointIds() = 0;
    };

    class HitResultList {
    public:
        virtual ~HitResultList() {}
        virtual void getItem(int index, HitResult *outResult) = 0;
        virtual int size() = 0;
    };

    class HitResult {
    public:
        virtual ~HitResult() {}
        virtual float getDistance() = 0;
        virtual void getPose(Pose *outPose) = 0;
        virtual void getTransform(float *outTransform) = 0;
        virtual Trackable *acquireTrackable() = 0;
        virtual Anchor *acquireAnchor() = 0;
    };

    class Session {
    public:
        virtual ~Session() {}
        virtual ConfigStatus configure(Config *config) = 0;
        virtual void setDisplayGeometry(int rotation, int width, int height) = 0;
        virtual void setCameraTextureName(int32_t textureId) = 0;
        virtual void pause() = 0;
        virtual void resume() = 0;
        virtual void update(Frame *frame) = 0;

        virtual Config *createConfig(LightingMode lightingMode, PlaneFindingMode planeFindingMode,
                                     UpdateMode updateMode, CloudAnchorMode cloudAnchorMode,
                                     FocusMode focusMode, DepthMode depthMode, SemanticMode semanticMode,
                                     GeospatialMode geospatialMode) = 0;

        virtual bool isDepthModeSupported(DepthMode depthMode) = 0;
        virtual bool isSemanticModeSupported(SemanticMode semanticMode) = 0;
        virtual bool isGeospatialModeSupported(GeospatialMode mode) = 0;
        virtual TrackingState getEarthTrackingState() = 0;
        virtual bool getCameraGeospatialPose(GeospatialPoseData *outPose) = 0;
        virtual Anchor *createGeospatialAnchor(double latitude, double longitude, double altitude, float qx, float qy, float qz, float qw) = 0;
        virtual void createTerrainAnchor(double latitude, double longitude, double altitude, float qx, float qy, float qz, float qw,
                                         std::function<void(Anchor *anchor)> onSuccess,
                                         std::function<void(std::string error)> onFailure) = 0;
        virtual void createRooftopAnchor(double latitude, double longitude, double altitude, float qx, float qy, float qz, float qw,
                                         std::function<void(Anchor *anchor)> onSuccess,
                                         std::function<void(std::string error)> onFailure) = 0;
        virtual void checkVpsAvailability(double latitude, double longitude,
                                          std::function<void(VPSAvailability)> callback) = 0;

        virtual AugmentedImageDatabase *createAugmentedImageDatabase() = 0;
        virtual AugmentedImageDatabase *createAugmentedImageDatabase(uint8_t* raw_buffer, int64_t size) = 0;
        virtual Pose *createPose() = 0;
        virtual Pose *createPose(float px, float py, float pz, float qx, float qy, float qz, float qw) = 0;
        virtual AnchorList *createAnchorList() = 0;
        virtual TrackableList *createTrackableList() = 0;
        virtual HitResultList *createHitResultList() = 0;
        virtual LightEstimate *createLightEstimate() = 0;
        virtual Frame *createFrame() = 0;
        virtual HitResult *createHitResult() = 0;
        virtual Anchor *acquireNewAnchor(const Pose *pose) = 0;
        virtual Anchor *hostAndAcquireNewCloudAnchor(const Anchor *anchor, AnchorAcquireStatus *status) = 0;
        virtual Anchor *hostAndAcquireNewCloudAnchorWithTtl(const Anchor *anchor, int ttlDays, AnchorAcquireStatus *status) = 0;
        virtual Anchor *resolveAndAcquireNewCloudAnchor(const char *anchorId, AnchorAcquireStatus *status) = 0;
        virtual ArSession *getRawSession() = 0;
    };
}

#endif /* ARCORE_API_h */
