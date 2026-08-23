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
#include "VRODriverVisionOS.h"
#include "VROMetalPostProcess.h"
#include "VRORenderTarget.h"
#include "VRORenderTargetMetal.h"
#include "VROTexture.h"
#include "VROPortal.h"
#include "VROScene.h"
#include "VRORenderContext.h"

// Must match VROIBLUniforms in Shaders.metal.
struct VROMetalIBLUniforms {
    int   face;
    float roughness;
    float sample_delta;
    float padding;
};

static const int kVROIBLCubeSize        = 256;   // environment cubemap edge
static const int kVROIBLIrradianceSize  = 32;    // diffuse irradiance is very low frequency
static const int kVROIBLPrefilterSize   = 128;   // base mip of the specular chain
static const int kVROIBLPrefilterMips   = 5;
static const int kVROIBLBRDFSize        = 256;

// One full-screen blit into each face of a cube target.
static std::shared_ptr<VROTexture> VRORenderIBLCubeFaces(std::shared_ptr<VRODriver> &driver,
                                                        const std::string &function,
                                                        std::shared_ptr<VROTexture> source,
                                                        int size, int mipCount,
                                                        float sampleDelta) {
    std::shared_ptr<VRODriverVisionOS> metal = std::dynamic_pointer_cast<VRODriverVisionOS>(driver);
    if (!metal || !source) {
        return nullptr;
    }
    std::shared_ptr<VROImagePostProcess> post = metal->newMetalPostProcess(function);
    if (!post) {
        pinfo("VROIBLPreprocess: no post-process for '%s'", function.c_str());
        return nullptr;
    }

    std::shared_ptr<VRORenderTarget> target =
        driver->newRenderTarget(VRORenderTargetType::CubeTextureHDR16, 1, 1, mipCount > 1, false);
    std::shared_ptr<VRORenderTargetMetal> metalTarget =
        std::dynamic_pointer_cast<VRORenderTargetMetal>(target);
    if (!metalTarget) {
        return nullptr;
    }

    for (int mip = 0; mip < mipCount; mip++) {
        // Each mip is half the previous one, and the viewport has to follow or the blit
        // covers only a corner of the smaller level.
        const int mipSize = std::max(1, size >> mip);
        target->setViewport({ 0, 0, mipSize, mipSize });
        if (!target->hydrate()) {
            pinfo("VROIBLPreprocess: could not hydrate a %dx%d cube target", mipSize, mipSize);
            return nullptr;
        }
        // Roughness walks 0..1 across the chain; with a single mip it stays at 0.
        const float roughness = (mipCount > 1) ? (float)mip / (float)(mipCount - 1) : 0.0f;

        for (int face = 0; face < 6; face++) {
            VROMetalIBLUniforms uniforms = {};
            uniforms.face = face;
            uniforms.roughness = roughness;
            uniforms.sample_delta = sampleDelta;
            std::static_pointer_cast<VROMetalPostProcess>(post)->setUniforms(&uniforms, sizeof(uniforms));

            target->setTextureCubeFace(face, mip, 0);
            driver->bindRenderTarget(target, VRORenderTargetUnbindOp::None);
            post->blit({ source }, driver);
        }
    }
    return target->getTexture(0);
}

VROIBLPreprocess::VROIBLPreprocess() : _phase(VROIBLPhase::Idle) {}
VROIBLPreprocess::~VROIBLPreprocess() {}

void VROIBLPreprocess::doCubeConversionPhase(std::shared_ptr<VROScene> scene,
                                            VRORenderContext *context,
                                            std::shared_ptr<VRODriver> driver) {
    _cubeLightingEnvironment = VRORenderIBLCubeFaces(driver, "post_equirect_to_cube",
                                                     _currentLightingEnvironment,
                                                     kVROIBLCubeSize, 1, 0.0f);
}

void VROIBLPreprocess::doIrradianceConvolutionPhase(std::shared_ptr<VROScene> scene,
                                                   VRORenderContext *context,
                                                   std::shared_ptr<VRODriver> driver) {
    // 0.025 rad keeps the hemisphere integral under ~8k samples per texel, which is
    // affordable because the target is only 32x32.
    _irradianceMap = VRORenderIBLCubeFaces(driver, "post_irradiance_convolution",
                                           _cubeLightingEnvironment,
                                           kVROIBLIrradianceSize, 1, 0.025f);
}

