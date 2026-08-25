//
//  VRORenderTargetMetal.mm
//  ViroKit — visionOS
//
//  Copyright © 2026 ReactVision. All rights reserved.
//

#include "VRORenderTargetMetal.h"
#if VRO_METAL

#include "VROTexture.h"
#include "VROTextureSubstrateMetal.h"
#include "VROLog.h"
#include "VROMaterial.h"
#include "VROMetalFrameTimer.h"

//  Ownership: this target owns the MTLTextures it allocates. The VROTexture
//  wrappers handed out by getTexture() only reference them — VROTextureSubstrateMetal
//  deliberately does not release its texture — so releaseTextures() drops the
//  wrappers at the same time it releases the textures. This mirrors the OpenGL
//  target, where clearTextures() / deleteFramebuffers() likewise invalidate any
//  texture a caller is still holding.

VRORenderTargetMetal::VRORenderTargetMetal()
    : VRORenderTarget(VRORenderTargetType::Display, 1)
    , _host(nullptr)
    , _device(nil)
    , _encoder(nil)
    , _depthTexture(nil)
    , _passDescriptor(nil)
    , _numImages(1)
    , _enableMipmaps(false)
    , _needsDepthStencil(true)
    , _hydrated(true)
    , _clearColorPending(false)
    , _clearDepthPending(false)
    , _clearStencilPending(false)
    , _imageIndex(0)
    , _cubeFace(0)
    , _mipLevel(0)
    , _invalidated(false)
    , _displayPassStarted(false)
    , _metalViewport({})
    , _viewportSet(false)
    , _width(1), _height(1) {
}

VRORenderTargetMetal::VRORenderTargetMetal(VRORenderTargetType type,
                                           int numAttachments,
                                           int numImages,
                                           bool enableMipmaps,
                                           bool needsDepthStencil,
                                           id <MTLDevice> device,
                                           VROMetalRenderPassHost *host)
    : VRORenderTarget(type, numAttachments)
    , _host(host)
    , _device(device)
    , _encoder(nil)
    , _depthTexture(nil)
    , _passDescriptor(nil)
    , _numImages(numImages < 1 ? 1 : numImages)
    , _enableMipmaps(enableMipmaps)
    , _needsDepthStencil(needsDepthStencil)
    , _hydrated(false)
    , _clearColorPending(true)
    , _clearDepthPending(true)
    , _clearStencilPending(false)
    , _imageIndex(0)
    , _cubeFace(0)
    , _mipLevel(0)
    , _invalidated(false)
    , _displayPassStarted(false)
    , _metalViewport({})
    , _viewportSet(false)
    , _width(1), _height(1) {
}

VRORenderTargetMetal::~VRORenderTargetMetal() {
    releaseTextures();
    if (_passDescriptor) {
        [_passDescriptor release];
        _passDescriptor = nil;
    }
}

// ── Type → Metal format mapping ──────────────────────────────────────────────

bool VRORenderTargetMetal::typeHasColor() const {
    switch (_type) {
        case VRORenderTargetType::DepthTexture:
        case VRORenderTargetType::DepthTextureArray:
        case VRORenderTargetType::DepthTextureRaw:
            return false;
        default:
            return true;
    }
}

bool VRORenderTargetMetal::typeHasDepth() const {
    switch (_type) {
        case VRORenderTargetType::DepthTexture:
        case VRORenderTargetType::DepthTextureArray:
        case VRORenderTargetType::DepthTextureRaw:
            return true;
        default:
            return _needsDepthStencil;
    }
}

