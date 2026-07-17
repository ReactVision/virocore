//
//  VROSceneWeb.cpp
//  ViroRenderer — Web (WASM) library entry point
//
//  See VROSceneWeb.h. Render loop mirrors VROViewScene::drawFrame; the scene
//  build mirrors VROBoxTest but with no texture (so no image/SDL load at
//  scene-build time) and minimal lighting (Blinn), so we exercise the least
//  amount of the renderer needed to prove the pipeline.
//

#include "VROSceneWeb.h"

#include <emscripten/bind.h>

#include "VROLog.h"
#include "VRORenderer.h"
#include "VRORendererConfiguration.h"
#include "VRODriverOpenGLWasm.h"
#include "VROInputControllerWasm.h"
#include "VROInputControllerBase.h"
#include "VROThreadRestricted.h"
#include "VROEye.h"
#include "VROPlatformUtil.h"

#include "VROSceneController.h"
#include "VROScene.h"
#include "VROPortal.h"
#include "VRONode.h"
#include "VRONodeCamera.h"
#include "VROLight.h"
#include "VROBox.h"
#include "VROSphere.h"
#include "VROSurface.h"
#include "VROGeometry.h"
#include "VROMaterial.h"
#include "VROMaterialVisual.h"
#include "VROTexture.h"
#include "VROData.h"
#include "VROTransaction.h"
#include "VROEventDelegate.h"
#include "VROGLTFLoader.h"
#include "VROFBXLoader.h"
#include "VROModelIOUtil.h"
#include "VROExecutableAnimation.h"
#include <set>

#include <unordered_map>
#include <vector>

static VROSceneWeb *sInstance = nullptr;

// Minimal click handler for the demo cube: toggles the diffuse color on each
// click so touch → hit-test → click wiring is visible without any component API.
class CubeClickDelegate : public VROEventDelegate {
public:
    CubeClickDelegate(std::shared_ptr<VROMaterial> material) : _material(material), _toggled(false) {}
    virtual ~CubeClickDelegate() {}

    virtual void onClick(int source, std::shared_ptr<VRONode> node,
                         ClickState clickState, std::vector<float> position) {
        if (clickState != ClickState::ClickUp) {
            return;
        }
        std::shared_ptr<VROMaterial> material = _material.lock();
        if (!material) {
            return;
        }
        _toggled = !_toggled;
        if (_toggled) {
            material->getDiffuse().setColor({1.0, 0.4, 0.2, 1.0});
        } else {
            material->getDiffuse().setColor({0.2, 0.6, 1.0, 1.0});
        }
        pinfo("VROSceneWeb: cube clicked, toggled color");
    }

private:
    std::weak_ptr<VROMaterial> _material;
    bool _toggled;
};

// emscripten_set_main_loop requires a plain C-style callback.
static void VROSceneWebMainLoop() {
    if (sInstance != nullptr) {
        sInstance->drawFrame();
    }
}

VROSceneWeb::VROSceneWeb(std::string canvasSelector, int width, int height) :
    _frame(0),
    _width(width),
    _height(height),
    _angle(0),
    _canvasSelector(canvasSelector) {

    sInstance = this;
    VROThreadRestricted::setThread(VROThreadName::Renderer);

    pinfo("Constructing VROSceneWeb on canvas [%s] (%d x %d)",
          canvasSelector.c_str(), width, height);

    EmscriptenWebGLContextAttributes attribs;
    emscripten_webgl_init_context_attributes(&attribs);
    attribs.majorVersion = 2;
    attribs.minorVersion = 0;
    attribs.explicitSwapControl = 0;
    attribs.depth = 1;
    attribs.stencil = 1;
    attribs.antialias = 1;

    _context = emscripten_webgl_create_context(_canvasSelector.c_str(), &attribs);
    if (_context <= 0) {
        pabort("Failed to create WebGL2 context for canvas [%s]", _canvasSelector.c_str());
        return;
    }
    emscripten_webgl_make_context_current(_context);

    // WebGL2 needs EXT_color_buffer_float enabled before rendering to float
    // (RGBA16F/RG16F) targets, which HDR/bloom/PBR-IBL use. Enable it and record
    // support so the driver can degrade gracefully when it's unavailable.
    bool colorBufferFloat = emscripten_webgl_enable_extension(_context, "EXT_color_buffer_float");
    pinfo("EXT_color_buffer_float supported: %d", colorBufferFloat);

    _driver = std::make_shared<VRODriverOpenGLWasm>();
    _driver->setColorBufferFloatSupported(colorBufferFloat);
    _inputController = std::make_shared<VROInputControllerWasm>(_driver);

    // Request the full effect set. VROChoreographer auto-degrades each effect
    // based on driver capability (getColorRenderingMode / isBloomSupported),
    // which we've wired to EXT_color_buffer_float above — so on a GPU/browser
    // without float color buffers these fall back to the non-HDR pipeline.
    VRORendererConfiguration config;
    config.enableShadows = true;
    config.enableBloom = true;
    config.enableHDR = true;
    config.enablePBR = true;

    _renderer = std::make_shared<VRORenderer>(
        config, std::dynamic_pointer_cast<VROInputControllerBase>(_inputController));

    buildEmptyScene();
    emscripten_set_main_loop(VROSceneWebMainLoop, 0, 0);
}

