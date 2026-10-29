// VROHandOcclusionOpenXR.cpp
// ViroRenderer
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#include "VROHandOcclusionOpenXR.h"

#include <android/log.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "VRODriver.h"
#include "VROMaterial.h"
#include "VROQuaternion.h"

#define LOG_TAG "VROHandOcclusionOpenXR"
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR,   LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN,    LOG_TAG, __VA_ARGS__)
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)

// Mesh vertex: position (3), normal (3), blend indices (4, as floats), blend weights (4).
static constexpr int kMeshVertexFloats = 14;

// Joint radii are clamped to this range: a runtime that reports zero (or a
// palm radius covering the whole hand) should not erase the hole or the scene.
static constexpr float kMinRadius = 0.004f;
static constexpr float kMaxRadius = 0.035f;

// Unit capsule: rings of latitude from the top pole to the bottom, the equator
// twice. The top half rides end b (t = 1) and the bottom half end a (t = 0),
// so the band between the two equators is the cylinder.
static constexpr int kCapsuleSides = 8;
static const float kCapsuleLatitudes[] = { 90.0f, 60.0f, 30.0f, 0.0f, 0.0f, -30.0f, -60.0f, -90.0f };
static constexpr int kCapsuleRings = sizeof(kCapsuleLatitudes) / sizeof(kCapsuleLatitudes[0]);

// Joint pairs drawn as capsules (XR_EXT_hand_tracking joint order).
static const XrHandJointEXT kCapsuleSegments[][2] = {
    // Thumb
    { XR_HAND_JOINT_THUMB_METACARPAL_EXT,   XR_HAND_JOINT_THUMB_PROXIMAL_EXT },
    { XR_HAND_JOINT_THUMB_PROXIMAL_EXT,     XR_HAND_JOINT_THUMB_DISTAL_EXT },
    { XR_HAND_JOINT_THUMB_DISTAL_EXT,       XR_HAND_JOINT_THUMB_TIP_EXT },
    // Index
    { XR_HAND_JOINT_INDEX_METACARPAL_EXT,   XR_HAND_JOINT_INDEX_PROXIMAL_EXT },
    { XR_HAND_JOINT_INDEX_PROXIMAL_EXT,     XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT },
    { XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT, XR_HAND_JOINT_INDEX_DISTAL_EXT },
    { XR_HAND_JOINT_INDEX_DISTAL_EXT,       XR_HAND_JOINT_INDEX_TIP_EXT },
    // Middle
    { XR_HAND_JOINT_MIDDLE_METACARPAL_EXT,   XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT },
    { XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT,     XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT },
    { XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT, XR_HAND_JOINT_MIDDLE_DISTAL_EXT },
    { XR_HAND_JOINT_MIDDLE_DISTAL_EXT,       XR_HAND_JOINT_MIDDLE_TIP_EXT },
    // Ring
    { XR_HAND_JOINT_RING_METACARPAL_EXT,   XR_HAND_JOINT_RING_PROXIMAL_EXT },
    { XR_HAND_JOINT_RING_PROXIMAL_EXT,     XR_HAND_JOINT_RING_INTERMEDIATE_EXT },
    { XR_HAND_JOINT_RING_INTERMEDIATE_EXT, XR_HAND_JOINT_RING_DISTAL_EXT },
    { XR_HAND_JOINT_RING_DISTAL_EXT,       XR_HAND_JOINT_RING_TIP_EXT },
    // Little
    { XR_HAND_JOINT_LITTLE_METACARPAL_EXT,   XR_HAND_JOINT_LITTLE_PROXIMAL_EXT },
    { XR_HAND_JOINT_LITTLE_PROXIMAL_EXT,     XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT },
    { XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT, XR_HAND_JOINT_LITTLE_DISTAL_EXT },
    { XR_HAND_JOINT_LITTLE_DISTAL_EXT,       XR_HAND_JOINT_LITTLE_TIP_EXT },
    // Palm: wrist to palm centre and to the base of the thumb, then rows
    // across the knuckles and the metacarpal bases, crossed to fill the back
    // of the hand.
    { XR_HAND_JOINT_WRIST_EXT,             XR_HAND_JOINT_PALM_EXT },
    { XR_HAND_JOINT_WRIST_EXT,             XR_HAND_JOINT_THUMB_METACARPAL_EXT },
    { XR_HAND_JOINT_INDEX_PROXIMAL_EXT,    XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT },
    { XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT,   XR_HAND_JOINT_RING_PROXIMAL_EXT },
    { XR_HAND_JOINT_RING_PROXIMAL_EXT,     XR_HAND_JOINT_LITTLE_PROXIMAL_EXT },
    { XR_HAND_JOINT_INDEX_METACARPAL_EXT,  XR_HAND_JOINT_MIDDLE_METACARPAL_EXT },
    { XR_HAND_JOINT_MIDDLE_METACARPAL_EXT, XR_HAND_JOINT_RING_METACARPAL_EXT },
    { XR_HAND_JOINT_RING_METACARPAL_EXT,   XR_HAND_JOINT_LITTLE_METACARPAL_EXT },
    { XR_HAND_JOINT_INDEX_METACARPAL_EXT,  XR_HAND_JOINT_LITTLE_PROXIMAL_EXT },
    { XR_HAND_JOINT_LITTLE_METACARPAL_EXT, XR_HAND_JOINT_INDEX_PROXIMAL_EXT },
};
static constexpr int kCapsuleSegmentCount = sizeof(kCapsuleSegments) / sizeof(kCapsuleSegments[0]);

