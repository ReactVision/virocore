// VRODriverVisionOS.mm
// ViroKit — visionOS

#include "VRODriverVisionOS.h"
#if VRO_METAL

#include "VRORenderTargetMetal.h"
#include "VROTextureSubstrateMetal.h"
#include "VROLog.h"
#include "VROData.h"

// ── Constructor ──────────────────────────────────────────────────────────────

VRODriverVisionOS::VRODriverVisionOS(id <MTLDevice> device)
    : VRODriverMetal(device)
{
    // CompositorServices pixel formats (matching ViroImmersiveSpace.swift LayerConfiguration)
    setColorPixelFormat(MTLPixelFormatBGRA8Unorm_sRGB);
    setDepthPixelFormat(MTLPixelFormatDepth32Float);
    setSampleCount(1);

    _displayTarget = std::make_shared<VRORenderTargetMetal>();
    _displayTarget->setRenderPassHost(this);
}

// ── Active encoder ────────────────────────────────────────────────────────────

void VRODriverVisionOS::setFrameCommandBuffer(id <MTLCommandBuffer> commandBuffer) {
    _frameCommandBuffer = commandBuffer;
}

void VRODriverVisionOS::beginDisplayPass(MTLRenderPassDescriptor *descriptor) {
    _displayTarget->setDisplayPass(descriptor);
    // Nothing is bound until the base render pass binds its output target, so a
    // stale binding from the previous eye must not suppress that bind().
    _boundTarget = nullptr;
}

void VRODriverVisionOS::endDisplayPass() {
    _displayTarget->endDisplayPass();
    _boundTarget = nullptr;
}

// ── VROMetalRenderPassHost ────────────────────────────────────────────────────

std::shared_ptr<VRODriver> VRODriverVisionOS::getRenderPassDriver() {
    return std::static_pointer_cast<VRODriver>(shared_from_this());
}

void VRODriverVisionOS::setFallbackUniformBytes(int bufferIndex, const void *bytes, size_t length) {
    if (!bytes || length == 0) {
        _fallbackUniformIndex = -1;
        _fallbackUniformBytes.clear();
        return;
    }
    _fallbackUniformIndex = bufferIndex;
    _fallbackUniformBytes.assign((const uint8_t *)bytes, (const uint8_t *)bytes + length);
}

void VRODriverVisionOS::onRenderTargetEncoderBegan(id <MTLRenderCommandEncoder> encoder) {
    _openEncoder = encoder;
    VRODriverMetal::setActiveEncoder(encoder);

    if (encoder && _fallbackUniformIndex >= 0 && !_fallbackUniformBytes.empty()) {
        [encoder setVertexBytes:_fallbackUniformBytes.data()
                         length:_fallbackUniformBytes.size()
                        atIndex:_fallbackUniformIndex];
        [encoder setFragmentBytes:_fallbackUniformBytes.data()
                           length:_fallbackUniformBytes.size()
                          atIndex:_fallbackUniformIndex];
    }
}

void VRODriverVisionOS::endActiveEncoder() {
    if (!_openEncoder) {
        return;
    }
    [_openEncoder endEncoding];
    _openEncoder = nil;
    VRODriverMetal::setActiveEncoder(nil);
}

// ── Render target management ──────────────────────────────────────────────────

bool VRODriverVisionOS::bindRenderTarget(std::shared_ptr<VRORenderTarget> target,
                                          VRORenderTargetUnbindOp unbindOp) {
    if (!target) {
        return false;
    }
    if (unbindOp == VRORenderTargetUnbindOp::Invalidate && _boundTarget) {
        _boundTarget->invalidate();
    }
    if (_boundTarget == target) {
        return false;
    }
    target->bind();
    _boundTarget = target;
    return true;
}

void VRODriverVisionOS::unbindRenderTarget() {
    endActiveEncoder();
    _boundTarget = nullptr;
}

std::shared_ptr<VRORenderTarget> VRODriverVisionOS::newRenderTarget(
    VRORenderTargetType type,
    int numAttachments,
    int numImages,
    bool enableMipmaps,
    bool needsDepthStencil)
{
    return std::make_shared<VRORenderTargetMetal>(type, numAttachments, numImages,
                                                  enableMipmaps, needsDepthStencil,
                                                  getDevice(), this);
}

// ── Cull mode ─────────────────────────────────────────────────────────────────

void VRODriverVisionOS::setCullMode(VROCullMode cullMode) {
    id <MTLRenderCommandEncoder> encoder = getActiveEncoder();
    if (!encoder) { return; }
    // GLTF (and OpenGL) define CCW as the front face; Metal's default is CW.
    // Set CCW explicitly so backface culling removes the correct faces.
    [encoder setFrontFacingWinding:MTLWindingCounterClockwise];
    switch (cullMode) {
        case VROCullMode::None:  [encoder setCullMode:MTLCullModeNone];  break;
        case VROCullMode::Back:  [encoder setCullMode:MTLCullModeBack];  break;
        case VROCullMode::Front: [encoder setCullMode:MTLCullModeFront]; break;
    }
}

// ── Texture substrate ─────────────────────────────────────────────────────────

VROTextureSubstrate *VRODriverVisionOS::newTextureSubstrate(
    VROTextureType type,
    VROTextureFormat format,
    VROTextureInternalFormat internalFormat,
    bool sRGB,
    VROMipmapMode mipmapMode,
    std::vector<std::shared_ptr<VROData>> &data,
    int width, int height,
    std::vector<uint32_t> mipSizes,
    VROWrapMode wrapS, VROWrapMode wrapT,
    VROFilterMode minFilter, VROFilterMode magFilter, VROFilterMode mipFilter)
{
    if (data.empty() || !data[0]) {
        pinfo("VRODriverVisionOS: newTextureSubstrate called with no data");
        return nullptr;
    }
    // Use the first mip level.  VROTextureSubstrateMetal will upload the raw
    // bytes to a Metal texture with the specified dimensions.
    // shared_from_this() returns shared_ptr<VRODriverVisionOS>; upcast to VRODriver
    // before passing so the lvalue reference binds correctly.
    std::shared_ptr<VRODriver> driverPtr = std::static_pointer_cast<VRODriver>(shared_from_this());
    return new VROTextureSubstrateMetal(type, format, data[0], width, height, driverPtr);
}

// ── Typeface ──────────────────────────────────────────────────────────────────

std::shared_ptr<VROTypefaceCollection> VRODriverVisionOS::newTypefaceCollection(
    std::string typefaces, int size, VROFontStyle style, VROFontWeight weight)
{
    // Text rendering is not yet ported to Metal — Week 3 task.
    pabort("VRODriverVisionOS: newTypefaceCollection not yet implemented for visionOS");
}

#endif  // VRO_METAL