VROSceneWeb::~VROSceneWeb() {
    if (sInstance == this) {
        sInstance = nullptr;
    }
}

std::shared_ptr<VROPortal> VROSceneWeb::getRootNode() {
    if (!_scene) {
        return nullptr;
    }
    return _scene->getRootNode();
}

void VROSceneWeb::setActiveCameraNode(std::shared_ptr<VRONode> node) {
    if (_renderer && node) {
        _renderer->setPointOfView(node);
    }
}

std::shared_ptr<VRODriverOpenGLWasm> VROSceneWeb::getDriver() {
    return _driver;
}

void VROSceneWeb::buildEmptyScene() {
    _sceneController = std::make_shared<VROSceneController>();
    _scene = _sceneController->getScene();
    std::shared_ptr<VROPortal> rootNode = _scene->getRootNode();
    rootNode->setPosition({0, 0, 0});

    // No default lights: the bridge provides lights explicitly (parity with the
    // native SDK, where scenes need lights unless using Constant lighting). A
    // default camera is kept so scenes render before defining a ViroCamera.
    std::shared_ptr<VRONodeCamera> camera = std::make_shared<VRONodeCamera>();
    std::shared_ptr<VRONode> cameraNode = std::make_shared<VRONode>();
    cameraNode->setCamera(camera);
    rootNode->addChildNode(cameraNode);

    _renderer->setSceneController(_sceneController, _driver);
    _renderer->setPointOfView(cameraNode);
}

void VROSceneWeb::buildCubeScene() {
    // Reuse the empty scene's root/camera/lights, then add demo geometry.
    std::shared_ptr<VROPortal> rootNode = getRootNode();
    if (!rootNode) {
        return;
    }

    std::shared_ptr<VROBox> box = VROBox::createBox(2, 2, 2);
    box->setName("Cube");

    std::shared_ptr<VROMaterial> material = box->getMaterials()[0];
    material->setLightingModel(VROLightingModel::Blinn);
    material->getDiffuse().setColor({0.2, 0.6, 1.0, 1.0});
    // Emit bloom on the brightly-lit faces so the bloom pass is visibly validated.
    material->setBloomThreshold(0.6);

    _boxNode = std::make_shared<VRONode>();
    _boxNode->setGeometry(box);
    _boxNode->setPosition({0, 0, -5});
    rootNode->addChildNode(_boxNode);

    // A floor to receive the cube's shadow (validates the shadow pass visually).
    std::shared_ptr<VROSurface> floor = VROSurface::createSurface(20, 20);
    std::shared_ptr<VROMaterial> floorMaterial = floor->getMaterials()[0];
    floorMaterial->setLightingModel(VROLightingModel::Lambert);
    floorMaterial->getDiffuse().setColor({0.5, 0.5, 0.5, 1.0});

    std::shared_ptr<VRONode> floorNode = std::make_shared<VRONode>();
    floorNode->setGeometry(floor);
    floorNode->setPosition({0, -2, -5});
    floorNode->setRotationEuler({-(float) M_PI_2, 0, 0});
    rootNode->addChildNode(floorNode);

    // Make the cube tappable: toggle its color on click (demo feedback).
    _cubeDelegate = std::make_shared<CubeClickDelegate>(material);
    _cubeDelegate->setEnabledEvent(VROEventDelegate::EventAction::OnClick, true);
    _boxNode->setEventDelegate(_cubeDelegate);
}

