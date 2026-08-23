// VROARSessionOpenXR.cpp
// ViroRenderer
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#include "VROARSessionOpenXR.h"

#include <android/log.h>
#include <cmath>
#include <string>
#include <vector>
#include <set>

#include "VROARFrame.h"
#include "VROARNode.h"
#include "VROMatrix4f.h"
#include "VROQuaternion.h"
#include "VROVector3f.h"

#undef LOG_TAG
#define LOG_TAG "VROARSessionOpenXR"
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR,   LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN,    LOG_TAG, __VA_ARGS__)
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)

// Re-run a detection sweep at most this often (ms). XR_EXT_plane_detection is
// asynchronous; re-arming every frame wastes power and produces no new data,
// so we throttle the begin/poll cycle.
static const int64_t kSweepThrottleMs = 500;

// Minimum plane area (m²) to report. Filters out tiny noisy patches.
static const float kMinPlaneArea = 0.04f;  // ≈ 20 cm × 20 cm

// Max planes to retrieve per sweep.
static const uint32_t kMaxPlanes = 64;

// ──────────────────────────────────────────────────────────────────────────────
// Conversion helpers
// ──────────────────────────────────────────────────────────────────────────────

// Local copy of the renderer's pose→matrix helper (static there, not exported).
static VROMatrix4f poseToMatrix(const XrPosef &pose) {
    VROQuaternion q(pose.orientation.x, pose.orientation.y,
                    pose.orientation.z, pose.orientation.w);
    VROMatrix4f m = q.getMatrix();
    m[12] = pose.position.x;
    m[13] = pose.position.y;
    m[14] = pose.position.z;
    return m;
}

// OpenXR's plane space has the plane in its local X-Y plane with the normal
// along +Z; Viro/ARKit/ARCore put the plane in the X-Z plane with the normal
// along +Y. Right-multiply the pose by this rotation (+90° about X) so the
// anchor's +Y axis points along the plane normal, matching the Viro convention
// used by ViroARPlane and the rest of the anchor pipeline.
static VROMatrix4f planeAxisCorrection() {
    VROQuaternion q = VROQuaternion::fromAngleAxis((float)M_PI_2, VROVector3f(1, 0, 0));
    return q.getMatrix();
}

static VROARPlaneAlignment mapAlignment(XrPlaneDetectorOrientationEXT o) {
    switch (o) {
        case XR_PLANE_DETECTOR_ORIENTATION_HORIZONTAL_UPWARD_EXT:
            return VROARPlaneAlignment::HorizontalUpward;
        case XR_PLANE_DETECTOR_ORIENTATION_HORIZONTAL_DOWNWARD_EXT:
            return VROARPlaneAlignment::HorizontalDownward;
        case XR_PLANE_DETECTOR_ORIENTATION_VERTICAL_EXT:
            return VROARPlaneAlignment::Vertical;
        case XR_PLANE_DETECTOR_ORIENTATION_ARBITRARY_EXT:
        default:
            return VROARPlaneAlignment::Horizontal;
    }
}

static VROARPlaneClassification mapClassification(XrPlaneDetectorSemanticTypeEXT s) {
    switch (s) {
        case XR_PLANE_DETECTOR_SEMANTIC_TYPE_FLOOR_EXT:    return VROARPlaneClassification::Floor;
        case XR_PLANE_DETECTOR_SEMANTIC_TYPE_CEILING_EXT:  return VROARPlaneClassification::Ceiling;
        case XR_PLANE_DETECTOR_SEMANTIC_TYPE_WALL_EXT:     return VROARPlaneClassification::Wall;
        case XR_PLANE_DETECTOR_SEMANTIC_TYPE_PLATFORM_EXT: return VROARPlaneClassification::Table;
        case XR_PLANE_DETECTOR_SEMANTIC_TYPE_UNDEFINED_EXT:
        default:                                           return VROARPlaneClassification::Unknown;
    }
}

