//
//  VROParticleUBOMetal.cpp
//  ViroRenderer
//
//  Metal path for particle instanced rendering.

#include "VROParticleUBOMetal.h"
#if VRO_METAL

#include "VROParticle.h"
#include "VROShaderModifier.h"
#include "VRODriverMetal.h"
#include "VROMatrix4f.h"
#include "VROLog.h"
#include <string>
#include <vector>
#include <cmath>

// Matches the constants in VROParticleUBO.h — keep in sync.
static const int kMaxParticlesPerUBO    = 180;
static const int kMaxFloatsPerTransform = 16;
static const int kMaxFloatsPerColor     = 4;

// Buffer indices in ViroShadersSource.txt for the Constant lighting functions.
static const int kParticleTransformBufferIndex = 7;
static const int kParticleColorBufferIndex     = 7;   // fragment stage

std::shared_ptr<VROParticleUBOMetal> VROParticleUBOMetal::create(std::shared_ptr<VRODriver> &driver) {
    VRODriverMetal &metal = (VRODriverMetal &)(*driver);
    return std::make_shared<VROParticleUBOMetal>(metal.getDevice());
}

VROParticleUBOMetal::VROParticleUBOMetal(id<MTLDevice> device) : _encoder(nil) {
    _lastKnownBoundingBox = VROBoundingBox(0, 0, 0, 0, 0, 0);

    size_t vertexBytes   = kMaxParticlesPerUBO * kMaxFloatsPerTransform * sizeof(float);
    size_t fragmentBytes = kMaxParticlesPerUBO * kMaxFloatsPerColor * sizeof(float);

    _vertexBuffer = [device newBufferWithLength:vertexBytes
                                        options:MTLResourceStorageModeShared];
    _vertexBuffer.label = @"VROParticleUBOMetal_transforms";

    _fragmentBuffer = [device newBufferWithLength:fragmentBytes
                                          options:MTLResourceStorageModeShared];
    _fragmentBuffer.label = @"VROParticleUBOMetal_colors";
}

VROParticleUBOMetal::~VROParticleUBOMetal() {
    _vertexBuffer   = nil;
    _fragmentBuffer = nil;
}

// ---------------------------------------------------------------------------
// Shader modifiers
// ---------------------------------------------------------------------------

std::vector<std::shared_ptr<VROShaderModifier>> VROParticleUBOMetal::createInstanceShaderModifier() {
    std::vector<std::shared_ptr<VROShaderModifier>> modifiers;

    // Geometry modifier — override model_matrix so v_position (world-space) is correct.
    // Injected at #pragma geometry_modifier_body.
    // particle_transforms [[buffer(7)]] and v_instance_id [[instance_id]] are declared
    // in the constant_lighting_vertex function signature in ViroShadersSource.txt.
    std::vector<std::string> geomCode = {
        "_transforms.model_matrix = particle_transforms[v_instance_id];"
    };
    auto geomModifier = std::make_shared<VROShaderModifier>(VROShaderEntryPoint::Geometry, geomCode);
    geomModifier->setName("particle_geom");
    modifiers.push_back(geomModifier);

    // Vertex modifier — recompute clip-space position from the per-particle transform.
    // view.modelview_projection_matrix is a CPU uniform (proj×view×node_model) and does
    // NOT pick up the overridden model_matrix, so we must rebuild MVP here.
    // Injected at #pragma vertex_modifier_body.
    std::vector<std::string> vertCode = {
        "_vertex.position = view.projection_matrix * (view.view_matrix * particle_transforms[v_instance_id]) * float4(_geometry.position, 1.0);"
    };
    auto vertModifier = std::make_shared<VROShaderModifier>(VROShaderEntryPoint::Vertex, vertCode);
    vertModifier->setName("particle_vert");
    modifiers.push_back(vertModifier);

    // Surface modifier — apply per-particle color tint.
    // Sentinel: colorCurrent.x == -1.0 means no tint override.
    // particle_colors [[buffer(7)]] (fragment) and in.instance_id [[flat]] are declared
    // in the fragment function signature and VROConstantLightingVertexOut respectively.
    std::vector<std::string> surfCode = {
        "float4 _pc = particle_colors[in.instance_id];",
        "if (_pc.x != -1.0 && _surface.diffuse_color.a != 0.0) {",
        "    float4 _dest = _surface.diffuse_color;",
        "    float4 _final = _pc * 0.5 + _dest * 0.5;",
        "    _surface.diffuse_color = float4(_final.xyz, _surface.diffuse_color.a);",
        "}",
        "_surface.alpha = _surface.alpha * _pc.w;"
    };
    auto surfModifier = std::make_shared<VROShaderModifier>(VROShaderEntryPoint::Surface, surfCode);
    surfModifier->setName("particle_surf");
    modifiers.push_back(surfModifier);

    return modifiers;
}

// ---------------------------------------------------------------------------
// VROInstancedUBO interface
// ---------------------------------------------------------------------------

int VROParticleUBOMetal::getNumberOfDrawCalls() {
    if (_lastKnownParticles.empty()) {
        return 0;
    }
    return (int)std::ceil((float)_lastKnownParticles.size() / kMaxParticlesPerUBO);
}

int VROParticleUBOMetal::bindDrawData(int drawCallIndex) {
    if (_lastKnownParticles.empty() || !_encoder) {
        return 0;
    }

    int start = drawCallIndex * kMaxParticlesPerUBO;
    int end   = std::min(start + kMaxParticlesPerUBO, (int)_lastKnownParticles.size());
    int count = end - start;

    // Write this window's transforms into the vertex buffer.
    float *vDst = static_cast<float *>([_vertexBuffer contents]);
    for (int i = start; i < end; i++) {
        const float *mat = _lastKnownParticles[i].currentWorldTransform.getArray();
        memcpy(vDst + (i - start) * kMaxFloatsPerTransform,
               mat,
               kMaxFloatsPerTransform * sizeof(float));
    }

    // Write this window's colors into the fragment buffer.
    float *fDst = static_cast<float *>([_fragmentBuffer contents]);
    for (int i = start; i < end; i++) {
        fDst[(i - start) * 4 + 0] = _lastKnownParticles[i].colorCurrent.x;
        fDst[(i - start) * 4 + 1] = _lastKnownParticles[i].colorCurrent.y;
        fDst[(i - start) * 4 + 2] = _lastKnownParticles[i].colorCurrent.z;
        fDst[(i - start) * 4 + 3] = _lastKnownParticles[i].colorCurrent.w;
    }

    // Bind buffers to the active encoder.
    [_encoder setVertexBuffer:_vertexBuffer   offset:0 atIndex:kParticleTransformBufferIndex];
    [_encoder setFragmentBuffer:_fragmentBuffer offset:0 atIndex:kParticleColorBufferIndex];

    return count;
}

VROBoundingBox VROParticleUBOMetal::getInstancedBoundingBox() {
    return _lastKnownBoundingBox;
}

void VROParticleUBOMetal::update(std::vector<VROParticle> &particles, VROBoundingBox &box) {
    _lastKnownParticles   = particles;
    _lastKnownBoundingBox = box;
}

#endif  // VRO_METAL
