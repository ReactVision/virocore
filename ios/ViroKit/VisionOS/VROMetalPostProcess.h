//
//  VROMetalPostProcess.h
//  ViroKit — visionOS
//
//  Copyright © 2026 ReactVision. All rights reserved.
//
//  Full-screen post-process pass for Metal.
//
//  The OpenGL post-process assembles a GLSL fragment shader at runtime from string
//  fragments supplied by each render pass (see VROToneMappingRenderPass). MSL cannot
//  be assembled that way at acceptable cost, so on Metal each effect is a named
//  fragment function in Shaders.metal and a post-process is built by naming it.
//
//  A pass supplies its parameters as one opaque uniform block bound at fragment
//  buffer index 0. Input textures land on texture slots 0..N in the order given to
//  blit(), matching the OpenGL contract.
//
//  Pipeline states are cached per destination pixel format, because the same effect
//  is blitted into RGBA16Float offscreen targets and into the drawable's own format
//  within a single frame.

#ifndef VROMetalPostProcess_h
#define VROMetalPostProcess_h

#include "VRODefines.h"
#if VRO_METAL

#include "VROImagePostProcess.h"
#include "VROMetalRenderPassHost.h"
#include <Metal/Metal.h>
#include <string>
#include <vector>
#include <map>
#include <memory>

class VROMetalPostProcess : public VROImagePostProcess {
public:

    /*
     fragmentFunction must name a fragment function in the driver's MTLLibrary whose
     signature is (VROPostProcessVertexOut [[stage_in]], constant void *uniforms
     [[buffer(0)]], texture2d<float> ... [[texture(0..N)]]).
     */
    VROMetalPostProcess(id <MTLDevice> device,
                        id <MTLLibrary> library,
                        const std::string &fragmentFunction,
                        VROMetalRenderPassHost *host);
    virtual ~VROMetalPostProcess();

    /*
     Parameters for this effect, uploaded at fragment buffer index 0. Copied, so the
     caller keeps ownership. Safe to call between blits.
     */
    void setUniforms(const void *bytes, size_t length);

    bool isValid() const { return _fragmentFunction != nil; }

    // ── VROImagePostProcess ──────────────────────────────────────────────────

    void setVerticalFlip(bool flip) override { _verticalFlip = flip; }

    void blit(std::vector<std::shared_ptr<VROTexture>> textures,
              std::shared_ptr<VRODriver> &driver) override;
    void begin(std::shared_ptr<VRODriver> &driver) override;
    void blitOpt(std::vector<std::shared_ptr<VROTexture>> textures,
                 std::shared_ptr<VRODriver> &driver) override;
    void end(std::shared_ptr<VRODriver> &driver) override;

private:

    id <MTLRenderPipelineState> pipelineStateForFormat(MTLPixelFormat format);
    void draw(std::vector<std::shared_ptr<VROTexture>> &textures,
              std::shared_ptr<VRODriver> &driver);

    id <MTLDevice>   _device;
    id <MTLLibrary>  _library;
    id <MTLFunction> _vertexFunction;
    id <MTLFunction> _fragmentFunction;
    id <MTLSamplerState> _sampler;
    id <MTLDepthStencilState> _depthStencilState;

    // Keyed by destination colour pixel format.
    std::map<MTLPixelFormat, id <MTLRenderPipelineState>> _pipelineStates;

    VROMetalRenderPassHost *_host;
    std::vector<uint8_t> _uniforms;
    bool _verticalFlip;
};

#endif  // VRO_METAL
#endif  // VROMetalPostProcess_h
