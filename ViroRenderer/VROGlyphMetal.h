//
//  VROGlyphMetal.h
//  ViroRenderer
//
//  Copyright © 2026 ReactVision. All rights reserved.
//
//  Metal counterpart of VROGlyphOpenGL. The glyph work itself — freetype rasterisation,
//  stroking, outline vectorisation, atlas placement — is platform neutral and identical;
//  the only difference is that this creates VROGlyphAtlasMetal, which uploads to an
//  MTLTexture instead of a GL one.
//
//  Kept as a separate class rather than parameterising VROGlyphOpenGL because VROGlyph is
//  instantiated through VROTypeface, which picks the implementation per platform.
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

#ifndef VROGlyphMetal_h
#define VROGlyphMetal_h

#include "VRODefines.h"
#if VRO_METAL

#include "VROGlyph.h"

class VRODriver;
class VROVectorizer;

namespace p2t {
    class Point;
}

class VROGlyphMetal : public VROGlyph {
    
public:
    
    VROGlyphMetal();
    virtual ~VROGlyphMetal();
    
    bool loadMetrics(FT_Face face, uint32_t charCode, uint32_t variantSelector);
    bool loadBitmap(FT_Face face, uint32_t charCode, uint32_t variantSelector,
                    std::vector<std::shared_ptr<VROGlyphAtlas>> *atlases,
                    std::shared_ptr<VRODriver> driver);
    bool loadOutlineBitmap(FT_Library library, FT_Face face, uint32_t charCode, uint32_t variantSelector,
                           uint32_t outlineWidth,
                           std::vector<std::shared_ptr<VROGlyphAtlas>> *atlases,
                           std::shared_ptr<VRODriver> driver);
    bool loadVector(FT_Face face, uint32_t charCode, uint32_t variantSelector);
        
private:
    
    bool loadGlyph(FT_Face face, uint32_t charCode, uint32_t variantSelector);
    
    std::vector<p2t::Point *> triangulateContour(VROVectorizer &vectorizer, int c);
    
};

#endif  // VRO_METAL
#endif /* VROGlyphMetal_h */
