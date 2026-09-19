//
//  VRORenderTargetMetal.h
//  ViroKit — visionOS
//
//  Copyright © 2026 ReactVision. All rights reserved.
//
//  VRORenderTarget for Metal / CompositorServices.
//
//  Two shapes share this class:
//
//  • The *display* target, returned by VRODriverVisionOS::getDisplay(). Its encoder
//    is created by the Swift render loop (ViroImmersiveRenderer) from the
//    CompositorServices drawable and pushed in via setEncoder(). This target owns
//    no textures.
//
//  • *Offscreen* targets, returned by VRODriverVisionOS::newRenderTarget(). These
//    own their colour / depth MTLTextures and build their own
//    MTLRenderPassDescriptor. bind() ends whatever encoder is in flight and begins
//    a new one against those textures, so the choreographer's render-to-texture
//    passes (bloom, shadows, tone mapping, IBL) work the way they do on OpenGL.
//
//  Clearing works differently than in OpenGL and the difference leaks into the API.
//  Metal clears as part of beginning a pass (loadAction = Clear), not as a command
//  inside one, so clearColor() / clearDepth() / clearStencil() record intent and the
//  next bind() consumes it. A clear requested *after* bind() cannot be honoured
//  without ending and restarting the pass, which would discard what was drawn — so
//  those calls are recorded for the following bind, matching how the choreographer
//  actually sequences them (clear, then bind, then draw).

#ifndef VRORenderTargetMetal_h
#define VRORenderTargetMetal_h

#include "VRODefines.h"
#if VRO_METAL

#include "VRORenderTarget.h"
#include "VROViewport.h"
#include "VROMetalRenderPassHost.h"
#include <Metal/Metal.h>
#include <vector>
#include <memory>

class VROTexture;

class VRORenderTargetMetal : public VRORenderTarget {
public:

    /*
     Display target. Owns no textures; its encoder arrives from the Swift render
     loop via setEncoder().
     */
    VRORenderTargetMetal();

    /*
     Offscreen target. Allocates its own textures on hydrate().
     */
    VRORenderTargetMetal(VRORenderTargetType type,
                         int numAttachments,
                         int numImages,
                         bool enableMipmaps,
                         bool needsDepthStencil,
                         id <MTLDevice> device,
                         VROMetalRenderPassHost *host);

    virtual ~VRORenderTargetMetal();

    // ── Display-target encoder plumbing ──────────────────────────────────────

    /*
     Hand the display target the render pass describing this eye's drawable
     textures. The target opens its own encoder from the frame's command buffer
     when bind() is called, which is what lets an offscreen pass interleave: Metal
     allows one encoder per command buffer, so a detour has to end the display
     encoder and reopen it afterwards with a Load action.
     */
    void setDisplayPass(MTLRenderPassDescriptor *descriptor);

    /*
     Finish this eye. Ends the display encoder if one is open and forgets the
     descriptor, so the next eye starts with a Clear.
     */
    void endDisplayPass();

    id <MTLRenderCommandEncoder> getEncoder() const { return _encoder; }

    /*
     The display target is constructed before the driver finishes building, so its
     host is attached afterwards rather than passed to the constructor.
     */
    void setRenderPassHost(VROMetalRenderPassHost *host) { _host = host; }

    /*
     The colour texture backing attachment 0, or nil for the display target.
     Used by the Metal post-process to sample a target it did not create.
     */
    id <MTLTexture> getMetalTexture(int attachment) const;
    id <MTLTexture> getMetalDepthTexture() const { return _depthTexture; }

    /*
     How many colour attachments this target actually has. A pipeline rendering into it
     must declare exactly this many, and the lighting fragment functions are specialised
     against it.
     */
    int getColorAttachmentCount() const { return (int)_colorTextures.size(); }

    bool isDisplay() const { return _type == VRORenderTargetType::Display; }

