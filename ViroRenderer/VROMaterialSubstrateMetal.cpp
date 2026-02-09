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
#include <set>
#include <sstream>
#include <algorithm>

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

    size_t modifierCount = material.getShaderModifiers().size();

    if (modifierCount > 0) {
        std::string source = driver.getLibrarySource();
        if (!source.empty()) {
            NSLog(@"VROMaterialSubstrateMetal: Inflating %lu modifiers", material.getShaderModifiers().size());
            // Log a bit of the source to verify pragmas exist
            NSLog(@"VROMaterialSubstrateMetal: Source prefix: %s", source.substr(0, 100).c_str());
            if (source.find("#pragma surface_modifier_body") == std::string::npos) {
                NSLog(@"VROMaterialSubstrateMetal: Warning: Pragmas not found in source!");
            }
            
            inflateModifiers(source, material.getShaderModifiers());
            _dynamicLibrary = driver.newLibraryWithSource(source);
            if (_dynamicLibrary) {
                NSLog(@"VROMaterialSubstrateMetal: Successfully compiled dynamic shader library");
                library = _dynamicLibrary;
            } else {
                NSLog(@"VROMaterialSubstrateMetal: Failed to compile dynamic shader library. Source length: %lu", source.length());
                // The error is already logged in VRODriverMetal::newLibraryWithSource
            }
        } else {
            NSLog(@"VROMaterialSubstrateMetal: Warning: Driver library source is empty");
        }
    } else {
        NSLog(@"VROMaterialSubstrateMetal: Material has NO modifiers");
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
    // 1. Gather all unique uniform declarations and group by type
    std::set<std::string> seenUniforms;
    for (const auto &modifier : modifiers) {
        std::stringstream ss(modifier->getUniformsSource());
        std::string line;
        while (std::getline(ss, line)) {
            line = VROStringUtil::trim(line);
            if (line.empty() || line.find("uniform") == std::string::npos) continue;
            
            // Extract type and name: uniform type name;
            std::vector<std::string> parts = VROStringUtil::split(line, " \t;");
            if (parts.size() < 3) continue;
            
            std::string type = parts[1];
            std::string name = parts[2];
            
            if (seenUniforms.find(name) != seenUniforms.end()) continue;
            seenUniforms.insert(name);
            
            if (type == "float") _customLayout.floats.push_back(name);
            else if (type == "vec2") _customLayout.vec4s.push_back(name); // map vec2 to vec4 for easier alignment
            else if (type == "vec3") _customLayout.vec3s.push_back(name);
            else if (type == "vec4") _customLayout.vec4s.push_back(name);
            else if (type == "mat4") _customLayout.mat4s.push_back(name);
        }
    }
    
    // Sort for deterministic layout
    std::sort(_customLayout.floats.begin(), _customLayout.floats.end());
    std::sort(_customLayout.vec3s.begin(), _customLayout.vec3s.end());
    std::sort(_customLayout.vec4s.begin(), _customLayout.vec4s.end());
    std::sort(_customLayout.mat4s.begin(), _customLayout.mat4s.end());
    
    // 2. Build MSL struct and defines
    std::string customUniformsMembers;
    std::string customDefines;
    size_t offset = 0;
    
    for (const auto &name : _customLayout.floats) {
        customUniformsMembers += "    float " + name + ";\n";
        customDefines += "#define " + name + " _custom." + name + "\n";
        offset += 4;
    }
    // Aligns to 16 bytes for next group (float3/float4)
    if (offset % 16 != 0) {
        int padFloats = (16 - (offset % 16)) / 4;
        customUniformsMembers += "    float _pad[" + std::to_string(padFloats) + "];\n";
    }
    
    for (const auto &name : _customLayout.vec3s) {
        customUniformsMembers += "    float3 " + name + ";\n";
        customUniformsMembers += "    float _pad_" + name + ";\n"; // float3 is 12 bytes, but occupies 16 in constant buffers usually
        customDefines += "#define " + name + " _custom." + name + "\n";
    }
    
    for (const auto &name : _customLayout.vec4s) {
        customUniformsMembers += "    float4 " + name + ";\n";
        customDefines += "#define " + name + " _custom." + name + "\n";
    }
    
    for (const auto &name : _customLayout.mat4s) {
        customUniformsMembers += "    float4x4 " + name + ";\n";
        customDefines += "#define " + name + " _custom." + name + "\n";
    }

    if (customUniformsMembers.empty()) {
        customUniformsMembers = "    float _unused_padding;";
    }
    VROStringUtil::replaceAll(source, "#pragma custom_uniforms", customUniformsMembers);

    // 3. Inject modifier bodies, combining multiple modifiers for same directive
    std::map<std::string, std::string> combinedBodies;
    for (const auto &modifier : modifiers) {
        std::string bodyDirective = modifier->getDirective(VROShaderSection::Body);
        std::string body = modifier->getBodySource();
        
        // Basic GLSL to Metal type conversion for common types in the body
        VROStringUtil::replaceAll(body, "vec2", "float2");
        VROStringUtil::replaceAll(body, "vec3", "float3");
        VROStringUtil::replaceAll(body, "vec4", "float4");
        VROStringUtil::replaceAll(body, "mat4", "float4x4");
        
        combinedBodies[bodyDirective] += "\n{ // Modifier Start\n" + body + "\n} // Modifier End\n";
    }
    
    for (auto const& it : combinedBodies) {
        std::string directive = it.first;
        std::string body = it.second;
        std::string fullInjection = customDefines + body;
        VROStringUtil::replaceAll(source, directive, fullInjection);
    }
    
    // 4. Remove any remaining uniforms pragmas
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

    // Fill custom uniforms buffer based on the layout created during inflation
    if (!_material.getShaderModifiers().empty()) {
        uint8_t *customBuffer = (uint8_t *)_customUniformsBuffer->getWritableContents(eye, frame);
        size_t offset = 0;

        std::map<std::string, float> floats = _material.getShaderUniformFloats();
        for (const std::string &name : _customLayout.floats) {
            float val = 0;
            if (floats.count(name)) val = floats[name];
            if (offset + sizeof(float) <= 1024) {
                memcpy(customBuffer + offset, &val, sizeof(float));
                offset += sizeof(float);
            }
        }

        // Align to 16 bytes for vector types
        offset = (offset + 15) & ~15;

        std::map<std::string, VROVector3f> vec3s = _material.getShaderUniformVec3s();
        for (const std::string &name : _customLayout.vec3s) {
            VROVector3f val;
            if (vec3s.count(name)) val = vec3s[name];
            if (offset + sizeof(float) * 4 <= 1024) {
                simd_float4 vec = { val.x, val.y, val.z, 0.0f };
                memcpy(customBuffer + offset, &vec, sizeof(float) * 4);
                offset += sizeof(float) * 4;
            }
        }

        std::map<std::string, VROVector4f> vec4s = _material.getShaderUniformVec4s();
        for (const std::string &name : _customLayout.vec4s) {
            VROVector4f val;
            if (vec4s.count(name)) val = vec4s[name];
            if (offset + sizeof(float) * 4 <= 1024) {
                simd_float4 vec = { val.x, val.y, val.z, val.w };
                memcpy(customBuffer + offset, &vec, sizeof(float) * 4);
                offset += sizeof(float) * 4;
            }
        }

        std::map<std::string, VROMatrix4f> mat4s = _material.getShaderUniformMat4s();
        for (const std::string &name : _customLayout.mat4s) {
            VROMatrix4f val;
            if (mat4s.count(name)) val = mat4s[name];
            if (offset + sizeof(float) * 16 <= 1024) {
                memcpy(customBuffer + offset, val.getArray(), sizeof(float) * 16);
                offset += sizeof(float) * 16;
            }
        }
    }

    return *_materialUniformsBuffer;
}