// Map a Meta scene semantic label (XR_FB_scene) to a Viro plane classification.
// Labels are strings like "FLOOR", "CEILING", "WALL_FACE", "TABLE", "COUCH", etc.
static VROARPlaneClassification classifyLabel(const std::string &label) {
    if (label == "FLOOR")                                      return VROARPlaneClassification::Floor;
    if (label == "CEILING")                                    return VROARPlaneClassification::Ceiling;
    if (label == "WALL_FACE" || label == "INVISIBLE_WALL_FACE") return VROARPlaneClassification::Wall;
    if (label == "TABLE" || label == "STORAGE")                return VROARPlaneClassification::Table;
    if (label == "COUCH" || label == "BED")                    return VROARPlaneClassification::Seat;
    if (label == "DOOR_FRAME")                                 return VROARPlaneClassification::Door;
    if (label == "WINDOW_FRAME")                               return VROARPlaneClassification::Window;
    return VROARPlaneClassification::Unknown;
}

// Derive a plane alignment from a Meta semantic label (the room model carries
// semantics rather than an explicit orientation enum).
static VROARPlaneAlignment alignmentForLabel(const std::string &label) {
    if (label == "FLOOR" || label == "TABLE" || label == "STORAGE" ||
        label == "COUCH" || label == "BED")
        return VROARPlaneAlignment::HorizontalUpward;
    if (label == "CEILING")
        return VROARPlaneAlignment::HorizontalDownward;
    if (label == "WALL_FACE" || label == "INVISIBLE_WALL_FACE" ||
        label == "DOOR_FRAME" || label == "WINDOW_FRAME")
        return VROARPlaneAlignment::Vertical;
    return VROARPlaneAlignment::Horizontal;
}

// ──────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ──────────────────────────────────────────────────────────────────────────────

VROARSessionOpenXR::VROARSessionOpenXR()
    : VROARSession(VROTrackingType::DOF6, VROWorldAlignment::Gravity) {
}

VROARSessionOpenXR::~VROARSessionOpenXR() {
    destroyPlaneDetector();
}

bool VROARSessionOpenXR::initPlaneDetection(XrInstance instance, XrSession session,
                                            XrSpace baseSpace) {
    _instance  = instance;
    _session   = session;
    _baseSpace = baseSpace;

    auto loadFn = [&](const char *name, void **fn) -> bool {
        XrResult r = xrGetInstanceProcAddr(_instance, name, (PFN_xrVoidFunction *)fn);
        if (XR_FAILED(r) || *fn == nullptr) {
            ALOGW("xrGetInstanceProcAddr('%s') failed: %d", name, (int)r);
            return false;
        }
        return true;
    };

    bool ok = true;
    ok &= loadFn("xrCreatePlaneDetectorEXT",    (void **)&_pfnCreatePlaneDetector);
    ok &= loadFn("xrDestroyPlaneDetectorEXT",   (void **)&_pfnDestroyPlaneDetector);
    ok &= loadFn("xrBeginPlaneDetectionEXT",    (void **)&_pfnBeginPlaneDetection);
    ok &= loadFn("xrGetPlaneDetectionStateEXT", (void **)&_pfnGetPlaneDetectionState);
    ok &= loadFn("xrGetPlaneDetectionsEXT",     (void **)&_pfnGetPlaneDetections);
    ok &= loadFn("xrGetPlanePolygonBufferEXT",  (void **)&_pfnGetPlanePolygonBuffer);

    if (!ok) {
        ALOGW("XR_EXT_plane_detection functions unavailable — plane detection disabled");
        return false;
    }

    XrPlaneDetectorCreateInfoEXT createInfo = { XR_TYPE_PLANE_DETECTOR_CREATE_INFO_EXT };
    createInfo.flags = XR_PLANE_DETECTOR_ENABLE_CONTOUR_BIT_EXT;  // request boundary polygons
    XrResult r = _pfnCreatePlaneDetector(_session, &createInfo, &_planeDetector);
    if (XR_FAILED(r)) {
        ALOGE("xrCreatePlaneDetectorEXT failed: %d", (int)r);
        _planeDetector = XR_NULL_HANDLE;
        return false;
    }

    ALOGV("XR_EXT_plane_detection initialised");
    return true;
}

