//
//  VROCloudAnchorProviderReactVision.mm
//  ViroKit
//
//  Copyright © 2026 ReactVision. All rights reserved.
//  Proprietary and Confidential
//
//  iOS bridge: converts ViroCore / ARKit types to ReactVisionCCA C++ types,
//  delegates to RVCCACloudAnchorProvider, and returns results back.
//

#import "VROCloudAnchorProviderReactVision.h"

// ReactVisionCCA C++ headers
#include "ReactVisionCCA/RVCCACloudAnchorProvider.h"
#include "ReactVisionCCA/RVCCATypes.h"

#include <memory>
#include <string>

// ============================================================================
// Helpers: ARKit ↔ ReactVisionCCA type conversion
// ============================================================================

// Build a flat float[16] column-major matrix from simd_float4x4.
// simd_float4x4 columns are .columns[0..3], each simd_float4.
static void simdToFloatArray(const simd_float4x4 &m, float out[16]) {
    out[0]  = m.columns[0].x; out[1]  = m.columns[0].y;
    out[2]  = m.columns[0].z; out[3]  = m.columns[0].w;
    out[4]  = m.columns[1].x; out[5]  = m.columns[1].y;
    out[6]  = m.columns[1].z; out[7]  = m.columns[1].w;
    out[8]  = m.columns[2].x; out[9]  = m.columns[2].y;
    out[10] = m.columns[2].z; out[11] = m.columns[2].w;
    out[12] = m.columns[3].x; out[13] = m.columns[3].y;
    out[14] = m.columns[3].z; out[15] = m.columns[3].w;
}

// Build a simd_float4x4 from a flat float[16] column-major array.
static simd_float4x4 floatArrayToSimd(const float m[16]) {
    simd_float4x4 r;
    r.columns[0] = simd_make_float4(m[0],  m[1],  m[2],  m[3]);
    r.columns[1] = simd_make_float4(m[4],  m[5],  m[6],  m[7]);
    r.columns[2] = simd_make_float4(m[8],  m[9],  m[10], m[11]);
    r.columns[3] = simd_make_float4(m[12], m[13], m[14], m[15]);
    return r;
}

// ============================================================================
// Thin VROARFrame / VROARCamera shims for RVCCACloudAnchorProvider
//
// Rather than rebuilding all of ViroCore's AR session stack, we implement
// lightweight shims that satisfy RVCCACloudAnchorProvider's expectations:
//   - VROARCamera:  position + rotation + projection + imageSize
//   - VROARFrame:   camera + pointCloud + timestamp
//
// These are compiled only into this translation unit via anonymous namespace.
// ============================================================================

#include "VROARFrame.h"
#include "VROARCamera.h"
#include "VROARPointCloud.h"
#include "VROMatrix4f.h"
#include "VROVector3f.h"
#include "VROVector4f.h"
#include "VROViewport.h"

namespace {

// --------------------------------------------------------------------------
// ARKitCamera: wraps ARCamera and satisfies VROARCamera
// --------------------------------------------------------------------------
class ARKitCamera : public VROARCamera {
public:
    ARKitCamera(ARCamera *cam, CGSize imageSize)
        : _cam(cam), _imageSize(imageSize) {}

    VROARTrackingState getTrackingState() const override {
        switch (_cam.trackingState) {
            case ARTrackingStateNormal:   return VROARTrackingState::Normal;
            case ARTrackingStateLimited:  return VROARTrackingState::Limited;
            case ARTrackingStateNotAvailable:
            default:                     return VROARTrackingState::Unavailable;
        }
    }

    VROARTrackingStateReason getLimitedTrackingStateReason() const override {
        return VROARTrackingStateReason::None;
    }

    // Rotation-only (upper-left 3x3) portion of camera transform
    VROMatrix4f getRotation() const override {
        float m[16];
        simdToFloatArray(_cam.transform, m);
        VROMatrix4f mat(m);
        // Zero translation so the caller can set it via getPosition()
        mat[12] = 0; mat[13] = 0; mat[14] = 0;
        return mat;
    }

