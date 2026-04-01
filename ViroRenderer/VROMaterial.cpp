//
//  VROMaterial.cpp
//  ViroRenderer
//
//  Created by Raj Advani on 11/17/15.
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

#include "VROMaterial.h"
#include "VROAnimationFloat.h"
#include "VROMaterialSubstrate.h"
#include "VRODriver.h"
#include "VROTransaction.h"
#include "VROAllocationTracker.h"
#include "VROSortKey.h"
#include "VROThreadRestricted.h"
#include "VROLog.h"
#include "VROShaderModifier.h"
#include <atomic>
#include <algorithm>

static std::atomic_int sMaterialId;

VROMaterial::VROMaterial() : VROThreadRestricted(VROThreadName::Renderer),
    _materialId(sMaterialId++),
    _shininess(2.0),
    _fresnelExponent(1.0),
    _transparency(1.0),
    _transparencyMode(VROTransparencyMode::AOne),
    _alphaCutoff(0.5f),  // glTF 2.0 default value
    _lightingModel(VROLightingModel::Constant),
    _litPerPixel(true),
    _cullMode(VROCullMode::Back),
    _blendMode(VROBlendMode::Alpha),
    _writesToDepthBuffer(true),
    _readsFromDepthBuffer(true),
    _colorWriteMask(VROColorMaskAll),
    _bloomThreshold(-1),
    _postProcessMask(false),
    _receivesShadows(true),
    _castsShadows(true),
    _chromaKeyFilteringEnabled(false),
    _chromaKeyFilteringColor({ 0, 1, 0 }),
    _needsToneMapping(true),
    _renderingOrder(0),
    _substrate(nullptr) {
   
    _diffuse          = new VROMaterialVisual(*this, (int)VROTextureType::None |
                                                     (int)VROTextureType::Texture2D |
                                                     (int)VROTextureType::TextureCube |
                                                     (int)VROTextureType::TextureEGLImage);
    _roughness        = new VROMaterialVisual(*this, (int)VROTextureType::None |
                                                     (int)VROTextureType::Texture2D,
                                              { 0.484529, 0.484529, 0.484529, 1.0 }); // Sensible default for shapes
    _metalness        = new VROMaterialVisual(*this, (int)VROTextureType::None |
                                                     (int)VROTextureType::Texture2D,
                                              { 0, 0, 0, 1.0 });
    _specular         = new VROMaterialVisual(*this, (int)VROTextureType::Texture2D);
    _normal           = new VROMaterialVisual(*this, (int)VROTextureType::Texture2D);
    _reflective       = new VROMaterialVisual(*this, (int)VROTextureType::TextureCube);
    _ambientOcclusion = new VROMaterialVisual(*this, (int)VROTextureType::None |
                                                     (int)VROTextureType::Texture2D);
        
    // TODO These are not yet implemented
    _emission         = new VROMaterialVisual(*this, (int)VROTextureType::None |
                                                     (int)VROTextureType::Texture2D,
                                              { 0, 0, 0, 1.0 }); // Default black: no emission unless explicitly set
    _multiply         = new VROMaterialVisual(*this, (int)VROTextureType::None |
                                                     (int)VROTextureType::Texture2D);
    _selfIllumination = new VROMaterialVisual(*this, (int)VROTextureType::None |
                                                     (int)VROTextureType::Texture2D);
        
    ALLOCATION_TRACKER_ADD(Materials, 1);
}