bool VROARSessionOpenXR::initSceneDetection(XrInstance instance, XrSession session,
                                            XrSpace baseSpace) {
    _instance  = instance;
    _session   = session;
    _baseSpace = baseSpace;

    auto loadFn = [&](const char *name, void **fn) -> bool {
        XrResult r = xrGetInstanceProcAddr(_instance, name, (PFN_xrVoidFunction *)fn);
        if (XR_FAILED(r) || *fn == nullptr) {
            ALOGW("xrGetInstanceProcAddr('%s') failed: %d", name, (int)r);
            return false;
        }
        return true;
    };

    bool ok = true;
    ok &= loadFn("xrQuerySpacesFB",               (void **)&_pfnQuerySpaces);
    ok &= loadFn("xrRetrieveSpaceQueryResultsFB", (void **)&_pfnRetrieveSpaceQueryResults);
    ok &= loadFn("xrGetSpaceComponentStatusFB",   (void **)&_pfnGetSpaceComponentStatus);
    ok &= loadFn("xrSetSpaceComponentStatusFB",   (void **)&_pfnSetSpaceComponentStatus);
    ok &= loadFn("xrGetSpaceBoundingBox2DFB",     (void **)&_pfnGetSpaceBoundingBox2D);
    ok &= loadFn("xrGetSpaceSemanticLabelsFB",    (void **)&_pfnGetSpaceSemanticLabels);
    ok &= loadFn("xrGetSpaceBoundary2DFB",        (void **)&_pfnGetSpaceBoundary2D);

    if (!ok) {
        ALOGW("XR_FB_scene functions unavailable — scene plane source disabled");
        return false;
    }
    _sceneAvailable = true;
    ALOGV("XR_FB_scene initialised (room model from Space Setup)");
    return true;
}

void VROARSessionOpenXR::destroyPlaneDetector() {
    if (_planeDetector != XR_NULL_HANDLE && _pfnDestroyPlaneDetector) {
        _pfnDestroyPlaneDetector(_planeDetector);
    }
    _planeDetector = XR_NULL_HANDLE;
    _sceneAvailable = false;
    _planes.clear();
    _scenePlanes.clear();
}

// ──────────────────────────────────────────────────────────────────────────────
// Per-frame update — drives the async detection state machine
// ──────────────────────────────────────────────────────────────────────────────

std::unique_ptr<VROARFrame> &VROARSessionOpenXR::updateFrame() {
    // ── XR_FB_scene path: kick off / re-arm a room query on a slow cadence ────
    // The Meta room model is static within a session, but re-querying lets newly
    // completed Space Setup data appear. Throttle to once every ~5s while idle.
    if (_sceneAvailable && !_sceneQueryInFlight) {
        auto now = std::chrono::steady_clock::now();
        int64_t since = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - _lastSceneQuery).count();
        if (_lastSceneQuery.time_since_epoch().count() == 0 || since >= 5000) {
            beginSceneQuery();
        }
    }

    // ── XR_EXT_plane_detection path (cross-vendor; absent on Meta runtime) ────
    if (_planeDetector == XR_NULL_HANDLE) {
        return _currentFrame;
    }

    if (!_detectionInFlight) {
        // Throttle re-arming so we don't spin the detector every frame.
        auto now = std::chrono::steady_clock::now();
        int64_t sinceLast = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - _lastSweepStart).count();
        if (_lastSweepStart.time_since_epoch().count() == 0 || sinceLast >= kSweepThrottleMs) {
            beginDetectionSweep();
        }
        return _currentFrame;
    }

    // A sweep is running — poll for completion.
    XrPlaneDetectionStateEXT state = XR_PLANE_DETECTION_STATE_NONE_EXT;
    XrResult r = _pfnGetPlaneDetectionState(_planeDetector, &state);
    if (XR_FAILED(r)) {
        ALOGW("xrGetPlaneDetectionStateEXT failed: %d", (int)r);
        _detectionInFlight = false;
        return _currentFrame;
    }

    switch (state) {
        case XR_PLANE_DETECTION_STATE_DONE_EXT:
            collectDetectedPlanes();
            _detectionInFlight = false;
            break;
        case XR_PLANE_DETECTION_STATE_ERROR_EXT:
        case XR_PLANE_DETECTION_STATE_FATAL_EXT:
            ALOGW("plane detection sweep failed (state=%d)", (int)state);
            _detectionInFlight = false;
            break;
        case XR_PLANE_DETECTION_STATE_PENDING_EXT:
        case XR_PLANE_DETECTION_STATE_NONE_EXT:
        default:
            break;  // still running — check again next frame
    }
    return _currentFrame;
}

