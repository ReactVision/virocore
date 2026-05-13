//
//  VRODriverMetal.h
//  ViroRenderer
//
//  Created by Raj Advani on 10/13/15.
//  Copyright © 2015 Viro Media. All rights reserved.
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

#ifndef VRORenderContextMetal_h
#define VRORenderContextMetal_h

#include "VRODefines.h"
#if VRO_METAL

#include <stdio.h>
#include <Metal/Metal.h>
#include <MetalKit/MetalKit.h>
#include "VRODriver.h"
#include "VRORenderTarget.h"
#include "VROMatrix4f.h"
#include <memory>
#include "VROGeometrySubstrateMetal.h"
#include "VROMaterialSubstrateMetal.h"
#include "VROTextureSubstrateMetal.h"
#include "VROFrameScheduler.h"
#include "VROVideoTextureCacheMetal.h"

/*
 Driver for Metal.
 */
class VRODriverMetal : public VRODriver {

public:

    VRODriverMetal(id <MTLDevice> device) {
        _device = device;
        _commandQueue = [device newCommandQueue];
        _scheduler = std::make_shared<VROFrameScheduler>();
        _colorPixelFormat = MTLPixelFormatBGRA8Unorm_sRGB;
        _depthPixelFormat = MTLPixelFormatDepth32Float;
        _sampleCount = 1;
        _activeEncoder = nil;

        // Try the named ViroKit bundle first (iOS/macOS dynamic framework).
        // Fall back to newDefaultLibrary for static-library deployments (visionOS).
        NSBundle *bundle = [NSBundle bundleWithIdentifier:@"com.viro.ViroKit"];
        NSURL *shadersURL = bundle ? [bundle URLForResource:@"default" withExtension:@"metallib"] : nil;
        if (shadersURL) {
            _library = [device newLibraryWithURL:shadersURL error:nil];
        }
        if (!_library) {
            _library = [device newDefaultLibrary];
        }

        NSString *shadersSrcPath = bundle ? [bundle pathForResource:@"Shaders" ofType:@"metal"] : nil;
        if (shadersSrcPath) {
            NSLog(@"VRODriverMetal: Found shaders at %@, loading source", shadersSrcPath);
            _librarySource = std::string([[NSString stringWithContentsOfFile:shadersSrcPath encoding:NSUTF8StringEncoding error:nil] UTF8String]);
            NSLog(@"VRODriverMetal: Loaded %lu bytes of shader source", _librarySource.length());
        } else {
            NSLog(@"VRODriverMetal: Warning! Could not find Shaders.metal in ViroKit bundle");
        }
    }

    void willRenderFrame(const VRORenderContext &context) override {}
    void didRenderFrame(const VROFrameTimer &timer, const VRORenderContext &context) override {}
    void willRenderEye(const VRORenderContext &context) override {}
    void didRenderEye(const VRORenderContext &context) override {}
    void pause() override {}
    void resume() override {}
    void readGPUType() override {}
    VROGPUType getGPUType() override { return VROGPUType::Normal; }
    void readDisplayFramebuffer() override {}
    void setActiveTextureUnit(int unit) override {}
    void bindTexture(int target, int texture) override {}
    void bindTexture(int unit, int target, int texture) override {}
    void setDepthWritingEnabled(bool enabled) override {}
    void setDepthReadingEnabled(bool enabled) override {}
    void setStencilTestEnabled(bool enabled) override {}
    void setCullMode(VROCullMode cullMode) override {}
    void setRenderTargetColorWritingMask(VROColorMask mask) override {}
    void setMaterialColorWritingMask(VROColorMask mask) override {}
    void bindShader(std::shared_ptr<VROShaderProgram> program) override {}
    void unbindShader() override {}
    bool bindRenderTarget(std::shared_ptr<VRORenderTarget> target, VRORenderTargetUnbindOp unbindOp) override { return false; }
    void unbindRenderTarget() override {}
    VROColorRenderingMode getColorRenderingMode() override { return VROColorRenderingMode::NonLinear; }
    void setHasSoftwareGammaPass(bool softwareGamma) override {}
    bool hasSoftwareGammaPass() const override { return false; }
    bool isBloomSupported() override { return false; }
    std::shared_ptr<VRORenderTarget> newRenderTarget(VRORenderTargetType type, int numAttachments, int numImages,
                                                     bool enableMipmaps, bool needsDepthStencil) override {
        return nullptr;
    }
    std::shared_ptr<VROVertexBuffer> newVertexBuffer(std::shared_ptr<VROData> data) override {
        return nullptr;
    }
    std::shared_ptr<VRORenderTarget> getDisplay() override { return nullptr; }
    std::shared_ptr<VROImagePostProcess> newImagePostProcess(std::shared_ptr<VROShaderProgram> shader) override {
        return nullptr;
    }
    VROTextureSubstrate *newTextureSubstrate(VROTextureType type,
                                              VROTextureFormat format,
                                              VROTextureInternalFormat internalFormat, bool sRGB,
                                              VROMipmapMode mipmapMode,
                                              std::vector<std::shared_ptr<VROData>> &data,
                                              int width, int height, std::vector<uint32_t> mipSizes,
                                              VROWrapMode wrapS, VROWrapMode wrapT,
                                              VROFilterMode minFilter, VROFilterMode magFilter, VROFilterMode mipFilter) override {
        std::shared_ptr<VRODriver> self(static_cast<VRODriver *>(this), [](VRODriver *) {});
        return new VROTextureSubstrateMetal(type, format, data.front(), width, height, self);
    }
    std::shared_ptr<VROSound> newSound(std::shared_ptr<VROSoundData> data, VROSoundType type) override { return nullptr; }
    std::shared_ptr<VROAudioPlayer> newAudioPlayer(std::shared_ptr<VROSoundData> data) override { return nullptr; }
    std::shared_ptr<VROTypefaceCollection> newTypefaceCollection(std::string typefaces, int size, VROFontStyle style, VROFontWeight weight) override {
        return nullptr;
    }
    void setSoundRoom(float sizeX, float sizeY, float sizeZ, std::string wallMaterial,
                      std::string ceilingMaterial, std::string floorMaterial) override {}
    void setBlendingMode(VROBlendMode mode) override {}
    std::shared_ptr<VROFrameScheduler> getFrameScheduler() override { return _scheduler; }
    void *getGraphicsContext() override { return nullptr; }