void VROSceneWeb::drawFrame() {
    emscripten_webgl_make_context_current(_context);

    VROViewport viewport(0, 0, _width, _height);
    if (viewport.getWidth() == 0 || viewport.getHeight() == 0) {
        return;
    }

    // Demo-only: spin the cube if buildCubeScene created one. Bridge-built
    // scenes animate via the C API / their own logic instead.
    if (_boxNode) {
        _angle += 0.01f;
        _boxNode->setRotationEuler({0, _angle, 0});
    }

    VROFieldOfView fov = _renderer->computeUserFieldOfView(viewport.getWidth(), viewport.getHeight());
    VROMatrix4f projection = fov.toPerspectiveProjection(kZNear, _renderer->getFarClippingPlane());

    // Give the input controller the same view/projection/viewport used to render
    // this frame, so screen touches unproject into matching world rays.
    _inputController->setRenderState(_renderer->getLookAtMatrix(), projection, _width, _height);

    _renderer->setClearColor({0.1, 0.1, 0.12, 1.0}, _driver);

    _renderer->prepareFrame(_frame, viewport, fov, VROMatrix4f::identity(), projection, _driver);
    glViewport(viewport.getX(), viewport.getY(), viewport.getWidth(), viewport.getHeight());
    _renderer->renderEye(VROEyeType::Monocular, _renderer->getLookAtMatrix(), projection, viewport, _driver);
    _renderer->renderHUD(VROEyeType::Monocular, VROMatrix4f::identity(), projection, _driver);
    _renderer->endFrame(_driver);

    _frame++;
}

void VROSceneWeb::setSize(int width, int height) {
    _width = width;
    _height = height;
}

void VROSceneWeb::onTouch(int action, float x, float y) {
    if (_inputController) {
        _inputController->onScreenTouch(action, x, y);
    }
}

#pragma mark - JS bindings

// A single global scene instance owned by the module. JS calls initViroScene()
// once the canvas exists, then the emscripten main loop drives drawFrame().
static std::shared_ptr<VROSceneWeb> sScene;

static void initViroScene(std::string canvasSelector, int width, int height) {
    sScene = std::make_shared<VROSceneWeb>(canvasSelector, width, height);
}

static void setViroSceneSize(int width, int height) {
    if (sScene) {
        sScene->setSize(width, height);
    }
}

static void viroOnTouch(int action, float x, float y) {
    if (sScene) {
        sScene->onTouch(action, x, y);
    }
}

static void viroBuildDemoCube() {
    if (sScene) {
        sScene->buildCubeScene();
    }
}

#pragma mark - Web C API (handle-based scene graph)

// The bridge (TS reconciler) builds the scene by calling these functions with
// opaque integer handles instead of pointers. Handles index into per-type
// tables of shared_ptrs held here; JS never sees a raw pointer or shared_ptr.
// Single-threaded (wasm main loop), so no locking is needed — the C API is
// invoked from JS callbacks between frames on the same thread as drawFrame.

static std::unordered_map<int, std::shared_ptr<VRONode>> sNodes;
static std::unordered_map<int, std::shared_ptr<VROGeometry>> sGeometries;
static std::unordered_map<int, std::shared_ptr<VROMaterial>> sMaterials;
static int sNextHandle = 1;
static int sRootHandle = 0;

// Per-node running animation, retained so it can be paused/resumed/stopped and
// re-executed for looping. (Declared here so viroDestroyNode can clear it.)
struct WebAnimState {
    std::shared_ptr<VROExecutableAnimation> anim;
    std::shared_ptr<VRONode> node;
    bool loop;
};
static std::unordered_map<int, std::shared_ptr<WebAnimState>> sNodeAnimations;

#pragma mark - Event marshaling (WASM -> JS)

// Single JS callback the bridge registers to receive node events. Signature:
//   cb(nodeHandle, eventAction, source, intArg, x, y, z)
// eventAction matches VROEventDelegate::EventAction (1=Hover, 2=Click). intArg
// carries ClickState for clicks (1=down,2=up,3=clicked) or isHovering (0/1).
static emscripten::val sEventCallback = emscripten::val::undefined();

// Per-node event delegate that forwards to sEventCallback tagged with the node's
// handle, so JS can route the event back to the right React component.
class VROWebEventDelegate : public VROEventDelegate {
public:
    VROWebEventDelegate(int handle) : _handle(handle) {}
    virtual ~VROWebEventDelegate() {}

    virtual void onClick(int source, std::shared_ptr<VRONode> node,
                         ClickState clickState, std::vector<float> position) {
        emit(EventAction::OnClick, source, (int) clickState, position);
    }
    virtual void onHover(int source, std::shared_ptr<VRONode> node,
                         bool isHovering, std::vector<float> position) {
        emit(EventAction::OnHover, source, isHovering ? 1 : 0, position);
    }

private:
    void emit(int action, int source, int intArg, const std::vector<float> &pos) {
        if (sEventCallback.isUndefined() || sEventCallback.isNull()) {
            return;
        }
        float x = pos.size() > 0 ? pos[0] : 0.0f;
        float y = pos.size() > 1 ? pos[1] : 0.0f;
        float z = pos.size() > 2 ? pos[2] : 0.0f;
        sEventCallback(_handle, action, source, intArg, x, y, z);
    }
    int _handle;
};