MTLPixelFormat VRORenderTargetMetal::colorPixelFormatForType() const {
    switch (_type) {
        case VRORenderTargetType::ColorTextureRG16:
            return MTLPixelFormatRG16Float;
        case VRORenderTargetType::ColorTextureSRGB:
            return MTLPixelFormatBGRA8Unorm_sRGB;
        case VRORenderTargetType::ColorTextureHDR16:
        case VRORenderTargetType::CubeTextureHDR16:
            return MTLPixelFormatRGBA16Float;
        case VRORenderTargetType::ColorTextureHDR32:
        case VRORenderTargetType::CubeTextureHDR32:
            return MTLPixelFormatRGBA32Float;
        default:
            // ColorTexture, CubeTexture, Renderbuffer, Display
            return MTLPixelFormatBGRA8Unorm;
    }
}

MTLTextureType VRORenderTargetMetal::metalTextureType() const {
    switch (_type) {
        case VRORenderTargetType::CubeTexture:
        case VRORenderTargetType::CubeTextureHDR16:
        case VRORenderTargetType::CubeTextureHDR32:
            return MTLTextureTypeCube;
        case VRORenderTargetType::DepthTextureArray:
            return MTLTextureType2DArray;
        default:
            return MTLTextureType2D;
    }
}

// ── Allocation ───────────────────────────────────────────────────────────────

void VRORenderTargetMetal::releaseTextures() {
    for (id <MTLTexture> texture : _colorTextures) {
        [texture release];
    }
    _colorTextures.clear();

    if (_depthTexture) {
        [_depthTexture release];
        _depthTexture = nil;
    }
    _textureWrappers.clear();
    _hydrated = false;
}

bool VRORenderTargetMetal::hydrate() {
    if (isDisplay() || _hydrated) {
        return true;
    }
    if (!_device || _width <= 0 || _height <= 0) {
        return false;
    }

    releaseTextures();

    const MTLTextureType textureType = metalTextureType();

    if (typeHasColor()) {
        for (int i = 0; i < _numAttachments; i++) {
            MTLTextureDescriptor *descriptor = [MTLTextureDescriptor new];
            descriptor.textureType = textureType;
            descriptor.pixelFormat = colorPixelFormatForType();
            descriptor.width  = _width;
            descriptor.height = _height;
            descriptor.mipmapLevelCount = _enableMipmaps ? 0 : 1;
            descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            descriptor.storageMode = MTLStorageModePrivate;
            if (textureType == MTLTextureType2DArray) {
                descriptor.arrayLength = _numImages;
            }
            if (_enableMipmaps) {
                // A descriptor mipmapLevelCount of 0 is invalid; compute the full chain.
                int levels = 1;
                int dimension = _width > _height ? _width : _height;
                while (dimension > 1) { dimension >>= 1; levels++; }
                descriptor.mipmapLevelCount = levels;
            }

            id <MTLTexture> texture = [_device newTextureWithDescriptor:descriptor];
            [descriptor release];
            if (!texture) {
                pinfo("VRORenderTargetMetal: failed to allocate color attachment %d (%dx%d)",
                      i, _width, _height);
                releaseTextures();
                return false;
            }
            _colorTextures.push_back(texture);
        }
    }

    if (typeHasDepth()) {
        MTLTextureDescriptor *descriptor = [MTLTextureDescriptor new];
        descriptor.textureType = (_type == VRORenderTargetType::DepthTextureArray)
                                     ? MTLTextureType2DArray : MTLTextureType2D;
        // A stencil buffer is only allocated for colour targets that asked for
        // depth+stencil. Depth-only targets (shadow maps) skip it so they can be
        // sampled as a plain depth texture.
        const bool wantsStencil = _needsDepthStencil && typeHasColor();
        descriptor.pixelFormat = wantsStencil ? MTLPixelFormatDepth32Float_Stencil8
                                              : MTLPixelFormatDepth32Float;
        descriptor.width  = _width;
        descriptor.height = _height;
        descriptor.mipmapLevelCount = 1;
        descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        descriptor.storageMode = MTLStorageModePrivate;
        if (descriptor.textureType == MTLTextureType2DArray) {
            descriptor.arrayLength = _numImages;
        }

        _depthTexture = [_device newTextureWithDescriptor:descriptor];
        [descriptor release];
        if (!_depthTexture) {
            pinfo("VRORenderTargetMetal: failed to allocate depth attachment (%dx%d)",
                  _width, _height);
            releaseTextures();
            return false;
        }
    }

    // Wrap the attachments so the choreographer can feed them to a later pass.
    // Depth-only targets expose the depth texture as attachment 0, matching how
    // VRORenderTargetOpenGL presents a DepthTexture target.
    if (typeHasColor()) {
        const VROTextureType wrapperType = (metalTextureType() == MTLTextureTypeCube)
                                               ? VROTextureType::TextureCube
                                               : VROTextureType::Texture2D;
        for (id <MTLTexture> texture : _colorTextures) {
            std::unique_ptr<VROTextureSubstrate> substrate(new VROTextureSubstrateMetal(texture));
            _textureWrappers.push_back(std::make_shared<VROTexture>(wrapperType,
                                                                    VROTextureInternalFormat::RGBA8,
                                                                    std::move(substrate)));
        }
    } else if (_depthTexture) {
        std::unique_ptr<VROTextureSubstrate> substrate(new VROTextureSubstrateMetal(_depthTexture));
        // R32F is the single-channel float format used elsewhere for depth maps.
        _textureWrappers.push_back(std::make_shared<VROTexture>(VROTextureType::Texture2D,
                                                                VROTextureInternalFormat::R32F,
                                                                std::move(substrate)));
    }

    _hydrated = true;
    rebuildPassDescriptor();
    return true;
}