VROMaterial::VROMaterial(std::shared_ptr<VROMaterial> material) : VROThreadRestricted(VROThreadName::Renderer),
 _materialId(sMaterialId++),
 _name(material->_name),
 _shininess(material->_shininess),
 _fresnelExponent(material->_fresnelExponent),
 _transparency(material->_transparency),
 _transparencyMode(material->_transparencyMode),
 _alphaCutoff(material->_alphaCutoff),
 _lightingModel(material->_lightingModel),
 _litPerPixel(material->_litPerPixel),
 _cullMode(material->_cullMode),
 _blendMode(material->_blendMode),
 _writesToDepthBuffer(material->_writesToDepthBuffer),
 _readsFromDepthBuffer(material->_readsFromDepthBuffer),
 _colorWriteMask(material->_colorWriteMask),
 _bloomThreshold(material->_bloomThreshold),
 _postProcessMask(material->_postProcessMask),
 _receivesShadows(material->_receivesShadows),
 _castsShadows(material->_castsShadows),
 _chromaKeyFilteringEnabled(material->_chromaKeyFilteringEnabled),
 _chromaKeyFilteringColor(material->_chromaKeyFilteringColor),
 _needsToneMapping(material->needsToneMapping()),
 _renderingOrder(material->_renderingOrder),
 _substrate(nullptr),
 _shaderModifiers(material->_shaderModifiers),
 _shaderUniformFloats(material->_shaderUniformFloats),
 _shaderUniformVec3s(material->_shaderUniformVec3s),
 _shaderUniformVec4s(material->_shaderUniformVec4s),
 _shaderUniformMat4s(material->_shaderUniformMat4s) {

     _diffuse = new VROMaterialVisual(*this, *material->_diffuse);
     _roughness = new VROMaterialVisual(*this, *material->_roughness);
     _metalness = new VROMaterialVisual(*this, *material->_metalness);
     _specular = new VROMaterialVisual(*this, *material->_specular);
     _normal = new VROMaterialVisual(*this, *material->_normal);
     _reflective = new VROMaterialVisual(*this, *material->_reflective);
     _emission = new VROMaterialVisual(*this, *material->_emission);
     _multiply = new VROMaterialVisual(*this, *material->_multiply);
     _ambientOcclusion = new VROMaterialVisual(*this, *material->_ambientOcclusion);
     _selfIllumination = new VROMaterialVisual(*this, *material->_selfIllumination);
     
     ALLOCATION_TRACKER_ADD(Materials, 1);
}

VROMaterial::~VROMaterial() {
    delete (_diffuse);
    delete (_roughness);
    delete (_metalness);
    delete (_specular);
    delete (_normal);
    delete (_reflective);
    delete (_emission);
    delete (_multiply);
    delete (_ambientOcclusion);
    delete (_selfIllumination);
    delete (_substrate);
    
    ALLOCATION_TRACKER_SUB(Materials, 1);
}

void VROMaterial::deleteGL() {
    _diffuse->deleteGL();
    _roughness->deleteGL();
    _metalness->deleteGL();
    _specular->deleteGL();
    _normal->deleteGL();
    _reflective->deleteGL();
    _emission->deleteGL();
    _multiply->deleteGL();
    _ambientOcclusion->deleteGL();
    _selfIllumination->deleteGL();
}

void VROMaterial::copyFrom(std::shared_ptr<VROMaterial> material) {
    _name = material->_name;
    _shininess = material->_shininess;
    _fresnelExponent = material->_fresnelExponent;
    _transparency = material->_transparency;
    _transparencyMode = material->_transparencyMode;
    _alphaCutoff = material->_alphaCutoff;
    _lightingModel = material->_lightingModel;
    _litPerPixel = material->_litPerPixel;
    _cullMode = material->_cullMode;
    _blendMode = material->_blendMode;
    _writesToDepthBuffer = material->_writesToDepthBuffer;
    _readsFromDepthBuffer = material->_readsFromDepthBuffer;
    _colorWriteMask = material->_colorWriteMask;
    _bloomThreshold = material->_bloomThreshold;
    _postProcessMask = material->_postProcessMask;
    _receivesShadows = material->_receivesShadows;
    _castsShadows = material->_castsShadows;
    _chromaKeyFilteringEnabled = material->_chromaKeyFilteringEnabled;
    _chromaKeyFilteringColor = material->_chromaKeyFilteringColor;
    _needsToneMapping = material->_needsToneMapping;
    _renderingOrder = material->_renderingOrder;
    
    _substrate = nullptr;

    // Copy shader modifiers (DEBUG: if substrate doesn't see them, this is the problem)
    _shaderModifiers = material->_shaderModifiers;
    _shaderUniformFloats = material->_shaderUniformFloats;
    _shaderUniformVec3s = material->_shaderUniformVec3s;
    _shaderUniformVec4s = material->_shaderUniformVec4s;
    _shaderUniformMat4s = material->_shaderUniformMat4s;

    _diffuse->copyFrom(*material->_diffuse);
    _roughness->copyFrom(*material->_roughness);
    _metalness->copyFrom(*material->_metalness);
    _specular->copyFrom(*material->_specular);
    _normal->copyFrom(*material->_normal);
    _reflective->copyFrom(*material->_reflective);
    _emission->copyFrom(*material->_emission);
    _multiply->copyFrom(*material->_multiply);
    _ambientOcclusion->copyFrom(*material->_ambientOcclusion);
    _selfIllumination->copyFrom(*material->_selfIllumination);
}

