// VRORenderTargetMetal.h
// ViroKit — visionOS
//
// Minimal VRORenderTarget implementation for Metal / CompositorServices.
//
// In the visionOS rendering path the platform (ViroImmersiveRenderer.swift) is
// responsible for creating MTLRenderCommandEncoder objects — one per eye — and
// handing them to VRODriverVisionOS via setActiveEncoder().  This class just
// satisfies the VRORenderTarget interface so VROChoreographer (which creates and
// tracks a few render targets even when HDR is off) does not crash.
//
// The "display" target (returned by VRODriverVisionOS::getDisplay()) also tracks
// the current encoder and forwards setViewport() to it so that the choreographer
// can set the Metal viewport in the normal way.

#ifndef VRORenderTargetMetal_h
#define VRORenderTargetMetal_h

#include "VRODefines.h"
#if VRO_METAL

#include "VRORenderTarget.h"
#include "VROViewport.h"
#include <Metal/Metal.h>

class VRORenderTargetMetal : public VRORenderTarget {
public:

    VRORenderTargetMetal()
        : VRORenderTarget(VRORenderTargetType::Display, 1)
        , _encoder(nil)
        , _width(1), _height(1) {}

    virtual ~VRORenderTargetMetal() {}

    // ── Encoder plumbing ──────────────────────────────────────────────────────
    void setEncoder(id <MTLRenderCommandEncoder> encoder) {
        _encoder = encoder;
        // Re-apply stored viewport to the new encoder if we have one.
        if (_encoder && _viewportSet) {
            [_encoder setViewport:_metalViewport];
        }
    }
    id <MTLRenderCommandEncoder> getEncoder() const { return _encoder; }

    // ── VRORenderTarget interface ─────────────────────────────────────────────

    bool setViewport(VROViewport viewport) override {
        _width  = viewport.getWidth();
        _height = viewport.getHeight();
        _metalViewport = { (double)viewport.getX(), (double)viewport.getY(),
                           (double)_width, (double)_height, 0.0, 1.0 };
        _viewportSet = true;
        if (_encoder) {
            [_encoder setViewport:_metalViewport];
        }
        return true;
    }

    bool   hydrate()    override { return true; }
    int    getWidth()   const override { return _width; }
    int    getHeight()  const override { return _height; }
    void   bind()       override {}
    void   bindRead()   override {}
    void   invalidate() override {}

    void blitColor(std::shared_ptr<VRORenderTarget>, bool, std::shared_ptr<VRODriver>) override {}
    void blitStencil(std::shared_ptr<VRORenderTarget>, bool, std::shared_ptr<VRODriver>) override {}
    void blitDepth(std::shared_ptr<VRORenderTarget>) override {}
    void deleteFramebuffers()  override {}
    bool restoreFramebuffers() override { return true; }

    bool hasTextureAttached(int)  override { return false; }
    void clearTextures()          override {}
    bool attachNewTextures()      override { return true; }
    void setTextureImageIndex(int, int) override {}
    void setTextureCubeFace(int, int, int) override {}
    void setMipLevel(int, int) override {}
    void attachTexture(std::shared_ptr<VROTexture>, int) override {}
    const std::shared_ptr<VROTexture> getTexture(int) const override { return nullptr; }

    void clearStencil()       override {}
    void clearDepth()         override {}
    void clearColor()         override {}
    void clearDepthAndColor() override {}

    void enablePortalStencilWriting(VROFace) override {}
    void enablePortalStencilRemoval(VROFace) override {}
    void disablePortalStencilWriting(VROFace) override {}
    void setPortalStencilPassFunction(VROFace, VROStencilFunc, int) override {}

private:
    id <MTLRenderCommandEncoder> _encoder;
    MTLViewport _metalViewport = {};
    bool _viewportSet = false;
    int _width, _height;
};

#endif  // VRO_METAL
#endif  // VRORenderTargetMetal_h