void VRORenderTargetMetal::rebuildPassDescriptor() {
    // The display's descriptor is supplied by the render loop, not built here.
    if (isDisplay()) {
        return;
    }
    if (_passDescriptor) {
        [_passDescriptor release];
        _passDescriptor = nil;
    }
    // renderPassDescriptor returns an autoreleased object; this target outlives the
    // pool, so retain it.
    _passDescriptor = [[MTLRenderPassDescriptor renderPassDescriptor] retain];

    // Binding clears. This matches VRORenderTargetOpenGL::bind(), which issues a
    // glClear of colour, depth and stencil on every bind — "prevent logical buffer load by
    // immediately clearing". The renderer relies on that: an offscreen target holds one
    // frame's work and is fully redrawn the next, so preserving its contents leaves the
    // previous frame underneath. It shows up as a trail behind anything that moves, and as
    // a stale depth buffer that silently rejects fragments.
    for (size_t i = 0; i < _colorTextures.size(); i++) {
        MTLRenderPassColorAttachmentDescriptor *attachment = _passDescriptor.colorAttachments[i];
        attachment.texture = _colorTextures[i];
        attachment.level   = _mipLevel;
        attachment.slice   = (metalTextureType() == MTLTextureTypeCube) ? _cubeFace : _imageIndex;
        attachment.loadAction  = MTLLoadActionClear;
        attachment.storeAction = _invalidated ? MTLStoreActionDontCare : MTLStoreActionStore;
        attachment.clearColor  = MTLClearColorMake(_clearColor.x, _clearColor.y,
                                                  _clearColor.z, _clearColor.w);
    }

    if (_depthTexture) {
        MTLRenderPassDepthAttachmentDescriptor *depth = _passDescriptor.depthAttachment;
        depth.texture     = _depthTexture;
        depth.slice       = (_type == VRORenderTargetType::DepthTextureArray) ? _imageIndex : 0;
        depth.loadAction  = MTLLoadActionClear;
        depth.storeAction = MTLStoreActionStore;
        depth.clearDepth  = 1.0;

        if (_depthTexture.pixelFormat == MTLPixelFormatDepth32Float_Stencil8) {
            MTLRenderPassStencilAttachmentDescriptor *stencil = _passDescriptor.stencilAttachment;
            stencil.texture      = _depthTexture;
            stencil.slice        = depth.slice;
            stencil.loadAction   = MTLLoadActionClear;
            stencil.storeAction  = MTLStoreActionStore;
            stencil.clearStencil = 0;
        }
    }

    // A depth-only target still needs a render area; Metal derives it from the
    // attachments, so nothing more to set here.
}

