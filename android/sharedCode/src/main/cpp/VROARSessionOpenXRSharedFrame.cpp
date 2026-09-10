//
//  VROARSessionOpenXRSharedFrame.cpp
//  ViroRenderer
//
//  Copyright © 2026 ReactVision. All rights reserved.
//
//  Platform-native co-location for Quest (CL-H).
//
//  Quest cannot relocalise a ReactVision cloud anchor — the OpenXR session
//  produces no camera image for the SIFT localiser — but it does not need to.
//  Meta's spatial anchors already solve co-location, sensor-fused, and this
//  drives them through OpenXR alone:
//
//    create  xrCreateSpatialAnchorFB → enable STORABLE + SHARABLE
//            → xrShareSpacesMETA to a group UUID
//    join    xrQuerySpacesFB filtered by that group UUID
//            → enable LOCATABLE → xrLocateSpace
//
//  Group sharing (XR_META_spatial_entity_group_sharing) rather than
//  XR_FB_spatial_entity_sharing: the FB path addresses recipients as
//  XrSpaceUserFB handles built from Meta account ids, which only the Meta
//  Platform SDK can supply. Group sharing takes an app-chosen UUID, so the flow
//  stays inside OpenXR — and that UUID is also the co-location room key, which
//  means one identifier names the space, the frame and the channel room.
//
//  Everything here is asynchronous through the renderer's xrPollEvent loop; see
//  onSpatialEvent() in VROARSessionOpenXR.cpp for the dispatch.
//

#include "VROARSessionOpenXR.h"
#include "VROLog.h"

#include <android/log.h>
#define LOG_TAG "ViroSharedFrame"
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN,    LOG_TAG, __VA_ARGS__)
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)
#include "VROMatrix4f.h"
#include "VROVector3f.h"
#include "VROQuaternion.h"

#include <cstdio>
#include <cstring>
#include <vector>

// ============================================================================
// UUID <-> string
// ============================================================================

namespace {

/** Canonical 8-4-4-4-12 hex. Returns false on anything else. */
bool parseUuid(const std::string &str, XrUuid *out) {
    if (!out) return false;
    std::memset(out, 0, sizeof(XrUuid));

    // Accept with or without hyphens; anything else is a caller error.
    std::string hex;
    hex.reserve(32);
    for (char c : str) {
        if (c == '-') continue;
        if (!isxdigit((unsigned char)c)) return false;
        hex += c;
    }
    if (hex.size() != 32) return false;

    for (int i = 0; i < 16; i++) {
        unsigned int byte = 0;
        if (sscanf(hex.c_str() + i * 2, "%2x", &byte) != 1) return false;
        out->data[i] = (uint8_t)byte;
    }
    return true;
}

std::string formatUuid(const XrUuid &uuid) {
    char buf[37];
    snprintf(buf, sizeof(buf),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             uuid.data[0],  uuid.data[1],  uuid.data[2],  uuid.data[3],
             uuid.data[4],  uuid.data[5],  uuid.data[6],  uuid.data[7],
             uuid.data[8],  uuid.data[9],  uuid.data[10], uuid.data[11],
             uuid.data[12], uuid.data[13], uuid.data[14], uuid.data[15]);
    return std::string(buf);
}

/** Same encoding resolveCloudAnchor() emits, so JS treats both identically. */
std::string matrixToCsv(const VROMatrix4f &m) {
    const float *a = m.getArray();
    std::string csv;
    char buf[32];
    for (int i = 0; i < 16; ++i) {
        snprintf(buf, sizeof(buf), i == 0 ? "%g" : ",%g", a[i]);
        csv += buf;
    }
    return csv;
}

VROMatrix4f poseToMatrix(const XrPosef &pose) {
    VROQuaternion q(pose.orientation.x, pose.orientation.y,
                    pose.orientation.z, pose.orientation.w);
    VROMatrix4f m = q.getMatrix();
    m.translate({ pose.position.x, pose.position.y, pose.position.z });
    return m;
}

} // namespace

// ============================================================================
// Public API
// ============================================================================

