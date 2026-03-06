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
//  ReactVisionCCA is an optional proprietary library.  When its headers are
//  not on the HEADER_SEARCH_PATHS (i.e. the .a has not been deployed via
//  scripts/build_ios.sh), this file compiles stub implementations that report
//  the feature as unavailable.  Open-source builds of ViroCore are unaffected.
//

#import "VROCloudAnchorProviderReactVision.h"

// Detect ReactVisionCCA availability at compile time.
// The build_ios.sh script deploys libreactvisioncca.a to
//   virocore/ios/Libraries/reactvisioncca/{arch}/
// and the headers live in reactvisioncca/include/ (sibling repo).
// HEADER_SEARCH_PATHS includes $(SRCROOT)/../../reactvisioncca/include so
// __has_include returns true only when the SDK is actually present.
#if __has_include("ReactVisionCCA/RVCCACloudAnchorProvider.h")
#  define RVCCA_AVAILABLE 1
#  include "ReactVisionCCA/RVCCACloudAnchorProvider.h"
#  include "ReactVisionCCA/RVCCATypes.h"
#else
#  define RVCCA_AVAILABLE 0
#endif

#include <memory>
#include <string>

// ============================================================================
// Full implementation — compiled only when ReactVisionCCA SDK is present
// ============================================================================

#if RVCCA_AVAILABLE

#include "VROARFrame.h"
#include "VROARCamera.h"
#include "VROARAnchor.h"
#include "VROARPointCloud.h"
#include "VROCameraTexture.h"
#include "VROMatrix4f.h"
#include "VROVector3f.h"
#include "VROVector4f.h"
#include "VROViewport.h"

// ── Helpers: ARKit ↔ ReactVisionCCA type conversion ──────────────────────────

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

static simd_float4x4 floatArrayToSimd(const float m[16]) {
    simd_float4x4 r;
    r.columns[0] = simd_make_float4(m[0],  m[1],  m[2],  m[3]);
    r.columns[1] = simd_make_float4(m[4],  m[5],  m[6],  m[7]);
    r.columns[2] = simd_make_float4(m[8],  m[9],  m[10], m[11]);
    r.columns[3] = simd_make_float4(m[12], m[13], m[14], m[15]);
    return r;
}

// Improvement 2: map ErrorCode to Google-compatible state string, encoded as
// "message|StateString" so the caller can split and forward the state to JS.
static std::string encodeError(
    const std::string &msg,
    ReactVisionCCA::RVCCACloudAnchorProvider::ErrorCode code)
{
    using EC = ReactVisionCCA::RVCCACloudAnchorProvider::ErrorCode;
    const char *state;
    switch (code) {
        case EC::NetworkError:         state = "ErrorNetworkFailure";                    break;
        case EC::AuthenticationFailed: state = "ErrorAuthenticationFailed";              break;
        case EC::InsufficientFeatures: state = "ErrorHostingInsufficientVisualFeatures"; break;
        case EC::LocalizationFailed:   state = "ErrorResolvingLocalizationNoMatch";      break;
        case EC::AnchorNotFound:       state = "ErrorCloudIdNotFound";                   break;
        case EC::AnchorExpired:        state = "ErrorAnchorExpired";                     break;
        case EC::Timeout:              state = "ErrorNetworkFailure";                    break;
        default:                       state = "ErrorInternal";                          break;
    }
    return msg + "|" + state;
}

// ── ARKit shims ───────────────────────────────────────────────────────────────

namespace {

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

    VROMatrix4f getRotation() const override {
        float m[16];
        simdToFloatArray(_cam.transform, m);
        VROMatrix4f mat(m);
        mat[12] = 0; mat[13] = 0; mat[14] = 0;
        return mat;
    }

    VROVector3f getPosition() const override {
        return VROVector3f(
            _cam.transform.columns[3].x,
            _cam.transform.columns[3].y,
            _cam.transform.columns[3].z);
    }