// ── Viewport ─────────────────────────────────────────────────────────────────

bool VRORenderTargetMetal::setViewport(VROViewport viewport) {
    const int width  = viewport.getWidth();
    const int height = viewport.getHeight();
    const bool sizeChanged = (width != _width || height != _height);

    _width  = width;
    _height = height;
    _metalViewport = { (double)viewport.getX(), (double)viewport.getY(),
                       (double)_width, (double)_height, 0.0, 1.0 };
    _viewportSet = true;

    if (_encoder) {
        [_encoder setViewport:_metalViewport];
    }

    // A resize invalidates the attachments, exactly as in OpenGL: the caller is
    // expected to re-hydrate.
    if (sizeChanged && !isDisplay()) {
        releaseTextures();
        return true;
    }
    return false;
}

// A label for the timing report. Types are named after their role in the choreographer so
// a reader can map a line of the report onto a decision — "shadow map" means turn shadows
// down, "hdr colour" means the scene pass itself.
static const char *VROTargetTimingLabel(VRORenderTargetType type, int attachments) {
    switch (type) {
        case VRORenderTargetType::Display:           return "display";
        case VRORenderTargetType::DepthTexture:
        case VRORenderTargetType::DepthTextureArray: return "shadow map";
        case VRORenderTargetType::DepthTextureRaw:   return "scene depth";
        case VRORenderTargetType::ColorTextureHDR16:
        case VRORenderTargetType::ColorTextureHDR32:
            // The scene renders into the multi-attachment HDR target; the single-attachment
            // ones are the post-process ping-pong and the blur.
            return attachments > 1 ? "hdr scene" : "post-process";
        case VRORenderTargetType::CubeTexture:
        case VRORenderTargetType::CubeTextureHDR16:
        case VRORenderTargetType::CubeTextureHDR32:  return "ibl cube";
        default:                                     return "offscreen";
    }
}

// ── Binding ──────────────────────────────────────────────────────────────────

void VRORenderTargetMetal::setDisplayPass(MTLRenderPassDescriptor *descriptor) {
    if (_passDescriptor) {
        [_passDescriptor release];
    }
    _passDescriptor = [descriptor retain];
    _displayPassStarted = false;
    _encoder = nil;
}

void VRORenderTargetMetal::endDisplayPass() {
    if (_host) {
        _host->endActiveEncoder();
    }
    _encoder = nil;
    _displayPassStarted = false;
    if (_passDescriptor) {
        [_passDescriptor release];
        _passDescriptor = nil;
    }
}

void VRORenderTargetMetal::bind() {
    // TEMP diagnostic (device crash triage 2026-08-24): the first device run segfaults right
    // after the base pass. Printing the label on every bind identifies the last pass entered.
    // Remove once the crash is located.
    pinfo("VRORenderTargetMetal: bind() → %s [%dx%d]", VROTargetTimingLabel(_type, (int)_colorTextures.size()), _width, _height);

    if (!_host) {
        pinfo("VRORenderTargetMetal: cannot bind — no render-pass host");
        return;
    }
    id <MTLCommandBuffer> commandBuffer = _host->getFrameCommandBuffer();
    if (!commandBuffer) {
        pinfo("VRORenderTargetMetal: bind() outside a frame — no command buffer");
        return;
    }

    if (isDisplay()) {
        if (!_passDescriptor) {
            pinfo("VRORenderTargetMetal: display bind() with no pass descriptor for this eye");
            return;
        }
        if (_displayPassStarted) {
            // Reopening after an offscreen detour: keep what was already drawn.
            // CompositorServices requires stored depth for late-stage reprojection,
            // so the store actions stay as the render loop set them.
            _passDescriptor.colorAttachments[0].loadAction = MTLLoadActionLoad;
            _passDescriptor.depthAttachment.loadAction     = MTLLoadActionLoad;
        }
    } else {
        if (!_hydrated && !hydrate()) {
            return;
        }
        rebuildPassDescriptor();
    }

    // Metal allows one render command encoder per command buffer at a time.
    _host->endActiveEncoder();

    // Timestamp sampling has to be attached to the descriptor before the encoder is created.
    if (VROMetalFrameTimer *timer = _host->getFrameTimer()) {
        timer->beginPass(_passDescriptor,
                         VROTargetTimingLabel(_type, (int)_colorTextures.size()));
    }

    id <MTLRenderCommandEncoder> encoder =
        [commandBuffer renderCommandEncoderWithDescriptor:_passDescriptor];
    if (!encoder) {
        pinfo("VRORenderTargetMetal: failed to create render command encoder");
        return;
    }

    if (isDisplay()) {
        _displayPassStarted = true;
    } else {
        _invalidated              = false;
        _attachmentSelectionDirty = false;
    }

    _encoder = encoder;
    if (_viewportSet) {
        [encoder setViewport:_metalViewport];
    }
    _host->onRenderTargetEncoderBegan(encoder);
}

