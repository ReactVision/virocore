// WASM/WebGL2 variant: only the matrix-palette skinning block is kept.
//
// The master (ViroRenderer/skinning_vsh.glsl) also declares a `bones_dq` block
// and dual-quaternion helper functions. Dual-quaternion skinning is disabled
// (kDualQuaternionEnabled = false in VROBoneUBO.h), so that code is dead — but
// WebGL2 keeps the declared `bones_dq` uniform block active and then faults with
// "uniform buffer too small" because no buffer is bound to it. Desktop/mobile GL
// tolerate the unused block; WebGL2 does not. Keeping only the `bones` block used
// by the non-DQ path fixes it. Re-add the DQ block here if DQ is ever enabled.
layout (std140) uniform bones {
    mat4 bone_matrices[192];
};