// ──────────────────────────────────────────────────────────────────────────────
// Shaders
// ──────────────────────────────────────────────────────────────────────────────

// Linear-blend skinning, four weights per vertex. Inflation is applied along
// the bind-pose normal before skinning; the joint transforms are rigid, so the
// offset keeps its length.
static const char *kMeshVertexShader = R"(#version 300 es
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_blendIndices;
layout(location = 3) in vec4 a_blendWeights;
uniform mat4 u_viewProjection;
uniform mat4 u_joints[26];
uniform float u_inflate;
void main() {
    vec4 p = vec4(a_position + a_normal * u_inflate, 1.0);
    ivec4 j = ivec4(a_blendIndices);
    vec4 skinned = (u_joints[j.x] * p) * a_blendWeights.x +
                   (u_joints[j.y] * p) * a_blendWeights.y +
                   (u_joints[j.z] * p) * a_blendWeights.z +
                   (u_joints[j.w] * p) * a_blendWeights.w;
    gl_Position = u_viewProjection * vec4(skinned.xyz, 1.0);
}
)";

// a_capsule.xyz is a unit direction in the capsule's frame (y along a→b),
// a_capsule.w selects the end it hangs off.
static const char *kCapsuleVertexShader = R"(#version 300 es
layout(location = 0) in vec4 a_capsule;
uniform mat4 u_viewProjection;
uniform vec3 u_a;
uniform vec3 u_b;
uniform float u_radius;
void main() {
    vec3 axis = u_b - u_a;
    float len = length(axis);
    vec3 y = len > 1e-5 ? axis / len : vec3(0.0, 1.0, 0.0);
    vec3 helper = abs(y.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 x = normalize(cross(helper, y));
    vec3 z = cross(x, y);
    vec3 centre = mix(u_a, u_b, a_capsule.w);
    vec3 p = centre + u_radius * (x * a_capsule.x + y * a_capsule.y + z * a_capsule.z);
    gl_Position = u_viewProjection * vec4(p, 1.0);
}
)";

// Colour writes are masked off; the fragment stage only exists to link.
static const char *kDepthFragmentShader = R"(#version 300 es
precision mediump float;
out vec4 frag_color;
void main() {
    frag_color = vec4(0.0);
}
)";

static GLuint compileShader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        ALOGE("Shader compile failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint linkProgram(const char *vertexSource, const char *fragmentSource) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vs == 0 || fs == 0) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        ALOGE("Program link failed: %s", log);
        glDeleteProgram(program);
        return 0;
    }
    return program;
}

// ──────────────────────────────────────────────────────────────────────────────
// Conversion helpers
// ──────────────────────────────────────────────────────────────────────────────

static VROVector3f xrVec3ToVRO(XrVector3f v) {
    return VROVector3f(v.x, v.y, v.z);
}