void VROARSessionOpenXR::beginDetectionSweep() {
    static const XrPlaneDetectorOrientationEXT kOrientations[] = {
        XR_PLANE_DETECTOR_ORIENTATION_HORIZONTAL_UPWARD_EXT,
        XR_PLANE_DETECTOR_ORIENTATION_HORIZONTAL_DOWNWARD_EXT,
        XR_PLANE_DETECTOR_ORIENTATION_VERTICAL_EXT,
        XR_PLANE_DETECTOR_ORIENTATION_ARBITRARY_EXT,
    };
    static const XrPlaneDetectorSemanticTypeEXT kSemantics[] = {
        XR_PLANE_DETECTOR_SEMANTIC_TYPE_CEILING_EXT,
        XR_PLANE_DETECTOR_SEMANTIC_TYPE_FLOOR_EXT,
        XR_PLANE_DETECTOR_SEMANTIC_TYPE_WALL_EXT,
        XR_PLANE_DETECTOR_SEMANTIC_TYPE_PLATFORM_EXT,
    };

    XrPlaneDetectorBeginInfoEXT begin = { XR_TYPE_PLANE_DETECTOR_BEGIN_INFO_EXT };
    begin.baseSpace          = _baseSpace;
    begin.time               = _displayTime;
    begin.orientationCount   = (uint32_t)(sizeof(kOrientations) / sizeof(kOrientations[0]));
    begin.orientations       = kOrientations;
    begin.semanticTypeCount  = (uint32_t)(sizeof(kSemantics) / sizeof(kSemantics[0]));
    begin.semanticTypes      = kSemantics;
    begin.maxPlanes          = kMaxPlanes;
    begin.minArea            = kMinPlaneArea;
    // Search a generous volume centred on the world origin (eye-level start).
    begin.boundingBoxPose    = { {0, 0, 0, 1}, {0, 0, 0} };
    begin.boundingBoxExtent  = { 20.0f, 20.0f, 20.0f };

    XrResult r = _pfnBeginPlaneDetection(_planeDetector, &begin);
    if (XR_FAILED(r)) {
        ALOGW("xrBeginPlaneDetectionEXT failed: %d", (int)r);
        return;
    }
    _detectionInFlight = true;
    _lastSweepStart = std::chrono::steady_clock::now();
}

