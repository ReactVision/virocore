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
#include "VROTransaction.h"
#include "VROEventDelegate.h"

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

void VROSceneWeb::buildEmptyScene() {
    _sceneController = std::make_shared<VROSceneController>();
    _scene = _sceneController->getScene();
    std::shared_ptr<VROPortal> rootNode = _scene->getRootNode();
    rootNode->setPosition({0, 0, 0});

    // Default lighting so bridge-created geometry is visible before the bridge
    // manages lights itself (Phase 2 lights task).
    std::shared_ptr<VROLight> ambient = std::make_shared<VROLight>(VROLightType::Ambient);
    ambient->setColor({0.6, 0.6, 0.6});
    rootNode->addLight(ambient);

    std::shared_ptr<VROLight> directional = std::make_shared<VROLight>(VROLightType::Directional);
    directional->setColor({1.0, 1.0, 1.0});
    directional->setDirection({0.0, -1.0, -0.6});
    directional->setCastsShadow(true);
    directional->setShadowOrthographicSize(20);
    directional->setShadowNearZ(1);
    directional->setShadowFarZ(30);
    rootNode->addLight(directional);

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

    emscripten::function("viroSetEventCallback", &viroSetEventCallback);
    emscripten::function("viroSetNodeEventEnabled", &viroSetNodeEventEnabled);
}

// The module has no work to do at startup — JS calls initViroScene() once the
// canvas exists. An empty main() keeps the default emscripten entry happy so we
// don't need --no-entry.
int main() {
    return 0;
}