void VROMaterialSubstrateMetal::updateSortKey(VROSortKey &key, const std::vector<std::shared_ptr<VROLight>> &lights,
                                              const VRORenderContext &context,
                                              std::shared_ptr<VRODriver> driver) {
    key.shader = _program->getShaderId();
    key.textures = hashTextures(_textures);
}

bool VROMaterialSubstrateMetal::bindShader(int lightsHash,
                                           const std::vector<std::shared_ptr<VROLight>> &lights,
                                           const VRORenderContext &context,
                                           std::shared_ptr<VRODriver> &driver) {
    // In Metal, pipeline state is bound by the geometry substrate, not the material substrate.
    // However, we need to bind the lighting uniforms here, similar to OpenGL.
    // This is the CRITICAL FIX: bindLights was defined but never called!
    NSLog(@"[METAL LIGHTING] bindShader() called with %zu lights, hash=%d", lights.size(), lightsHash);
    bindLights(lightsHash, lights, context, driver);
    NSLog(@"[METAL LIGHTING] bindLights() completed");
    return true;
}

void VROMaterialSubstrateMetal::bindProperties(std::shared_ptr<VRODriver> &driver) {
    // In Metal, material properties are bound via bindMaterialUniforms in the geometry substrate
    // This is called from VROGeometrySubstrateMetal::renderMaterial
}

void VROMaterialSubstrateMetal::bindGeometry(float opacity, const VROGeometry &geometry) {
    // In Metal, geometry-specific properties are handled in the geometry substrate
}

void VROMaterialSubstrateMetal::bindView(VROMatrix4f modelMatrix, VROMatrix4f viewMatrix,
                                         VROMatrix4f projectionMatrix, VROMatrix4f normalMatrix,
                                         VROVector3f cameraPosition, VROEyeType eyeType,
                                         const VRORenderContext &context) {
    // In Metal, view uniforms are bound in VROGeometrySubstrateMetal::render
}

void VROMaterialSubstrateMetal::updateTextures() {
    // Textures are managed through the _textures vector and updated when materials change
}

void VROMaterialSubstrateMetal::bindShader() {
    // Legacy method kept for compatibility
    // The virtual bindShader(int lightsHash, ...) should be used instead
}

void VROMaterialSubstrateMetal::bindLights(int lightsHash,
                                           const std::vector<std::shared_ptr<VROLight>> &lights,
                                           const VRORenderContext &context,
                                           std::shared_ptr<VRODriver> &driver) {

    NSLog(@"[METAL LIGHTING] bindLights() starting - received %zu lights", lights.size());

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

    NSLog(@"[METAL LIGHTING] Final values - num_lights=%d, ambient=(%f,%f,%f)",
          uniforms->num_lights,
          uniforms->ambient_light_color.x,
          uniforms->ambient_light_color.y,
          uniforms->ambient_light_color.z);

    [renderEncoder setVertexBuffer:_lightingUniformsBuffer->getMTLBuffer(eyeType)
                            offset:_lightingUniformsBuffer->getWriteOffset(frame)
                           atIndex:4];
    [renderEncoder setFragmentBuffer:_lightingUniformsBuffer->getMTLBuffer(eyeType)
                              offset:_lightingUniformsBuffer->getWriteOffset(frame)
                             atIndex:4];

    NSLog(@"[METAL LIGHTING] Lighting buffer bound at index 4 for vertex and fragment shaders");
}

uint32_t VROMaterialSubstrateMetal::hashTextures(const std::vector<std::shared_ptr<VROTexture>> &textures) const {
    uint32_t h = 0;
    for (const std::shared_ptr<VROTexture> &texture : textures) {
        h = 31 * h + texture->getTextureId();
    }
    return h;
}

#endif