    VROMatrix4f getProjection(VROViewport viewport, float near, float far,
                              VROFieldOfView *outFOV) override {
        float fx = _cam.intrinsics.columns[0].x;
        float fy = _cam.intrinsics.columns[1].y;
        float cx = _cam.intrinsics.columns[2].x;
        float cy = _cam.intrinsics.columns[2].y;
        float W  = static_cast<float>(_imageSize.width);
        float H  = static_cast<float>(_imageSize.height);

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
    ARCamera *_cam;
    CGSize    _imageSize;
};

static std::shared_ptr<VROARPointCloud> makeARKitPointCloud(ARPointCloud *pc) {
    std::vector<VROVector4f> pts;
    std::vector<uint64_t>    ids;
    if (pc) {
        pts.reserve(pc.count);
        for (NSUInteger i = 0; i < pc.count; ++i) {
            simd_float3 p = pc.points[i];
            // ARKit doesn't expose per-point confidence; use w=1.0 so the
            // confidence filter in RVCCACloudAnchorProvider always accepts them.
            pts.push_back(VROVector4f(p.x, p.y, p.z, 1.0f));
        }
        for (NSUInteger i = 0; i < pc.count; ++i) {
            ids.push_back(pc.identifiers[i]);
        }
    }
    return std::make_shared<VROARPointCloud>(pts, ids);
}

class ARKitFrame : public VROARFrame {
public:
    explicit ARKitFrame(ARFrame *frame) {
        _timestamp  = frame.timestamp;
        _camera     = std::make_shared<ARKitCamera>(
            frame.camera, frame.camera.imageResolution);
        _pointCloud = makeARKitPointCloud(frame.rawFeaturePoints);
        // Store a strong reference so the pixel buffer stays valid until the
        // first getCameraImageY() call.  We do NOT copy the luma here: at
        // 3840×2160 that would be 8.3 MB per render frame (500 MB/s at 60fps),
        // which stalls the render thread and causes ARKit to retain 10+ frames.
        _arFrame = frame;
    }

    bool getCameraImageY(const uint8_t** data, int* width, int* height) override {
        if (_lumaData.empty()) {
            // Lazy copy: only executed when the SIFT pipeline actually needs
            // the luma (motion gate passed or active resolve).
            if (!_arFrame) return false;
            CVPixelBufferRef buf = _arFrame.capturedImage;
            if (!buf) return false;
            CVPixelBufferLockBaseAddress(buf, kCVPixelBufferLock_ReadOnly);
            const uint8_t* src = (const uint8_t*)CVPixelBufferGetBaseAddressOfPlane(buf, 0);
            size_t w  = CVPixelBufferGetWidthOfPlane(buf, 0);
            size_t h  = CVPixelBufferGetHeightOfPlane(buf, 0);
            size_t st = CVPixelBufferGetBytesPerRowOfPlane(buf, 0);
            if (src && w > 0 && h > 0) {
                _lumaW = (int)w; _lumaH = (int)h;
                _lumaData.resize(w * h);
                for (size_t row = 0; row < h; ++row)
                    memcpy(_lumaData.data() + row * w, src + row * st, w);
            }
            CVPixelBufferUnlockBaseAddress(buf, kCVPixelBufferLock_ReadOnly);
            _arFrame = nil; // release ARFrame; luma is now in _lumaData

            // Subsample to ≤ 1280 px wide immediately so downstream copies
            // (collectBgKeyframe lumaCopy, pr.lastLuma) are small (~740 KB
            // instead of 8.3 MB at 3840×2160).
            while (_lumaW > 1280) {
                int nw = _lumaW / 2, nh = _lumaH / 2;
                std::vector<uint8_t> out((size_t)nw * nh);
                for (int y = 0; y < nh; ++y) {
                    const uint8_t* r0 = _lumaData.data() + (size_t)(y*2)   * _lumaW;
                    const uint8_t* r1 = _lumaData.data() + (size_t)(y*2+1) * _lumaW;
                    uint8_t*       od = out.data()        + (size_t)y       * nw;
                    for (int x = 0; x < nw; ++x)
                        od[x] = (uint8_t)(((unsigned)r0[x*2] + r0[x*2+1] +
                                           r1[x*2] + r1[x*2+1] + 2u) >> 2);
                }
                _lumaData = std::move(out);
                _lumaW = nw; _lumaH = nh;
            }
        }
        if (_lumaData.empty()) return false;
        *data = _lumaData.data(); *width = _lumaW; *height = _lumaH;
        return true;
    }

    double getTimestamp() const override { return _timestamp; }

    const std::shared_ptr<VROARCamera> &getCamera() const override {
        return _camera;
    }

    std::shared_ptr<VROARPointCloud> getPointCloud() override {
        return _pointCloud;
    }

    VROCameraOrientation getOrientation() const override {
        return VROCameraOrientation::Portrait;
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
    ARFrame * __strong               _arFrame = nil; // held until first getCameraImageY()
    std::vector<uint8_t>             _lumaData;      // populated lazily, stored subsampled
    int                              _lumaW = 0, _lumaH = 0;
};

} // anonymous namespace

// ── ObjC class — full implementation ─────────────────────────────────────────

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
    cfg.enableLogging = YES;  // DEBUG: enable to trace SIFT pipeline

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

    auto vroAnchor = std::make_shared<VROARAnchor>();
    float m[16];
    simdToFloatArray(anchor.transform, m);
    vroAnchor->setTransform(VROMatrix4f(m));
    vroAnchor->setId(anchor.identifier.UUIDString.UTF8String);