    VROVector3f getPosition() const override {
        return VROVector3f(
            _cam.transform.columns[3].x,
            _cam.transform.columns[3].y,
            _cam.transform.columns[3].z);
    }

    // Build GL projection matrix from ARCamera intrinsics.
    // ARCamera.intrinsics is a 3x3 matrix:
    //   intrinsics[0][0]=fx, intrinsics[1][1]=fy, intrinsics[2][0]=cx, intrinsics[2][1]=cy
    // (column-major simd convention)
    VROMatrix4f getProjection(VROViewport viewport, float near, float far,
                              VROFieldOfView *outFOV) override {
        float fx = _cam.intrinsics.columns[0].x;
        float fy = _cam.intrinsics.columns[1].y;
        float cx = _cam.intrinsics.columns[2].x;
        float cy = _cam.intrinsics.columns[2].y;
        float W  = static_cast<float>(_imageSize.width);
        float H  = static_cast<float>(_imageSize.height);

        // Standard GL projection from camera intrinsics
        float m[16] = {};
        m[0]  = 2.0f * fx / W;
        m[5]  = 2.0f * fy / H;
        m[8]  = 1.0f - 2.0f * cx / W;
        m[9]  = 2.0f * cy / H - 1.0f;
        m[10] = -(far + near) / (far - near);
        m[11] = -1.0f;
        m[14] = -2.0f * far * near / (far - near);
        return VROMatrix4f(m);
    }

    VROVector3f getImageSize() override {
        return VROVector3f(static_cast<float>(_imageSize.width),
                           static_cast<float>(_imageSize.height),
                           0.0f);
    }

private:
    ARCamera *_cam;  // __weak-like; frame keeps it alive
    CGSize    _imageSize;
};

// --------------------------------------------------------------------------
// ARKitPointCloud: wraps ARPointCloud and satisfies VROARPointCloud
// --------------------------------------------------------------------------
class ARKitPointCloud : public VROARPointCloud {
public:
    explicit ARKitPointCloud(ARPointCloud *pc) {
        if (!pc) return;
        _pts.reserve(pc.count);
        for (NSUInteger i = 0; i < pc.count; ++i) {
            simd_float3 p = pc.points[i];
            _pts.push_back(VROVector4f(p.x, p.y, p.z, 1.0f));
        }
    }

    const std::vector<VROVector4f> &getPoints() const override { return _pts; }
    const std::vector<uint64_t>   &getIds()    const override { return _ids;  }

private:
    std::vector<VROVector4f> _pts;
    std::vector<uint64_t>    _ids;
};

// --------------------------------------------------------------------------
// ARKitFrame: wraps ARFrame and satisfies VROARFrame
// --------------------------------------------------------------------------
class ARKitFrame : public VROARFrame {
public:
    explicit ARKitFrame(ARFrame *frame) {
        _timestamp = frame.timestamp;
        _camera = std::make_shared<ARKitCamera>(
            frame.camera,
            frame.camera.imageResolution);
        _pointCloud = std::make_shared<ARKitPointCloud>(frame.rawFeaturePoints);
    }

    double getTimestamp() const override { return _timestamp; }

    const std::shared_ptr<VROARCamera> &getCamera() const override {
        return _camera;
    }

    std::shared_ptr<VROARPointCloud> getPointCloud() override {
        return _pointCloud;
    }

    // Stubs for methods not needed by RVCCACloudAnchorProvider
    VROCameraOrientation getOrientation() const override {
        return VROCameraOrientation::Portrait; // placeholder
    }
    std::vector<std::shared_ptr<VROARHitTestResult>>
    hitTest(int, int, std::set<VROARHitTestResultType>) override { return {}; }
    std::vector<std::shared_ptr<VROARHitTestResult>>
    hitTestRay(VROVector3f *, VROVector3f *,
               std::set<VROARHitTestResultType>) override { return {}; }
    VROMatrix4f getViewportToCameraImageTransform() const override {
        return VROMatrix4f::identity();
    }
    float       getAmbientLightIntensity() const override { return 1.0f; }
    VROVector3f getAmbientLightColor()     const override { return {1,1,1}; }
    const std::vector<std::shared_ptr<VROARAnchor>> &getAnchors() const override {
        static std::vector<std::shared_ptr<VROARAnchor>> empty;
        return empty;
    }

private:
    double                           _timestamp;
    std::shared_ptr<VROARCamera>     _camera;
    std::shared_ptr<VROARPointCloud> _pointCloud;
};

} // anonymous namespace