static std::unordered_map<int, std::shared_ptr<VROWebEventDelegate>> sNodeDelegates;

static std::shared_ptr<VRONode> getNode(int h) {
    auto it = sNodes.find(h);
    return it == sNodes.end() ? nullptr : it->second;
}
static std::shared_ptr<VROGeometry> getGeometry(int h) {
    auto it = sGeometries.find(h);
    return it == sGeometries.end() ? nullptr : it->second;
}
static std::shared_ptr<VROMaterial> getMaterial(int h) {
    auto it = sMaterials.find(h);
    return it == sMaterials.end() ? nullptr : it->second;
}

// --- Nodes ---

static int viroCreateNode() {
    int h = sNextHandle++;
    sNodes[h] = std::make_shared<VRONode>();
    return h;
}

// Registers the active scene's root node and returns its (cached) handle.
static int viroGetRootNode() {
    if (sRootHandle != 0) {
        return sRootHandle;
    }
    if (!sScene) {
        return 0;
    }
    std::shared_ptr<VROPortal> root = sScene->getRootNode();
    if (!root) {
        return 0;
    }
    sRootHandle = sNextHandle++;
    sNodes[sRootHandle] = root;
    return sRootHandle;
}

static void viroSetNodePosition(int node, float x, float y, float z) {
    if (auto n = getNode(node)) n->setPosition({x, y, z});
}
static void viroSetNodeRotation(int node, float x, float y, float z) {
    if (auto n = getNode(node)) n->setRotationEuler({x, y, z});
}
static void viroSetNodeScale(int node, float x, float y, float z) {
    if (auto n = getNode(node)) n->setScale({x, y, z});
}
static void viroSetNodeOpacity(int node, float opacity) {
    if (auto n = getNode(node)) n->setOpacity(opacity);
}
static void viroSetNodeVisible(int node, bool visible) {
    if (auto n = getNode(node)) n->setHidden(!visible);
}
static void viroSetNodeGeometry(int node, int geometry) {
    auto n = getNode(node);
    auto g = getGeometry(geometry);
    if (n && g) n->setGeometry(g);
}
static void viroAddChildNode(int parent, int child) {
    auto p = getNode(parent);
    auto c = getNode(child);
    if (p && c) p->addChildNode(c);
}
static void viroRemoveNodeFromParent(int node) {
    if (auto n = getNode(node)) n->removeFromParentNode();
}
static void viroDestroyNode(int node) {
    sNodes.erase(node);
    sNodeDelegates.erase(node);
    sNodeAnimations.erase(node);
}

// --- Events ---

static void viroSetEventCallback(emscripten::val callback) {
    sEventCallback = callback;
}

// eventAction: VROEventDelegate::EventAction (1=Hover, 2=Click, ...).
static void viroSetNodeEventEnabled(int node, int eventAction, bool enabled) {
    auto n = getNode(node);
    if (!n) return;

    auto it = sNodeDelegates.find(node);
    std::shared_ptr<VROWebEventDelegate> delegate;
    if (it == sNodeDelegates.end()) {
        delegate = std::make_shared<VROWebEventDelegate>(node);
        sNodeDelegates[node] = delegate;
        n->setEventDelegate(delegate);
    } else {
        delegate = it->second;
    }
    delegate->setEnabledEvent(static_cast<VROEventDelegate::EventAction>(eventAction), enabled);
}

// --- Geometries ---

static int viroCreateBox(float width, float height, float length) {
    int h = sNextHandle++;
    sGeometries[h] = VROBox::createBox(width, height, length);
    return h;
}
static int viroCreateSphere(float radius) {
    int h = sNextHandle++;
    sGeometries[h] = VROSphere::createSphere(radius, 20, 20, true);
    return h;
}
static int viroCreateSurface(float width, float height) {
    int h = sNextHandle++;
    sGeometries[h] = VROSurface::createSurface(width, height);
    return h;
}
static void viroSetGeometryMaterial(int geometry, int material) {
    auto g = getGeometry(geometry);
    auto m = getMaterial(material);
    if (g && m) {
        std::vector<std::shared_ptr<VROMaterial>> materials = { m };
        g->setMaterials(materials);
    }
}
static void viroDestroyGeometry(int geometry) {
    sGeometries.erase(geometry);
}

// --- Materials ---

