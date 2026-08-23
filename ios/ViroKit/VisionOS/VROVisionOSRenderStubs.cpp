// VROVisionOSRenderStubs.cpp
// Stub implementations for ViroKit classes excluded from the ViroKitVisionOS target.
// These exist solely to satisfy the linker on visionOS; none are called during a
// typical visionOS render (shadows/bloom/HDR/physics are all disabled in the POC config).

// Standard headers first to avoid order-dependency issues across class headers.
#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <memory>

// VRODefines.h must precede the platform guard so VRO_PLATFORM_VISION is defined.
#include "VRODefines.h"

#if VRO_PLATFORM_VISION

// ── Physics ───────────────────────────────────────────────────────────────────

#include "VROPhysicsBody.h"
#include "VROPhysicsWorld.h"

VROPhysicsBody::VROPhysicsBody(std::shared_ptr<VRONode> node,
                               VROPhysicsBody::VROPhysicsBodyType type,
                               float mass,
                               std::shared_ptr<VROPhysicsShape> shape) {}
VROPhysicsBody::~VROPhysicsBody() {}
void VROPhysicsBody::refreshBody() {}
void VROPhysicsBody::setKinematicDrag(bool isDragging) {}

VROPhysicsWorld::VROPhysicsWorld() {}
VROPhysicsWorld::~VROPhysicsWorld() {}
void VROPhysicsWorld::addPhysicsBody(std::shared_ptr<VROPhysicsBody> body) {}
void VROPhysicsWorld::removePhysicsBody(std::shared_ptr<VROPhysicsBody> body) {}
void VROPhysicsWorld::computePhysics(const VRORenderContext &context) {}

// ── IBL / Shadow preprocesses ─────────────────────────────────────────────────

#include "VROIBLPreprocess.h"

VROIBLPreprocess::VROIBLPreprocess() {}
VROIBLPreprocess::~VROIBLPreprocess() {}
void VROIBLPreprocess::execute(std::shared_ptr<VROScene> scene,
                               VRORenderContext *context,
                               std::shared_ptr<VRODriver> driver) {}

// ── Shader program base + image shader ───────────────────────────────────────
// VROShaderProgram.cpp is excluded from the visionOS target (GL-only impl).
// We stub the constructor/destructor so VROImageShaderProgram can call its base.

#include "VROShaderProgram.h"
#include "VROImageShaderProgram.h"

VROShaderProgram::VROShaderProgram(std::string vertexShader,
                                   std::string fragmentShader,
                                   const std::vector<std::string> &samplers,
                                   const std::vector<std::shared_ptr<VROShaderModifier>> &modifiers,
                                   int attributes,
                                   std::shared_ptr<VRODriverOpenGL> driver) {}
VROShaderProgram::~VROShaderProgram() {}
void VROShaderProgram::bindAttributes() {}
void VROShaderProgram::bindUniformBlocks() {}
void VROShaderProgram::addStandardUniforms() {}

std::shared_ptr<VROShaderProgram> VROImageShaderProgram::create(
        const std::vector<std::string> &samplers,
        const std::vector<std::string> &code,
        std::shared_ptr<VRODriver> driver) {
    return nullptr;
}

VROImageShaderProgram::VROImageShaderProgram(
        const std::vector<std::string> &samplers,
        const std::vector<std::shared_ptr<VROShaderModifier>> &modifiers,
        std::shared_ptr<VRODriver> driver)
    : VROShaderProgram("", "", {}, {}, 0, {}) {}
VROImageShaderProgram::~VROImageShaderProgram() {}
void VROImageShaderProgram::bindAttributes() {}
void VROImageShaderProgram::bindUniformBlocks() {}
void VROImageShaderProgram::addStandardUniforms() {}

// ── Portal tree render pass ───────────────────────────────────────────────────
// VROPortal.h must be included before VROScene.h so VROPortal is fully defined
// when VROScene.h's forward declaration is seen.

#include "VROPortal.h"
#include "VROGeometry.h"
#include "VROMaterial.h"
#include "VROLight.h"
#include "VRORenderContext.h"
#include "VROScene.h"
#include "VROPortalTreeRenderPass.h"
#include "VRORenderTarget.h"
#include "VRODriver.h"

VROPortalTreeRenderPass::VROPortalTreeRenderPass() {}
VROPortalTreeRenderPass::~VROPortalTreeRenderPass() {}
void VROPortalTreeRenderPass::render(std::shared_ptr<VROScene> scene,
                                     std::shared_ptr<VROScene> outgoingScene,
                                     VRORenderPassInputOutput &inputs,
                                     VRORenderContext *context,
                                     std::shared_ptr<VRODriver> &driver) {
    if (!scene) return;

    // Bind the pass's output target before drawing. On Metal this is what opens the
    // render command encoder, so it is not optional the way glBindFramebuffer would
    // be — without it there is nothing to encode into. It is also what makes
    // render-to-texture work: the choreographer hands us an offscreen target for the
    // HDR / bloom / RTT paths and the display target otherwise.
    std::shared_ptr<VRORenderTarget> target = inputs.outputTarget;
    if (target) {
        driver->bindRenderTarget(target, VRORenderTargetUnbindOp::Invalidate);
    }

    const auto &treeNode = scene->getPortalTree();
    const std::shared_ptr<VROPortal> &rootPortal = treeNode.value;
    if (rootPortal) {
        rootPortal->renderContents(*context, driver);
    }
}

