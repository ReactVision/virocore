//
//  VROBoneUBOMetal.cpp
//  ViroRenderer
//
//  Metal path for skeletal animation bone transforms.

#include "VROBoneUBOMetal.h"
#if VRO_METAL

#include "VROBoneUBO.h"
#include "VROShaderModifier.h"
#include "VROShaderProgram.h"
#include "VROSkinner.h"
#include "VROSkeleton.h"
#include "VROMatrix4f.h"
#include "VROLog.h"
#include <string>
#include <vector>

static thread_local std::shared_ptr<VROShaderModifier> sMetalSkinningModifier;

std::shared_ptr<VROShaderModifier> VROBoneUBOMetal::createSkinningShaderModifier(bool hasScale) {
    // MSL skinning modifier.  bone_matrices is declared as buffer(5) in the four
    // main lighting vertex functions.  The modifier body runs at
    // #pragma geometry_modifier_body after _geometry is populated from attributes.
    if (!sMetalSkinningModifier) {
        std::vector<std::string> modifierCode = {
            "float4 pos_h = float4(_geometry.position, 1.0);",
            "float4 pos_blended = "
                "(bone_matrices[_geometry.bone_indices.x] * pos_h) * _geometry.bone_weights.x + "
                "(bone_matrices[_geometry.bone_indices.y] * pos_h) * _geometry.bone_weights.y + "
                "(bone_matrices[_geometry.bone_indices.z] * pos_h) * _geometry.bone_weights.z + "
                "(bone_matrices[_geometry.bone_indices.w] * pos_h) * _geometry.bone_weights.w;",
            "_geometry.position = pos_blended.xyz;"
        };
        sMetalSkinningModifier = std::make_shared<VROShaderModifier>(VROShaderEntryPoint::Geometry,
                                                                     modifierCode);
        sMetalSkinningModifier->setName("skin");
        sMetalSkinningModifier->setAttributes((int)VROShaderMask::BoneIndex | (int)VROShaderMask::BoneWeight);
    }
    return sMetalSkinningModifier;
}

VROBoneUBOMetal::VROBoneUBOMetal(id<MTLDevice> device) {
    // Initialise all bone matrices to identity so unskinned indices produce
    // zero-displacement (same as OpenGL path initializing to identity).
    VROBonesData data;
    VROMatrix4f identity;
    for (int i = 0; i < kMaxBones; i++) {
        memcpy(&data.bone_transforms[i * kFloatsPerBone], identity.getArray(),
               kFloatsPerBone * sizeof(float));
    }
    _bonesBuffer = [device newBufferWithBytes:&data
                                       length:sizeof(VROBonesData)
                                      options:MTLResourceStorageModeShared];
    _bonesBuffer.label = @"VROBoneUBOMetal";
}

VROBoneUBOMetal::~VROBoneUBOMetal() {
    _bonesBuffer = nil;
}

void VROBoneUBOMetal::update(const std::shared_ptr<VROSkinner> &skinner) {
    VROBonesData *data = static_cast<VROBonesData *>([_bonesBuffer contents]);
    int numBones = skinner->getSkeleton()->getNumBones();
    for (int i = 0; i < numBones && i < kMaxBones; i++) {
        VROMatrix4f transform = skinner->getModelTransform(i);
        memcpy(&data->bone_transforms[i * kFloatsPerBone],
               transform.getArray(),
               kFloatsPerBone * sizeof(float));
    }
}

#endif  // VRO_METAL
