//
//  Material_JNI.cpp
//  ViroRenderer
//
//  Copyright © 2016 Viro Media. All rights reserved.
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

#include <VROPlatformUtil.h>
#include "Material_JNI.h"
#include "VROStringUtil.h"
#include "VROLog.h"
#include "VROARShadow.h"
#include "VROShaderModifier.h"
#include "VROMatrix4f.h"
#include <sstream>

#if VRO_PLATFORM_ANDROID
#define VRO_METHOD(return_type, method_name) \
  JNIEXPORT return_type JNICALL              \
      Java_com_viro_core_Material_##method_name
#else
#define VRO_METHOD(return_type, method_name) \
    return_type Material_##method_name
#endif

VROVector4f parseColor(VRO_LONG color) {
    float a = ((color >> 24) & 0xFF) / 255.0;
    float r = ((color >> 16) & 0xFF) / 255.0;
    float g = ((color >> 8) & 0xFF) / 255.0;
    float b = (color & 0xFF) / 255.0;
    return {r, g, b, a};
}

extern "C" {

VROLightingModel parseLightingModel(std::string strName) {
    if (VROStringUtil::strcmpinsensitive(strName, "Blinn")) {
        return VROLightingModel::Blinn;
    }
    else if (VROStringUtil::strcmpinsensitive(strName, "Lambert")) {
        return VROLightingModel::Lambert;
    }
    else if (VROStringUtil::strcmpinsensitive(strName, "Phong")) {
        return VROLightingModel::Phong;
    }
    else if (VROStringUtil::strcmpinsensitive(strName, "PBR")) {
        return VROLightingModel::PhysicallyBased;
    }
    else {
        // Default lightingModel is Constant, so no use checking.
        return VROLightingModel::Constant;
    }
}

VROBlendMode parseBlendMode(std::string blendMode) {
    if (VROStringUtil::strcmpinsensitive(blendMode, "None")) {
        return VROBlendMode::None;
    }
    else if (VROStringUtil::strcmpinsensitive(blendMode, "Alpha")) {
        return VROBlendMode::Alpha;
    }
    else if (VROStringUtil::strcmpinsensitive(blendMode, "Add")) {
        return VROBlendMode::Add;
    }
    else if (VROStringUtil::strcmpinsensitive(blendMode, "Subtract")) {
        return VROBlendMode::Subtract;
    }
    else if (VROStringUtil::strcmpinsensitive(blendMode, "Multiply")) {
        return VROBlendMode::Multiply;
    }
    else if (VROStringUtil::strcmpinsensitive(blendMode, "Screen")) {
        return VROBlendMode::Screen;
    }
    else {
        return VROBlendMode::None;
    }
}

VROTransparencyMode parseTransparencyMode(std::string strName) {
    if (VROStringUtil::strcmpinsensitive(strName, "RGBZero")) {
        return VROTransparencyMode::RGBZero;
    }
    else {
        // Default transparencyMode is AOne, so no use checking.
        return VROTransparencyMode::AOne;
    }
}

VROCullMode parseCullMode(std::string strName) {
    if (VROStringUtil::strcmpinsensitive(strName, "None")) {
        return VROCullMode::None;
    }
    else if (VROStringUtil::strcmpinsensitive(strName, "Front")) {
        return VROCullMode::Front;
    } else {
        // Default cullMode is Back, so no use checking.
        return VROCullMode::Back;
    }
}

VROColorMask parseColorMask(std::string strName) {
    if (VROStringUtil::strcmpinsensitive(strName, "Red")) {
        return VROColorMaskRed;
    } else if (VROStringUtil::strcmpinsensitive(strName, "Green")) {
        return VROColorMaskGreen;
    } else if (VROStringUtil::strcmpinsensitive(strName, "Blue")) {
        return VROColorMaskBlue;
    } else if (VROStringUtil::strcmpinsensitive(strName, "Alpha")) {
        return VROColorMaskAlpha;
    } else if (VROStringUtil::strcmpinsensitive(strName, "All")) {
        return VROColorMaskAll;
    } else if (VROStringUtil::strcmpinsensitive(strName, "None")) {
        return VROColorMaskNone;
    } else {
        // Default color mask is All.
        return VROColorMaskAll;
    }
}

VROColorMask parseColorMaskArray(VRO_ENV env, VRO_STRING_ARRAY masks_j) {
    VROColorMask mask = VROColorMaskNone;

    int numMasks = VRO_ARRAY_LENGTH(masks_j);
    for (int i = 0; i < numMasks; i++) {
        VRO_STRING mask_j = VRO_STRING_ARRAY_GET(masks_j, i);
        std::string mask_s = VRO_STRING_STL(mask_j);
        mask = (VROColorMask) (mask | parseColorMask(mask_s));
    }
    return mask;
}

VROShaderEntryPoint parseShaderEntryPoint(std::string strName) {
    if (VROStringUtil::strcmpinsensitive(strName, "geometry")) {
        return VROShaderEntryPoint::Geometry;
    }
    else if (VROStringUtil::strcmpinsensitive(strName, "vertex")) {
        return VROShaderEntryPoint::Vertex;
    }
    else if (VROStringUtil::strcmpinsensitive(strName, "surface")) {
        return VROShaderEntryPoint::Surface;
    }
    else if (VROStringUtil::strcmpinsensitive(strName, "fragment")) {
        return VROShaderEntryPoint::Fragment;
    }
    else if (VROStringUtil::strcmpinsensitive(strName, "lightingModel")) {
        return VROShaderEntryPoint::LightingModel;
    }
    else if (VROStringUtil::strcmpinsensitive(strName, "image")) {
        return VROShaderEntryPoint::Image;
    }
    else {
        // Default to Fragment if unknown
        pwarn("Unknown shader entry point [%s], defaulting to Fragment", strName.c_str());
        return VROShaderEntryPoint::Fragment;
    }
}

VRO_METHOD(VRO_REF(VROMaterial), nativeCreateMaterial)(VRO_NO_ARGS) {
    std::shared_ptr<VROMaterial> materialPtr = std::make_shared<VROMaterial>();
    return VRO_REF_NEW(VROMaterial, materialPtr);
}

VRO_METHOD(VRO_REF(VROMaterial), nativeCopyMaterial)(VRO_ARGS
                                                      VRO_REF(VROMaterial) source_j) {
    VRO_METHOD_PREAMBLE;

    std::shared_ptr<VROMaterial> source = VRO_REF_GET(VROMaterial, source_j);
    if (!source) {
        return 0;
    }

    // Use the C++ copy constructor which copies all properties including shader modifiers
    std::shared_ptr<VROMaterial> copy = std::make_shared<VROMaterial>(source);
    return VRO_REF_NEW(VROMaterial, copy);
}

VRO_METHOD(VRO_REF(VROMaterial), nativeCreateImmutableMaterial)(VRO_ARGS
                                                   VRO_STRING lightingModel, VRO_LONG diffuseColor, VRO_REF(VROTexture) diffuseTexture,
                                                   VRO_FLOAT diffuseIntensity, VRO_REF(VROTexture) specularTexture,
                                                   VRO_FLOAT shininess, VRO_FLOAT fresnelExponent, VRO_REF(VROTexture) normalMap, VRO_STRING cullMode,
                                                   VRO_STRING transparencyMode, VRO_STRING blendMode, VRO_FLOAT bloomThreshold,
                                                   VRO_BOOL writesToDepthBuffer, VRO_BOOL readsFromDepthBuffer,
                                                   VRO_STRING_ARRAY colorWriteMask) {
    VRO_METHOD_PREAMBLE;

    std::shared_ptr<VROMaterial> material = std::make_shared<VROMaterial>();
    material->setThreadRestrictionEnabled(false);

    material->setLightingModel(parseLightingModel(VRO_STRING_STL(lightingModel)));
    material->getDiffuse().setColor(parseColor(diffuseColor));
    if (diffuseTexture != 0) { material->getDiffuse().setTexture(VRO_REF_GET(VROTexture, diffuseTexture)); }
    material->getDiffuse().setIntensity(diffuseIntensity);
    if (specularTexture != 0) { material->getSpecular().setTexture(VRO_REF_GET(VROTexture, specularTexture)); }
    material->setShininess(shininess);
    material->setFresnelExponent(fresnelExponent);
    if (normalMap != 0) { material->getNormal().setTexture(VRO_REF_GET(VROTexture, normalMap)); }
    material->setCullMode(parseCullMode(VRO_STRING_STL(cullMode)));
    material->setTransparencyMode(parseTransparencyMode(VRO_STRING_STL(transparencyMode)));
    material->setBlendMode(parseBlendMode(VRO_STRING_STL(blendMode)));
    material->setBloomThreshold(bloomThreshold);
    material->setWritesToDepthBuffer(writesToDepthBuffer);
    material->setReadsFromDepthBuffer(readsFromDepthBuffer);
    material->setColorWriteMask(parseColorMaskArray(env, colorWriteMask));
    material->setThreadRestrictionEnabled(true);

    return VRO_REF_NEW(VROMaterial, material);
}

VRO_METHOD(void, nativeSetWritesToDepthBuffer)(VRO_ARGS
                                               VRO_REF(VROMaterial) material_j,
                                               VRO_BOOL writesToDepthBuffer) {
    std::weak_ptr<VROMaterial> material_w = VRO_REF_GET(VROMaterial, material_j);
    VROPlatformDispatchAsyncRenderer([material_w, writesToDepthBuffer] {
        std::shared_ptr<VROMaterial> material = material_w.lock();
        if (material) {
            material->setWritesToDepthBuffer(writesToDepthBuffer);
        }
    });
}

VRO_METHOD(void, nativeSetReadsFromDepthBuffer)(VRO_ARGS
                                                VRO_REF(VROMaterial) material_j,
                                                VRO_BOOL readsFromDepthBuffer) {
    std::weak_ptr<VROMaterial> material_w = VRO_REF_GET(VROMaterial, material_j);
    VROPlatformDispatchAsyncRenderer([material_w, readsFromDepthBuffer] {
        std::shared_ptr<VROMaterial> material = material_w.lock();
        if (material) {
            material->setReadsFromDepthBuffer(readsFromDepthBuffer);
        }
    });
}

VRO_METHOD(void, nativeSetTexture)(VRO_ARGS
                                   VRO_REF(VROMaterial) material_j,
                                   VRO_REF(VROTexture) textureRef,
                                   VRO_STRING materialPropertyName) {
    VRO_METHOD_PREAMBLE;
    std::string strName = VRO_STRING_STL(materialPropertyName);

    std::shared_ptr<VROTexture> texture;
    if (textureRef) {
        texture = VRO_REF_GET(VROTexture, textureRef);
    }
    std::weak_ptr<VROMaterial> material_w = VRO_REF_GET(VROMaterial, material_j);

    VROPlatformDispatchAsyncRenderer([texture, material_w, strName] {
        std::shared_ptr<VROMaterial> material = material_w.lock();
        if (material) {
            // Depending on the name, set the texture
            if (VROStringUtil::strcmpinsensitive(strName, "diffuseTexture")) {
                material->getDiffuse().setTexture(texture);
            } else if (VROStringUtil::strcmpinsensitive(strName, "specularTexture")) {
                material->getSpecular().setTexture(texture);
            } else if (VROStringUtil::strcmpinsensitive(strName, "normalTexture")) {
                material->getNormal().setTexture(texture);
            } else if (VROStringUtil::strcmpinsensitive(strName, "reflectiveTexture")) {
                material->getReflective().setTexture(texture);
            } else if (VROStringUtil::strcmpinsensitive(strName, "emissionTexture")) {
                material->getEmission().setTexture(texture);
            } else if (VROStringUtil::strcmpinsensitive(strName, "roughnessTexture")) {
                material->getRoughness().setTexture(texture);
            } else if (VROStringUtil::strcmpinsensitive(strName, "metalnessTexture")) {
                material->getMetalness().setTexture(texture);
            } else if (VROStringUtil::strcmpinsensitive(strName, "multiplyTexture")) {
                material->getMultiply().setTexture(texture);
            } else if (VROStringUtil::strcmpinsensitive(strName, "ambientOcclusionTexture")) {
                material->getAmbientOcclusion().setTexture(texture);
            } else if (VROStringUtil::strcmpinsensitive(strName, "selfIlluminationTexture")) {
                material->getSelfIllumination().setTexture(texture);
            }
        }
    });
}

VRO_METHOD(void, nativeSetColor)(VRO_ARGS
                                 VRO_REF(VROMaterial) material_j,
                                 VRO_LONG color,
                                 VRO_STRING materialPropertyName) {
    VRO_METHOD_PREAMBLE;
    std::string strName = VRO_STRING_STL(materialPropertyName);
    VROVector4f vecColor = parseColor(color);

    std::weak_ptr<VROMaterial> material_w = VRO_REF_GET(VROMaterial, material_j);
    VROPlatformDispatchAsyncRenderer([vecColor, material_w, strName] {
        std::shared_ptr<VROMaterial> material = material_w.lock();
        if (material) {
            // Depending on the name, set the correct color
            if (VROStringUtil::strcmpinsensitive(strName, "diffuseColor")) {
                material->getDiffuse().setColor(vecColor);
            } else if (VROStringUtil::strcmpinsensitive(strName, "specularColor")) {
                material->getSpecular().setColor(vecColor);
            } else if (VROStringUtil::strcmpinsensitive(strName, "normalColor")) {
                material->getNormal().setColor(vecColor);
            } else if (VROStringUtil::strcmpinsensitive(strName, "reflectiveColor")) {
                material->getReflective().setColor(vecColor);
            } else if (VROStringUtil::strcmpinsensitive(strName, "emissionColor")) {
                material->getEmission().setColor(vecColor);
            } else if (VROStringUtil::strcmpinsensitive(strName, "multiplyColor")) {
                material->getMultiply().setColor(vecColor);
            } else if (VROStringUtil::strcmpinsensitive(strName, "ambientOcclusionColor")) {
                material->getAmbientOcclusion().setColor(vecColor);
            } else if (VROStringUtil::strcmpinsensitive(strName, "selfIlluminationColor")) {
                material->getSelfIllumination().setColor(vecColor);
            }
        }
    });
}

VRO_METHOD(void, nativeSetFloat)(VRO_ARGS
                                 VRO_REF(VROMaterial) material_j,
                                 VRO_FLOAT value,
                                 VRO_STRING name_j) {
    VRO_METHOD_PREAMBLE;
    std::string name_s = VRO_STRING_STL(name_j);

    std::weak_ptr<VROMaterial> material_w = VRO_REF_GET(VROMaterial, material_j);
    VROPlatformDispatchAsyncRenderer([value, material_w, name_s] {
        std::shared_ptr<VROMaterial> material = material_w.lock();
        if (material) {
            if (VROStringUtil::strcmpinsensitive(name_s, "metalness")) {
                material->getMetalness().setColor({ value, value, value, 1.0 });
            } else if (VROStringUtil::strcmpinsensitive(name_s, "roughness")) {
                material->getRoughness().setColor({ value, value, value, 1.0 });
            }
        }
    });
}

VRO_METHOD(void, nativeSetShininess)(VRO_ARGS
                                     VRO_REF(VROMaterial) material_j,
                                     VRO_DOUBLE shininess) {
    VRO_METHOD_PREAMBLE;

    std::weak_ptr<VROMaterial> material_w = VRO_REF_GET(VROMaterial, material_j);
    VROPlatformDispatchAsyncRenderer([material_w, shininess] {
        std::shared_ptr<VROMaterial> material = material_w.lock();
        if (material) {
            material->setShininess(shininess);
        }
    });
}

VRO_METHOD(void, nativeSetFresnelExponent)(VRO_ARGS
                                           VRO_REF(VROMaterial) material_j,
                                           VRO_DOUBLE fresnelExponent) {
    std::weak_ptr<VROMaterial> material_w = VRO_REF_GET(VROMaterial, material_j);
    VROPlatformDispatchAsyncRenderer([material_w, fresnelExponent] {
        std::shared_ptr<VROMaterial> material = material_w.lock();
        if (material) {
            material->setFresnelExponent(fresnelExponent);
        }
    });
}

VRO_METHOD(void, nativeSetLightingModel)(VRO_ARGS
                                         VRO_REF(VROMaterial) material_j,
                                         VRO_STRING lightingModelName) {
    VRO_METHOD_PREAMBLE;

    std::string strName = VRO_STRING_STL(lightingModelName);
    std::weak_ptr<VROMaterial> material_w = VRO_REF_GET(VROMaterial, material_j);

    VROPlatformDispatchAsyncRenderer([material_w, strName] {
        std::shared_ptr<VROMaterial> material = material_w.lock();
        if (material) {
            material->setLightingModel(parseLightingModel(strName));
        }
    });
}

VRO_METHOD(void, nativeSetBlendMode)(VRO_ARGS
                                     VRO_REF(VROMaterial) material_j,
                                     VRO_STRING blendMode_s) {
    VRO_METHOD_PREAMBLE;

    std::string blendMode = VRO_STRING_STL(blendMode_s);
    std::weak_ptr<VROMaterial> material_w = VRO_REF_GET(VROMaterial, material_j);

    VROPlatformDispatchAsyncRenderer([material_w, blendMode] {
        std::shared_ptr<VROMaterial> material = material_w.lock();
        if (material) {
            material->setBlendMode(parseBlendMode(blendMode));
        }
    });
}

VRO_METHOD(void, nativeSetTransparencyMode)(VRO_ARGS
                                            VRO_REF(VROMaterial) material_j,
                                            VRO_STRING transparencyModeName) {
    VRO_METHOD_PREAMBLE;

    std::string strName = VRO_STRING_STL(transparencyModeName);
    std::weak_ptr<VROMaterial> material_w = VRO_REF_GET(VROMaterial, material_j);

    VROPlatformDispatchAsyncRenderer([material_w, strName] {
        std::shared_ptr<VROMaterial> material = material_w.lock();
        if (material) {
            material->setTransparencyMode(parseTransparencyMode(strName));
        }
    });
}

VRO_METHOD(void, nativeSetCullMode)(VRO_ARGS
                                    VRO_REF(VROMaterial) material_j,
                                    VRO_STRING cullModeName) {
    VRO_METHOD_PREAMBLE;

    std::string strName = VRO_STRING_STL(cullModeName);
    std::weak_ptr<VROMaterial> material_w = VRO_REF_GET(VROMaterial, material_j);

    VROPlatformDispatchAsyncRenderer([material_w, strName] {
        std::shared_ptr<VROMaterial> material = material_w.lock();
        if (material) {
            material->setCullMode(parseCullMode(strName));
        }
    });
}

VRO_METHOD(void, nativeSetDiffuseIntensity)(VRO_ARGS
                                            VRO_REF(VROMaterial) material_j, VRO_FLOAT diffuseIntensity) {
    std::weak_ptr<VROMaterial> material_w = VRO_REF_GET(VROMaterial, material_j);
    VROPlatformDispatchAsyncRenderer([material_w, diffuseIntensity] {
        std::shared_ptr<VROMaterial> material = material_w.lock();
        if (material) {
            material->getDiffuse().setIntensity(diffuseIntensity);
        }
    });
}

VRO_METHOD(void, nativeSetBloomThreshold)(VRO_ARGS
                                          VRO_REF(VROMaterial) material_j, VRO_FLOAT bloomThreshold) {
    std::weak_ptr<VROMaterial> material_w = VRO_REF_GET(VROMaterial, material_j);
    VROPlatformDispatchAsyncRenderer([material_w, bloomThreshold] {
        std::shared_ptr<VROMaterial> material = material_w.lock();
        if (material) {
            material->setBloomThreshold(bloomThreshold);
        }
    });
}

VRO_METHOD(void, nativeDestroyMaterial)(VRO_ARGS
                                        VRO_REF(VROMaterial) nativeRef) {
    VRO_REF_DELETE(VROMaterial, nativeRef);
}

VRO_METHOD(void, nativeSetShadowMode(VRO_ARGS
                                     VRO_REF(VROMaterial) material_j, VRO_STRING shadow_j)) {
    VRO_METHOD_PREAMBLE;

    std::string shadow_s = VRO_STRING_STL(shadow_j);
    std::weak_ptr<VROMaterial> material_w = VRO_REF_GET(VROMaterial, material_j);

    VROPlatformDispatchAsyncRenderer([material_w, shadow_s] {
        std::shared_ptr<VROMaterial> material = material_w.lock();
        if (!material) {
            return;
        }

        if (VROStringUtil::strcmpinsensitive(shadow_s, "Disabled")) {
            VROARShadow::remove(material);
            material->setReceivesShadows(false);
        }
        else if (VROStringUtil::strcmpinsensitive(shadow_s, "Transparent")) {
            VROARShadow::apply(material);
            material->setReceivesShadows(true);
        }
        else { // Normal
            VROARShadow::remove(material);
            material->setReceivesShadows(true);
        }
    });
}

VRO_METHOD(void, nativeSetName(VRO_ARGS
                               VRO_REF(VROMaterial) jMaterial, VRO_STRING jName)) {
    VRO_METHOD_PREAMBLE;

    std::string strName = VRO_STRING_STL(jName);
    std::shared_ptr<VROMaterial> material = VRO_REF_GET(VROMaterial, jMaterial);

    // Set name synchronously during material initialization to avoid race conditions
    // with shader modifiers that are also set synchronously
    if (material) {
        material->setThreadRestrictionEnabled(false);
        material->setName(strName);
        material->setThreadRestrictionEnabled(true);
    }
}

VRO_METHOD(void, nativeSetChromaKeyFilteringEnabled)(VRO_ARGS
                                                     VRO_REF(VROMaterial) material_j, VRO_BOOL enabled) {
    std::weak_ptr<VROMaterial> material_w = VRO_REF_GET(VROMaterial, material_j);

    VROPlatformDispatchAsyncRenderer([material_w, enabled] {
        std::shared_ptr<VROMaterial> material = material_w.lock();
        if (!material) {
            return;
        }
        material->setChromaKeyFilteringEnabled(enabled);
    });
}

VRO_METHOD(void, nativeSetChromaKeyFilteringColor)(VRO_ARGS
                                                   VRO_REF(VROMaterial) material_j, VRO_LONG color_j) {
    std::weak_ptr<VROMaterial> material_w = VRO_REF_GET(VROMaterial, material_j);
    VROVector4f color = parseColor(color_j);

    VROPlatformDispatchAsyncRenderer([material_w, color] {
        std::shared_ptr<VROMaterial> material = material_w.lock();
        if (!material) {
            return;
        }
        material->setChromaKeyFilteringColor({ color.x, color.y, color.z });
    });
}

VRO_METHOD(void, nativeSetColorWriteMask)(VRO_ARGS
                                          VRO_REF(VROMaterial) material_j, VRO_STRING_ARRAY masks_j) {
    VRO_METHOD_PREAMBLE;
    std::weak_ptr<VROMaterial> material_w = VRO_REF_GET(VROMaterial, material_j);
    VROColorMask mask = parseColorMaskArray(env, masks_j);

    VROPlatformDispatchAsyncRenderer([material_w, mask] {
        std::shared_ptr<VROMaterial> material = material_w.lock();
        if (!material) {
            return;
        }
        material->setColorWriteMask(mask);
    });
}

VRO_METHOD(void, nativeAddShaderModifier)(VRO_ARGS
                                          VRO_REF(VROMaterial) material_j,
                                          VRO_STRING entryPoint_j,
                                          VRO_STRING shaderCode_j) {
    VRO_METHOD_PREAMBLE;

    std::string entryPointStr = VRO_STRING_STL(entryPoint_j);
    std::string shaderCodeStr = VRO_STRING_STL(shaderCode_j);

    VROShaderEntryPoint entryPoint = parseShaderEntryPoint(entryPointStr);

    // Split shader code into lines
    std::vector<std::string> lines;
    std::stringstream ss(shaderCodeStr);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    // Add shader modifiers synchronously during material creation to avoid race conditions.
    // Temporarily disable thread restrictions like the immutable constructor does.
    std::shared_ptr<VROMaterial> material = VRO_REF_GET(VROMaterial, material_j);
    if (material) {
        material->setThreadRestrictionEnabled(false);
        auto modifier = std::make_shared<VROShaderModifier>(entryPoint, lines);
        material->addShaderModifier(modifier);
        material->setThreadRestrictionEnabled(true);
    } else {
        pwarn("Material_JNI: Material reference is null!");
    }
}

VRO_METHOD(void, nativeSetShaderUniformFloat)(VRO_ARGS
                                              VRO_REF(VROMaterial) material_j,
                                              VRO_STRING uniformName_j,
                                              VRO_FLOAT value) {
    VRO_METHOD_PREAMBLE;

    std::string uniformName = VRO_STRING_STL(uniformName_j);

    std::weak_ptr<VROMaterial> material_w = VRO_REF_GET(VROMaterial, material_j);
    VROPlatformDispatchAsyncRenderer([material_w, uniformName, value] {
        std::shared_ptr<VROMaterial> material = material_w.lock();
        if (material) {
            material->setShaderUniform(uniformName, value);
        }
    });
}

VRO_METHOD(void, nativeSetShaderUniformVec3)(VRO_ARGS
                                             VRO_REF(VROMaterial) material_j,
                                             VRO_STRING uniformName_j,
                                             VRO_FLOAT x, VRO_FLOAT y, VRO_FLOAT z) {
    VRO_METHOD_PREAMBLE;

    std::string uniformName = VRO_STRING_STL(uniformName_j);
    VROVector3f value(x, y, z);

    std::weak_ptr<VROMaterial> material_w = VRO_REF_GET(VROMaterial, material_j);
    VROPlatformDispatchAsyncRenderer([material_w, uniformName, value] {
        std::shared_ptr<VROMaterial> material = material_w.lock();
        if (!material) {
            return;
        }
        material->setShaderUniform(uniformName, value);
    });
}

VRO_METHOD(void, nativeSetShaderUniformVec4)(VRO_ARGS
                                             VRO_REF(VROMaterial) material_j,
                                             VRO_STRING uniformName_j,
                                             VRO_FLOAT x, VRO_FLOAT y, VRO_FLOAT z, VRO_FLOAT w) {
    VRO_METHOD_PREAMBLE;

    std::string uniformName = VRO_STRING_STL(uniformName_j);
    VROVector4f value(x, y, z, w);

    std::weak_ptr<VROMaterial> material_w = VRO_REF_GET(VROMaterial, material_j);
    VROPlatformDispatchAsyncRenderer([material_w, uniformName, value] {
        std::shared_ptr<VROMaterial> material = material_w.lock();
        if (!material) {
            return;
        }
        material->setShaderUniform(uniformName, value);
    });
}

VRO_METHOD(void, nativeSetShaderUniformMat4)(VRO_ARGS
                                             VRO_REF(VROMaterial) material_j,
                                             VRO_STRING uniformName_j,
                                             VRO_FLOAT_ARRAY matrix_j) {
    VRO_METHOD_PREAMBLE;

    std::string uniformName = VRO_STRING_STL(uniformName_j);

    // Get the matrix elements from the Java array (should be 16 elements)
    int length = VRO_ARRAY_LENGTH(matrix_j);
    if (length != 16) {
        pwarn("Matrix array must have 16 elements for mat4, got %d", length);
        return;
    }

    VRO_FLOAT *elements = VRO_FLOAT_ARRAY_GET_ELEMENTS(matrix_j);
    VROMatrix4f value(elements);
    VRO_FLOAT_ARRAY_RELEASE_ELEMENTS(matrix_j, elements);

    std::weak_ptr<VROMaterial> material_w = VRO_REF_GET(VROMaterial, material_j);
    VROPlatformDispatchAsyncRenderer([material_w, uniformName, value] {
        std::shared_ptr<VROMaterial> material = material_w.lock();
        if (!material) {
            return;
        }
        material->setShaderUniform(uniformName, value);
    });
}

VRO_METHOD(void, nativeCopyShaderUniforms)(VRO_ARGS
                                           VRO_REF(VROMaterial) dest_j,
                                           VRO_REF(VROMaterial) source_j) {
    VRO_METHOD_PREAMBLE;

    std::shared_ptr<VROMaterial> source = VRO_REF_GET(VROMaterial, source_j);
    std::weak_ptr<VROMaterial> dest_w = VRO_REF_GET(VROMaterial, dest_j);

    if (!source) {
        return;
    }

    // Capture uniform values by value (copy them now on this thread)
    // This avoids race conditions when the render thread processes them
    auto floatUniforms = source->getShaderUniformFloats();
    auto vec3Uniforms = source->getShaderUniformVec3s();
    auto vec4Uniforms = source->getShaderUniformVec4s();
    auto mat4Uniforms = source->getShaderUniformMat4s();
    auto textureUniforms = source->getShaderUniformTextures();

    // Dispatch to render thread with captured values
    VROPlatformDispatchAsyncRenderer([dest_w, floatUniforms, vec3Uniforms, vec4Uniforms, mat4Uniforms, textureUniforms] {
        std::shared_ptr<VROMaterial> dest = dest_w.lock();
        if (!dest) {
            return;
        }

        // Copy float uniforms
        for (const auto &uniform : floatUniforms) {
            dest->setShaderUniform(uniform.first, uniform.second);
        }

        // Copy vec3 uniforms
        for (const auto &uniform : vec3Uniforms) {
            dest->setShaderUniform(uniform.first, uniform.second);
        }

        // Copy vec4 uniforms
        for (const auto &uniform : vec4Uniforms) {
            dest->setShaderUniform(uniform.first, uniform.second);
        }

        // Copy mat4 uniforms
        for (const auto &uniform : mat4Uniforms) {
            dest->setShaderUniform(uniform.first, uniform.second);
        }

        // Copy texture uniforms
        for (const auto &uniform : textureUniforms) {
            dest->setShaderUniform(uniform.first, uniform.second);
        }
    });
}

VRO_METHOD(void, nativeCopyShaderModifiers)(VRO_ARGS
                                            VRO_REF(VROMaterial) dest_j,
                                            VRO_REF(VROMaterial) source_j) {
    VRO_METHOD_PREAMBLE;

    std::shared_ptr<VROMaterial> source = VRO_REF_GET(VROMaterial, source_j);
    std::shared_ptr<VROMaterial> dest = VRO_REF_GET(VROMaterial, dest_j);

    if (!source || !dest) {
        return;
    }

    // Copy shader modifiers synchronously (during material setup)
    // Disable thread restrictions temporarily
    dest->setThreadRestrictionEnabled(false);

    // Copy all shader modifiers from source to destination
    for (const auto &modifier : source->getShaderModifiers()) {
        dest->addShaderModifier(modifier);
    }

    // Copy all uniforms as well
    for (const auto &uniform : source->getShaderUniformFloats()) {
        dest->setShaderUniform(uniform.first, uniform.second);
    }
    for (const auto &uniform : source->getShaderUniformVec3s()) {
        dest->setShaderUniform(uniform.first, uniform.second);
    }
    for (const auto &uniform : source->getShaderUniformVec4s()) {
        dest->setShaderUniform(uniform.first, uniform.second);
    }
    for (const auto &uniform : source->getShaderUniformMat4s()) {
        dest->setShaderUniform(uniform.first, uniform.second);
    }
    for (const auto &uniform : source->getShaderUniformTextures()) {
        dest->setShaderUniform(uniform.first, uniform.second);
    }

    dest->setThreadRestrictionEnabled(true);
}

VRO_METHOD(void, nativeRemoveAllShaderModifiers)(VRO_ARGS
                                                  VRO_REF(VROMaterial) material_j) {
    VRO_METHOD_PREAMBLE;

    std::shared_ptr<VROMaterial> material = VRO_REF_GET(VROMaterial, material_j);
    if (!material) {
        return;
    }

    // Remove shader modifiers synchronously (during material setup)
    // This matches the synchronous behavior of copyShaderModifiers
    // Disable thread restrictions temporarily
    material->setThreadRestrictionEnabled(false);
    material->removeAllShaderModifiers();
    material->setThreadRestrictionEnabled(true);
}

}  // extern "C"