// ── Tone mapping render pass ──────────────────────────────────────────────────
//
// The shared implementation assembles a GLSL fragment shader from string fragments, so
// it cannot be reused on Metal. This is the Metal equivalent: each tone-mapping method
// is a named MSL function in Shaders.metal and the parameters travel as one uniform
// block. See the post_tone_map_* functions there.

#include "VROToneMappingRenderPass.h"
#include "VRODriverVisionOS.h"
#include "VROMetalPostProcess.h"
#include "VRORenderTarget.h"
#include "VROTexture.h"

// Must match VROToneMappingUniforms in Shaders.metal.
struct VROMetalToneMappingUniforms {
    float exposure;
    float white_point;
    float gamma_correct;
};

VROToneMappingRenderPass::VROToneMappingRenderPass(VROToneMappingMethod method,
                                                   bool gammaCorrectSoftware,
                                                   std::shared_ptr<VRODriver> driver) :
    _method(method),
    _exposure(kToneMappingDefaultExposure),
    _whitePoint(kToneMappingDefaultWhitePoint),
    _gammaCorrectionEnabled(gammaCorrectSoftware) {
}

VROToneMappingRenderPass::~VROToneMappingRenderPass() {}

std::shared_ptr<VROImagePostProcess> VROToneMappingRenderPass::createPostProcess(std::shared_ptr<VRODriver> driver,
                                                                                VROToneMappingMethod method) {
    std::shared_ptr<VRODriverVisionOS> metal = std::dynamic_pointer_cast<VRODriverVisionOS>(driver);
    if (!metal) {
        return nullptr;
    }
    const char *function = "post_tone_map_disabled";
    switch (method) {
        case VROToneMappingMethod::Disabled:            function = "post_tone_map_disabled";        break;
        case VROToneMappingMethod::Reinhard:            function = "post_tone_map_reinhard";        break;
        case VROToneMappingMethod::Hable:               function = "post_tone_map_hable";           break;
        case VROToneMappingMethod::HableLuminanceOnly:  function = "post_tone_map_hable_luminance"; break;
        default:                                        function = "post_tone_map_exposure";        break;
    }
    return metal->newMetalPostProcess(function);
}

void VROToneMappingRenderPass::render(std::shared_ptr<VROScene> scene,
                                      std::shared_ptr<VROScene> outgoingScene,
                                      VRORenderPassInputOutput &inputs,
                                      VRORenderContext *context,
                                      std::shared_ptr<VRODriver> &driver) {
    if (!_postProcess) {
        _postProcess = createPostProcess(driver, _method);
        if (!_postProcess) {
            return;
        }
    }

    std::shared_ptr<VROTexture> hdrInput = inputs.textures[kToneMappingHDRInput];
    std::shared_ptr<VROTexture> mask     = inputs.textures[kToneMappingMaskInput];
    std::shared_ptr<VRORenderTarget> target = inputs.outputTarget;
    if (!hdrInput || !target) {
        return;
    }
    // The mask is optional: without it every fragment is treated as fully tone-mapped,
    // which is the sane default for a scene that never asked for per-material control.
    if (!mask) {
        mask = hdrInput;
    }

    VROMetalToneMappingUniforms uniforms;
    uniforms.exposure      = _exposure;
    uniforms.white_point   = _whitePoint;
    uniforms.gamma_correct = _gammaCorrectionEnabled ? 1.0f : 0.0f;
    std::static_pointer_cast<VROMetalPostProcess>(_postProcess)->setUniforms(&uniforms, sizeof(uniforms));

    driver->bindRenderTarget(target, VRORenderTargetUnbindOp::Invalidate);
    _postProcess->blit({ hdrInput, mask }, driver);
}

void VROToneMappingRenderPass::setExposure(float exposure) {
    _exposure = exposure;
}

void VROToneMappingRenderPass::setWhitePoint(float whitePoint) {
    _whitePoint = whitePoint;
}

void VROToneMappingRenderPass::setMethod(VROToneMappingMethod method) {
    if (_method == method) {
        return;
    }
    _method = method;
    // Dropped so the next render rebuilds it against the new method's MSL function.
    _postProcess = nullptr;
}

// ── Gaussian blur render pass ─────────────────────────────────────────────────
// VROViewport must be fully defined (setViewPort takes it by value).