static VROMatrix4f xrPoseToMatrix(const XrPosef &pose) {
    VROQuaternion q(pose.orientation.x, pose.orientation.y,
                    pose.orientation.z, pose.orientation.w);
    VROMatrix4f m = q.getMatrix();
    m[12] = pose.position.x;
    m[13] = pose.position.y;
    m[14] = pose.position.z;
    return m;
}

static float clampRadius(float r) {
    return std::min(std::max(r, kMinRadius), kMaxRadius);
}

// ──────────────────────────────────────────────────────────────────────────────
// Setup
// ──────────────────────────────────────────────────────────────────────────────

VROHandOcclusionOpenXR::VROHandOcclusionOpenXR() {}

VROHandOcclusionOpenXR::~VROHandOcclusionOpenXR() {
    // GL objects are released by destroyGL() on the render thread; the
    // context may not be current here.
}

void VROHandOcclusionOpenXR::init(XrInstance instance, bool meshExtAvailable,
                                  PFN_xrLocateHandJointsEXT locateHandJoints,
                                  XrHandTrackerEXT leftTracker, XrHandTrackerEXT rightTracker) {
    _pfnLocateHandJoints = locateHandJoints;
    _hands[0].tracker = leftTracker;
    _hands[1].tracker = rightTracker;

    if (!meshExtAvailable) {
        ALOGV("XR_FB_hand_tracking_mesh absent — hand occlusion uses capsules");
        return;
    }
    PFN_xrGetHandMeshFB getHandMesh = nullptr;
    XrResult r = xrGetInstanceProcAddr(instance, "xrGetHandMeshFB",
                                       (PFN_xrVoidFunction *)&getHandMesh);
    if (XR_FAILED(r) || getHandMesh == nullptr) {
        ALOGW("xrGetInstanceProcAddr('xrGetHandMeshFB') failed: %d — using capsules", (int)r);
        return;
    }
    for (Hand &hand : _hands) {
        if (hand.tracker != XR_NULL_HANDLE) {
            loadMesh(getHandMesh, hand);
        }
    }
}

