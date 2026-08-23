//
//  VROMetalPostProcess.mm
//  ViroKit — visionOS
//
//  Copyright © 2026 ReactVision. All rights reserved.
//

#include "VROMetalPostProcess.h"
#if VRO_METAL

#include "VRODriverMetal.h"
#include "VRORenderTargetMetal.h"
#include "VROTexture.h"
#include "VROTextureSubstrateMetal.h"
#include "VROLog.h"

static const int kVROPostProcessMaxTextures = 8;

VROMetalPostProcess::VROMetalPostProcess(id <MTLDevice> device,
                                         id <MTLLibrary> library,
                                         const std::string &fragmentFunction,
                                         VROMetalRenderPassHost *host)
    : _device(device)
    , _library(library)
    , _vertexFunction(nil)
    , _fragmentFunction(nil)
    , _sampler(nil)
    , _depthStencilState(nil)
    , _host(host)
    , _verticalFlip(false) {

    if (!_library) {
        pinfo("VROMetalPostProcess: no Metal library — cannot build '%s'", fragmentFunction.c_str());
        return;
    }
    _vertexFunction   = [_library newFunctionWithName:@"post_process_vertex"];
    _fragmentFunction = [_library newFunctionWithName:
                            [NSString stringWithUTF8String:fragmentFunction.c_str()]];
    if (!_vertexFunction || !_fragmentFunction) {
        pinfo("VROMetalPostProcess: missing shader function (vertex=%s fragment=%s '%s')",
              _vertexFunction ? "ok" : "nil",
              _fragmentFunction ? "ok" : "nil",
              fragmentFunction.c_str());
        return;
    }

    // A post-process covers the whole target and must neither test nor write depth.
    MTLDepthStencilDescriptor *depthDescriptor = [MTLDepthStencilDescriptor new];
    depthDescriptor.depthCompareFunction = MTLCompareFunctionAlways;
    depthDescriptor.depthWriteEnabled    = NO;
    _depthStencilState = [_device newDepthStencilStateWithDescriptor:depthDescriptor];
    [depthDescriptor release];
}

VROMetalPostProcess::~VROMetalPostProcess() {
    for (auto &entry : _pipelineStates) {
        [entry.second release];
    }
    _pipelineStates.clear();
    [_vertexFunction release];
    [_fragmentFunction release];
    [_depthStencilState release];
    [_sampler release];
}

void VROMetalPostProcess::setUniforms(const void *bytes, size_t length) {
    if (!bytes || length == 0) {
        _uniforms.clear();
        return;
    }
    _uniforms.assign((const uint8_t *)bytes, (const uint8_t *)bytes + length);
}

// ── Pipeline states ──────────────────────────────────────────────────────────

id <MTLRenderPipelineState> VROMetalPostProcess::pipelineStateForFormat(MTLPixelFormat format) {
    auto it = _pipelineStates.find(format);
    if (it != _pipelineStates.end()) {
        return it->second;
    }

    MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.vertexFunction   = _vertexFunction;
    descriptor.fragmentFunction = _fragmentFunction;
    descriptor.colorAttachments[0].pixelFormat = format;
    // The destination is fully overwritten, so no blending is needed. The OpenGL path
    // additively blends bloom back in via a blend mode; here post_additive_blend reads
    // both inputs and does the add in-shader instead, which keeps this state simple.
    descriptor.colorAttachments[0].blendingEnabled = NO;
    descriptor.sampleCount = 1;

    NSError *error = nil;
    id <MTLRenderPipelineState> state =
        [_device newRenderPipelineStateWithDescriptor:descriptor error:&error];
    [descriptor release];

    if (!state) {
        pinfo("VROMetalPostProcess: pipeline state failed for format %lu: %s",
              (unsigned long)format,
              error ? [[error localizedDescription] UTF8String] : "unknown");
        return nil;
    }
    _pipelineStates[format] = state;
    return state;
}

// ── Draw ─────────────────────────────────────────────────────────────────────

void VROMetalPostProcess::draw(std::vector<std::shared_ptr<VROTexture>> &textures,
                               std::shared_ptr<VRODriver> &driver) {
    if (!isValid()) {
        return;
    }
    VRODriverMetal *metal = dynamic_cast<VRODriverMetal *>(driver.get());
    if (!metal) {
        return;
    }
    id <MTLRenderCommandEncoder> encoder = metal->getActiveEncoder();
    if (!encoder) {
        pinfo("VROMetalPostProcess: blit with no active encoder — is a render target bound?");
        return;
    }

    // The pipeline's colour format must match whatever is currently bound. Within one
    // frame the same effect can be blitted into an RGBA16Float offscreen target and
    // into the drawable's own format, hence the per-format cache.
    MTLPixelFormat format = metal->getColorPixelFormat();
    std::shared_ptr<VRORenderTargetMetal> target =
        std::dynamic_pointer_cast<VRORenderTargetMetal>(driver->getRenderTarget());
    if (target && !target->isDisplay()) {
        id <MTLTexture> colorTexture = target->getMetalTexture(0);
        if (colorTexture) {
            format = colorTexture.pixelFormat;
        }
    }

    id <MTLRenderPipelineState> state = pipelineStateForFormat(format);
    if (!state) {
        return;
    }

    [encoder setRenderPipelineState:state];
    [encoder setDepthStencilState:_depthStencilState];
    [encoder setCullMode:MTLCullModeNone];

    const float flipY = _verticalFlip ? 1.0f : 0.0f;
    [encoder setVertexBytes:&flipY length:sizeof(flipY) atIndex:0];

    if (!_uniforms.empty()) {
        [encoder setFragmentBytes:_uniforms.data() length:_uniforms.size() atIndex:0];
    }

    const int count = (int)textures.size() < kVROPostProcessMaxTextures
                          ? (int)textures.size() : kVROPostProcessMaxTextures;
    for (int i = 0; i < count; i++) {
        if (!textures[i]) {
            continue;
        }
        VROTextureSubstrate *substrate = textures[i]->getSubstrate(0, driver, true);
        VROTextureSubstrateMetal *metalSubstrate =
            dynamic_cast<VROTextureSubstrateMetal *>(substrate);
        if (!metalSubstrate) {
            continue;
        }
        [encoder setFragmentTexture:metalSubstrate->getTexture() atIndex:i];
    }

    // Full-screen quad straight from vertex_id; no vertex buffer.
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
}

// ── VROImagePostProcess ──────────────────────────────────────────────────────

void VROMetalPostProcess::blit(std::vector<std::shared_ptr<VROTexture>> textures,
                               std::shared_ptr<VRODriver> &driver) {
    draw(textures, driver);
}

void VROMetalPostProcess::begin(std::shared_ptr<VRODriver> &driver) {
    // Metal has no shader/VAO binding to hoist out of the loop: pipeline state and
    // bindings are set per encoder, and an encoder may change between blitOpt calls.
    // begin/end therefore have nothing to do, and blitOpt is the same as blit.
}

void VROMetalPostProcess::blitOpt(std::vector<std::shared_ptr<VROTexture>> textures,
                                  std::shared_ptr<VRODriver> &driver) {
    draw(textures, driver);
}

void VROMetalPostProcess::end(std::shared_ptr<VRODriver> &driver) {
}

#endif  // VRO_METAL
