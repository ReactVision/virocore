//
//  VROMaterialSubstrateMetal.cpp
//  ViroRenderer
//
//  Created by Raj Advani on 11/30/15.
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

#include "VROMaterialSubstrateMetal.h"
#if VRO_METAL

#include "VROSharedStructures.h"
#include "VROMetalUtils.h"
#include "VRODriverMetal.h"
#include "VROMatrix4f.h"
#include "VROLight.h"
#include "VROMath.h"
#include "VROAllocationTracker.h"
#include "VROConcurrentBuffer.h"
#include "VROSortKey.h"
#include "VRORenderContext.h"

static std::map<std::string, std::shared_ptr<VROMetalShader>> _sharedPrograms;

std::shared_ptr<VROMetalShader> VROMaterialSubstrateMetal::getPooledShader(std::string vertexShader,
                                                                           std::string fragmentShader,
                                                                           id <MTLLibrary> library) {
    std::string name = vertexShader + "_" + fragmentShader;
    
    std::map<std::string, std::shared_ptr<VROMetalShader>>::iterator it = _sharedPrograms.find(name);
    if (it == _sharedPrograms.end()) {
        id <MTLFunction> vertexProgram = [library newFunctionWithName:[NSString stringWithUTF8String:vertexShader.c_str()]];
        id <MTLFunction> fragmentProgram = [library newFunctionWithName:[NSString stringWithUTF8String:fragmentShader.c_str()]];
        
        std::shared_ptr<VROMetalShader> program = std::make_shared<VROMetalShader>(vertexProgram, fragmentProgram);
        _sharedPrograms[name] = program;
        
        return program;
    }
    else {
        return it->second;
    }
}

VROMaterialSubstrateMetal::VROMaterialSubstrateMetal(const VROMaterial &material,
                                                     VRODriverMetal &driver) :
    _material(material),
    _lightingModel(material.getLightingModel()) {

    id <MTLDevice> device = driver.getDevice();
    id <MTLLibrary> library = driver.getLibrary();
    
    _dynamicLibrary = nil;
    if (!material.getShaderModifiers().empty()) {
        std::string source = driver.getLibrarySource();
        if (!source.empty()) {
           inflateModifiers(source, material.getShaderModifiers());
           _dynamicLibrary = driver.newLibraryWithSource(source);
           if (_dynamicLibrary) {
               library = _dynamicLibrary;
           }
        }
    }

    _lightingUniformsBuffer = new VROConcurrentBuffer(sizeof(VROSceneLightingUniforms), @"VROSceneLightingUniformBuffer", device);
    _materialUniformsBuffer = new VROConcurrentBuffer(sizeof(VROMaterialUniforms), @"VROMaterialUniformBuffer", device);
    _customUniformsBuffer = new VROConcurrentBuffer(1024, @"VROCustomUniformBuffer", device);
    
    switch (material.getLightingModel()) {
        case VROLightingModel::Constant:
            loadConstantLighting(material, library, device, driver);
            break;
            
        case VROLightingModel::Blinn:
            loadBlinnLighting(material, library, device, driver);
            break;
            
        case VROLightingModel::Lambert:
            loadLambertLighting(material, library, device, driver);
            break;
            
        case VROLightingModel::Phong:
            loadPhongLighting(material, library, device, driver);
            break;
            
        case VROLightingModel::PhysicallyBased:
            // Fallback to Blinn/Phong for PBR on Metal until native PBR is implemented
            loadBlinnLighting(material, library, device, driver);
            break;

        default:
            break;
    }
        
    ALLOCATION_TRACKER_ADD(MaterialSubstrates, 1);
}

VROMaterialSubstrateMetal::~VROMaterialSubstrateMetal() {
    delete (_materialUniformsBuffer);
    delete (_lightingUniformsBuffer);
    delete (_customUniformsBuffer);
    
    ALLOCATION_TRACKER_SUB(MaterialSubstrates, 1);
}