void VROARSessionOpenXR::collectDetectedPlanes() {
    XrPlaneDetectorGetInfoEXT info = { XR_TYPE_PLANE_DETECTOR_GET_INFO_EXT };
    info.baseSpace = _baseSpace;
    info.time      = _displayTime;

    // Two-call idiom: probe count, then fill.
    XrPlaneDetectorLocationsEXT locs = { XR_TYPE_PLANE_DETECTOR_LOCATIONS_EXT };
    locs.planeLocationCapacityInput = 0;
    XrResult r = _pfnGetPlaneDetections(_planeDetector, &info, &locs);
    if (XR_FAILED(r)) {
        ALOGW("xrGetPlaneDetectionsEXT (probe) failed: %d", (int)r);
        return;
    }

    uint32_t count = locs.planeLocationCountOutput;
    std::vector<XrPlaneDetectorLocationEXT> locations(
        count, { XR_TYPE_PLANE_DETECTOR_LOCATION_EXT });
    locs.planeLocationCapacityInput = count;
    locs.planeLocations             = locations.data();
    r = _pfnGetPlaneDetections(_planeDetector, &info, &locs);
    if (XR_FAILED(r)) {
        ALOGW("xrGetPlaneDetectionsEXT (fill) failed: %d", (int)r);
        return;
    }
    count = locs.planeLocationCountOutput;

    // Track which planeIds are present this sweep so we can remove stale ones.
    std::set<uint64_t> present;

    for (uint32_t i = 0; i < count; ++i) {
        const XrPlaneDetectorLocationEXT &loc = locations[i];
        bool tracked = (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
                       (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);
        if (!tracked) {
            continue;
        }
        present.insert(loc.planeId);

        auto it = _planes.find(loc.planeId);
        if (it == _planes.end()) {
            // New plane.
            auto anchor = std::make_shared<VROARPlaneAnchor>();
            anchor->setId(std::to_string(loc.planeId));
            populatePlaneAnchor(anchor, loc);
            anchor->recordUpdate(true);
            _planes[loc.planeId] = anchor;
            addAnchor(anchor);  // fires anchorWasDetected
        } else {
            // Existing plane — update only on significant change (throttled).
            auto &anchor = it->second;
            VROVector3f newCenter(0, 0, 0);
            VROVector3f newExtent(loc.extents.width, 0, loc.extents.height);
            VROARPlaneAlignment newAlignment = mapAlignment(loc.orientation);
            std::vector<VROVector3f> newBoundary = queryPolygon(loc.planeId);

            if (!anchor->shouldThrottleUpdate() &&
                anchor->hasSignificantChanges(newCenter, newExtent, newAlignment, newBoundary)) {
                populatePlaneAnchor(anchor, loc);
                anchor->recordUpdate(true);
                updateAnchor(anchor);  // fires anchorWillUpdate / anchorDidUpdate
            }
        }
    }

    // Remove planes that disappeared this sweep.
    for (auto it = _planes.begin(); it != _planes.end();) {
        if (present.find(it->first) == present.end()) {
            std::shared_ptr<VROARPlaneAnchor> gone = it->second;
            it = _planes.erase(it);
            removeAnchor(gone);  // fires anchorWasRemoved
        } else {
            ++it;
        }
    }
}

void VROARSessionOpenXR::populatePlaneAnchor(
        const std::shared_ptr<VROARPlaneAnchor> &anchor,
        const XrPlaneDetectorLocationEXT &loc) {
    // World transform, with the OpenXR→Viro plane-axis correction applied.
    VROMatrix4f transform = poseToMatrix(loc.pose).multiply(planeAxisCorrection());
    anchor->setTransform(transform);

    anchor->setCenter(VROVector3f(0, 0, 0));
    anchor->setExtent(VROVector3f(loc.extents.width, 0, loc.extents.height));
    anchor->setAlignment(mapAlignment(loc.orientation));
    anchor->setClassification(mapClassification(loc.semanticType));
    anchor->setBoundaryVertices(queryPolygon(loc.planeId));
}

std::vector<VROVector3f> VROARSessionOpenXR::queryPolygon(uint64_t planeId) {
    std::vector<VROVector3f> result;
    if (!_pfnGetPlanePolygonBuffer) {
        return result;
    }

    // Two-call idiom for the outer contour (polygon buffer index 0).
    XrPlaneDetectorPolygonBufferEXT poly = { XR_TYPE_PLANE_DETECTOR_POLYGON_BUFFER_EXT };
    poly.vertexCapacityInput = 0;
    XrResult r = _pfnGetPlanePolygonBuffer(_planeDetector, planeId, 0, &poly);
    if (XR_FAILED(r) || poly.vertexCountOutput == 0) {
        return result;
    }

    uint32_t n = poly.vertexCountOutput;
    std::vector<XrVector2f> verts(n);
    poly.vertexCapacityInput = n;
    poly.vertices            = verts.data();
    r = _pfnGetPlanePolygonBuffer(_planeDetector, planeId, 0, &poly);
    if (XR_FAILED(r)) {
        return result;
    }

    // Polygon vertices are 2D in the plane's local X-Y frame. Apply the same
    // +90°-about-X correction baked into the anchor transform so boundary
    // vertices are anchor-local (x, 0, z) as ViroARPlane expects.
    result.reserve(poly.vertexCountOutput);
    for (uint32_t i = 0; i < poly.vertexCountOutput; ++i) {
        result.emplace_back(verts[i].x, 0.0f, -verts[i].y);
    }
    return result;
}

// ──────────────────────────────────────────────────────────────────────────────
// Anchor lifecycle — mirrors VROARSessionARCore (delegate fan-out)
// ──────────────────────────────────────────────────────────────────────────────

void VROARSessionOpenXR::addAnchor(std::shared_ptr<VROARAnchor> anchor) {
    std::shared_ptr<VROARSessionDelegate> delegate = getDelegate();
    if (delegate) {
        delegate->anchorWasDetected(anchor);
    }
}

void VROARSessionOpenXR::removeAnchor(std::shared_ptr<VROARAnchor> anchor) {
    std::shared_ptr<VROARSessionDelegate> delegate = getDelegate();
    if (delegate) {
        delegate->anchorWasRemoved(anchor);
    }
}

void VROARSessionOpenXR::updateAnchor(std::shared_ptr<VROARAnchor> anchor) {
    std::shared_ptr<VROARSessionDelegate> delegate = getDelegate();
    if (delegate) {
        delegate->anchorWillUpdate(anchor);
    }
    anchor->updateNodeTransform();
    if (delegate) {
        delegate->anchorDidUpdate(anchor);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// XR_FB_scene — Meta room model (Space Setup) → plane anchors
// ──────────────────────────────────────────────────────────────────────────────

void VROARSessionOpenXR::beginSceneQuery() {
    if (!_pfnQuerySpaces) return;

    // Stamp the attempt time up-front so a failure still throttles the retry to
    // the normal cadence (otherwise a failing query re-fires every frame).
    _lastSceneQuery = std::chrono::steady_clock::now();

    // Load all spaces stored locally (the room entities from Space Setup). Use a
    // null filter — a storage-location filter would require XR_FB_spatial_entity
    // _storage (not enabled) and fails validation on this runtime.
    XrSpaceQueryInfoFB query = { XR_TYPE_SPACE_QUERY_INFO_FB };
    query.queryAction    = XR_SPACE_QUERY_ACTION_LOAD_FB;
    query.maxResultCount = 64;
    query.timeout        = 0;
    query.filter         = nullptr;
    query.excludeFilter  = nullptr;

    XrAsyncRequestIdFB requestId = 0;
    XrResult r = _pfnQuerySpaces(
        _session, reinterpret_cast<const XrSpaceQueryInfoBaseHeaderFB *>(&query), &requestId);
    if (XR_FAILED(r)) {
        ALOGW("xrQuerySpacesFB failed: %d", (int)r);
        return;
    }
    _sceneQueryRequestId = requestId;
    _sceneQueryInFlight  = true;
}

void VROARSessionOpenXR::onSpatialEvent(const XrEventDataBuffer &event) {
    switch (event.type) {
        case XR_TYPE_EVENT_DATA_SPACE_QUERY_RESULTS_AVAILABLE_FB: {
            auto *ev = reinterpret_cast<const XrEventDataSpaceQueryResultsAvailableFB *>(&event);
            processSceneQueryResults(ev->requestId);
            break;
        }
        case XR_TYPE_EVENT_DATA_SPACE_QUERY_COMPLETE_FB: {
            auto *ev = reinterpret_cast<const XrEventDataSpaceQueryCompleteFB *>(&event);
            if (XR_FAILED(ev->result)) {
                ALOGW("space query completed with error: %d", (int)ev->result);
            }
            _sceneQueryInFlight = false;
            break;
        }
        case XR_TYPE_EVENT_DATA_SPACE_SET_STATUS_COMPLETE_FB: {
            // A component (LOCATABLE) finished enabling — re-query promptly so the
            // now-locatable plane gets built without waiting the full cadence.
            auto *ev = reinterpret_cast<const XrEventDataSpaceSetStatusCompleteFB *>(&event);
            if (!XR_FAILED(ev->result) && !_sceneQueryInFlight) {
                _lastSceneQuery = std::chrono::steady_clock::time_point{};  // re-arm next frame
            }
            break;
        }
        default:
            break;
    }
}

void VROARSessionOpenXR::processSceneQueryResults(XrAsyncRequestIdFB requestId) {
    if (!_pfnRetrieveSpaceQueryResults) return;

    // Two-call idiom: probe count, then fill.
    XrSpaceQueryResultsFB results = { XR_TYPE_SPACE_QUERY_RESULTS_FB };
    results.resultCapacityInput = 0;
    XrResult r = _pfnRetrieveSpaceQueryResults(_session, requestId, &results);
    if (XR_FAILED(r)) {
        ALOGW("xrRetrieveSpaceQueryResultsFB (probe) failed: %d", (int)r);
        return;
    }

    uint32_t count = results.resultCountOutput;
    std::vector<XrSpaceQueryResultFB> buf(count);
    results.resultCapacityInput = count;
    results.results             = buf.data();
    r = _pfnRetrieveSpaceQueryResults(_session, requestId, &results);
    if (XR_FAILED(r)) {
        ALOGW("xrRetrieveSpaceQueryResultsFB (fill) failed: %d", (int)r);
        return;
    }
    count = results.resultCountOutput;
    ALOGV("scene query returned %u spaces", count);

    // One-shot per-space diagnostics: dump component availability so we can see
    // why planes are / aren't being built on this device's room model.
    if (!_sceneDiagLogged && _pfnGetSpaceComponentStatus) {
        _sceneDiagLogged = true;
        auto compStr = [&](XrSpace s, XrSpaceComponentTypeFB t) -> const char * {
            XrSpaceComponentStatusFB st = { XR_TYPE_SPACE_COMPONENT_STATUS_FB };
            XrResult cr = _pfnGetSpaceComponentStatus(s, t, &st);
            if (XR_FAILED(cr)) return "err";
            return st.enabled ? "ON" : "off";
        };
        for (uint32_t i = 0; i < count; ++i) {
            XrSpace s = buf[i].space;
            ALOGV("space[%u]=%p  2D=%s 3D=%s LOCATABLE=%s SEMANTIC=%s ROOMLAYOUT=%s", i,
                  (void *)s,
                  compStr(s, XR_SPACE_COMPONENT_TYPE_BOUNDED_2D_FB),
                  compStr(s, XR_SPACE_COMPONENT_TYPE_BOUNDED_3D_FB),
                  compStr(s, XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB),
                  compStr(s, XR_SPACE_COMPONENT_TYPE_SEMANTIC_LABELS_FB),
                  compStr(s, XR_SPACE_COMPONENT_TYPE_ROOM_LAYOUT_FB));
        }
    }

    std::set<uint64_t> present;
    int built = 0, with2D = 0;
    for (uint32_t i = 0; i < count; ++i) {
        XrSpace space = buf[i].space;
        uint64_t key = (uint64_t)space;

        // Only spaces carrying a 2D boundary are planes (walls/floor/ceiling/desk).
        XrSpaceComponentStatusFB status = { XR_TYPE_SPACE_COMPONENT_STATUS_FB };
        if (!_pfnGetSpaceComponentStatus ||
            XR_FAILED(_pfnGetSpaceComponentStatus(space, XR_SPACE_COMPONENT_TYPE_BOUNDED_2D_FB, &status)) ||
            !status.enabled) {
            continue;
        }
        with2D++;

        // The plane needs LOCATABLE enabled before xrLocateSpace works. Room
        // entities load with LOCATABLE off, so enable it (async) the first time we
        // see the space; a later query cycle builds it once enabling completes.
        XrSpaceComponentStatusFB locStatus = { XR_TYPE_SPACE_COMPONENT_STATUS_FB };
        bool locatable = !XR_FAILED(_pfnGetSpaceComponentStatus(
                space, XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB, &locStatus)) && locStatus.enabled;
        if (!locatable) {
            if (_pfnSetSpaceComponentStatus &&
                _locatableRequested.find(key) == _locatableRequested.end()) {
                XrSpaceComponentStatusSetInfoFB setInfo = { XR_TYPE_SPACE_COMPONENT_STATUS_SET_INFO_FB };
                setInfo.componentType = XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB;
                setInfo.enabled       = XR_TRUE;
                setInfo.timeout       = 0;
                XrAsyncRequestIdFB rid = 0;
                if (XR_FAILED(_pfnSetSpaceComponentStatus(space, &setInfo, &rid))) {
                    ALOGW("enable LOCATABLE failed for space %p", (void *)space);
                } else {
                    _locatableRequested.insert(key);
                }
            }
            continue;  // build on a later cycle once LOCATABLE is enabled
        }

        present.insert(key);
        auto it = _scenePlanes.find(key);
        if (it != _scenePlanes.end()) {
            continue;  // already published; room model is static within a session
        }
        std::shared_ptr<VROARPlaneAnchor> anchor = buildPlaneFromSpace(space);
        if (anchor) {
            anchor->setId(std::to_string(key));
            anchor->recordUpdate(true);
            _scenePlanes[key] = anchor;
            addAnchor(anchor);  // fires anchorWasDetected → onAnchorFound
        }
    }

    // Remove planes no longer present (e.g. room re-scanned).
    for (auto it = _scenePlanes.begin(); it != _scenePlanes.end();) {
        if (present.find(it->first) == present.end()) {
            std::shared_ptr<VROARPlaneAnchor> gone = it->second;
            it = _scenePlanes.erase(it);
            removeAnchor(gone);
        } else {
            ++it;
        }
    }
}

std::shared_ptr<VROARPlaneAnchor> VROARSessionOpenXR::buildPlaneFromSpace(XrSpace space) {
    // Locate the entity in the world frame.
    XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
    XrResult r = xrLocateSpace(space, _baseSpace, _displayTime, &loc);
    bool located = !XR_FAILED(r) &&
                   (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
                   (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);
    if (!located) {
        return nullptr;
    }

    // 2D bounding box (plane-local X-Y rect: offset = lower-left corner).
    XrRect2Df bbox = {};
    if (!_pfnGetSpaceBoundingBox2D ||
        XR_FAILED(_pfnGetSpaceBoundingBox2D(_session, space, &bbox))) {
        return nullptr;
    }

    // Semantic label (single label expected for a room-entity face).
    std::string label;
    if (_pfnGetSpaceSemanticLabels) {
        XrSemanticLabelsFB labels = { XR_TYPE_SEMANTIC_LABELS_FB };
        labels.bufferCapacityInput = 0;
        if (!XR_FAILED(_pfnGetSpaceSemanticLabels(_session, space, &labels)) &&
            labels.bufferCountOutput > 0) {
            std::vector<char> buf(labels.bufferCountOutput + 1, 0);
            labels.bufferCapacityInput = labels.bufferCountOutput;
            labels.buffer              = buf.data();
            if (!XR_FAILED(_pfnGetSpaceSemanticLabels(_session, space, &labels))) {
                label.assign(buf.data(), labels.bufferCountOutput);
                // Labels may be comma-separated; take the first.
                size_t comma = label.find(',');
                if (comma != std::string::npos) label = label.substr(0, comma);
            }
        }
    }

    auto anchor = std::make_shared<VROARPlaneAnchor>();

    // World transform with the OpenXR(+Z normal) → Viro(+Y normal) correction.
    VROMatrix4f transform = poseToMatrix(loc.pose).multiply(planeAxisCorrection());
    anchor->setTransform(transform);

    // Extent (width × height) and centre. The bbox offset is the lower-left
    // corner in plane-local X-Y; the plane centre sits at offset + extent/2.
    float w = bbox.extent.width, h = bbox.extent.height;
    float cx = bbox.offset.x + w * 0.5f;
    float cy = bbox.offset.y + h * 0.5f;
    anchor->setExtent(VROVector3f(w, 0, h));
    anchor->setCenter(VROVector3f(cx, 0, -cy));  // map plane-local (x,y) → (x,0,-y)
    anchor->setAlignment(alignmentForLabel(label));
    anchor->setClassification(classifyLabel(label));

    // Boundary polygon (plane-local X-Y → anchor-local x,0,-y).
    if (_pfnGetSpaceBoundary2D) {
        XrBoundary2DFB boundary = { XR_TYPE_BOUNDARY_2D_FB };
        boundary.vertexCapacityInput = 0;
        if (!XR_FAILED(_pfnGetSpaceBoundary2D(_session, space, &boundary)) &&
            boundary.vertexCountOutput > 0) {
            std::vector<XrVector2f> verts(boundary.vertexCountOutput);
            boundary.vertexCapacityInput = boundary.vertexCountOutput;
            boundary.vertices            = verts.data();
            if (!XR_FAILED(_pfnGetSpaceBoundary2D(_session, space, &boundary))) {
                std::vector<VROVector3f> poly;
                poly.reserve(boundary.vertexCountOutput);
                for (uint32_t i = 0; i < boundary.vertexCountOutput; ++i) {
                    poly.emplace_back(verts[i].x, 0.0f, -verts[i].y);
                }
                anchor->setBoundaryVertices(poly);
            }
        }
    }

    ALOGV("scene plane: label='%s' %.2fx%.2f m", label.c_str(), w, h);
    return anchor;
}
