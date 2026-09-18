//
//  VROTypefaceWasm.cpp
//  ViroRenderer
//
//  Created by Raj Advani on 3/11/18.
//  Copyright © 2018 Viro Media. All rights reserved.
//

#include "VROTypefaceWasm.h"
#include "VROLog.h"
#include "VROGlyphOpenGL.h"
#include "VRODriverOpenGLWasm.h"

// Roboto, and not the platform's own face: this build's fonts are baked into
// the wasm data blob and therefore redistributed with it, so the font has to be
// one we are allowed to redistribute. Roboto is Apache 2.0, and it is also what
// Android and Quest render with — which makes the web player agree with two of
// the three instead of being a third typeface.
static const std::string kSystemFont = "Roboto";

VROTypefaceWasm::VROTypefaceWasm(std::string name, int size, VROFontStyle style, VROFontWeight weight,
                                 std::shared_ptr<VRODriver> driver) :
        VROTypeface(name, size, style, weight),
        _driver(driver),
        _face(nullptr) {
}

VROTypefaceWasm::~VROTypefaceWasm() {
    std::shared_ptr<VRODriver> driver = _driver.lock();
    if (driver && _face != nullptr) {
        // FT crashes if we delete a face after the freetype library has been deleted
        if (std::dynamic_pointer_cast<VRODriverOpenGLWasm>(driver)->getFreetype() != nullptr) {
            FT_Done_Face(_face);
        }
    }
}

FT_FaceRec_ *VROTypefaceWasm::loadFTFace() {
    std::shared_ptr<VRODriver> driver = _driver.lock();
    if (!driver) {
        return nullptr;
    }
    
    FT_Library ft = std::dynamic_pointer_cast<VRODriverOpenGLWasm>(driver)->getFreetype();
    // The requested face first, then the system one. Each is tried as .ttf and
    // .ttc, because the suffix is a property of the file someone preloaded and
    // not of the name a scene asked for.
    if (!openFace(ft, getName()) && !openFace(ft, kSystemFont)) {
        pabort("Failed to load system font %s", kSystemFont.c_str());
    }

    FT_Set_Pixel_Sizes(_face, 0, getSize());
    return _face;
}

float VROTypefaceWasm::getLineHeight() const {
    return _face->size->metrics.height >> 6;
}

std::shared_ptr<VROGlyph> VROTypefaceWasm::loadGlyph(uint32_t charCode, uint32_t variantSelector,
                                                     uint32_t outlineWidth, VROGlyphRenderMode renderMode) {
    std::shared_ptr<VROGlyph> glyph = std::make_shared<VROGlyphOpenGL>();
    std::shared_ptr<VRODriverOpenGLWasm> driver = std::dynamic_pointer_cast<VRODriverOpenGLWasm>(_driver.lock());
    if (!driver) {
        return glyph;
    }

    if (renderMode == VROGlyphRenderMode::None) {
        glyph->loadMetrics(_face, charCode, variantSelector);
    } else if (renderMode == VROGlyphRenderMode::Bitmap) {
        glyph->loadBitmap(_face, charCode, variantSelector, &_glyphAtlases, driver);
        if (outlineWidth > 0) {
            glyph->loadOutlineBitmap(driver->getFreetype(), _face, charCode, variantSelector, outlineWidth,
                                     &_outlineAtlases[outlineWidth], driver);
        }
    } else {
        glyph->loadVector(_face, charCode, variantSelector);
    }

    return glyph;
}

std::string VROTypefaceWasm::getFontPath(std::string fontName) {
    // Kept for callers that want the canonical path; openFace is what the loader
    // uses, because it has to try both suffixes.
    return "/" + fontName + ".ttf";
}

/**
 Opens a preloaded face by name, trying each suffix a font file comes with.
 Returns false when none of them is there, which is the caller's cue to fall
 back rather than an error: a scene naming a font this build does not carry is
 ordinary, and rendering it in the system face is the right answer.
 */
bool VROTypefaceWasm::openFace(FT_Library ft, const std::string &fontName) {
    for (const std::string &suffix : { ".ttf", ".ttc" }) {
        if (FT_New_Face(ft, ("/" + fontName + suffix).c_str(), 0, &_face) == 0) {
            return true;
        }
    }
    return false;
}
