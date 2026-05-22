//
//  VROParticleUBOMetal.h
//  ViroRenderer
//
//  Metal path for particle instanced rendering (replaces VROParticleUBO on Metal).
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

#ifndef VROParticleUBOMetal_h
#define VROParticleUBOMetal_h

#include "VRODefines.h"
#if VRO_METAL

#include <memory>
#include <vector>
#include <Metal/Metal.h>
#include "VROInstancedUBO.h"
#include "VROAtomic.h"
#include "VROBoundingBox.h"

class VROParticle;
class VRODriver;

/*
 Metal equivalent of VROParticleUBO. Holds per-particle transforms at vertex
 buffer index 7 and per-particle colors at fragment buffer index 7. Both are
 shared MTLBuffers updated each frame.

 Buffer layout:
   vertex   buffer(7): float4x4[kMaxParticlesPerUBO]   — world transforms
   fragment buffer(7): float4[kMaxParticlesPerUBO]      — color tints
 */
class VROParticleUBOMetal : public VROInstancedUBO {
public:
    // Factory — extracts the MTLDevice from the driver and constructs the UBO.
    static std::shared_ptr<VROParticleUBOMetal> create(std::shared_ptr<VRODriver> &driver);

    VROParticleUBOMetal(id<MTLDevice> device);
    ~VROParticleUBOMetal();

    std::vector<std::shared_ptr<VROShaderModifier>> createInstanceShaderModifier() override;
    int  getNumberOfDrawCalls() override;
    int  bindDrawData(int drawCallIndex) override;
    VROBoundingBox getInstancedBoundingBox() override;

    // Copy latest particle data from the emitter into the MTLBuffers.
    void update(std::vector<VROParticle> &particles, VROBoundingBox &box);

    // Must be called before bindDrawData so the buffers can be bound to the encoder.
    void setEncoder(id<MTLRenderCommandEncoder> encoder) { _encoder = encoder; }

private:
    id<MTLBuffer> _vertexBuffer;    // kMaxParticlesPerUBO × float4x4
    id<MTLBuffer> _fragmentBuffer;  // kMaxParticlesPerUBO × float4

    // Not retained — valid only during a single draw call.
    __unsafe_unretained id<MTLRenderCommandEncoder> _encoder;

    std::vector<VROParticle> _lastKnownParticles;
    VROAtomic<VROBoundingBox> _lastKnownBoundingBox;
};

#endif  // VRO_METAL
#endif  /* VROParticleUBOMetal_h */