void VROMaterial::prewarm(std::shared_ptr<VRODriver> driver) {
    getSubstrate(driver);
}

int VROMaterial::hydrateAsync(std::function<void()> callback,
                              std::shared_ptr<VRODriver> &driver) {
    int count = 0;
    VROMaterialVisual *visuals[10] = { _diffuse, _roughness, _metalness, _specular, _normal, _reflective,
                                       _emission, _multiply, _ambientOcclusion, _selfIllumination };
    for (int i = 0; i < 10; i++) {
        VROMaterialVisual *visual = visuals[i];
        if (visual->getTextureType() != VROTextureType::None) {
            if (!visual->getTexture()->isHydrated()) {
                visual->getTexture()->hydrateAsync(callback, driver);
                ++count;
            }
        }
    }
    return count;
}

bool VROMaterial::isHydrated() {
    if (_substrate == nullptr) {
        return false;
    }
    VROMaterialVisual *visuals[10] = { _diffuse, _roughness, _metalness, _specular, _normal, _reflective,
                                       _emission, _multiply, _ambientOcclusion, _selfIllumination };
    for (int i = 0; i < 10; i++) {
        VROMaterialVisual *visual = visuals[i];
        if (visual->getTextureType() != VROTextureType::None) {
            if (!visual->getTexture()->isHydrated()) {
                return false;
            }
        }
    }
    return true;
}

void VROMaterial::setTransparency(float transparency) {
    animate(std::make_shared<VROAnimationFloat>([](VROAnimatable *const animatable, float v) {
        ((VROMaterial *)animatable)->_transparency = v;
    }, _transparency, transparency));
}

void VROMaterial::setShininess(float shininess) {
    animate(std::make_shared<VROAnimationFloat>([](VROAnimatable *const animatable, float v) {
        ((VROMaterial *)animatable)->_shininess = v;
    }, _shininess, shininess));
}

void VROMaterial::setFresnelExponent(float fresnelExponent) {
    animate(std::make_shared<VROAnimationFloat>([](VROAnimatable *const animatable, float v) {
        ((VROMaterial *)animatable)->_fresnelExponent = v;
    }, _fresnelExponent, fresnelExponent));
}

void VROMaterial::fadeSnapshot() {
    if (VROThreadRestricted::isThread(VROThreadName::Renderer)) {
        std::shared_ptr<VROTransaction> transaction = VROTransaction::get();
        if (transaction && !transaction->isDegenerate()) {
            std::shared_ptr<VROMaterial> shared = std::static_pointer_cast<VROMaterial>(shared_from_this());
            std::shared_ptr<VROMaterial> outgoing = std::make_shared<VROMaterial>(shared);
            _outgoing = outgoing;

            // Fade the incoming material (this material) in, up to its previous transparency
            float previousTransparency = _transparency;
            _transparency = 0.0;
            animate(std::make_shared<VROAnimationFloat>(
                    [](VROAnimatable *const animatable, float v) {
                        ((VROMaterial *) animatable)->_transparency = v;
                    },
                    0.0, previousTransparency,
                    [outgoing](VROAnimatable *const animatable) {
                        VROMaterial *material = ((VROMaterial *) animatable);
                        // Ensure we're not removing a more recent animation
                        if (outgoing == material->_outgoing) {
                            material->removeOutgoingMaterial();
                        }
                    }
            ));

            // Fade the outgoing material out as well; it looks better to cross-fade between materials,
            // so there won't be a 'pop' effect when we suddenly remove the outgoing material
            // (when the incoming material is opaque this is not a problem because it completely
            // blocks the outgoing material as its transparency reaches 1.0).

            // VA: Before we had a conditional if(previousTransparency < 1.0), now we fade out regardless
            //     since textures can contain an alpha texture.
            // RA: the problem with removing the previousTransparency conditional is that when we crossfade two
            //     opaque materials, we end up with opacity < 1.0 during the crossfade. e.g., when incoming opacity
            //     is 0.25 and outgoing is 0.75, the total opacity is < 1.0.

            // TODO Figure out how to cross-fade in a way that works for both opaque and transparent materials
            _outgoing->_transparency = previousTransparency;
            _outgoing->animate(std::make_shared<VROAnimationFloat>(
                    [](VROAnimatable *const animatable, float v) {
                        ((VROMaterial *) animatable)->_transparency = v;
                    }, previousTransparency, 0.0));
        }
    }
}