void VROMaterialSubstrateMetal::inflateModifiers(std::string &source, const std::vector<std::shared_ptr<VROShaderModifier>> &modifiers) {
    std::string customUniformsMembers;
    std::string customDefines;

    // 1. Process all uniforms to build the struct and defines
    for (const auto &modifier : modifiers) {
        std::string rawUniforms = modifier->getUniformsSource();
        std::stringstream ss(rawUniforms);
        std::string line;
        while (std::getline(ss, line)) {
            line = VROStringUtil::trim(line);
            if (line.empty() || line.find("#pragma") != std::string::npos) continue;
            
            // Basic GLSL to Metal type conversion for common types
            VROStringUtil::replaceAll(line, "vec2 ", "float2 ");
            VROStringUtil::replaceAll(line, "vec3 ", "float3 ");
            VROStringUtil::replaceAll(line, "vec4 ", "float4 ");
            VROStringUtil::replaceAll(line, "mat4 ", "float4x4 ");
            VROStringUtil::replaceAll(line, "uniform ", "");
            
            customUniformsMembers += "    " + line + "\n";
            
            // Generate define for direct access: #define u_time _custom.u_time
            size_t semi = line.find(';');
            if (semi != std::string::npos) {
                std::string decl = line.substr(0, semi);
                size_t lastSpace = decl.find_last_of(" \t");
                if (lastSpace != std::string::npos) {
                    std::string varName = decl.substr(lastSpace + 1);
                    customDefines += "#define " + varName + " _custom." + varName + "\n";
                }
            }
        }
    }

    if (customUniformsMembers.empty()) {
        customUniformsMembers = "    float _unused_padding;";
    }
    VROStringUtil::replaceAll(source, "#pragma custom_uniforms", customUniformsMembers);

    // 2. Inject modifier bodies
    for (const auto &modifier : modifiers) {
        std::string bodyDirective = modifier->getDirective(VROShaderSection::Body);
        std::string body = modifier->getBodySource();
        
        // Prepend defines to the body so they can access _custom members directly
        body = customDefines + "\n" + body;

        size_t bodyPos = source.find(bodyDirective);
        while (bodyPos != std::string::npos) {
            source.replace(bodyPos, bodyDirective.length(), body);
            bodyPos = source.find(bodyDirective, bodyPos + body.length());
        }
    }
    
    // 3. Remove any remaining uniforms pragmas (as we put them in the struct)
    VROStringUtil::replaceAll(source, "#pragma geometry_modifier_uniforms", "");
    VROStringUtil::replaceAll(source, "#pragma vertex_modifier_uniforms", "");
    VROStringUtil::replaceAll(source, "#pragma surface_modifier_uniforms", "");
    VROStringUtil::replaceAll(source, "#pragma fragment_modifier_uniforms", "");
    VROStringUtil::replaceAll(source, "#pragma lighting_model_modifier_uniforms", "");
    VROStringUtil::replaceAll(source, "#pragma image_modifier_uniforms", "");
}

void VROMaterialSubstrateMetal::loadConstantLighting(const VROMaterial &material,
                                                     id <MTLLibrary> library, id <MTLDevice> device,
                                                     VRODriverMetal &driver) {
    
    
    std::string vertexProgram = "constant_lighting_vertex";
    std::string fragmentProgram;
    
    VROMaterialVisual &diffuse = material.getDiffuse();

    if (diffuse.getTextureType() == VROTextureType::None) {
        fragmentProgram = "constant_lighting_fragment_c";
    }
    else if (diffuse.getTextureType() == VROTextureType::Texture2D) {
        _textures.push_back(diffuse.getTexture());
        fragmentProgram = "constant_lighting_fragment_t";
    }
    else {
        _textures.push_back(diffuse.getTexture());
        fragmentProgram = "constant_lighting_fragment_q";
    }
    
    _program = getPooledShader(vertexProgram, fragmentProgram, library);
}

void VROMaterialSubstrateMetal::loadLambertLighting(const VROMaterial &material,
                                                    id <MTLLibrary> library, id <MTLDevice> device,
                                                    VRODriverMetal &driver) {
    
    std::string vertexProgram = "lambert_lighting_vertex";
    std::string fragmentProgram;
    
    VROMaterialVisual &diffuse = material.getDiffuse();
    VROMaterialVisual &reflective = material.getReflective();
    
    if (diffuse.getTextureType() == VROTextureType::None) {
        if (reflective.getTextureType() == VROTextureType::TextureCube) {
            _textures.push_back(reflective.getTexture());
            fragmentProgram = "lambert_lighting_fragment_c_reflect";
        }
        else {
            fragmentProgram = "lambert_lighting_fragment_c";
        }
    }
    else {
        _textures.push_back(diffuse.getTexture());
        
        if (reflective.getTextureType() == VROTextureType::TextureCube) {
            _textures.push_back(reflective.getTexture());
            fragmentProgram = "lambert_lighting_fragment_t_reflect";
        }
        else {
            fragmentProgram = "lambert_lighting_fragment_t";
        }
    }
    
    _program = getPooledShader(vertexProgram, fragmentProgram, library);
}