static int viroCreateMaterial() {
    int h = sNextHandle++;
    sMaterials[h] = std::make_shared<VROMaterial>();
    return h;
}
static void viroSetMaterialDiffuseColor(int material, float r, float g, float b, float a) {
    if (auto m = getMaterial(material)) m->getDiffuse().setColor({r, g, b, a});
}
// model: 0=Constant, 1=Lambert, 2=Blinn, 3=Phong, 4=PhysicallyBased
static void viroSetMaterialLightingModel(int material, int model) {
    auto m = getMaterial(material);
    if (!m) return;
    switch (model) {
        case 0: m->setLightingModel(VROLightingModel::Constant); break;
        case 1: m->setLightingModel(VROLightingModel::Lambert); break;
        case 2: m->setLightingModel(VROLightingModel::Blinn); break;
        case 3: m->setLightingModel(VROLightingModel::Phong); break;
        case 4: m->setLightingModel(VROLightingModel::PhysicallyBased); break;
        default: break;
    }
}
static void viroDestroyMaterial(int material) {
    sMaterials.erase(material);
}

// Scalar material properties.
static void viroSetMaterialShininess(int material, float shininess) {
    if (auto m = getMaterial(material)) m->setShininess(shininess);
}
static void viroSetMaterialFresnelExponent(int material, float fresnel) {
    if (auto m = getMaterial(material)) m->setFresnelExponent(fresnel);
}
static void viroSetMaterialRoughness(int material, float roughness) {
    if (auto m = getMaterial(material)) m->getRoughness().setColor({roughness, roughness, roughness, 1.0});
}
static void viroSetMaterialMetalness(int material, float metalness) {
    if (auto m = getMaterial(material)) m->getMetalness().setColor({metalness, metalness, metalness, 1.0});
}
static void viroSetMaterialDiffuseIntensity(int material, float intensity) {
    if (auto m = getMaterial(material)) m->getDiffuse().setIntensity(intensity);
}
// mode: 0=Back, 1=Front, 2=None (VROCullMode)
static void viroSetMaterialCullMode(int material, int mode) {
    auto m = getMaterial(material);
    if (!m) return;
    switch (mode) {
        case 1: m->setCullMode(VROCullMode::Front); break;
        case 2: m->setCullMode(VROCullMode::None); break;
        default: m->setCullMode(VROCullMode::Back); break;
    }
}
// mode: 0=None,1=Alpha,2=Add,3=Multiply,4=Subtract,5=Screen (VROBlendMode)
static void viroSetMaterialBlendMode(int material, int mode) {
    auto m = getMaterial(material);
    if (!m) return;
    switch (mode) {
        case 1: m->setBlendMode(VROBlendMode::Alpha); break;
        case 2: m->setBlendMode(VROBlendMode::Add); break;
        case 3: m->setBlendMode(VROBlendMode::Multiply); break;
        case 4: m->setBlendMode(VROBlendMode::Subtract); break;
        case 5: m->setBlendMode(VROBlendMode::Screen); break;
        default: m->setBlendMode(VROBlendMode::None); break;
    }
}
static void viroSetMaterialWritesToDepthBuffer(int material, bool writes) {
    if (auto m = getMaterial(material)) m->setWritesToDepthBuffer(writes);
}
static void viroSetMaterialReadsFromDepthBuffer(int material, bool reads) {
    if (auto m = getMaterial(material)) m->setReadsFromDepthBuffer(reads);
}

// --- Textures ---

static std::unordered_map<int, std::shared_ptr<VROTexture>> sTextures;
static std::shared_ptr<VROTexture> getTexture(int h) {
    auto it = sTextures.find(h);
    return it == sTextures.end() ? nullptr : it->second;
}