    /*
     True when the face / slice / mip this target writes to has changed since it was last
     bound, so the driver knows a rebind is needed even though the target itself is
     already the bound one. Rendering the six faces of a cubemap is exactly that case.
     */
    bool needsRebind() const { return _attachmentSelectionDirty; }

    // ── VRORenderTarget ──────────────────────────────────────────────────────

    bool setViewport(VROViewport viewport) override;
    bool hydrate() override;
    int  getWidth()  const override { return _width;  }
    int  getHeight() const override { return _height; }

    void bind()     override;
    void bindRead() override;
    void invalidate() override;

    void blitColor(std::shared_ptr<VRORenderTarget> destination, bool flipY,
                   std::shared_ptr<VRODriver> driver) override;
    void blitStencil(std::shared_ptr<VRORenderTarget> destination, bool flipY,
                     std::shared_ptr<VRODriver> driver) override;
    void blitDepth(std::shared_ptr<VRORenderTarget> destination) override;

    void deleteFramebuffers()  override;
    bool restoreFramebuffers() override;

    bool hasTextureAttached(int attachment) override;
    void clearTextures() override;
    bool attachNewTextures() override;
    void setTextureImageIndex(int index, int attachment) override;
    void setTextureCubeFace(int face, int mipLevel, int attachmentIndex) override;
    void setMipLevel(int mipLevel, int attachmentIndex) override;
    void attachTexture(std::shared_ptr<VROTexture> texture, int attachment) override;
    const std::shared_ptr<VROTexture> getTexture(int attachment) const override;

    void clearStencil() override;
    void clearDepth()   override;
    void clearColor()   override;
    void clearDepthAndColor() override;

    void enablePortalStencilWriting(VROFace face) override;
    void enablePortalStencilRemoval(VROFace face) override;
    void disablePortalStencilWriting(VROFace face) override;
    void setPortalStencilPassFunction(VROFace face, VROStencilFunc func, int ref) override;

    /*
     Stencil state the portal passes asked for, read by VROGeometrySubstrateMetal
     when it builds a depth-stencil state. Metal bakes stencil operations into
     MTLDepthStencilState rather than setting them as encoder state, so the target
     records what was requested and the substrate applies it at pipeline time.
     */
    struct StencilState {
        bool writing        = false;   // write _stencilRef into the buffer
        bool removal        = false;   // write 0 (portal removal pass)
        MTLCompareFunction compareFunc = MTLCompareFunctionAlways;
        int  reference      = 0;
    };
    const StencilState &getStencilState() const { return _stencil; }

private:

    MTLPixelFormat colorPixelFormatForType() const;
    bool           typeHasColor() const;
    bool           typeHasDepth() const;
    MTLTextureType metalTextureType() const;
    void           rebuildPassDescriptor();
    void           releaseTextures();

    VROMetalRenderPassHost *_host;
    id <MTLDevice> _device;

    // Display target only: encoder supplied from outside.
    id <MTLRenderCommandEncoder> _encoder;

    // Offscreen targets.
    std::vector<id <MTLTexture>> _colorTextures;
    id <MTLTexture> _depthTexture;
    std::vector<std::shared_ptr<VROTexture>> _textureWrappers;
    MTLRenderPassDescriptor *_passDescriptor;

    int  _numImages;
    bool _enableMipmaps;
    bool _needsDepthStencil;
    bool _hydrated;

    // Pending clears, consumed by the next bind().
    bool _clearColorPending;
    bool _clearDepthPending;
    bool _clearStencilPending;

    // Which slice / face / mip the next pass writes to.
    int _imageIndex;
    int _cubeFace;
    int _mipLevel;
    bool _attachmentSelectionDirty = false;

    bool _invalidated;

    // Display target: has this eye's pass been opened once already? A reopen after
    // an offscreen detour must Load rather than Clear.
    bool _displayPassStarted;

    MTLViewport _metalViewport;
    bool _viewportSet;
    int  _width, _height;

    StencilState _stencil;
};

#endif  // VRO_METAL
#endif  // VRORenderTargetMetal_h