bool VROARSessionOpenXR::rvSupportsSharedFrame() {
    return _sharingAvailable && _pfnCreateSpatialAnchor && _pfnShareSpaces &&
           _pfnQuerySpaces && _pfnSetSpaceComponentStatus;
}

void VROARSessionOpenXR::rvCreateSharedFrame(
        std::string groupId,
        std::function<void(bool, std::string, std::string, std::string)> callback) {

    if (!rvSupportsSharedFrame()) {
        if (callback) callback(false, "", "",
            "Shared frames unavailable: XR_META_spatial_entity_group_sharing not present");
        return;
    }
    if (_sharedFrame.phase != SharedFrameOp::Phase::Idle) {
        if (callback) callback(false, "", "", "A shared-frame operation is already running");
        return;
    }

    XrUuid group{};
    if (!parseUuid(groupId, &group)) {
        if (callback) callback(false, "", "", "groupId must be a UUID");
        return;
    }

    _sharedFrame = SharedFrameOp{};
    _sharedFrame.joining   = false;
    _sharedFrame.groupUuid = group;
    _sharedFrame.callback  = std::move(callback);
    _sharedFrame.phase     = SharedFrameOp::Phase::Creating;

    // Anchor at the app's reference-space origin rather than at the head pose:
    // the frame should not depend on where the user happened to be standing when
    // they tapped, and every device joining it gets the same origin regardless.
    XrSpatialAnchorCreateInfoFB info{ XR_TYPE_SPATIAL_ANCHOR_CREATE_INFO_FB };
    info.space              = _baseSpace;
    info.poseInSpace.orientation = { 0, 0, 0, 1 };
    info.poseInSpace.position    = { 0, 0, 0 };
    info.time               = _displayTime;

    XrAsyncRequestIdFB requestId = 0;
    XrResult r = _pfnCreateSpatialAnchor(_session, &info, &requestId);
    if (XR_FAILED(r)) {
        finishSharedFrame(false, "xrCreateSpatialAnchorFB failed: " + std::to_string((int)r));
        return;
    }
    _sharedFrame.requestId = requestId;
}

void VROARSessionOpenXR::rvJoinSharedFrame(
        std::string groupId,
        std::function<void(bool, std::string, std::string, std::string)> callback) {

    if (!rvSupportsSharedFrame()) {
        if (callback) callback(false, "", "",
            "Shared frames unavailable: XR_META_spatial_entity_group_sharing not present");
        return;
    }
    if (_sharedFrame.phase != SharedFrameOp::Phase::Idle) {
        if (callback) callback(false, "", "", "A shared-frame operation is already running");
        return;
    }

    XrUuid group{};
    if (!parseUuid(groupId, &group)) {
        if (callback) callback(false, "", "", "groupId must be a UUID");
        return;
    }

    _sharedFrame = SharedFrameOp{};
    _sharedFrame.joining   = true;
    _sharedFrame.groupUuid = group;
    _sharedFrame.callback  = std::move(callback);

    beginGroupQuery();
}

// ============================================================================
// Internals
// ============================================================================

void VROARSessionOpenXR::beginGroupQuery() {
    // The group filter chains onto the standard query the scene path already
    // uses; only the filter differs.
    XrSpaceGroupUuidFilterInfoMETA groupFilter{
        (XrStructureType)XR_TYPE_SPACE_GROUP_UUID_FILTER_INFO_META };
    groupFilter.groupUuid = _sharedFrame.groupUuid;

    XrSpaceQueryInfoFB query{ XR_TYPE_SPACE_QUERY_INFO_FB };
    query.queryAction  = XR_SPACE_QUERY_ACTION_LOAD_FB;
    query.maxResultCount = 16;
    query.timeout      = 0;
    query.filter       = reinterpret_cast<const XrSpaceFilterInfoBaseHeaderFB *>(&groupFilter);
    query.excludeFilter = nullptr;

    XrAsyncRequestIdFB requestId = 0;
    XrResult r = _pfnQuerySpaces(
        _session, reinterpret_cast<const XrSpaceQueryInfoBaseHeaderFB *>(&query), &requestId);
    if (XR_FAILED(r)) {
        finishSharedFrame(false, "xrQuerySpacesFB failed: " + std::to_string((int)r));
        return;
    }
    _sharedFrame.requestId = requestId;
    _sharedFrame.phase     = SharedFrameOp::Phase::Querying;
}

