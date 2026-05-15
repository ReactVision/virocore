//
//  VROBoneUBOMetal.h
//  ViroRenderer
//
//  Metal path for skeletal animation bone transforms (replaces VROBoneUBO on Metal).
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

#ifndef VROBoneUBOMetal_h
#define VROBoneUBOMetal_h

#include "VRODefines.h"
#if VRO_METAL

#include <memory>
#include <Metal/Metal.h>

class VROSkinner;
class VROShaderModifier;

/*
 Metal equivalent of VROBoneUBO.  Holds the bone transform matrix array in a
 shared MTLBuffer that is bound at vertex-buffer index 5 every draw call when
 the geometry has an active VROSkinner.

 Buffer layout matches VROBonesData: kMaxBones contiguous float4x4 matrices
 (192 × 16 floats, column-major, matching the GLSL UBO layout).
 */
class VROBoneUBOMetal {

public:

    // Returns a geometry-modifier that injects MSL skinning code into the
    // #pragma geometry_modifier_body injection point.  The result is cached
    // per-thread so multiple skinned meshes share the same VROShaderModifier.
    static std::shared_ptr<VROShaderModifier> createSkinningShaderModifier(bool hasScaling);

    VROBoneUBOMetal(id<MTLDevice> device);
    ~VROBoneUBOMetal();

    // Upload the latest bone transforms from the skinner into the MTLBuffer.
    void update(const std::shared_ptr<VROSkinner> &skinner);

    id<MTLBuffer> getBuffer() const { return _bonesBuffer; }

private:

    id<MTLBuffer> _bonesBuffer;
};

#endif  // VRO_METAL
#endif  /* VROBoneUBOMetal_h */