void VROMaterial::removeOutgoingMaterial() {
    _outgoing.reset();
}

void VROMaterial::updateSubstrateTextures() {
    if (_substrate) {
        _substrate->updateTextures();
    }
}

void VROMaterial::updateSubstrate() {
    passert_thread(__func__);
    if (_substrate != nullptr) {
        delete (_substrate);
    }
    _substrate = nullptr;
}

VROMaterialSubstrate *const VROMaterial::getSubstrate(std::shared_ptr<VRODriver> &driver) {
    if (!_substrate) {
        _substrate = driver->newMaterialSubstrate(*this);
    }
    return _substrate;
}

void VROMaterial::updateSortKey(VROSortKey &key, const std::vector<std::shared_ptr<VROLight>> &lights,
                                const VRORenderContext &context,
                                std::shared_ptr<VRODriver> &driver) {
    key.material = _materialId;
    getSubstrate(driver)->updateSortKey(key, lights, context, driver);
}

void VROMaterial::bindProperties(std::shared_ptr<VRODriver> &driver) {
    driver->setCullMode(_cullMode);
    driver->setDepthReadingEnabled(_readsFromDepthBuffer);
    driver->setDepthWritingEnabled(_writesToDepthBuffer);
    driver->setMaterialColorWritingMask(_colorWriteMask);
    driver->setBlendingMode(_blendMode);
    getSubstrate(driver)->bindProperties(driver);
}

bool VROMaterial::bindShader(int lightsHash,
                             const std::vector<std::shared_ptr<VROLight>> &lights,
                             const VRORenderContext &context,
                             std::shared_ptr<VRODriver> &driver) {
    return getSubstrate(driver)->bindShader(lightsHash, lights, context, driver);
}

bool VROMaterial::hasDiffuseAlpha() const {
    if (_diffuse->getTextureType() == VROTextureType::None) {
        return _diffuse->getColor().w < (1.0 - kEpsilon);
    }
    else {
        return _diffuse->getColor().w < (1.0 - kEpsilon) ||
               _diffuse->getTexture()->hasAlpha();
    }
}

void VROMaterial::setChromaKeyFilteringEnabled(bool enabled) {
    _chromaKeyFilteringEnabled = enabled;
    updateSubstrate();
}

void VROMaterial::setChromaKeyFilteringColor(VROVector3f color) {
    _chromaKeyFilteringColor = color;
    updateSubstrate();
}

void VROMaterial::setNeedsToneMapping(bool needsToneMapping) {
    _needsToneMapping = needsToneMapping;
}

// ============================================================
// Semantic Masking
// ============================================================

void VROMaterial::setSemanticMaskEnabled(bool enabled) {
    if (_semanticMaskEnabled == enabled) {
        return;
    }
    _semanticMaskEnabled = enabled;
    applySemanticMaskModifier();
}

void VROMaterial::setSemanticMaskMode(VROSemanticMaskMode mode) {
    _semanticMaskMode = mode;
}

void VROMaterial::setSemanticLabelMask(uint16_t mask) {
    _semanticLabelMask = mask;
}