void VROHandOcclusionOpenXR::loadMesh(PFN_xrGetHandMeshFB getHandMesh, Hand &hand) {
    // First call with zero capacities for the counts, second with buffers.
    XrHandTrackingMeshFB mesh = { XR_TYPE_HAND_TRACKING_MESH_FB };
    XrResult r = getHandMesh(hand.tracker, &mesh);
    if (XR_FAILED(r)) {
        ALOGW("xrGetHandMeshFB (counts) failed: %d", (int)r);
        return;
    }
    if (mesh.jointCountOutput != kJointCount || mesh.vertexCountOutput == 0 ||
        mesh.indexCountOutput == 0) {
        ALOGW("xrGetHandMeshFB: unexpected mesh (joints=%u vertices=%u indices=%u)",
              mesh.jointCountOutput, mesh.vertexCountOutput, mesh.indexCountOutput);
        return;
    }

    std::vector<XrPosef>        bindPoses(mesh.jointCountOutput);
    std::vector<float>          jointRadii(mesh.jointCountOutput);
    std::vector<XrHandJointEXT> jointParents(mesh.jointCountOutput);
    std::vector<XrVector3f>     positions(mesh.vertexCountOutput);
    std::vector<XrVector3f>     normals(mesh.vertexCountOutput);
    std::vector<XrVector2f>     uvs(mesh.vertexCountOutput);
    std::vector<XrVector4sFB>   blendIndices(mesh.vertexCountOutput);
    std::vector<XrVector4f>     blendWeights(mesh.vertexCountOutput);
    std::vector<int16_t>        indices(mesh.indexCountOutput);

    mesh.jointCapacityInput  = mesh.jointCountOutput;
    mesh.jointBindPoses      = bindPoses.data();
    mesh.jointRadii          = jointRadii.data();
    mesh.jointParents        = jointParents.data();
    mesh.vertexCapacityInput = mesh.vertexCountOutput;
    mesh.vertexPositions     = positions.data();
    mesh.vertexNormals       = normals.data();
    mesh.vertexUVs           = uvs.data();
    mesh.vertexBlendIndices  = blendIndices.data();
    mesh.vertexBlendWeights  = blendWeights.data();
    mesh.indexCapacityInput  = mesh.indexCountOutput;
    mesh.indices             = indices.data();

    r = getHandMesh(hand.tracker, &mesh);
    if (XR_FAILED(r)) {
        ALOGW("xrGetHandMeshFB (data) failed: %d", (int)r);
        return;
    }

    HandMesh &out = hand.mesh;
    for (uint32_t j = 0; j < mesh.jointCountOutput; ++j) {
        out.inverseBindPoses[j] = xrPoseToMatrix(bindPoses[j]).invert();
    }

    out.vertices.resize((size_t)mesh.vertexCountOutput * kMeshVertexFloats);
    for (uint32_t v = 0; v < mesh.vertexCountOutput; ++v) {
        float *dst = &out.vertices[(size_t)v * kMeshVertexFloats];
        const XrVector4sFB &bi = blendIndices[v];
        const XrVector4f   &bw = blendWeights[v];
        dst[0]  = positions[v].x; dst[1]  = positions[v].y; dst[2]  = positions[v].z;
        dst[3]  = normals[v].x;   dst[4]  = normals[v].y;   dst[5]  = normals[v].z;
        // Out-of-range indices would read past u_joints; pin them to the wrist.
        auto joint = [](int16_t i) { return (float)((i >= 0 && i < kJointCount) ? i : XR_HAND_JOINT_WRIST_EXT); };
        dst[6]  = joint(bi.x); dst[7]  = joint(bi.y); dst[8]  = joint(bi.z); dst[9]  = joint(bi.w);
        dst[10] = bw.x;        dst[11] = bw.y;        dst[12] = bw.z;        dst[13] = bw.w;
    }

    out.indices.resize(mesh.indexCountOutput);
    for (uint32_t i = 0; i < mesh.indexCountOutput; ++i) {
        out.indices[i] = (GLushort)indices[i];
    }
    out.loaded = true;
    ALOGV("Hand mesh loaded: %u vertices, %u indices", mesh.vertexCountOutput, mesh.indexCountOutput);
}

