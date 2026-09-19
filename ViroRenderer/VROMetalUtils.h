//
//  VROMetalUtils.h
//  ViroRenderer
//
//  Inline helpers to convert between Viro math types and Metal/simd types.
//  This header was deleted in 2017; recreated for the visionOS Metal path.
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

#ifndef VROMetalUtils_h
#define VROMetalUtils_h

#include "VRODefines.h"
#if VRO_METAL

#include <simd/simd.h>
#include "VROVector3f.h"
#include "VROVector4f.h"
#include "VROMatrix4f.h"

static inline simd_float3 toVectorFloat3(const VROVector3f &v) {
    return simd_make_float3(v.x, v.y, v.z);
}

static inline simd_float4 toVectorFloat4(const VROVector4f &v) {
    return simd_make_float4(v.x, v.y, v.z, v.w);
}

// VROMatrix4f is column-major (OpenGL / GLM convention); simd_float4x4 is also
// column-major, so we can copy the 16 floats directly into the four columns.
static inline simd_float4x4 toMatrixFloat4x4(const VROMatrix4f &m) {
    const float *a = m.getArray();
    simd_float4x4 result;
    result.columns[0] = simd_make_float4(a[0],  a[1],  a[2],  a[3]);
    result.columns[1] = simd_make_float4(a[4],  a[5],  a[6],  a[7]);
    result.columns[2] = simd_make_float4(a[8],  a[9],  a[10], a[11]);
    result.columns[3] = simd_make_float4(a[12], a[13], a[14], a[15]);
    return result;
}

// Reverse: simd_float4x4 (column-major) → VROMatrix4f (column-major).
// Used by VRORendererBridge to convert CompositorServices view matrices.
static inline VROMatrix4f toMatrix4f(const simd_float4x4 &m) {
    float a[16];
    a[0]  = m.columns[0][0]; a[1]  = m.columns[0][1]; a[2]  = m.columns[0][2]; a[3]  = m.columns[0][3];
    a[4]  = m.columns[1][0]; a[5]  = m.columns[1][1]; a[6]  = m.columns[1][2]; a[7]  = m.columns[1][3];
    a[8]  = m.columns[2][0]; a[9]  = m.columns[2][1]; a[10] = m.columns[2][2]; a[11] = m.columns[2][3];
    a[12] = m.columns[3][0]; a[13] = m.columns[3][1]; a[14] = m.columns[3][2]; a[15] = m.columns[3][3];
    return VROMatrix4f(a);
}

#endif  // VRO_METAL
#endif  // VROMetalUtils_h