void VROMaterial::applySemanticMaskModifier() {
    // Remove old modifier if present.
    if (_semanticMaskModifier) {
        removeShaderModifier(_semanticMaskModifier);
        _semanticMaskModifier = nullptr;
    }

    if (!_semanticMaskEnabled) {
        return;
    }

    // Fragment shader modifier lines. The VROShaderModifier constructor splits these:
    // - lines starting with "uniform " → uniforms section
    // - other lines → body section
    //
    // semantic_texture           : R8 label texture; each texel = VROSemanticLabel value (0-11).
    // semantic_confidence_texture: R8 confidence texture; 0=uncertain, 1=certain (after /255).
    // semantic_mask_mode : 0 = ShowOnly  — keep fragments where label IS in mask
    //                      1 = Hide      — keep fragments where label is NOT in mask
    //                      2 = Debug     — colorise by label (no masking)
    // semantic_label_mask: bitmask float; bit N set → label N is selected.
    // ar_viewport_size   : always-available built-in (vec3, xy = width/height in pixels).
    //
    // Alpha-blend approach (replaces discard):
    //   ShowOnly: alpha *= conf  when matched, alpha *= 0 otherwise
    //   Hide:     alpha *= 0     when matched, alpha *= 1 otherwise
    //             (matched boundary pixels get alpha = 1-conf, giving soft hide edges)
    // The material's default VROBlendMode::Alpha handles the compositing.
    std::vector<std::string> lines = {
        // parseCustomUniforms sees "uniform sampler2D" and places each sampler in
        // _modifierSamplers; loadTextures() binds them to the matching VROGlobalTextureType.
        "uniform sampler2D semantic_texture;",
        "uniform sampler2D semantic_confidence_texture;",
        // ar_viewport_size and ar_semantic_texture_transform are base program uniforms
        // (VROShaderProgram::addUniform) bound unconditionally each frame.
        // ar_semantic_texture_transform = getViewportToCameraImageTransform() in GL convention
        // (y=0 bottom), matching gl_FragCoord directly — no Y-flip needed.
        "uniform highp vec3 ar_viewport_size;",
        "uniform highp mat4 ar_semantic_texture_transform;",
        "uniform highp float semantic_mask_mode;",
        "uniform highp float semantic_label_mask;",
        // Compute shared UV once. No Y-flip: gl_FragCoord and the transform are both GL-convention.
        "highp vec2 semUV = (ar_semantic_texture_transform * vec4(gl_FragCoord.xy / ar_viewport_size.xy, 0.0, 1.0)).xy;",
        "highp float label_raw = texture(semantic_texture, semUV).r * 255.0;",
        "int semLabel = int(floor(label_raw + 0.5));",
        "bool semMatches = (semLabel >= 1 && semLabel <= 11 && (int(semantic_label_mask) & (1 << semLabel)) != 0);",
        // Debug mode (semantic_mask_mode == 2): colorise by label for overlay visualisation.
        // Blue = unlabeled, teal→orange gradient = classified pixels (fully opaque).
        "if (semantic_mask_mode >= 1.5) {",
        "    highp float t = label_raw / 11.0;",
        "    _output_color = vec4(t, float(semLabel > 0) * 0.8, 1.0 - t, 1.0);",
        "} else {",
        // Confidence value from texture [0,1]. Used as alpha weight for soft edges.
        // On iOS (no platform confidence) the texture is 1x1 white → conf = 1.0 → hard edges.
        "    highp float conf = texture(semantic_confidence_texture, semUV).r;",
        // ShowOnly (mode=0): show matched pixels with weight = conf; hide everything else.
        // Hide (mode=1): hide matched pixels; show others at full opacity.
        "    if (semantic_mask_mode < 0.5) {",
        "        _output_color.a *= semMatches ? conf : 0.0;",
        "    } else {",
        "        _output_color.a *= semMatches ? (1.0 - conf) : 1.0;",
        "    }",
        "}"
    };

    _semanticMaskModifier = std::make_shared<VROShaderModifier>(
        VROShaderEntryPoint::Fragment, lines);
    _semanticMaskModifier->setName("semantic_mask");

    // Capture current values by value — if mode/mask change, applySemanticMaskModifier()
    // is called again, creating a new modifier with updated captures.
    float modeVal = (float)static_cast<int>(_semanticMaskMode);
    float maskVal = (float)_semanticLabelMask;
    _semanticMaskModifier->setUniformBinder("semantic_mask_mode", VROShaderProperty::Float,
        [modeVal](VROUniform *uniform, const VROGeometry *, const VROMaterial *) {
            uniform->setFloat(modeVal);
        });
    _semanticMaskModifier->setUniformBinder("semantic_label_mask", VROShaderProperty::Float,
        [maskVal](VROUniform *uniform, const VROGeometry *, const VROMaterial *) {
            uniform->setFloat(maskVal);
        });

    addShaderModifier(_semanticMaskModifier);
}