void VROMaterialSubstrateMetal::loadPhongLighting(const VROMaterial &material,
                                                  id <MTLLibrary> library, id <MTLDevice> device,
                                                  VRODriverMetal &driver) {
    
    /*
     If there's no specular map, then we fall back to Lambert lighting.
     */
    VROMaterialVisual &specular = material.getSpecular();
    if (specular.getTextureType() != VROTextureType::Texture2D) {
        loadLambertLighting(material, library, device, driver);
        return;
    }
    
    std::string vertexProgram = "phong_lighting_vertex";
    std::string fragmentProgram;
    
    VROMaterialVisual &diffuse = material.getDiffuse();
    VROMaterialVisual &reflective = material.getReflective();
    
    if (diffuse.getTextureType() == VROTextureType::None) {
        _textures.push_back(specular.getTexture());
        
        if (reflective.getTextureType() == VROTextureType::TextureCube) {
            _textures.push_back(reflective.getTexture());
            fragmentProgram = "phong_lighting_fragment_c_reflect";
        }
        else {
            fragmentProgram = "phong_lighting_fragment_c";
        }
    }
    else {
        _textures.push_back(diffuse.getTexture());
        _textures.push_back(specular.getTexture());
        
        if (reflective.getTextureType() == VROTextureType::TextureCube) {
            _textures.push_back(reflective.getTexture());
            fragmentProgram = "phong_lighting_fragment_t_reflect";
        }
        else {
            fragmentProgram = "phong_lighting_fragment_t";
        }
    }
    
    _program = getPooledShader(vertexProgram, fragmentProgram, library);
}

void VROMaterialSubstrateMetal::loadBlinnLighting(const VROMaterial &material,
                                                  id <MTLLibrary> library, id <MTLDevice> device,
                                                  VRODriverMetal &driver) {
    
    /*
     If there's no specular map, then we fall back to Lambert lighting.
     */
    VROMaterialVisual &specular = material.getSpecular();
    if (specular.getTextureType() != VROTextureType::Texture2D) {
        loadLambertLighting(material, library, device, driver);
        return;
    }
    
    std::string vertexProgram = "blinn_lighting_vertex";
    std::string fragmentProgram;
    
    VROMaterialVisual &diffuse = material.getDiffuse();
    VROMaterialVisual &reflective = material.getReflective();
    
    if (diffuse.getTextureType() == VROTextureType::None) {
        _textures.push_back(specular.getTexture());

        if (reflective.getTextureType() == VROTextureType::TextureCube) {
            _textures.push_back(reflective.getTexture());
            fragmentProgram = "blinn_lighting_fragment_c_reflect";
        }
        else {
            fragmentProgram = "blinn_lighting_fragment_c";
        }
    }
    else {
        _textures.push_back(diffuse.getTexture());
        _textures.push_back(specular.getTexture());

        if (reflective.getTextureType() == VROTextureType::TextureCube) {
            _textures.push_back(reflective.getTexture());
            fragmentProgram = "blinn_lighting_fragment_t_reflect";
        }
        else {
            fragmentProgram = "blinn_lighting_fragment_t";
        }
    }
    
    _program = getPooledShader(vertexProgram, fragmentProgram, library);
}

