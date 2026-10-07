// VROOpenXRBoundaryVisibility.h
// ViroRenderer
//
// XR_META_boundary_visibility arrived in OpenXR 1.1.59; the loader headers this
// build compiles against predate it. Values are copied from the Khronos header,
// and the guard makes this a no-op once the headers are bumped.
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#ifndef ANDROID_VROOPENXRBOUNDARYVISIBILITY_H
#define ANDROID_VROOPENXRBOUNDARYVISIBILITY_H

#include <openxr/openxr.h>

#ifndef XR_META_boundary_visibility
#define XR_META_boundary_visibility 1
#define XR_META_boundary_visibility_SPEC_VERSION 1
#define XR_META_BOUNDARY_VISIBILITY_EXTENSION_NAME "XR_META_boundary_visibility"

// A success code, so XR_FAILED / XR_CHECK do not catch it.
#define XR_BOUNDARY_VISIBILITY_SUPPRESSION_NOT_ALLOWED_META ((XrResult)1000528000)
#define XR_TYPE_SYSTEM_BOUNDARY_VISIBILITY_PROPERTIES_META ((XrStructureType)1000528000)
#define XR_TYPE_EVENT_DATA_BOUNDARY_VISIBILITY_CHANGED_META ((XrStructureType)1000528001)

typedef enum XrBoundaryVisibilityMETA {
    XR_BOUNDARY_VISIBILITY_NOT_SUPPRESSED_META = 1,
    XR_BOUNDARY_VISIBILITY_SUPPRESSED_META = 2,
    XR_BOUNDARY_VISIBILITY_MAX_ENUM_META = 0x7FFFFFFF
} XrBoundaryVisibilityMETA;

typedef struct XrSystemBoundaryVisibilityPropertiesMETA {
    XrStructureType       type;
    void* XR_MAY_ALIAS    next;
    XrBool32              supportsBoundaryVisibility;
} XrSystemBoundaryVisibilityPropertiesMETA;

typedef struct XrEventDataBoundaryVisibilityChangedMETA {
    XrStructureType             type;
    const void* XR_MAY_ALIAS    next;
    XrBoundaryVisibilityMETA    boundaryVisibility;
} XrEventDataBoundaryVisibilityChangedMETA;

typedef XrResult (XRAPI_PTR *PFN_xrRequestBoundaryVisibilityMETA)(
    XrSession session, XrBoundaryVisibilityMETA boundaryVisibility);
#endif  // XR_META_boundary_visibility

#endif  // ANDROID_VROOPENXRBOUNDARYVISIBILITY_H
