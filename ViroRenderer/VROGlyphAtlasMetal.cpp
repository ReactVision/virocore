//
//  VROGlyphAtlasMetal.cpp
//  ViroRenderer
//
//  Copyright © 2026 ReactVision. All rights reserved.
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

#include "VROGlyphAtlasMetal.h"
#if VRO_METAL

#include "VROLog.h"
#include "VROTexture.h"
#include "VROTextureSubstrateMetal.h"
#include "VRODriverMetal.h"
#include "VROMath.h"

static const int kGlyphAtlasTextureSize = 512;
static const int kGlyphPadding = 8;

VROGlyphAtlasMetal::VROGlyphAtlasMetal(bool isOutline) :
    _texture2D(nil) {
    _outline = isOutline;
    _luminanceAlphaBitmap = (uint8_t *)malloc( sizeof(uint8_t) * 2 *
                                               kGlyphAtlasTextureSize * kGlyphAtlasTextureSize );

    for (int j = 0; j < kGlyphAtlasTextureSize; j++) {
        for (int i = 0; i < kGlyphAtlasTextureSize; i++) {
            _luminanceAlphaBitmap[2 * (i + j * kGlyphAtlasTextureSize)] = 255;
            _luminanceAlphaBitmap[2 * (i + j * kGlyphAtlasTextureSize) + 1] = 0;
        }
    }
}

VROGlyphAtlasMetal::~VROGlyphAtlasMetal() {
    free (_luminanceAlphaBitmap);
    [_texture2D release];
}

int VROGlyphAtlasMetal::getSize() const {
    return kGlyphAtlasTextureSize;
}

bool VROGlyphAtlasMetal::glyphWillFit(FT_Bitmap &bitmap, VROAtlasLocation *outLocation) {
    int texWidth  = bitmap.width;
    int texHeight = bitmap.rows;

    int minU = 0;
    int minV = 0;

    int rowWidthRemaining = kGlyphAtlasTextureSize - _occupiedU;
    int columnHeightRemaining = kGlyphAtlasTextureSize - _occupiedBottomV;

    if (rowWidthRemaining < texWidth + kGlyphPadding) {
        // End of this row; is there room for a new one?
        if (columnHeightRemaining < texHeight + kGlyphPadding) {
            return false;
        } else {
            minU = kGlyphPadding;
            minV = _occupiedBottomV + kGlyphPadding;

            _occupiedU = minU + texWidth;
            _occupiedTopV = minV;
        }
    } else {
        // Enough horizontal room; check vertical.
        if (columnHeightRemaining < texHeight + kGlyphPadding) {
            return false;
        } else {
            minU = _occupiedU + kGlyphPadding;
            minV = _occupiedTopV;

            _occupiedU = minU + texWidth;
            _occupiedBottomV = fmax(_occupiedBottomV, minV + texHeight);
        }
    }

    outLocation->minU = minU;
    outLocation->maxU = minU + texWidth;
    outLocation->minV = minV;
    outLocation->maxV = minV + texHeight;

    return true;
}

void VROGlyphAtlasMetal::write(FT_Bitmap &bitmap, const VROAtlasLocation &location,
                               std::shared_ptr<VRODriver> driver) {
    int texWidth  = bitmap.width;
    int texHeight = bitmap.rows;

    int minU = location.minU;
    int minV = location.minV;

    /*
     Each pixel is an 8-bit luminance and 8-bit alpha pair. Luminance is held at 255 so
     RGB sample as 1.0 and the text colour comes entirely from the material's diffuse
     colour; alpha carries the glyph's coverage.
     */
    for (int j = 0; j < texHeight; j++) {
        for (int i = 0; i < texWidth; i++) {
            _luminanceAlphaBitmap[2 * (minU + i + (j + minV) * kGlyphAtlasTextureSize) + 0] = 255;
            _luminanceAlphaBitmap[2 * (minU + i + (j + minV) * kGlyphAtlasTextureSize) + 1] = bitmap.buffer[i + bitmap.width * j];
        }
    }
}

void VROGlyphAtlasMetal::refreshTexture(std::shared_ptr<VRODriver> driver) {
    VRODriverMetal *metal = dynamic_cast<VRODriverMetal *>(driver.get());
    if (!metal) {
        return;
    }
    id <MTLDevice> device = metal->getDevice();
    if (!device) {
        return;
    }

    const bool isNew = (_texture2D == nil);
    if (isNew) {
        MTLTextureDescriptor *descriptor = [MTLTextureDescriptor new];
        descriptor.textureType = MTLTextureType2D;
        // RG8Unorm matches the OpenGL path's GL_RG8: R is luminance, G is coverage. The
        // shader samples it through the same material slot, so the layout has to agree.
        descriptor.pixelFormat = MTLPixelFormatRG8Unorm;
        descriptor.width  = kGlyphAtlasTextureSize;
        descriptor.height = kGlyphAtlasTextureSize;
        // Text is minified far more often than magnified, so the mip chain matters;
        // 512 -> 1 is ten levels.
        int levels = 1;
        int dimension = kGlyphAtlasTextureSize;
        while (dimension > 1) { dimension >>= 1; levels++; }
        descriptor.mipmapLevelCount = levels;
        descriptor.usage = MTLTextureUsageShaderRead;
        // Shared so the CPU can refresh level 0 in place as new glyphs are packed in.
        descriptor.storageMode = MTLStorageModeShared;

        // Reproduce GL_LUMINANCE_ALPHA sampling. The OpenGL path relies on a two-channel
        // texture reading back as (l, l, l, a); Metal samples RG8 literally as (r, g, 0, 1),
        // which paints every glyph on an opaque red block. Swizzling R into RGB and G into
        // A fixes it in the texture rather than in every shader that samples a glyph.
        descriptor.swizzle = MTLTextureSwizzleChannelsMake(MTLTextureSwizzleRed,
                                                          MTLTextureSwizzleRed,
                                                          MTLTextureSwizzleRed,
                                                          MTLTextureSwizzleGreen);

        _texture2D = [device newTextureWithDescriptor:descriptor];
        [descriptor release];

        if (!_texture2D) {
            pinfo("VROGlyphAtlasMetal: could not allocate a %dx%d glyph atlas texture",
                  kGlyphAtlasTextureSize, kGlyphAtlasTextureSize);
            return;
        }
    }

    MTLRegion region = MTLRegionMake2D(0, 0, kGlyphAtlasTextureSize, kGlyphAtlasTextureSize);
    [_texture2D replaceRegion:region
                  mipmapLevel:0
                    withBytes:_luminanceAlphaBitmap
                  bytesPerRow:kGlyphAtlasTextureSize * 2];

    // Regenerate the mip chain. Unlike glGenerateMipmap this needs a command buffer, and
    // it has to complete before the atlas is sampled — the glyph is typically drawn on the
    // very next frame, so this waits rather than letting the first frame sample garbage.
    id <MTLCommandQueue> queue = metal->getCommandQueue();
    if (queue) {
        id <MTLCommandBuffer> commandBuffer = [queue commandBuffer];
        id <MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
        [blit generateMipmapsForTexture:_texture2D];
        [blit endEncoding];
        [commandBuffer commit];
        [commandBuffer waitUntilCompleted];
    }

    if (isNew) {
        std::unique_ptr<VROTextureSubstrate> substrate =
            std::unique_ptr<VROTextureSubstrateMetal>(new VROTextureSubstrateMetal(_texture2D));
        _texture = std::make_shared<VROTexture>(VROTextureType::Texture2D,
                                                VROTextureInternalFormat::RG8,
                                                std::move(substrate));
    }
}

#endif  // VRO_METAL