    void setRenderTarget(std::shared_ptr<VRORenderTarget> renderTarget) {
        _renderTarget = renderTarget;
    }

    // ── Metal pixel format / sample count ─────────────────────────────────────
    // Set by VRODriverVisionOS (or VROViewMetal) before the first frame.

    void setColorPixelFormat(MTLPixelFormat format) { _colorPixelFormat = format; }
    MTLPixelFormat getColorPixelFormat() const { return _colorPixelFormat; }

    void setDepthPixelFormat(MTLPixelFormat format) { _depthPixelFormat = format; }
    MTLPixelFormat getDepthPixelFormat() const { return _depthPixelFormat; }

    // Stencil pixel format — MTLPixelFormatInvalid (0) when there is no stencil
    // attachment (e.g. visionOS POC).  Set to a combined depth+stencil format
    // (e.g. MTLPixelFormatDepth32Float_Stencil8) on platforms that use stencil.
    void setStencilPixelFormat(MTLPixelFormat format) { _stencilPixelFormat = format; }
    MTLPixelFormat getStencilPixelFormat() const { return _stencilPixelFormat; }

    void setSampleCount(int count) { _sampleCount = count; }
    int  getSampleCount()  const   { return _sampleCount; }

    // ── Active render encoder ─────────────────────────────────────────────────
    // Must be set by the platform render loop before each eye render call.

    virtual void setActiveEncoder(id <MTLRenderCommandEncoder> encoder) {
        _activeEncoder = encoder;
    }
    id <MTLRenderCommandEncoder> getActiveEncoder() const { return _activeEncoder; }

    // ── Metal device / queue / library ────────────────────────────────────────

    id <MTLDevice> getDevice() const {
        return _device;
    }
    id <MTLCommandQueue> getCommandQueue() const {
        return _commandQueue;
    }
    id <MTLLibrary> getLibrary() const {
        return _library;
    }

    std::string getLibrarySource() const {
        return _librarySource;
    }

    id <MTLLibrary> newLibraryWithSource(std::string source) {
        NSError *error = nil;
        id <MTLLibrary> library = [_device newLibraryWithSource:[NSString stringWithUTF8String:source.c_str()]
                                                       options:nil
                                                         error:&error];
        if (!library) {
            NSLog(@"Failed to compile shader library: %@", error);
        }
        return library;
    }

    std::shared_ptr<VRORenderTarget> getRenderTarget() override {
        return _renderTarget;
    }

    // ── VRODriver substrate factories ─────────────────────────────────────────

    VROGeometrySubstrate *newGeometrySubstrate(const VROGeometry &geometry) override {
        return new VROGeometrySubstrateMetal(geometry, *this);
    }

    VROMaterialSubstrate *newMaterialSubstrate(VROMaterial &material) override {
        return new VROMaterialSubstrateMetal(material, *this);
    }

    // Non-virtual helpers used by iOS VROViewMetal (not the VRODriver virtual overload).
    VROTextureSubstrate *newTextureSubstrateMetal(VROTextureType type, std::vector<std::shared_ptr<VROImage>> &images) {
        std::shared_ptr<VRODriver> self(static_cast<VRODriver *>(this), [](VRODriver *) {});
        return new VROTextureSubstrateMetal(type, images, self);
    }

    VROTextureSubstrate *newTextureSubstrateMetal(VROTextureType type, VROTextureFormat format,
                                                  std::shared_ptr<VROData> data, int width, int height) {
        std::shared_ptr<VRODriver> self(static_cast<VRODriver *>(this), [](VRODriver *) {});
        return new VROTextureSubstrateMetal(type, format, data, width, height, self);
    }

    std::shared_ptr<VROVideoTextureCache> newVideoTextureCache() override {
        return std::make_shared<VROVideoTextureCacheMetal>(_device);
    }

    // Sound / audio / typeface — no-op in the Metal base driver.
    // iOS callers use VRODriverOpenGLiOS; visionOS callers override in VRODriverVisionOS.

    std::shared_ptr<VROSound> newSound(std::string resource, VROResourceType resourceType, VROSoundType type) override {
        return nullptr;
    }
    std::shared_ptr<VROAudioPlayer> newAudioPlayer(std::string path, bool isLocal) override {
        return nullptr;
    }

private:

    id <MTLDevice> _device;
    id <MTLCommandQueue> _commandQueue;
    id <MTLLibrary> _library;
    std::string _librarySource;
    std::shared_ptr<VROFrameScheduler> _scheduler;

    MTLPixelFormat _colorPixelFormat;
    MTLPixelFormat _depthPixelFormat;
    MTLPixelFormat _stencilPixelFormat = MTLPixelFormatInvalid;
    int _sampleCount;
    id <MTLRenderCommandEncoder> _activeEncoder;

    std::shared_ptr<VRORenderTarget> _renderTarget;

};

#endif
#endif /* VRORenderContextMetal_h */