// Create a 2D texture from an RGBA8 pixel buffer passed from JS (Uint8Array).
// sRGB should be true for color (diffuse) textures, false for data maps
// (normal/roughness/metalness/AO).
static int viroCreateTextureRGBA(emscripten::val pixels, int width, int height, bool sRGB) {
    std::vector<uint8_t> bytes = emscripten::convertJSArrayToNumberVector<uint8_t>(pixels);
    auto data = std::make_shared<VROData>(bytes.data(), (int) bytes.size());
    std::vector<std::shared_ptr<VROData>> dataVec = { data };

    int h = sNextHandle++;
    sTextures[h] = std::make_shared<VROTexture>(
        VROTextureType::Texture2D,
        VROTextureFormat::RGBA8,
        VROTextureInternalFormat::RGBA8,
        sRGB,
        VROMipmapMode::Runtime,
        dataVec, width, height, std::vector<uint32_t>());
    return h;
}
// mode: 0=Clamp,1=Repeat,2=ClampToBorder,3=Mirror
static VROWrapMode wrapModeValue(int mode) {
    switch (mode) {
        case 1: return VROWrapMode::Repeat;
        case 2: return VROWrapMode::ClampToBorder;
        case 3: return VROWrapMode::Mirror;
        default: return VROWrapMode::Clamp;
    }
}
static void viroSetTextureWrap(int texture, int wrapS, int wrapT) {
    if (auto t = getTexture(texture)) {
        t->setWrapS(wrapModeValue(wrapS));
        t->setWrapT(wrapModeValue(wrapT));
    }
}
// filter: 0=None,1=Nearest,2=Linear
static VROFilterMode filterModeValue(int filter) {
    switch (filter) {
        case 0: return VROFilterMode::None;
        case 1: return VROFilterMode::Nearest;
        default: return VROFilterMode::Linear;
    }
}
static void viroSetTextureFilter(int texture, int minFilter, int magFilter, int mipFilter) {
    if (auto t = getTexture(texture)) {
        t->setMinificationFilter(filterModeValue(minFilter));
        t->setMagnificationFilter(filterModeValue(magFilter));
        t->setMipFilter(filterModeValue(mipFilter));
    }
}
// channel: 0=diffuse,1=specular,2=normal,3=roughness,4=metalness,5=ambientOcclusion
static void viroSetMaterialTexture(int material, int channel, int texture) {
    auto m = getMaterial(material);
    auto t = getTexture(texture);
    if (!m || !t) return;
    switch (channel) {
        case 1: m->getSpecular().setTexture(t); break;
        case 2: m->getNormal().setTexture(t); break;
        case 3: m->getRoughness().setTexture(t); break;
        case 4: m->getMetalness().setTexture(t); break;
        case 5: m->getAmbientOcclusion().setTexture(t); break;
        default: m->getDiffuse().setTexture(t); break;
    }
}
static void viroDestroyTexture(int texture) {
    sTextures.erase(texture);
}

// --- Lights ---

static std::unordered_map<int, std::shared_ptr<VROLight>> sLights;
static std::shared_ptr<VROLight> getLight(int h) {
    auto it = sLights.find(h);
    return it == sLights.end() ? nullptr : it->second;
}

// type: 0=Ambient, 1=Directional, 2=Omni, 3=Spot
static int viroCreateLight(int type) {
    VROLightType lt = VROLightType::Ambient;
    switch (type) {
        case 1: lt = VROLightType::Directional; break;
        case 2: lt = VROLightType::Omni; break;
        case 3: lt = VROLightType::Spot; break;
        default: lt = VROLightType::Ambient; break;
    }
    int h = sNextHandle++;
    sLights[h] = std::make_shared<VROLight>(lt);
    return h;
}
static void viroSetLightColor(int light, float r, float g, float b) {
    if (auto l = getLight(light)) l->setColor({r, g, b});
}
static void viroSetLightIntensity(int light, float intensity) {
    if (auto l = getLight(light)) l->setIntensity(intensity);
}
static void viroSetLightTemperature(int light, float temperature) {
    if (auto l = getLight(light)) l->setTemperature(temperature);
}
static void viroSetLightDirection(int light, float x, float y, float z) {
    if (auto l = getLight(light)) l->setDirection({x, y, z});
}
static void viroSetLightPosition(int light, float x, float y, float z) {
    if (auto l = getLight(light)) l->setPosition({x, y, z});
}
static void viroSetLightAttenuation(int light, float start, float end) {
    if (auto l = getLight(light)) {
        l->setAttenuationStartDistance(start);
        l->setAttenuationEndDistance(end);
    }
}
static void viroSetLightSpotAngles(int light, float inner, float outer) {
    if (auto l = getLight(light)) {
        l->setSpotInnerAngle(inner);
        l->setSpotOuterAngle(outer);
    }
}
static void viroSetLightCastsShadow(int light, bool castsShadow) {
    if (auto l = getLight(light)) l->setCastsShadow(castsShadow);
}
static void viroAddLightToNode(int node, int light) {
    auto n = getNode(node);
    auto l = getLight(light);
    if (n && l) n->addLight(l);
}
static void viroRemoveLightFromNode(int node, int light) {
    auto n = getNode(node);
    auto l = getLight(light);
    if (n && l) n->removeLight(l);
}
static void viroDestroyLight(int light) {
    sLights.erase(light);
}

// --- Camera ---

static void viroSetNodeCamera(int node) {
    if (auto n = getNode(node)) n->setCamera(std::make_shared<VRONodeCamera>());
}
static void viroSetActiveCameraNode(int node) {
    if (sScene) sScene->setActiveCameraNode(getNode(node));
}

// --- Model loading (GLB / glTF / VRX) ---

// cb(nodeHandle, success). Registered by the bridge to know when a load finishes.
static emscripten::val sModelLoadCallback = emscripten::val::undefined();
static void viroSetModelLoadCallback(emscripten::val callback) {
    sModelLoadCallback = callback;
}