#include "VROViewport.h"
#include "VROGaussianBlurRenderPass.h"

VROGaussianBlurRenderPass::VROGaussianBlurRenderPass() {}
VROGaussianBlurRenderPass::~VROGaussianBlurRenderPass() {}
void VROGaussianBlurRenderPass::render(std::shared_ptr<VROScene> scene,
                                       std::shared_ptr<VROScene> outgoingScene,
                                       VRORenderPassInputOutput &inputs,
                                       VRORenderContext *context,
                                       std::shared_ptr<VRODriver> &driver) {}
void VROGaussianBlurRenderPass::createRenderTargets(std::shared_ptr<VRODriver> &driver) {}
void VROGaussianBlurRenderPass::resetRenderTargets() {}
void VROGaussianBlurRenderPass::setViewPort(VROViewport viewport,
                                            std::shared_ptr<VRODriver> &driver) {}
void VROGaussianBlurRenderPass::setClearColor(VROVector4f color,
                                              std::shared_ptr<VRODriver> driver) {}
void VROGaussianBlurRenderPass::setClearColor(VROVector4f color) {}
void VROGaussianBlurRenderPass::setNumBlurIterations(int numIterations) {}
void VROGaussianBlurRenderPass::setBlurKernel(int kernelSize, float sigma, bool normalized) {}
void VROGaussianBlurRenderPass::setBilinearTextureLookup(bool enabled) {}
// Called from inline setReinforcedIntensity() in the header.
void VROGaussianBlurRenderPass::resetShaders() {}

// ── Portal traversal listener ─────────────────────────────────────────────────

#include "VROPortalTraversalListener.h"

VROPortalTraversalListener::VROPortalTraversalListener(std::shared_ptr<VROScene> scene) {}
VROPortalTraversalListener::~VROPortalTraversalListener() {}
void VROPortalTraversalListener::onFrameWillRender(const VRORenderContext &context) {}
void VROPortalTraversalListener::onFrameDidRender(const VRORenderContext &context) {}

// ── IK Rig ────────────────────────────────────────────────────────────────────

#include "VROIKRig.h"

VROIKRig::VROIKRig(std::shared_ptr<VRONode> root,
                   std::map<std::string, std::shared_ptr<VRONode>> endAffectors) {}
VROIKRig::VROIKRig(std::shared_ptr<VROSkeleton> skeleton,
                   std::map<std::string, int> endEffectorBoneIndexMap) {}
VROIKRig::~VROIKRig() {}
void VROIKRig::processRig() {}

// ── Portal ────────────────────────────────────────────────────────────────────

VROPortal::VROPortal() {}
VROPortal::~VROPortal() {}
void VROPortal::deleteGL() {}

void VROPortal::traversePortals(int frame, int recursionLevel,
                                std::shared_ptr<VROPortalFrame> activeFrame,
                                tree<std::shared_ptr<VROPortal>> *outPortals) {
    outPortals->value = std::dynamic_pointer_cast<VROPortal>(shared_from_this());
    // No recursive portal support in visionOS POC.
}

void VROPortal::sortNodesBySortKeys() {
    _keys.clear();
    getSortKeysForVisibleNodes(&_keys);
    std::sort(_keys.begin(), _keys.end());
}

void VROPortal::renderContents(const VRORenderContext &context, std::shared_ptr<VRODriver> &driver) {
    uint32_t boundMaterialId = UINT32_MAX;
    std::vector<std::shared_ptr<VROLight>> boundLights;

    for (VROSortKey &key : _keys) {
        VRONode *node = (VRONode *)key.node;
        const std::shared_ptr<VROGeometry> &geometry = node->getGeometry();
        if (!geometry) continue;

        std::shared_ptr<VROMaterial> material = geometry->getMaterialForElement(key.elementIndex);
        if (!key.incoming) material = material->getOutgoing();

        if (key.material != boundMaterialId || boundLights != node->getComputedLights()) {
            if (!material->bindShader(key.lights, node->getComputedLights(), context, driver)) continue;
            material->bindProperties(driver);
            boundMaterialId = key.material;
            boundLights = node->getComputedLights();
        }

        if (!boundLights.empty() ||
            material->getLightingModel() == VROLightingModel::Constant ||
            (material->getLightingModel() == VROLightingModel::PhysicallyBased &&
             context.getIrradianceMap() != nullptr)) {
            node->render(key.elementIndex, material, context, driver);
        }
    }
}

// ── Reticle ───────────────────────────────────────────────────────────────────

#include "VROReticle.h"

VROReticle::VROReticle(std::shared_ptr<VROTexture> icon) {}
VROReticle::~VROReticle() {}
void VROReticle::renderEye(VROEyeType eye,
                           const VRORenderContext &renderContext,
                           std::shared_ptr<VRODriver> &driver) {}
bool VROReticle::isHeadlocked() { return false; }

#endif  // VRO_PLATFORM_VISION