void VROIBLPreprocess::doPrefilterConvolutionPhase(std::shared_ptr<VROScene> scene,
                                                   VRORenderContext *context,
                                                   std::shared_ptr<VRODriver> driver) {
    _prefilterMap = VRORenderIBLCubeFaces(driver, "post_prefilter_convolution",
                                          _cubeLightingEnvironment,
                                          kVROIBLPrefilterSize, kVROIBLPrefilterMips, 0.0f);
}

void VROIBLPreprocess::doBRDFComputationPhase(std::shared_ptr<VROScene> scene,
                                              VRORenderContext *context,
                                              std::shared_ptr<VRODriver> driver) {
    std::shared_ptr<VRODriverVisionOS> metal = std::dynamic_pointer_cast<VRODriverVisionOS>(driver);
    if (!metal) {
        return;
    }
    std::shared_ptr<VROImagePostProcess> post = metal->newMetalPostProcess("post_brdf_integration");
    if (!post) {
        return;
    }
    // A plain 2D LUT indexed by (NdotV, roughness); it depends on nothing in the scene, so
    // it is computed once and reused.
    std::shared_ptr<VRORenderTarget> target =
        driver->newRenderTarget(VRORenderTargetType::ColorTextureHDR16, 1, 1, false, false);
    target->setViewport({ 0, 0, kVROIBLBRDFSize, kVROIBLBRDFSize });
    if (!target->hydrate()) {
        return;
    }
    driver->bindRenderTarget(target, VRORenderTargetUnbindOp::None);
    post->blit({}, driver);
    _brdfMap = target->getTexture(0);
}

void VROIBLPreprocess::execute(std::shared_ptr<VROScene> scene,
                               VRORenderContext *context,
                               std::shared_ptr<VRODriver> driver) {
    // One phase per frame, matching the shared implementation: each is expensive and
    // spreading them keeps the first frames after a lighting-environment change smooth.
    if (_phase == VROIBLPhase::Idle) {
        std::shared_ptr<VROPortal> portal = scene->getActivePortal();
        if (!portal) {
            return;
        }
        std::shared_ptr<VROTexture> environment = portal->getLightingEnvironment();

        if (environment != nullptr && environment != _currentLightingEnvironment) {
            _currentLightingEnvironment = environment;
            _phase = VROIBLPhase::CubeConvert;
        }
        else if (environment == nullptr && _currentLightingEnvironment != nullptr) {
            context->setIrradianceMap(nullptr);
            context->setBRDFMap(nullptr);
            context->setPrefilteredMap(nullptr);
            _currentLightingEnvironment = nullptr;
        }
    }
    else if (_phase == VROIBLPhase::CubeConvert) {
        doCubeConversionPhase(scene, context, driver);
        _phase = _cubeLightingEnvironment ? VROIBLPhase::IrradianceConvolution : VROIBLPhase::Idle;
    }
    else if (_phase == VROIBLPhase::IrradianceConvolution) {
        doIrradianceConvolutionPhase(scene, context, driver);
        context->setIrradianceMap(_irradianceMap);
        _phase = VROIBLPhase::PrefilterConvolution;
    }
    else if (_phase == VROIBLPhase::PrefilterConvolution) {
        doPrefilterConvolutionPhase(scene, context, driver);
        context->setPrefilteredMap(_prefilterMap);
        _phase = VROIBLPhase::BRDFConvolution;
    }
    else if (_phase == VROIBLPhase::BRDFConvolution) {
        doBRDFComputationPhase(scene, context, driver);
        context->setBRDFMap(_brdfMap);
        _phase = VROIBLPhase::Idle;
    }
}

// ── Shader program base + image shader ───────────────────────────────────────
// VROShaderProgram.cpp is excluded from the visionOS target (GL-only impl).
// We stub the constructor/destructor so VROImageShaderProgram can call its base.

#include "VROShaderProgram.h"
#include "VROImageShaderProgram.h"

// The GL program itself has no meaning on Metal; only the name survives, and it is used
// to carry which MSL function a shared render pass asked for. See
// VROImageShaderProgram::create below and VRODriverVisionOS::newImagePostProcess.
VROShaderProgram::VROShaderProgram(std::string vertexShader,
                                   std::string fragmentShader,
                                   const std::vector<std::string> &samplers,
                                   const std::vector<std::shared_ptr<VROShaderModifier>> &modifiers,
                                   int attributes,
                                   std::shared_ptr<VRODriverOpenGL> driver) {
    _shaderName = fragmentShader;
}
VROShaderProgram::~VROShaderProgram() {}
void VROShaderProgram::bindAttributes() {}
void VROShaderProgram::bindUniformBlocks() {}
void VROShaderProgram::addStandardUniforms() {}