// Loads a model at `path` (already written to the emscripten virtual FS by JS)
// into the node. format: 0=GLB, 1=glTF, 2=VRX. Self-contained assets (GLB/VRX)
// need only the single file; the VRX loader handles gzip.
static void viroLoadModel(int nodeHandle, std::string path, int format) {
    auto node = getNode(nodeHandle);
    if (!node || !sScene) {
        return;
    }
    std::shared_ptr<VRODriver> driver = sScene->getDriver();

    auto onFinish = [nodeHandle](std::shared_ptr<VRONode> node, bool success) {
        if (!sModelLoadCallback.isUndefined() && !sModelLoadCallback.isNull()) {
            sModelLoadCallback(nodeHandle, success);
        }
    };

    if (format == 2) {
        VROFBXLoader::loadFBXFromResource(path, VROResourceType::LocalFile, node, driver, onFinish);
    } else {
        bool isBinary = (format == 0); // 0=GLB binary, 1=glTF text
        VROGLTFLoader::loadGLTFFromResource(path, {}, VROResourceType::LocalFile, node,
                                            isBinary, driver, onFinish);
    }
}

// --- Animations (skeletal/keyframe animations embedded in loaded models) ---

// cb(nodeHandle, eventType): 0 = start, 1 = finish.
static emscripten::val sAnimationCallback = emscripten::val::undefined();
static void viroSetAnimationCallback(emscripten::val cb) {
    sAnimationCallback = cb;
}
static void emitAnim(int nodeHandle, int eventType) {
    if (!sAnimationCallback.isUndefined() && !sAnimationCallback.isNull()) {
        sAnimationCallback(nodeHandle, eventType);
    }
}

static void runAnim(int nodeHandle, std::shared_ptr<WebAnimState> state) {
    std::weak_ptr<WebAnimState> weak = state;
    state->anim->execute(state->node, [nodeHandle, weak]() {
        auto s = weak.lock();
        if (!s) return;
        // Only continue if this is still the node's active animation.
        auto it = sNodeAnimations.find(nodeHandle);
        if (it == sNodeAnimations.end() || it->second != s) return;
        if (s->loop) {
            runAnim(nodeHandle, s);
        } else {
            sNodeAnimations.erase(nodeHandle);
            emitAnim(nodeHandle, 1);
        }
    });
}

// Returns the model's animation names (recursive) as a JS array of strings.
static emscripten::val viroGetAnimationKeys(int nodeHandle) {
    emscripten::val result = emscripten::val::array();
    auto node = getNode(nodeHandle);
    if (!node) return result;
    std::set<std::string> keys = node->getAnimationKeys(true);
    int i = 0;
    for (const std::string &k : keys) {
        result.set(i++, emscripten::val(k));
    }
    return result;
}

static void viroStartAnimation(int nodeHandle, std::string name, bool loop) {
    auto node = getNode(nodeHandle);
    if (!node) return;
    std::shared_ptr<VROExecutableAnimation> anim = node->getAnimation(name, true);
    if (!anim) {
        pinfo("VROSceneWeb: no animation named [%s] on node %d", name.c_str(), nodeHandle);
        return;
    }
    auto state = std::make_shared<WebAnimState>();
    state->anim = anim->copy(); // copy so per-run tweaks don't mutate the original
    state->node = node;
    state->loop = loop;
    sNodeAnimations[nodeHandle] = state;
    emitAnim(nodeHandle, 0);
    runAnim(nodeHandle, state);
}
static void viroPauseAnimation(int nodeHandle) {
    auto it = sNodeAnimations.find(nodeHandle);
    if (it != sNodeAnimations.end()) it->second->anim->pause();
}
static void viroResumeAnimation(int nodeHandle) {
    auto it = sNodeAnimations.find(nodeHandle);
    if (it != sNodeAnimations.end()) it->second->anim->resume();
}
static void viroStopAnimation(int nodeHandle, bool jumpToEnd) {
    auto it = sNodeAnimations.find(nodeHandle);
    if (it != sNodeAnimations.end()) {
        it->second->anim->terminate(jumpToEnd);
        sNodeAnimations.erase(it);
    }
}

