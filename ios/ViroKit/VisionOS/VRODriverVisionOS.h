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
#include "VROMetalRenderPassHost.h"
#include "VROMetalFrameTimer.h"
#include "VRORenderTargetMetal.h"
#include "VROTextureSubstrateMetal.h"
#include "VROMaterial.h"
#include "VROVertexBuffer.h"
#include "VROFrameScheduler.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <memory>
#include <vector>
#include <map>
#include <string>
#include <cstdint>

// CPU-only vertex buffer: holds VROData for CPU-side operations (e.g. processTangent).
// hydrate() is a no-op because VROGeometrySubstrateMetal creates its own MTLBuffer.
class VROTypeface;

class VROVertexBufferCPU final : public VROVertexBuffer {
public:
    explicit VROVertexBufferCPU(std::shared_ptr<VROData> data) : VROVertexBuffer(data) {}
    void hydrate() override {}
};

class VRODriverVisionOS : public VRODriverMetal,
                          public VROMetalRenderPassHost,
                          public std::enable_shared_from_this<VRODriverVisionOS> {
public:

    VRODriverVisionOS(id <MTLDevice> device);
    virtual ~VRODriverVisionOS() {
        if (_freetype != nullptr) {
            FT_Done_FreeType(_freetype);
            _freetype = nullptr;
        }
    }

    // ── Frame plumbing ───────────────────────────────────────────────────────
    // The render loop supplies the frame's command buffer before rendering any eye.
    // Every render command encoder — the display pass included — is opened from it
    // by the render target being bound, because Metal permits only one encoder per
    // command buffer at a time and an offscreen pass has to interleave with the
    // display pass.
    void setFrameCommandBuffer(id <MTLCommandBuffer> commandBuffer);

    // Hand the display target this eye's render pass, then end it once the eye is
    // done. beginDisplayPass must precede VRORenderer::renderEye().
    void beginDisplayPass(MTLRenderPassDescriptor *descriptor);
    void endDisplayPass();

    /*
     Install a default scene-lighting block as the fallback at buffer index 4.
     Shaders read that slot before a material binds its own lights (Constant shaders,
     early-exit paths), and an unbound buffer read is undefined.

     This lives here rather than in the bridge because the block must match
     VROSceneLightingUniforms exactly — the bridge used to keep a hand-written copy of
     that struct and it silently went stale the moment shadow fields were added.
     */
    void installDefaultLightingFallback(float ambient,
                                        float directionX, float directionY, float directionZ);

    // Bytes bound at the given buffer index on every encoder this driver opens,
    // as a fallback for shaders that read a slot before the material binds it
    // (Constant shaders, early-exit paths). Re-applied per encoder because Metal
    // buffer bindings do not survive the end of a render pass. Pass length 0 to
    // clear.
    void setFallbackUniformBytes(int bufferIndex, const void *bytes, size_t length);

    // ── VROMetalRenderPassHost ───────────────────────────────────────────────
    std::shared_ptr<VRODriver> getRenderPassDriver() override;
    id <MTLCommandBuffer> getFrameCommandBuffer() override { return _frameCommandBuffer; }
    void onRenderTargetEncoderBegan(id <MTLRenderCommandEncoder> encoder) override;
    void endActiveEncoder() override;

    /// True while a render command encoder is open on the frame's command buffer.
    ///
    /// CompositorServices aborts the process from cp_drawable_encode_present if the command
    /// buffer it is handed still has one, so the immersive render loop checks this before
    /// presenting rather than finding out as __BUG_IN_CLIENT__.
    bool hasOpenEncoder() const { return _openEncoder != nil; }
    VROMetalFrameTimer *getFrameTimer() override { return _frameTimer.get(); }

    /*
     Turn frame timing on. Off by default: it costs a counter sample buffer and a completion
     handler per frame, which is not something to carry in a shipping build.
     */
    void setFrameTimingEnabled(bool enabled);
    VROMetalFrameTimer *getFrameTimerForBridge() { return _frameTimer.get(); }

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
    void unbindRenderTarget() override;
    std::shared_ptr<VRORenderTarget> getRenderTarget() override {
        return _boundTarget ? _boundTarget : _displayTarget;
    }

    std::shared_ptr<VRORenderTarget> getDisplay() override { return _displayTarget; }

    std::shared_ptr<VRORenderTarget> newRenderTarget(VRORenderTargetType type,
                                                     int numAttachments,
                                                     int numImages,
                                                     bool enableMipmaps,
                                                     bool needsDepthStencil) override;

    // CompositorServices hands us a linear float drawable and composites in linear
    // space, so colour work happens in linear RGB with no software gamma pass. This is
    // also what gates HDR: VROChoreographer disables it outright while the driver
    // reports NonLinear.
    VROColorRenderingMode getColorRenderingMode() override {
        return VROColorRenderingMode::Linear;
    }
    void setHasSoftwareGammaPass(bool) override {}
    bool hasSoftwareGammaPass() const   override { return false; }

    // Bloom: the lighting shaders write the bloom buffer to a third colour attachment,
    // VROGaussianBlurRenderPass has a Metal implementation, and the additive blend the
    // choreographer asks for is resolved to post_additive_blend by newImagePostProcess.
    bool isBloomSupported()     override { return true; }

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

    // CPU-backed vertex buffer for processTangent and VROGeometrySubstrateMetal.
    // The Metal substrate reads getData() to create its own MTLBuffer; hydrate() is a no-op.
    std::shared_ptr<VROVertexBuffer> newVertexBuffer(std::shared_ptr<VROData> data) override {
        return std::make_shared<VROVertexBufferCPU>(data);
    }

    /*
     Shared render passes build a full-screen effect by handing over a GLSL
     VROShaderProgram. On visionOS VROImageShaderProgram::create resolves the effect to a
     named MSL function and stores it as the program's name, which this reads.
     */
    std::shared_ptr<VROImagePostProcess> newImagePostProcess(std::shared_ptr<VROShaderProgram> program) override;

    /*
     A full-screen post-process running the named fragment function from Shaders.metal.
     Returns null if the function is missing.
     */
    std::shared_ptr<VROImagePostProcess> newMetalPostProcess(const std::string &fragmentFunction);

    // Sound — not supported; return null
    std::shared_ptr<VROSound> newSound(std::shared_ptr<VROSoundData>, VROSoundType) override {
        return nullptr;
    }
    std::shared_ptr<VROAudioPlayer> newAudioPlayer(std::shared_ptr<VROSoundData>) override {
        return nullptr;
    }
    void setSoundRoom(float, float, float, std::string, std::string, std::string) override {}

    /*
     Text. freetype is built from source for xros (ios/build_freetype_visionos.sh) and the
     library handle lives here, mirroring VRODriverOpenGLiOS. VROTypefaceiOS is reused as
     is: its CoreText font lookup works on visionOS, and only the glyph implementation and
     this handle differ from the iOS path.
     */
    std::shared_ptr<VROTypefaceCollection> newTypefaceCollection(std::string typefaceNames, int size,
                                                                  VROFontStyle style,
                                                                  VROFontWeight weight) override;

    FT_Library getFreetype() {
        return _freetype;
    }

    std::shared_ptr<VROFrameScheduler> getFrameScheduler() override { return _frameScheduler; }

    // Raw graphics context — not applicable
    void *getGraphicsContext() override { return nullptr; }

private:
    std::shared_ptr<VRORenderTargetMetal> _displayTarget;
    std::shared_ptr<VRORenderTarget> _boundTarget;
    std::shared_ptr<VROFrameScheduler> _frameScheduler = std::make_shared<VROFrameScheduler>();

    // The frame's command buffer, owned by the Swift render loop.
    id <MTLCommandBuffer> _frameCommandBuffer = nil;

    // The encoder currently open on _frameCommandBuffer, which must be ended
    // before any target can open another.
    id <MTLRenderCommandEncoder> _openEncoder = nil;

    std::unique_ptr<VROMetalFrameTimer> _frameTimer;

    // freetype, initialised on first use and torn down with the driver.
    FT_Library _freetype = nullptr;

    // Typefaces are cached by name+size+style+weight so repeated text nodes share a face.
    std::map<std::string, std::weak_ptr<VROTypeface>> _typefaces;

    // Fallback uniform bytes re-bound on every new encoder.
    std::vector<uint8_t> _fallbackUniformBytes;
    int _fallbackUniformIndex = -1;

    // Library used for post-process shaders. Normally the driver's precompiled
    // default.metallib; compiled from the bundled shader source when the host app
    // ships no metallib of its own.
    id <MTLLibrary> postProcessLibrary();
    id <MTLLibrary> _postProcessLibrary = nil;
};

#endif  // VRO_METAL
#endif  // VRODriverVisionOS_h