// Shared render passes ask for a full-screen effect by handing over GLSL. Metal cannot
// compile that, so the effect is identified by its sampler signature and the matching
// MSL function name is carried in the program's name for newImagePostProcess to pick up.
//
// A signature that is not recognised falls back to a straight copy, which is visually
// wrong but never a crash — and it is logged, because a silent wrong copy is worse than
// a noisy one.
std::shared_ptr<VROShaderProgram> VROImageShaderProgram::create(
        const std::vector<std::string> &samplers,
        const std::vector<std::string> &code,
        std::shared_ptr<VRODriver> driver) {
    auto hasSampler = [&samplers](const char *name) {
        return std::find(samplers.begin(), samplers.end(), std::string(name)) != samplers.end();
    };

    std::string function;
    if (hasSampler("hdr_texture") && hasSampler("bloom_texture")) {
        function = "post_additive_blend";
    } else if (samplers.size() <= 1) {
        function = "post_blit";
    } else {
        pinfo("VROImageShaderProgram: no Metal equivalent for a %zu-sampler effect; "
              "falling back to a straight copy", samplers.size());
        function = "post_blit";
    }
    return std::make_shared<VROShaderProgram>("", function, samplers,
                                              std::vector<std::shared_ptr<VROShaderModifier>>(),
                                              0, std::shared_ptr<VRODriverOpenGL>());
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
//
// Metal implementation of the separable two-pass blur used for bloom. The shared
// implementation builds its GLSL at runtime and passes the kernel through uniform
// binders on a VROShaderModifier, neither of which exists on Metal — so the kernel is
// computed here and handed to post_gaussian_blur as one uniform block.
//
// The kernel is the same construction the OpenGL path uses: samples of a Gaussian
// integrated over each tap, optionally normalised so the blur's spread is independent of
// kernel size, and optionally collapsed into bilinear taps so N texels cost N/2 samples.

#include "VROViewport.h"
#include "VROGaussianBlurRenderPass.h"
#include "VRODriverVisionOS.h"
#include "VROMetalPostProcess.h"
#include "VRORenderTarget.h"
#include "VROTexture.h"
#include <cmath>
#include <algorithm>
#include <simd/simd.h>

// Must match VROGaussianBlurUniforms in Shaders.metal.
struct VROMetalGaussianUniforms {
    simd_float2 direction;
    int         tap_count;
    float       intensity;
    simd_float4 weights[8];
    simd_float4 offsets[8];
};

static const int kVROMaxGaussianTaps = 32;

VROGaussianBlurRenderPass::VROGaussianBlurRenderPass() :
    _blurScaling(0.5),
    _numBlurIterations(4),
    _horizontal(false),
    _bilinearTextureLookup(true),
    _sigma(1),
    _kernelSize(5),
    _reinforcedIntensity(1),
    _normalizedKernel(true),
    _considerTransparentPixels(false),
    _preBlurPass(nullptr),
    _gaussianBlur(nullptr),
    _blurTargetA(nullptr),
    _blurTargetB(nullptr) {
}

VROGaussianBlurRenderPass::~VROGaussianBlurRenderPass() {}

void VROGaussianBlurRenderPass::createRenderTargets(std::shared_ptr<VRODriver> &driver) {
    _blurTargetA = driver->newRenderTarget(VRORenderTargetType::ColorTextureHDR16, 1, 1, false, false);
    _blurTargetB = driver->newRenderTarget(VRORenderTargetType::ColorTextureHDR16, 1, 1, false, false);
}

void VROGaussianBlurRenderPass::resetRenderTargets() {
    _blurTargetA = nullptr;
    _blurTargetB = nullptr;
}

void VROGaussianBlurRenderPass::setViewPort(VROViewport viewport, std::shared_ptr<VRODriver> &driver) {
    // The blur runs at a fraction of the display resolution; the result is upsampled by
    // the bilinear filter when it is sampled back, which is both cheaper and softer.
    const int width  = std::max(1, (int)(viewport.getWidth()  * _blurScaling));
    const int height = std::max(1, (int)(viewport.getHeight() * _blurScaling));
    if (_blurTargetA) {
        _blurTargetA->setViewport({ 0, 0, width, height });
        _blurTargetA->hydrate();
    }
    if (_blurTargetB) {
        _blurTargetB->setViewport({ 0, 0, width, height });
        _blurTargetB->hydrate();
    }
}

void VROGaussianBlurRenderPass::setClearColor(VROVector4f color, std::shared_ptr<VRODriver> driver) {
    setClearColor(color);
}

void VROGaussianBlurRenderPass::setClearColor(VROVector4f color) {
    if (_blurTargetA) { _blurTargetA->setClearColor(color); }
    if (_blurTargetB) { _blurTargetB->setClearColor(color); }
}

void VROGaussianBlurRenderPass::setNumBlurIterations(int numIterations) {
    _numBlurIterations = std::max(1, numIterations);
}

void VROGaussianBlurRenderPass::setBlurKernel(int kernelSize, float sigma, bool normalized) {
    _kernelSize = std::max(1, kernelSize);
    _sigma = sigma;
    _normalizedKernel = normalized;
    resetShaders();
}

void VROGaussianBlurRenderPass::setBilinearTextureLookup(bool enabled) {
    _bilinearTextureLookup = enabled;
    resetShaders();
}

void VROGaussianBlurRenderPass::resetShaders() {
    _gaussianBlur = nullptr;
    _preBlurPass = nullptr;
}

float VROGaussianBlurRenderPass::gaussianDistribution(float x, float mu, float sigma) {
    const float d = x - mu;
    const float n = 1.0f / (std::sqrt(2.0f * (float)M_PI) * sigma);
    return std::exp(-d * d / (2.0f * sigma * sigma)) * n;
}

std::vector<std::vector<float>> VROGaussianBlurRenderPass::calculateSamplesForRange(float minInclusive,
                                                                                   float maxInclusive,
                                                                                   int sampleCount,
                                                                                   float sigma) {
    std::vector<std::vector<float>> samples;
    const float step = (maxInclusive - minInclusive) / std::max(1, sampleCount - 1);
    for (int i = 0; i < sampleCount; i++) {
        const float x = minInclusive + step * i;
        samples.push_back({ x, gaussianDistribution(x, 0.0f, sigma) });
    }
    return samples;
}

float VROGaussianBlurRenderPass::integrateSimphson(std::vector<std::vector<float>> tapSamples) {
    // Composite Simpson's rule over the samples, which requires an odd sample count.
    if (tapSamples.size() < 3) {
        return tapSamples.empty() ? 0.0f : tapSamples.front()[1];
    }
    float result = tapSamples.front()[1] + tapSamples.back()[1];
    for (size_t i = 1; i + 1 < tapSamples.size(); i++) {
        result += tapSamples[i][1] * ((i % 2 == 1) ? 4.0f : 2.0f);
    }
    const float h = tapSamples[1][0] - tapSamples[0][0];
    return result * h / 3.0f;
}

std::vector<float> VROGaussianBlurRenderPass::calculateKernel(float sigma, float kernelSize,
                                                             float sampleCount) {
    // One weight per tap from the centre outwards, each the integral of the Gaussian over
    // that tap's texel. Normalising by the total makes the weights sum to 1 so the blur
    // neither brightens nor darkens the image.
    const int taps = std::max(1, (int)kernelSize);
    std::vector<float> weights;
    float total = 0.0f;
    for (int i = 0; i < taps; i++) {
        const float centre = (float)i;
        std::vector<std::vector<float>> samples =
            calculateSamplesForRange(centre - 0.5f, centre + 0.5f, (int)sampleCount | 1, sigma);
        const float weight = integrateSimphson(samples);
        weights.push_back(weight);
        // The centre tap is counted once, every other tap twice (both sides).
        total += (i == 0) ? weight : weight * 2.0f;
    }
    if (_normalizedKernel && total > 0.0f) {
        for (float &weight : weights) {
            weight /= total;
        }
    }
    return weights;
}

void VROGaussianBlurRenderPass::convertToLinearKernelSample(std::vector<float> &kernelWeights,
                                                            std::vector<float> &kernelOffsets) {
    // Collapse pairs of adjacent taps into one bilinear tap: two texels with weights
    // w0, w1 are equivalent to a single sample at their weighted midpoint with weight
    // w0 + w1. Halves the sample count for the same kernel.
    if (kernelWeights.size() < 3) {
        return;
    }
    std::vector<float> linearWeights = { kernelWeights[0] };
    std::vector<float> linearOffsets = { 0.0f };
    for (size_t i = 1; i + 1 < kernelWeights.size(); i += 2) {
        const float w0 = kernelWeights[i];
        const float w1 = kernelWeights[i + 1];
        const float combined = w0 + w1;
        if (combined <= 0.0f) {
            continue;
        }
        const float o0 = kernelOffsets[i];
        const float o1 = kernelOffsets[i + 1];
        linearWeights.push_back(combined);
        linearOffsets.push_back((o0 * w0 + o1 * w1) / combined);
    }
    kernelWeights = linearWeights;
    kernelOffsets = linearOffsets;
}

void VROGaussianBlurRenderPass::initPostProcess(std::shared_ptr<VRODriver> driver) {
    initBlurPass(driver);
    initPreBlurPass(driver);
}

void VROGaussianBlurRenderPass::initBlurPass(std::shared_ptr<VRODriver> driver) {
    std::shared_ptr<VRODriverVisionOS> metal = std::dynamic_pointer_cast<VRODriverVisionOS>(driver);
    if (metal) {
        _gaussianBlur = metal->newMetalPostProcess("post_gaussian_blur");
    }
}

void VROGaussianBlurRenderPass::initPreBlurPass(std::shared_ptr<VRODriver> driver) {
    // The OpenGL path runs a pre-pass to reinforce intensity before blurring. On Metal
    // the intensity is folded into the blur shader's uniform block, so no extra pass.
    _preBlurPass = nullptr;
}

void VROGaussianBlurRenderPass::render(std::shared_ptr<VROScene> scene,
                                       std::shared_ptr<VROScene> outgoingScene,
                                       VRORenderPassInputOutput &inputs,
                                       VRORenderContext *context,
                                       std::shared_ptr<VRODriver> &driver) {
    std::shared_ptr<VROTexture> input = inputs.textures[kGaussianInput];
    if (!input || !_blurTargetA || !_blurTargetB) {
        return;
    }
    if (!_gaussianBlur) {
        initPostProcess(driver);
        if (!_gaussianBlur) {
            return;
        }
    }

    std::vector<float> weights = calculateKernel(_sigma, _kernelSize, 5);
    std::vector<float> offsets;
    for (size_t i = 0; i < weights.size(); i++) {
        offsets.push_back((float)i);
    }
    if (_bilinearTextureLookup) {
        convertToLinearKernelSample(weights, offsets);
    }
    const int taps = std::min((int)weights.size(), kVROMaxGaussianTaps);

    std::shared_ptr<VROMetalPostProcess> blur =
        std::static_pointer_cast<VROMetalPostProcess>(_gaussianBlur);

    const float texelWidth  = 1.0f / std::max(1, _blurTargetA->getWidth());
    const float texelHeight = 1.0f / std::max(1, _blurTargetA->getHeight());

    std::shared_ptr<VROTexture> source = input;
    std::shared_ptr<VRORenderTarget> destination = _blurTargetA;
    std::shared_ptr<VRORenderTarget> other = _blurTargetB;

    // Each iteration is one horizontal plus one vertical pass, ping-ponging between the
    // two targets. The intensity reinforcement is applied on the first pass only, so it
    // is not compounded once per iteration.
    for (int iteration = 0; iteration < _numBlurIterations; iteration++) {
        for (int axis = 0; axis < 2; axis++) {
            VROMetalGaussianUniforms uniforms = {};
            uniforms.direction = (axis == 0) ? (simd_float2){ texelWidth, 0.0f }
                                            : (simd_float2){ 0.0f, texelHeight };
            uniforms.tap_count = taps;
            uniforms.intensity = (iteration == 0 && axis == 0) ? _reinforcedIntensity : 1.0f;
            for (int i = 0; i < taps; i++) {
                uniforms.weights[i >> 2][i & 3] = weights[i];
                uniforms.offsets[i >> 2][i & 3] = offsets[i];
            }
            blur->setUniforms(&uniforms, sizeof(uniforms));

            driver->bindRenderTarget(destination, VRORenderTargetUnbindOp::Invalidate);
            blur->blit({ source }, driver);

            source = destination->getTexture(0);
            std::swap(destination, other);
        }
    }

    // The last write landed in `other` after the final swap.
    inputs.outputTarget = other;
}

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

// VROPortal.cpp is excluded from this target, so the lighting-environment accessors are
// implemented here. They are what feeds VROIBLPreprocess.
void VROPortal::setLightingEnvironment(std::shared_ptr<VROTexture> texture) {
    _lightingEnvironment = texture;
}

std::shared_ptr<VROTexture> VROPortal::getLightingEnvironment() const {
    return _lightingEnvironment;
}

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