bool VROARSessionOpenXR::enableSharedFrameComponent(XrSpace space,
                                                    XrSpaceComponentTypeFB component) {
    XrSpaceComponentStatusSetInfoFB set{ XR_TYPE_SPACE_COMPONENT_STATUS_SET_INFO_FB };
    set.componentType = component;
    set.enabled       = XR_TRUE;
    set.timeout       = 0;

    XrAsyncRequestIdFB requestId = 0;
    XrResult r = _pfnSetSpaceComponentStatus(space, &set, &requestId);

    // Already enabled is a success, not a failure — a rejoined anchor arrives
    // with components the previous session turned on.
    if (r == XR_ERROR_SPACE_COMPONENT_STATUS_ALREADY_SET_FB) return false;
    if (XR_FAILED(r)) {
        ALOGW("[shared-frame] enable component %d failed: %d", (int)component, (int)r);
        return false;
    }
    return true;   // async: expect a SET_STATUS_COMPLETE event
}

void VROARSessionOpenXR::locateAndReportSharedFrame() {
    XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
    XrResult r = xrLocateSpace(_sharedFrame.space, _baseSpace, _displayTime, &loc);

    const XrSpaceLocationFlags needed =
        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;

    if (XR_FAILED(r) || (loc.locationFlags & needed) != needed) {
        // Not an error worth failing on immediately — tracking may simply not
        // have caught up with a just-loaded anchor. The caller retries.
        finishSharedFrame(false, "shared frame is not locatable yet");
        return;
    }

    std::string csv = matrixToCsv(poseToMatrix(loc.pose));
    std::string frameId = formatUuid(_sharedFrame.spaceUuid);

    SharedFrameCallback cb = std::move(_sharedFrame.callback);
    _sharedFrame = SharedFrameOp{};
    if (cb) cb(true, frameId, csv, "");
}

void VROARSessionOpenXR::finishSharedFrame(bool success, const std::string &error) {
    SharedFrameCallback cb = std::move(_sharedFrame.callback);
    std::string frameId = success ? formatUuid(_sharedFrame.spaceUuid) : "";
    _sharedFrame = SharedFrameOp{};
    if (cb) cb(success, frameId, "", error);
}

void VROARSessionOpenXR::processSharedFrameQueryResults(XrAsyncRequestIdFB requestId) {
    XrSpaceQueryResultsFB results{ XR_TYPE_SPACE_QUERY_RESULTS_FB };
    if (XR_FAILED(_pfnRetrieveSpaceQueryResults(_session, requestId, &results))) {
        finishSharedFrame(false, "retrieving group query results failed");
        return;
    }

    std::vector<XrSpaceQueryResultFB> buffer(results.resultCountOutput);
    results.resultCapacityInput = (uint32_t)buffer.size();
    results.results             = buffer.data();

    if (buffer.empty() ||
        XR_FAILED(_pfnRetrieveSpaceQueryResults(_session, requestId, &results))) {
        finishSharedFrame(false, "no shared frame published to this group yet");
        return;
    }

    // A group holds one frame by construction — the first device to publish
    // defines it. Extra results would mean two devices raced; taking the first
    // keeps every joiner on the same one rather than picking arbitrarily.
    _sharedFrame.space     = buffer[0].space;
    _sharedFrame.spaceUuid = buffer[0].uuid;
    _sharedFrame.phase     = SharedFrameOp::Phase::EnablingComponents;

    int pending = 0;
    if (enableSharedFrameComponent(buffer[0].space, XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB)) {
        pending++;
    }
    _sharedFrame.pendingComponents = pending;

    // Already locatable — a rejoin within the same session takes this path.
    if (pending == 0) locateAndReportSharedFrame();
}
