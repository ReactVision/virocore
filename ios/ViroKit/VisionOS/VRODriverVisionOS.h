// VRODriverVisionOS.h
// ViroKit — visionOS
//
// Concrete VRODriver implementation for visionOS / CompositorServices.
//
// Extends VRODriverMetal and provides no-op or minimal implementations for all
// remaining pure-virtual VRODriver methods.  Critical overrides:
//   • getDisplay()        — returns _displayTarget (VRORenderTargetMetal)
//   • bindRenderTarget()  — no-op (encoder managed externally by Swift)
//   • setCullMode()       — forwards to the active MTLRenderCommandEncoder
//   • setBlendingMode()   — no-op (blend state is baked into pipeline objects)
//   • getColorRenderingMode() — NonLinear (avoids HDR path in VROChoreographer)
//   • newRenderTarget()   — returns a stub VRORenderTargetMetal
//   • newTextureSubstrate()  — delegates to VROTextureSubstrateMetal

#ifndef VRODriverVisionOS_h
#define VRODriverVisionOS_h

#include "VRODefines.h"
#if VRO_METAL

#include "VRODriverMetal.h"
#include "VRORenderTargetMetal.h"
#include "VROTextureSubstrateMetal.h"
#include "VROMaterial.h"
#include <memory>

class VROFrameScheduler;

class VRODriverVisionOS : public VRODriverMetal,
                          public std::enable_shared_from_this<VRODriverVisionOS> {
public:

    VRODriverVisionOS(id <MTLDevice> device);
    virtual ~VRODriverVisionOS() {}

    // ── Active encoder ───────────────────────────────────────────────────────
    // Must be called by the Swift render loop before every VRORenderer::renderEye()
    // invocation.  The encoder is also forwarded to the display render target so
    // that setViewport() takes effect immediately.
    void setActiveEncoder(id <MTLRenderCommandEncoder> encoder);

    // ── VRODriver pure-virtual overrides ─────────────────────────────────────

    // Frame / eye lifecycle — no-op stubs
    void willRenderFrame(const VRORenderContext &context) override {}
    void didRenderFrame(const VROFrameTimer &timer, const VRORenderContext &context) override {}
    void willRenderEye(const VRORenderContext &context) override {}
    void didRenderEye(const VRORenderContext &context) override {}
    void pause()  override {}
    void resume() override {}

    // GPU type
    void      readGPUType() override {}
    VROGPUType getGPUType()  override { return VROGPUType::Normal; }

    // Framebuffer read-back — not applicable
    void readDisplayFramebuffer() override {}

    // Texture-unit / shader binding — OpenGL concepts, no-op for Metal
    void setActiveTextureUnit(int) override {}
    void bindTexture(int, int) override {}
    void bindTexture(int, int, int) override {}
    void setDepthWritingEnabled(bool) override {}
    void setDepthReadingEnabled(bool) override {}
    void setStencilTestEnabled(bool) override {}

    // Cull mode — forwarded to the active encoder
    void setCullMode(VROCullMode cullMode) override;

    // Color mask — no-op (would need to be baked into pipeline state; deferred)
    void setRenderTargetColorWritingMask(VROColorMask) override {}
    void setMaterialColorWritingMask(VROColorMask) override {}

    // Shader binding — managed via MTLRenderPipelineState; no-op for Metal
    void bindShader(std::shared_ptr<VROShaderProgram>) override {}
    void unbindShader() override {}

    // Render target management
    bool bindRenderTarget(std::shared_ptr<VRORenderTarget> target,
                          VRORenderTargetUnbindOp unbindOp) override;
    void unbindRenderTarget() override {}
    std::shared_ptr<VRORenderTarget> getRenderTarget() override { return _displayTarget; }

    std::shared_ptr<VRORenderTarget> getDisplay() override { return _displayTarget; }

    std::shared_ptr<VRORenderTarget> newRenderTarget(VRORenderTargetType type,
                                                     int numAttachments,
                                                     int numImages,
                                                     bool enableMipmaps,
                                                     bool needsDepthStencil) override;

    // Color rendering — NonLinear avoids the HDR / bloom paths in VROChoreographer
    VROColorRenderingMode getColorRenderingMode() override {
        return VROColorRenderingMode::NonLinear;
    }
    void setHasSoftwareGammaPass(bool) override {}
    bool hasSoftwareGammaPass() const   override { return false; }
    bool isBloomSupported()     override { return false; }

    // Blend mode — baked per-pipeline in VROGeometrySubstrateMetal; no-op here
    void setBlendingMode(VROBlendMode) override {}

    // Texture substrate — bridge to VROTextureSubstrateMetal
    VROTextureSubstrate *newTextureSubstrate(VROTextureType type,
                                             VROTextureFormat format,
                                             VROTextureInternalFormat internalFormat,
                                             bool sRGB,
                                             VROMipmapMode mipmapMode,
                                             std::vector<std::shared_ptr<VROData>> &data,
                                             int width, int height,
                                             std::vector<uint32_t> mipSizes,
                                             VROWrapMode wrapS, VROWrapMode wrapT,
                                             VROFilterMode minFilter,
                                             VROFilterMode magFilter,
                                             VROFilterMode mipFilter) override;

    // Vertex buffer — Metal geometry substrate uses MTLBuffer directly; return null
    std::shared_ptr<VROVertexBuffer> newVertexBuffer(std::shared_ptr<VROData>) override {
        return nullptr;
    }

    // Image post-process — not needed for basic scenes; return null stub
    std::shared_ptr<VROImagePostProcess> newImagePostProcess(std::shared_ptr<VROShaderProgram>) override {
        return nullptr;
    }

    // Sound — not supported; return null
    std::shared_ptr<VROSound> newSound(std::shared_ptr<VROSoundData>, VROSoundType) override {
        return nullptr;
    }
    std::shared_ptr<VROAudioPlayer> newAudioPlayer(std::shared_ptr<VROSoundData>) override {
        return nullptr;
    }
    void setSoundRoom(float, float, float, std::string, std::string, std::string) override {}

    // Typeface — not yet ported; pabort for debugging
    std::shared_ptr<VROTypefaceCollection> newTypefaceCollection(std::string, int,
                                                                  VROFontStyle, VROFontWeight) override;

    // Frame scheduler — not needed for visionOS POC
    std::shared_ptr<VROFrameScheduler> getFrameScheduler() override { return nullptr; }

    // Raw graphics context — not applicable
    void *getGraphicsContext() override { return nullptr; }

private:
    std::shared_ptr<VRORenderTargetMetal> _displayTarget;
};

#endif  // VRO_METAL
#endif  // VRODriverVisionOS_h