// ============================================================================
// VROCloudAnchorProviderReactVision implementation
// ============================================================================

@interface VROCloudAnchorProviderReactVision () {
    std::shared_ptr<ReactVisionCCA::RVCCACloudAnchorProvider> _provider;
}
@end

@implementation VROCloudAnchorProviderReactVision

+ (BOOL)isAvailable {
    return YES;
}

- (nullable instancetype)initWithApiKey:(NSString *)apiKey
                              projectId:(NSString *)projectId
                               endpoint:(nullable NSString *)endpoint {
    self = [super init];
    if (!self) return nil;

    ReactVisionCCA::RVCCACloudAnchorProvider::Config cfg;
    cfg.apiKey    = apiKey.UTF8String;
    cfg.projectId = projectId.UTF8String;
    if (endpoint) cfg.endpoint = endpoint.UTF8String;
    cfg.enableLogging = NO;

    try {
        _provider = std::make_shared<ReactVisionCCA::RVCCACloudAnchorProvider>(cfg);
    } catch (const std::exception &e) {
        NSLog(@"[ReactVisionCCA] Init failed: %s", e.what());
        return nil;
    }
    return self;
}

- (void)hostAnchor:(ARAnchor *)anchor
             frame:(ARFrame *)frame
           ttlDays:(NSInteger)ttlDays
         onSuccess:(void (^)(NSString *))onSuccess
         onFailure:(void (^)(NSString *))onFailure {

    // Wrap ARAnchor in a VROARAnchor
    auto vroAnchor = std::make_shared<VROARAnchor>();
    float m[16];
    simdToFloatArray(anchor.transform, m);
    vroAnchor->setTransform(VROMatrix4f(m));
    vroAnchor->setId(anchor.identifier.UUIDString.UTF8String);

    // Wrap ARFrame
    auto vroFrame = std::make_shared<ARKitFrame>(frame);

    _provider->hostCloudAnchor(
        vroAnchor, vroFrame, static_cast<int>(ttlDays),
        [onSuccess](const std::string &cloudId) {
            onSuccess([NSString stringWithUTF8String:cloudId.c_str()]);
        },
        [onFailure](const std::string &error,
                    ReactVisionCCA::RVCCACloudAnchorProvider::ErrorCode) {
            onFailure([NSString stringWithUTF8String:error.c_str()]);
        });
}

- (void)resolveCloudAnchorWithId:(NSString *)cloudAnchorId
                           frame:(ARFrame *)frame
                       onSuccess:(void (^)(NSString *, simd_float4x4))onSuccess
                       onFailure:(void (^)(NSString *))onFailure {

    auto vroFrame = std::make_shared<ARKitFrame>(frame);

    _provider->resolveCloudAnchor(
        cloudAnchorId.UTF8String, vroFrame,
        [onSuccess, cloudAnchorId](std::shared_ptr<VROARAnchor> resolved) {
            // Convert VROMatrix4f back to simd_float4x4
            VROMatrix4f t = resolved->getTransform();
            simd_float4x4 sim;
            sim.columns[0] = simd_make_float4(t[0],  t[1],  t[2],  t[3]);
            sim.columns[1] = simd_make_float4(t[4],  t[5],  t[6],  t[7]);
            sim.columns[2] = simd_make_float4(t[8],  t[9],  t[10], t[11]);
            sim.columns[3] = simd_make_float4(t[12], t[13], t[14], t[15]);
            onSuccess(cloudAnchorId, sim);
        },
        [onFailure](const std::string &error,
                    ReactVisionCCA::RVCCACloudAnchorProvider::ErrorCode) {
            onFailure([NSString stringWithUTF8String:error.c_str()]);
        });
}

- (void)cancelAllOperations {
    if (_provider) _provider->cancelAllOperations();
}

@end