void VRORenderTargetMetal::bindRead() {
    // Metal samples a target's texture directly; there is no read-framebuffer
    // binding to make. getTexture() is the read path.
}

void VRORenderTargetMetal::invalidate() {
    // Deliberately a no-op on Metal.
    //
    // In OpenGL this says "you need not preserve these buffers", and the choreographer
    // calls it on the target it is unbinding — after that pass has already been issued.
    // Metal decides the store action when the encoder is created, so honouring the call
    // at this point can only affect the *next* pass on this target, which is exactly
    // wrong: it would discard the HDR colour that the tone-mapping pass is about to
    // sample. Storing costs bandwidth; discarding costs the image.
}

// ── Blits ────────────────────────────────────────────────────────────────────

//  Only blitDepth is exercised by VROChoreographer (the display blit goes through
//  a post-process instead). All three use a MTLBlitCommandEncoder, which copies
//  but cannot flip: a flipped blit needs a draw, so flipY is reported rather than
//  silently ignored.

static void VROBlitMetalTexture(id <MTLCommandBuffer> commandBuffer,
                                id <MTLTexture> source,
                                id <MTLTexture> destination) {
    if (!commandBuffer || !source || !destination) {
        return;
    }
    if (source.pixelFormat != destination.pixelFormat) {
        // A Metal blit cannot convert formats, and for the scene-depth capture it never will:
        // the HDR target needs Depth32Float_Stencil8 (260) because portals stencil against it,
        // while a DepthTextureRaw target is plain Depth32Float (252) so it can be sampled as a
        // depth texture. The mismatch is structural, not a misconfiguration.
        //
        // It also costs nothing today. The only consumer of the captured texture is
        // VROTextureReference::SceneDepth, which is resolved by VROMaterialShaderBinding — an
        // OpenGL-only path. VROMaterialSubstrateMetal builds its texture list from material
        // visuals plus the explicit shadow/IBL slots and never looks at a global texture
        // reference, so nothing on Metal would sample the copy even if it succeeded. Making the
        // formats match would buy a per-frame depth copy that no shader reads.
        //
        // The real gap is one level up and worth a ticket rather than a format change: a
        // material declaring requiresSceneDepth (or requiresCameraTexture) is silently inert on
        // Metal. Fixing that means teaching the Metal substrate to resolve global texture
        // references; only then does this blit need to work.
        static bool sReported = false;
        if (!sReported) {
            sReported = true;
            pinfo("VRORenderTargetMetal: blit skipped — pixel formats differ (%lu vs %lu)",
                  (unsigned long)source.pixelFormat, (unsigned long)destination.pixelFormat);
        }
        return;
    }
    const NSUInteger width  = source.width  < destination.width  ? source.width  : destination.width;
    const NSUInteger height = source.height < destination.height ? source.height : destination.height;

    id <MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
    [blit copyFromTexture:source
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(width, height, 1)
                toTexture:destination
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
}

void VRORenderTargetMetal::blitColor(std::shared_ptr<VRORenderTarget> destination, bool flipY,
                                     std::shared_ptr<VRODriver> driver) {
    if (!_host || _colorTextures.empty()) {
        return;
    }
    std::shared_ptr<VRORenderTargetMetal> target =
        std::dynamic_pointer_cast<VRORenderTargetMetal>(destination);
    if (!target) {
        return;
    }
    if (flipY) {
        pinfo("VRORenderTargetMetal: blitColor(flipY) is not supported — a flip needs a draw pass");
        return;
    }
    _host->endActiveEncoder();
    VROBlitMetalTexture(_host->getFrameCommandBuffer(),
                        _colorTextures[0], target->getMetalTexture(0));
}

void VRORenderTargetMetal::blitStencil(std::shared_ptr<VRORenderTarget> destination, bool flipY,
                                       std::shared_ptr<VRODriver> driver) {
    // The stencil lives in the combined Depth32Float_Stencil8 texture, so a stencil
    // blit is a depth blit.
    blitDepth(destination);
}

void VRORenderTargetMetal::blitDepth(std::shared_ptr<VRORenderTarget> destination) {
    if (!_host || !_depthTexture) {
        return;
    }
    std::shared_ptr<VRORenderTargetMetal> target =
        std::dynamic_pointer_cast<VRORenderTargetMetal>(destination);
    if (!target) {
        return;
    }
    if (!target->_hydrated) {
        target->hydrate();
    }
    _host->endActiveEncoder();
    VROBlitMetalTexture(_host->getFrameCommandBuffer(),
                        _depthTexture, target->getMetalDepthTexture());
}

// ── Framebuffer lifecycle ────────────────────────────────────────────────────

void VRORenderTargetMetal::deleteFramebuffers() {
    releaseTextures();
    if (_passDescriptor) {
        [_passDescriptor release];
        _passDescriptor = nil;
    }
}

bool VRORenderTargetMetal::restoreFramebuffers() {
    return hydrate();
}

// ── Texture attachments ──────────────────────────────────────────────────────

id <MTLTexture> VRORenderTargetMetal::getMetalTexture(int attachment) const {
    if (attachment < 0 || attachment >= (int)_colorTextures.size()) {
        return nil;
    }
    return _colorTextures[attachment];
}

bool VRORenderTargetMetal::hasTextureAttached(int attachment) {
    if (!typeHasColor()) {
        return _depthTexture != nil;
    }
    return attachment >= 0 && attachment < (int)_colorTextures.size()
           && _colorTextures[attachment] != nil;
}

void VRORenderTargetMetal::clearTextures() {
    releaseTextures();
}

bool VRORenderTargetMetal::attachNewTextures() {
    releaseTextures();
    return hydrate();
}

void VRORenderTargetMetal::setTextureImageIndex(int index, int attachment) {
    if (_imageIndex != index) {
        _imageIndex = index;
        _attachmentSelectionDirty = true;
    }
}

void VRORenderTargetMetal::setTextureCubeFace(int face, int mipLevel, int attachmentIndex) {
    if (_cubeFace != face || _mipLevel != mipLevel) {
        _cubeFace = face;
        _mipLevel = mipLevel;
        _attachmentSelectionDirty = true;
    }
}

void VRORenderTargetMetal::setMipLevel(int mipLevel, int attachmentIndex) {
    if (_mipLevel != mipLevel) {
        _mipLevel = mipLevel;
        _attachmentSelectionDirty = true;
    }
}

void VRORenderTargetMetal::attachTexture(std::shared_ptr<VROTexture> texture, int attachment) {
    if (!texture || attachment < 0) {
        return;
    }
    // Adopt the caller's texture as the attachment. Only substrates backed by a
    // Metal texture can be rendered into.
    if (!_host) {
        pinfo("VRORenderTargetMetal: attachTexture ignored — no render-pass host");
        return;
    }
    std::shared_ptr<VRODriver> driver = _host->getRenderPassDriver();
    if (!driver) {
        pinfo("VRORenderTargetMetal: attachTexture ignored — no driver");
        return;
    }
    VROTextureSubstrate *substrate = texture->getSubstrate(0, driver, true);
    VROTextureSubstrateMetal *metalSubstrate = dynamic_cast<VROTextureSubstrateMetal *>(substrate);
    if (!metalSubstrate) {
        pinfo("VRORenderTargetMetal: attachTexture ignored — substrate is not Metal-backed");
        return;
    }
    if ((int)_colorTextures.size() <= attachment) {
        _colorTextures.resize(attachment + 1, nil);
    }
    if ((int)_textureWrappers.size() <= attachment) {
        _textureWrappers.resize(attachment + 1);
    }
    // The caller keeps ownership of an attached texture, so retain our reference
    // and release whatever occupied the slot.
    [_colorTextures[attachment] release];
    _colorTextures[attachment] = [metalSubstrate->getTexture() retain];
    _textureWrappers[attachment] = texture;
    _hydrated = true;
    rebuildPassDescriptor();
}

const std::shared_ptr<VROTexture> VRORenderTargetMetal::getTexture(int attachment) const {
    if (attachment < 0 || attachment >= (int)_textureWrappers.size()) {
        return nullptr;
    }
    return _textureWrappers[attachment];
}

// ── Clears ───────────────────────────────────────────────────────────────────

// No-ops by design. Binding an offscreen target already clears colour, depth and stencil,
// which is what these calls are asking for — see rebuildPassDescriptor. Metal cannot clear
// inside a pass anyway: a clear is a load action, decided when the encoder is created, so a
// clear requested after bind() could only be honoured by ending the pass and discarding
// whatever had been drawn into it.
void VRORenderTargetMetal::clearColor() {
}

void VRORenderTargetMetal::clearDepth() {
}

void VRORenderTargetMetal::clearStencil() {
}

void VRORenderTargetMetal::clearDepthAndColor() {
}

// ── Stencil ──────────────────────────────────────────────────────────────────

void VRORenderTargetMetal::enablePortalStencilWriting(VROFace face) {
    _stencil.writing = true;
    _stencil.removal = false;
}

void VRORenderTargetMetal::enablePortalStencilRemoval(VROFace face) {
    _stencil.writing = true;
    _stencil.removal = true;
}

void VRORenderTargetMetal::disablePortalStencilWriting(VROFace face) {
    _stencil.writing = false;
    _stencil.removal = false;
}

void VRORenderTargetMetal::setPortalStencilPassFunction(VROFace face, VROStencilFunc func, int ref) {
    switch (func) {
        case VROStencilFunc::Never:          _stencil.compareFunc = MTLCompareFunctionNever;        break;
        case VROStencilFunc::Less:           _stencil.compareFunc = MTLCompareFunctionLess;         break;
        case VROStencilFunc::LessOrEqual:    _stencil.compareFunc = MTLCompareFunctionLessEqual;    break;
        case VROStencilFunc::Greater:        _stencil.compareFunc = MTLCompareFunctionGreater;      break;
        case VROStencilFunc::GreaterOrEqual: _stencil.compareFunc = MTLCompareFunctionGreaterEqual; break;
        case VROStencilFunc::Equal:          _stencil.compareFunc = MTLCompareFunctionEqual;        break;
        case VROStencilFunc::NotEqual:       _stencil.compareFunc = MTLCompareFunctionNotEqual;     break;
        case VROStencilFunc::Always:         _stencil.compareFunc = MTLCompareFunctionAlways;       break;
    }
    _stencil.reference = ref;
}

#endif  // VRO_METAL
