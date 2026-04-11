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
}

// ── Active encoder ────────────────────────────────────────────────────────────

void VRODriverVisionOS::setActiveEncoder(id <MTLRenderCommandEncoder> encoder) {
    VRODriverMetal::setActiveEncoder(encoder);
    _displayTarget->setEncoder(encoder);
}

// ── Render target management ──────────────────────────────────────────────────

bool VRODriverVisionOS::bindRenderTarget(std::shared_ptr<VRORenderTarget> target,
                                          VRORenderTargetUnbindOp unbindOp) {
    // The encoder is created externally by the Swift render loop.
    // We don't create new encoders here — just track the active target.
    // Return false to indicate the target was already "bound" (no state change).
    return false;
}

std::shared_ptr<VRORenderTarget> VRODriverVisionOS::newRenderTarget(
    VRORenderTargetType type,
    int numAttachments,
    int numImages,
    bool enableMipmaps,
    bool needsDepthStencil)
{
    // Return a stub render target.  With HDR and MRT disabled, these targets
    // are created by the choreographer but never used for actual rendering.
    return std::make_shared<VRORenderTargetMetal>();
}

// ── Cull mode ─────────────────────────────────────────────────────────────────

void VRODriverVisionOS::setCullMode(VROCullMode cullMode) {
    id <MTLRenderCommandEncoder> encoder = getActiveEncoder();
    if (!encoder) { return; }
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