VROConcurrentBuffer &VROMaterialSubstrateMetal::bindMaterialUniforms(float opacity, VROEyeType eye,
                                                                     int frame) {
    VROMaterialUniforms *uniforms = (VROMaterialUniforms *)_materialUniformsBuffer->getWritableContents(eye, frame);
    uniforms->diffuse_surface_color = toVectorFloat4(_material.getDiffuse().getColor());
    uniforms->diffuse_intensity = _material.getDiffuse().getIntensity();
    uniforms->shininess = _material.getShininess();
    uniforms->alpha = _material.getTransparency() * opacity;
    uniforms->roughness = _material.getRoughness().getColor().x;
    uniforms->metalness = _material.getMetalness().getColor().x;
    uniforms->ao = _material.getAmbientOcclusion().getColor().x;

    // Fill custom uniforms buffer if we have modifiers
    if (!_material.getShaderModifiers().empty()) {
        uint8_t *customBuffer = (uint8_t *)_customUniformsBuffer->getWritableContents(eye, frame);
        size_t offset = 0;

        // Pack floats (sorted by name to match GLSL/Metal deterministic layout)
        std::map<std::string, float> floats = _material.getShaderUniformFloats();
        for (auto const& [name, val] : floats) {
            if (offset + sizeof(float) <= 1024) {
                memcpy(customBuffer + offset, &val, sizeof(float));
                offset += sizeof(float);
            }
        }

        // Align to 16 bytes for vector types
        offset = (offset + 15) & ~15;

        // Pack Vec3s (packed as float4 for Metal alignment)
        std::map<std::string, VROVector3f> vec3s = _material.getShaderUniformVec3s();
        for (auto const& [name, val] : vec3s) {
            if (offset + sizeof(float) * 4 <= 1024) {
                simd_float4 vec = { val.x, val.y, val.z, 0.0f };
                memcpy(customBuffer + offset, &vec, sizeof(float) * 4);
                offset += sizeof(float) * 4;
            }
        }

        // Pack Vec4s
        std::map<std::string, VROVector4f> vec4s = _material.getShaderUniformVec4s();
        for (auto const& [name, val] : vec4s) {
            if (offset + sizeof(float) * 4 <= 1024) {
                simd_float4 vec = { val.x, val.y, val.z, val.w };
                memcpy(customBuffer + offset, &vec, sizeof(float) * 4);
                offset += sizeof(float) * 4;
            }
        }

        // Pack Mat4s
        std::map<std::string, VROMatrix4f> mat4s = _material.getShaderUniformMat4s();
        for (auto const& [name, val] : mat4s) {
            if (offset + sizeof(float) * 16 <= 1024) {
                memcpy(customBuffer + offset, val.getArray(), sizeof(float) * 16);
                offset += sizeof(float) * 16;
            }
        }
    }

    return *_materialUniformsBuffer;
}

void VROMaterialSubstrateMetal::updateSortKey(VROSortKey &key) const {
    key.shader = _program->getShaderId();
    key.textures = hashTextures(_textures);
}

void VROMaterialSubstrateMetal::bindShader() {
    // Do nothing in Metal, consider changing this to binding pipeline state?
    // The problem is that pipeline state in metal emcompasses both shader and
    // vertex layout
}

void VROMaterialSubstrateMetal::bindLights(int lightsHash,
                                           const std::vector<std::shared_ptr<VROLight>> &lights,
                                           const VRORenderContext &context,
                                           std::shared_ptr<VRODriver> &driver) {
    
    VRODriverMetal &metal = (VRODriverMetal &)(*driver.get());
    id <MTLRenderCommandEncoder> renderEncoder = metal.getRenderTarget()->getRenderEncoder();
    
    VROEyeType eyeType = context.getEyeType();
    int frame = context.getFrame();
    
    VROSceneLightingUniforms *uniforms = (VROSceneLightingUniforms *)_lightingUniformsBuffer->getWritableContents(eyeType,
                                                                                                                  frame);
    uniforms->num_lights = 0;
    VROVector3f ambientLight;
    
    for (const std::shared_ptr<VROLight> &light : lights) {
        if (light->getType() == VROLightType::Ambient) {
            ambientLight += light->getColor();
        }
        else {
            VROLightUniforms &light_uniforms = uniforms->lights[uniforms->num_lights];
            light_uniforms.type = (int) light->getType();
            light_uniforms.color = toVectorFloat3(light->getColor());
            light_uniforms.position = toVectorFloat3(light->getTransformedPosition());
            light_uniforms.direction = toVectorFloat3(light->getDirection());
            light_uniforms.attenuation_start_distance = light->getAttenuationStartDistance();
            light_uniforms.attenuation_end_distance = light->getAttenuationEndDistance();
            light_uniforms.attenuation_falloff_exp = light->getAttenuationFalloffExponent();
            light_uniforms.spot_inner_angle = degrees_to_radians(light->getSpotInnerAngle());
            light_uniforms.spot_outer_angle = degrees_to_radians(light->getSpotOuterAngle());
            
            uniforms->num_lights++;
        }
    }
    
    uniforms->ambient_light_color = toVectorFloat3(ambientLight);
    
    [renderEncoder setVertexBuffer:_lightingUniformsBuffer->getMTLBuffer(eyeType)
                            offset:_lightingUniformsBuffer->getWriteOffset(frame)
                           atIndex:4];
    [renderEncoder setFragmentBuffer:_lightingUniformsBuffer->getMTLBuffer(eyeType)
                              offset:_lightingUniformsBuffer->getWriteOffset(frame)
                             atIndex:4];
}

uint32_t VROMaterialSubstrateMetal::hashTextures(const std::vector<std::shared_ptr<VROTexture>> &textures) const {
    uint32_t h = 0;
    for (const std::shared_ptr<VROTexture> &texture : textures) {
        h = 31 * h + texture->getTextureId();
    }
    return h;
}

#endif
