//
//  VROGlyphAtlasMetal.h
//  ViroRenderer
//
//  Copyright © 2026 ReactVision. All rights reserved.
//
//  Metal counterpart of VROGlyphAtlasOpenGL.
//
//  The packing is identical — glyphs are placed left to right, top to bottom, into a
//  512x512 luminance+alpha bitmap held on the CPU. Only the upload differs: instead of
//  glTexImage2D / glGenerateMipmap this fills an MTLTexture and generates its mip chain
//  with a blit encoder.
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

#ifndef VROGlyphAtlasMetal_h
#define VROGlyphAtlasMetal_h

#include "VRODefines.h"
#if VRO_METAL

#include "VROGlyphAtlas.h"
#include <stdint.h>

#if defined(__OBJC__)
#include <Metal/Metal.h>
#endif

class VRODriver;

class VROGlyphAtlasMetal : public VROGlyphAtlas {

public:

    VROGlyphAtlasMetal(bool isOutline);
    virtual ~VROGlyphAtlasMetal();

    void refreshTexture(std::shared_ptr<VRODriver> driver);
    bool glyphWillFit(FT_Bitmap &bitmap, VROAtlasLocation *outLocation);
    void write(FT_Bitmap &bitmap, const VROAtlasLocation &location, std::shared_ptr<VRODriver> driver);
    int getSize() const;

private:

    /*
     Luminance + alpha pairs, CPU side. Luminance is always 255 so the text colour comes
     entirely from the material's diffuse colour; alpha carries the glyph coverage.
     */
    uint8_t *_luminanceAlphaBitmap;

#if defined(__OBJC__)
    id <MTLTexture> _texture2D;
#else
    void *_texture2D;
#endif

};

#endif  // VRO_METAL
#endif /* VROGlyphAtlasMetal_h */