bool VROHandOcclusionOpenXR::initGL() {
    _meshProgram = linkProgram(kMeshVertexShader, kDepthFragmentShader);
    _capsuleProgram = linkProgram(kCapsuleVertexShader, kDepthFragmentShader);
    if (_meshProgram == 0 || _capsuleProgram == 0) {
        return false;
    }
    _meshViewProjLoc = glGetUniformLocation(_meshProgram, "u_viewProjection");
    _meshJointsLoc   = glGetUniformLocation(_meshProgram, "u_joints");
    _meshInflateLoc  = glGetUniformLocation(_meshProgram, "u_inflate");
    _capViewProjLoc  = glGetUniformLocation(_capsuleProgram, "u_viewProjection");
    _capALoc         = glGetUniformLocation(_capsuleProgram, "u_a");
    _capBLoc         = glGetUniformLocation(_capsuleProgram, "u_b");
    _capRadiusLoc    = glGetUniformLocation(_capsuleProgram, "u_radius");

    // Unit capsule
    std::vector<float> vertices;
    for (int ring = 0; ring < kCapsuleRings; ++ring) {
        float lat = kCapsuleLatitudes[ring] * (float)M_PI / 180.0f;
        float t = (ring < kCapsuleRings / 2) ? 1.0f : 0.0f;
        for (int side = 0; side < kCapsuleSides; ++side) {
            float lon = 2.0f * (float)M_PI * side / kCapsuleSides;
            vertices.push_back(cosf(lat) * cosf(lon));
            vertices.push_back(sinf(lat));
            vertices.push_back(cosf(lat) * sinf(lon));
            vertices.push_back(t);
        }
    }
    std::vector<GLushort> indices;
    for (int ring = 0; ring < kCapsuleRings - 1; ++ring) {
        for (int side = 0; side < kCapsuleSides; ++side) {
            GLushort a = (GLushort)(ring * kCapsuleSides + side);
            GLushort b = (GLushort)(ring * kCapsuleSides + (side + 1) % kCapsuleSides);
            GLushort c = (GLushort)(a + kCapsuleSides);
            GLushort d = (GLushort)(b + kCapsuleSides);
            indices.insert(indices.end(), { a, c, b, b, c, d });
        }
    }
    _capsuleIndexCount = (GLsizei)indices.size();

    glGenVertexArrays(1, &_capsuleVao);
    glGenBuffers(1, &_capsuleVbo);
    glGenBuffers(1, &_capsuleIbo);
    glBindVertexArray(_capsuleVao);
    glBindBuffer(GL_ARRAY_BUFFER, _capsuleVbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _capsuleIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLushort), indices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void VROHandOcclusionOpenXR::uploadMesh(HandMesh &mesh) {
    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ibo);
    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(float), mesh.vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(GLushort), mesh.indices.data(), GL_STATIC_DRAW);

    const GLsizei stride = kMeshVertexFloats * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)(0 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void *)(6 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, (void *)(10 * sizeof(float)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // The CPU copy is kept (a few hundred KB): destroyGL() runs whenever the
    // render thread exits, and the mesh is uploaded again when it restarts.
    mesh.indexCount = (GLsizei)mesh.indices.size();
}

void VROHandOcclusionOpenXR::destroyGL() {
    for (Hand &hand : _hands) {
        HandMesh &mesh = hand.mesh;
        if (mesh.vao) glDeleteVertexArrays(1, &mesh.vao);
        if (mesh.vbo) glDeleteBuffers(1, &mesh.vbo);
        if (mesh.ibo) glDeleteBuffers(1, &mesh.ibo);
        mesh.vao = mesh.vbo = mesh.ibo = 0;
    }
    if (_capsuleVao) glDeleteVertexArrays(1, &_capsuleVao);
    if (_capsuleVbo) glDeleteBuffers(1, &_capsuleVbo);
    if (_capsuleIbo) glDeleteBuffers(1, &_capsuleIbo);
    _capsuleVao = _capsuleVbo = _capsuleIbo = 0;
    if (_meshProgram) glDeleteProgram(_meshProgram);
    if (_capsuleProgram) glDeleteProgram(_capsuleProgram);
    _meshProgram = _capsuleProgram = 0;
    _glReady = false;
}

// ──────────────────────────────────────────────────────────────────────────────
// Per frame
// ──────────────────────────────────────────────────────────────────────────────

bool VROHandOcclusionOpenXR::useMesh(const Hand &hand) const {
    return _mode == VROHandOcclusionMode::Mesh && hand.mesh.loaded;
}

void VROHandOcclusionOpenXR::buildCapsules(const XrHandJointLocationEXT *joints, Hand &hand) {
    hand.capsules.clear();
    for (int s = 0; s < kCapsuleSegmentCount; ++s) {
        const XrHandJointLocationEXT &a = joints[kCapsuleSegments[s][0]];
        const XrHandJointLocationEXT &b = joints[kCapsuleSegments[s][1]];
        if (!(a.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) ||
            !(b.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
            continue;
        }
        hand.capsules.push_back({ xrVec3ToVRO(a.pose.position), xrVec3ToVRO(b.pose.position),
                                  clampRadius(0.5f * (a.radius + b.radius)) });
    }
}

void VROHandOcclusionOpenXR::update(XrSpace baseSpace, XrTime predictedDisplayTime, bool active) {
    _drawThisFrame = false;
    if (!_enabled) {
        _loggedInactive = false;
        return;
    }
    if (!active) {
        if (!_loggedInactive) {
            ALOGW("Hand occlusion is on but needs passthrough and hand tracking both enabled — "
                  "not drawing until they are");
            _loggedInactive = true;
        }
        return;
    }
    _loggedInactive = false;
    if (_pfnLocateHandJoints == nullptr) {
        return;
    }

    for (Hand &hand : _hands) {
        if (hand.tracker == XR_NULL_HANDLE) continue;

        XrHandJointLocationEXT joints[kJointCount] = {};
        XrHandJointLocationsEXT locations = { XR_TYPE_HAND_JOINT_LOCATIONS_EXT };
        locations.jointCount     = kJointCount;
        locations.jointLocations = joints;

        XrHandJointsLocateInfoEXT locateInfo = { XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT };
        locateInfo.baseSpace = baseSpace;
        locateInfo.time      = predictedDisplayTime;

        // Located at the same predicted time as the views: a hand drawn
        // against a stale pose leaves a halo on fast moves.
        XrResult r = _pfnLocateHandJoints(hand.tracker, &locateInfo, &locations);
        bool located = XR_SUCCEEDED(r) && locations.isActive;

        if (located && useMesh(hand)) {
            const XrSpaceLocationFlags poseValid = XR_SPACE_LOCATION_POSITION_VALID_BIT |
                                                   XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
            for (int j = 0; j < kJointCount && located; ++j) {
                located = (joints[j].locationFlags & poseValid) == poseValid;
            }
            if (located) {
                for (int j = 0; j < kJointCount; ++j) {
                    hand.skinMatrices[j] = xrPoseToMatrix(joints[j].pose)
                                               .multiply(hand.mesh.inverseBindPoses[j]);
                }
            }
        }
        if (located) {
            // Capsules are kept current in mesh mode too, so switching modes
            // at runtime has a pose to draw on the very next frame.
            buildCapsules(joints, hand);
            hand.havePose     = true;
            hand.lastPoseTime = predictedDisplayTime;
        } else if (hand.havePose && predictedDisplayTime - hand.lastPoseTime > kStaleNs) {
            hand.havePose = false;
        }
        _drawThisFrame |= hand.havePose;
    }
}

void VROHandOcclusionOpenXR::renderDepth(const VROMatrix4f &viewProjection,
                                         std::shared_ptr<VRODriver> &driver) {
    if (!_drawThisFrame || _glFailed) {
        return;
    }
    if (!_glReady) {
        if (!initGL()) {
            ALOGE("Hand occlusion GL setup failed — disabled for this session");
            destroyGL();
            _glFailed = true;
            return;
        }
        _glReady = true;
    }

    // Depth only. State goes through the driver so its cache stays in step
    // with GL; the base pass restores colour writes right after this returns.
    driver->setRenderTargetColorWritingMask(VROColorMaskNone);
    driver->setDepthWritingEnabled(true);
    driver->setDepthReadingEnabled(true);
    driver->setBlendingMode(VROBlendMode::None);
    // Closed meshes drawn depth-only resolve to the nearest surface either
    // way; skipping the cull keeps it independent of the mesh's winding.
    driver->setCullMode(VROCullMode::None);
    driver->unbindShader();

    const float inflate = std::max(0.0f, _inflateMm.load()) * 0.001f;

    for (Hand &hand : _hands) {
        if (!hand.havePose) continue;

        if (useMesh(hand)) {
            if (hand.mesh.vao == 0) {
                uploadMesh(hand.mesh);
            }
            glUseProgram(_meshProgram);
            glUniformMatrix4fv(_meshViewProjLoc, 1, GL_FALSE, viewProjection.getArray());
            float joints[kJointCount * 16];
            for (int j = 0; j < kJointCount; ++j) {
                memcpy(&joints[j * 16], hand.skinMatrices[j].getArray(), 16 * sizeof(float));
            }
            glUniformMatrix4fv(_meshJointsLoc, kJointCount, GL_FALSE, joints);
            glUniform1f(_meshInflateLoc, inflate);
            glBindVertexArray(hand.mesh.vao);
            glDrawElements(GL_TRIANGLES, hand.mesh.indexCount, GL_UNSIGNED_SHORT, (void *)0);
        } else {
            glUseProgram(_capsuleProgram);
            glUniformMatrix4fv(_capViewProjLoc, 1, GL_FALSE, viewProjection.getArray());
            glBindVertexArray(_capsuleVao);
            for (const Capsule &c : hand.capsules) {
                glUniform3f(_capALoc, c.a.x, c.a.y, c.a.z);
                glUniform3f(_capBLoc, c.b.x, c.b.y, c.b.z);
                glUniform1f(_capRadiusLoc, c.radius + inflate);
                glDrawElements(GL_TRIANGLES, _capsuleIndexCount, GL_UNSIGNED_SHORT, (void *)0);
            }
        }
    }

    glBindVertexArray(0);
    glUseProgram(0);
}