    // Pass the initial ARFrame for camera intrinsic capture; point-cloud
    // accumulation continues via updateWithFrame: each render frame (Imp 6B).
    auto vroFrame = std::make_shared<ARKitFrame>(frame);

    _provider->hostCloudAnchor(
        vroAnchor, vroFrame, static_cast<int>(ttlDays),
        [onSuccess](const std::string &cloudId) {
            onSuccess([NSString stringWithUTF8String:cloudId.c_str()]);
        },
        // Improvement 2: encode ErrorCode as "|StateString" suffix
        [onFailure](const std::string &error,
                    ReactVisionCCA::RVCCACloudAnchorProvider::ErrorCode code) {
            std::string encoded = encodeError(error, code);
            onFailure([NSString stringWithUTF8String:encoded.c_str()]);
        });
}

- (void)resolveCloudAnchorWithId:(NSString *)cloudAnchorId
                           frame:(ARFrame *)frame
                       onSuccess:(void (^)(NSString *, simd_float4x4))onSuccess
                       onFailure:(void (^)(NSString *))onFailure {

    // Improvement 1: frame is no longer used for single-shot localization.
    // updateWithFrame: drives localization across multiple render frames.
    // Pass nullptr; RVCCACloudAnchorProvider::resolveCloudAnchor ignores it.
    _provider->resolveCloudAnchor(
        cloudAnchorId.UTF8String, nullptr,
        [onSuccess, cloudAnchorId](std::shared_ptr<VROARAnchor> resolved) {
            VROMatrix4f t = resolved->getTransform();
            simd_float4x4 sim;
            sim.columns[0] = simd_make_float4(t[0],  t[1],  t[2],  t[3]);
            sim.columns[1] = simd_make_float4(t[4],  t[5],  t[6],  t[7]);
            sim.columns[2] = simd_make_float4(t[8],  t[9],  t[10], t[11]);
            sim.columns[3] = simd_make_float4(t[12], t[13], t[14], t[15]);
            onSuccess(cloudAnchorId, sim);
        },
        // Improvement 2: encode ErrorCode as "|StateString" suffix
        [onFailure](const std::string &error,
                    ReactVisionCCA::RVCCACloudAnchorProvider::ErrorCode code) {
            std::string encoded = encodeError(error, code);
            onFailure([NSString stringWithUTF8String:encoded.c_str()]);
        });
}

// Improvement 1 + 6B: called every render frame by VROARSessioniOS::updateFrame().
// Feeds a fresh ARKitFrame into the C++ provider's updateWithFrame(), which
// drives both host point-cloud accumulation and resolve localization.
- (void)updateWithFrame:(ARFrame *)frame {
    if (!_provider || !frame) return;

    auto vroFrame = std::make_shared<ARKitFrame>(frame);
    _provider->updateWithFrame(vroFrame);
}

// Improvement 3: store GPS coordinates that will be embedded in host requests.
- (void)setLastKnownLocationLat:(double)lat longitude:(double)lng altitude:(double)alt {
    if (_provider) {
        _provider->setLastKnownLocation(lat, lng, alt);
    }
}

- (void)cancelAllOperations {
    if (_provider) _provider->cancelAllOperations();
}

@end

// ============================================================================
// Stub implementation — compiled when ReactVisionCCA SDK is absent
// ============================================================================

#else // !RVCCA_AVAILABLE

@implementation VROCloudAnchorProviderReactVision

+ (BOOL)isAvailable {
    return NO;
}

- (nullable instancetype)initWithApiKey:(NSString *)apiKey
                              projectId:(NSString *)projectId
                               endpoint:(nullable NSString *)endpoint {
    NSLog(@"[ViroKit] ReactVision Cloud Anchors not available: "
          @"ReactVisionCCA library not linked. "
          @"Run scripts/build_ios.sh and rebuild ViroKit.framework to enable.");
    return nil;
}

- (void)hostAnchor:(ARAnchor *)anchor
             frame:(ARFrame *)frame
           ttlDays:(NSInteger)ttlDays
         onSuccess:(void (^)(NSString *))onSuccess
         onFailure:(void (^)(NSString *))onFailure {
    onFailure(@"ReactVision Cloud Anchors not available: ReactVisionCCA library not linked");
}

- (void)resolveCloudAnchorWithId:(NSString *)cloudAnchorId
                           frame:(ARFrame *)frame
                       onSuccess:(void (^)(NSString *, simd_float4x4))onSuccess
                       onFailure:(void (^)(NSString *))onFailure {
    onFailure(@"ReactVision Cloud Anchors not available: ReactVisionCCA library not linked");
}

- (void)updateWithFrame:(ARFrame *)frame {}

- (void)setLastKnownLocationLat:(double)lat longitude:(double)lng altitude:(double)alt {}

- (void)cancelAllOperations {}

@end

#endif // RVCCA_AVAILABLE