EMSCRIPTEN_BINDINGS(viro_web) {
    emscripten::function("initViroScene", &initViroScene);
    emscripten::function("setViroSceneSize", &setViroSceneSize);
    emscripten::function("viroOnTouch", &viroOnTouch);
    emscripten::function("viroBuildDemoCube", &viroBuildDemoCube);

    // Scene graph C API (handle-based)
    emscripten::function("viroCreateNode", &viroCreateNode);
    emscripten::function("viroGetRootNode", &viroGetRootNode);
    emscripten::function("viroSetNodePosition", &viroSetNodePosition);
    emscripten::function("viroSetNodeRotation", &viroSetNodeRotation);
    emscripten::function("viroSetNodeScale", &viroSetNodeScale);
    emscripten::function("viroSetNodeOpacity", &viroSetNodeOpacity);
    emscripten::function("viroSetNodeVisible", &viroSetNodeVisible);
    emscripten::function("viroSetNodeGeometry", &viroSetNodeGeometry);
    emscripten::function("viroAddChildNode", &viroAddChildNode);
    emscripten::function("viroRemoveNodeFromParent", &viroRemoveNodeFromParent);
    emscripten::function("viroDestroyNode", &viroDestroyNode);

    emscripten::function("viroCreateBox", &viroCreateBox);
    emscripten::function("viroCreateSphere", &viroCreateSphere);
    emscripten::function("viroCreateSurface", &viroCreateSurface);
    emscripten::function("viroSetGeometryMaterial", &viroSetGeometryMaterial);
    emscripten::function("viroDestroyGeometry", &viroDestroyGeometry);

    emscripten::function("viroCreateMaterial", &viroCreateMaterial);
    emscripten::function("viroSetMaterialDiffuseColor", &viroSetMaterialDiffuseColor);
    emscripten::function("viroSetMaterialLightingModel", &viroSetMaterialLightingModel);
    emscripten::function("viroDestroyMaterial", &viroDestroyMaterial);
    emscripten::function("viroSetMaterialShininess", &viroSetMaterialShininess);
    emscripten::function("viroSetMaterialFresnelExponent", &viroSetMaterialFresnelExponent);
    emscripten::function("viroSetMaterialRoughness", &viroSetMaterialRoughness);
    emscripten::function("viroSetMaterialMetalness", &viroSetMaterialMetalness);
    emscripten::function("viroSetMaterialDiffuseIntensity", &viroSetMaterialDiffuseIntensity);
    emscripten::function("viroSetMaterialCullMode", &viroSetMaterialCullMode);
    emscripten::function("viroSetMaterialBlendMode", &viroSetMaterialBlendMode);
    emscripten::function("viroSetMaterialWritesToDepthBuffer", &viroSetMaterialWritesToDepthBuffer);
    emscripten::function("viroSetMaterialReadsFromDepthBuffer", &viroSetMaterialReadsFromDepthBuffer);

    emscripten::function("viroCreateTextureRGBA", &viroCreateTextureRGBA);
    emscripten::function("viroSetTextureWrap", &viroSetTextureWrap);
    emscripten::function("viroSetTextureFilter", &viroSetTextureFilter);
    emscripten::function("viroSetMaterialTexture", &viroSetMaterialTexture);
    emscripten::function("viroDestroyTexture", &viroDestroyTexture);

    emscripten::function("viroSetEventCallback", &viroSetEventCallback);
    emscripten::function("viroSetNodeEventEnabled", &viroSetNodeEventEnabled);

    emscripten::function("viroCreateLight", &viroCreateLight);
    emscripten::function("viroSetLightColor", &viroSetLightColor);
    emscripten::function("viroSetLightIntensity", &viroSetLightIntensity);
    emscripten::function("viroSetLightTemperature", &viroSetLightTemperature);
    emscripten::function("viroSetLightDirection", &viroSetLightDirection);
    emscripten::function("viroSetLightPosition", &viroSetLightPosition);
    emscripten::function("viroSetLightAttenuation", &viroSetLightAttenuation);
    emscripten::function("viroSetLightSpotAngles", &viroSetLightSpotAngles);
    emscripten::function("viroSetLightCastsShadow", &viroSetLightCastsShadow);
    emscripten::function("viroAddLightToNode", &viroAddLightToNode);
    emscripten::function("viroRemoveLightFromNode", &viroRemoveLightFromNode);
    emscripten::function("viroDestroyLight", &viroDestroyLight);

    emscripten::function("viroSetNodeCamera", &viroSetNodeCamera);
    emscripten::function("viroSetActiveCameraNode", &viroSetActiveCameraNode);

    emscripten::function("viroSetModelLoadCallback", &viroSetModelLoadCallback);
    emscripten::function("viroLoadModel", &viroLoadModel);

    emscripten::function("viroSetAnimationCallback", &viroSetAnimationCallback);
    emscripten::function("viroGetAnimationKeys", &viroGetAnimationKeys);
    emscripten::function("viroStartAnimation", &viroStartAnimation);
    emscripten::function("viroPauseAnimation", &viroPauseAnimation);
    emscripten::function("viroResumeAnimation", &viroResumeAnimation);
    emscripten::function("viroStopAnimation", &viroStopAnimation);
}

// The module has no work to do at startup — JS calls initViroScene() once the
// canvas exists. An empty main() keeps the default emscripten entry happy so we
// don't need --no-entry.
int main() {
    return 0;
}

