// VROHandOcclusionOpenXR.h
// ViroRenderer
//
// Hand occlusion for Meta Quest passthrough. In mixed reality the real hands
// exist only in the passthrough video beneath the projection layer, so any
// virtual object in front of a hand hides it. Each eye, before the scene is
// drawn, this renders the tracked hands into the depth buffer only (colour
// writes off): scene pixels behind a hand fail the depth test, the layer stays
// transparent there, and the passthrough hand shows through. No depth
// submission or environment-depth API is involved.
//
// Two tiers of hand geometry:
//   Mesh      XR_FB_hand_tracking_mesh: the runtime's skinned hand mesh,
//             skinned on the GPU from the 26 located joints.
//   Capsules  XR_EXT_hand_tracking only: ~28 capsules between joints, sized
//             from the joint radii. The fallback when the mesh is absent.
//
// Optional and off by default. It only draws while passthrough and hand
// tracking are both on; otherwise it is a no-op.
//
// Threading: the setters are safe from any thread. init() runs on the thread
// that creates the session, update() and renderDepth() on the render thread
// (the latter inside the base render pass, via VRORenderContext's depth
// prepass), destroyGL() on the render thread with the context current.
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#ifndef ANDROID_VROHANDOCCLUSIONOPENXR_H
#define ANDROID_VROHANDOCCLUSIONOPENXR_H

#include <atomic>
#include <memory>
#include <vector>
#include <GLES3/gl3.h>
#include <openxr/openxr.h>
#include "VROMatrix4f.h"
#include "VROVector3f.h"

class VRODriver;

enum class VROHandOcclusionMode {
    Capsules,
    Mesh,  // falls back to Capsules when XR_FB_hand_tracking_mesh is absent
};

class VROHandOcclusionOpenXR {
public:

    VROHandOcclusionOpenXR();
    ~VROHandOcclusionOpenXR();

    /*
     Take the hand trackers (owned by VROInputControllerOpenXR) and, when
     meshExtAvailable, read each hand's mesh with xrGetHandMeshFB. The mesh is
     uploaded to GL lazily on the render thread.
     */
    void init(XrInstance instance, bool meshExtAvailable,
              PFN_xrLocateHandJointsEXT locateHandJoints,
              XrHandTrackerEXT leftTracker, XrHandTrackerEXT rightTracker);

    void setEnabled(bool enabled) { _enabled = enabled; }
    void setMode(VROHandOcclusionMode mode) { _mode = mode; }
    void setInflateMm(float mm) { _inflateMm = mm; }

    /*
     Once per frame, before the eyes are rendered. Locates both hands at the
     frame's predicted display time in baseSpace (the space the views are
     located in). active is passthrough && hand tracking; when false, nothing
     is drawn this frame.
     */
    void update(XrSpace baseSpace, XrTime predictedDisplayTime, bool active);

    /*
     Per eye, from the base render pass after its target is cleared. Draws the
     hands depth-only with the eye's view-projection.
     */
    void renderDepth(const VROMatrix4f &viewProjection, std::shared_ptr<VRODriver> &driver);

    void destroyGL();

private:

    static constexpr int kJointCount = XR_HAND_JOINT_COUNT_EXT;

    struct Capsule {
        VROVector3f a;
        VROVector3f b;
        float       radius;
    };

    // CPU copy of one hand's XR_FB_hand_tracking_mesh, and its GL buffers
    // (recreated from the copy after destroyGL()).
    struct HandMesh {
        bool                  loaded = false;     // read from the runtime
        std::vector<float>    vertices;           // interleaved, kMeshVertexFloats each
        std::vector<GLushort> indices;
        VROMatrix4f           inverseBindPoses[kJointCount];
        GLuint                vao = 0;
        GLuint                vbo = 0;
        GLuint                ibo = 0;
        GLsizei               indexCount = 0;
    };

    struct Hand {
        XrHandTrackerEXT     tracker = XR_NULL_HANDLE;
        HandMesh             mesh;

        // Last good pose. Kept up to kStaleNs after tracking drops so a
        // one-frame loss does not flicker the hole.
        bool                 havePose     = false;
        XrTime               lastPoseTime = 0;
        VROMatrix4f          skinMatrices[kJointCount];
        std::vector<Capsule> capsules;
    };

    static constexpr XrTime kStaleNs = 100 * 1000 * 1000;  // 100 ms

    std::atomic<bool>                 _enabled   { false };
    std::atomic<VROHandOcclusionMode> _mode      { VROHandOcclusionMode::Mesh };
    std::atomic<float>                _inflateMm { 2.0f };

    PFN_xrLocateHandJointsEXT _pfnLocateHandJoints = nullptr;
    Hand                      _hands[2];
    bool                      _drawThisFrame = false;
    VROHandOcclusionMode      _frameMode     = VROHandOcclusionMode::Mesh;   // _mode as this frame's update() read it
    bool                      _loggedInactive = false;

    // GL — created on first renderDepth().
    bool   _glReady        = false;
    bool   _glFailed       = false;
    GLuint _meshProgram    = 0;
    GLint  _meshViewProjLoc = -1;
    GLint  _meshJointsLoc   = -1;
    GLint  _meshInflateLoc  = -1;
    GLuint _capsuleProgram = 0;
    GLint  _capViewProjLoc = -1;
    GLint  _capALoc        = -1;
    GLint  _capBLoc        = -1;
    GLint  _capRadiusLoc   = -1;
    GLuint _capsuleVao     = 0;
    GLuint _capsuleVbo     = 0;
    GLuint _capsuleIbo     = 0;
    GLsizei _capsuleIndexCount = 0;

    void loadMesh(PFN_xrGetHandMeshFB getHandMesh, Hand &hand);
    bool initGL();
    void uploadMesh(HandMesh &mesh);
    bool useMesh(const Hand &hand) const;
    void buildCapsules(const XrHandJointLocationEXT *joints, Hand &hand);
};

#endif  // ANDROID_VROHANDOCCLUSIONOPENXR_H
