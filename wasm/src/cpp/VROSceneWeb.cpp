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
#include "VROPortalFrame.h"
#include "VRONode.h"
#include "VRONodeCamera.h"
#include "VROLight.h"
#include "VROBox.h"
#include "VROSphere.h"
#include "VROSurface.h"
#include "VROText.h"
#include "VROTypeface.h"
#include "VROPolyline.h"
#include "VROPolygon.h"
#include "VROGeometry.h"
#include "VROGeometrySource.h"
#include "VROGeometryElement.h"
#include "VROParticleEmitter.h"
#include "VROParticleModifier.h"
#include "VROBillboardConstraint.h"
#include "VROPhysicsWorld.h"
#include "VROPhysicsBody.h"
#include "VROPhysicsShape.h"
#include "VROPhysicsBodyDelegate.h"
#include "VROMaterial.h"
#include "VROMaterialVisual.h"
#include "VROShaderModifier.h"
#include "VROTexture.h"
#include "VROData.h"
#include "VROTransaction.h"
#include "VROEventDelegate.h"
#include "VROGLTFLoader.h"
#include "VROFBXLoader.h"
#include "VROOBJLoader.h"
#include "VROHDRLoader.h"
#include "VROModelIOUtil.h"
#include "VROExecutableAnimation.h"
#include "VROTimingFunction.h"
#include "VROARWeb.h"
#include "VROQuaternion.h"
#include "VROMorpher.h"
#include "VROStringUtil.h"
#include <set>

#include <unordered_map>
#include <vector>
#include <sstream>

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

std::shared_ptr<VRORenderer> VROSceneWeb::getRenderer() {
    return _renderer;
}

std::shared_ptr<VROScene> VROSceneWeb::getScene() {
    return _scene;
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
    _cameraNode = std::make_shared<VRONode>();
    _cameraNode->setCamera(camera);
    rootNode->addChildNode(_cameraNode);

    _renderer->setSceneController(_sceneController, _driver);
    _renderer->setPointOfView(_cameraNode);
}

void VROSceneWeb::initAR() {
    _arSession = std::make_shared<VROARSessionWeb>();
    _arSession->run();
}

std::shared_ptr<VROARSessionWeb> VROSceneWeb::getARSession() {
    return _arSession;
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

    // AR mode: drive the camera from the injected pose (slam-wasm) and draw the
    // camera background. Mirrors the native ARCore render loop.
    if (_arSession) {
        drawFrameAR(viewport);
        _frame++;
        return;
    }

    // Demo-only: spin the cube if buildCubeScene created one. Bridge-built
    // scenes animate via the C API / their own logic instead.
    if (_boxNode) {
        _angle += 0.01f;
        _boxNode->setRotationEuler({0, _angle, 0});
    }

    VROFieldOfView fov = _renderer->computeUserFieldOfView(viewport.getWidth(), viewport.getHeight());
    VROMatrix4f projection = _renderer->computeProjection(viewport.getWidth(), viewport.getHeight(),
                                                         kZNear, _renderer->getFarClippingPlane());

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

void VROSceneWeb::drawFrameAR(VROViewport viewport) {
    _arSession->setViewport(viewport);
    std::unique_ptr<VROARFrame> &frame = _arSession->updateFrame();
    const std::shared_ptr<VROARCamera> camera = frame->getCamera();

    // Draw the live camera feed behind the scene. Like the native ARCore path,
    // this is a screen-space VROSurface whose diffuse is the JS-uploaded camera
    // texture; created lazily and sized to the viewport.
    std::shared_ptr<VROTexture> cameraTexture = _arSession->getCameraBackgroundTexture();
    if (cameraTexture) {
        if (!_cameraBackground) {
            _cameraBackground = VROSurface::createSurface(
                viewport.getX() + viewport.getWidth() / 2.0f,
                viewport.getY() + viewport.getHeight() / 2.0f,
                viewport.getWidth(), viewport.getHeight(), 0, 0, 1, 1);
            _cameraBackground->setScreenSpace(true);
            _cameraBackground->setName("Camera");

            std::shared_ptr<VROMaterial> material = _cameraBackground->getMaterials()[0];
            material->setLightingModel(VROLightingModel::Constant);
            material->setWritesToDepthBuffer(false);
            material->setNeedsToneMapping(false);
        }
        _cameraBackground->getMaterials()[0]->getDiffuse().setTexture(cameraTexture);
        // JS uploads a fresh VROTexture each frame; force its GPU upload now so
        // the background samples real pixels this frame (otherwise the one-shot
        // texture is replaced before it hydrates, sampling empty → blank feed).
        cameraTexture->prewarm(_driver);
        _renderer->setCameraBackgroundTexture(cameraTexture);
        if (!_scene->getRootNode()->getBackground()) {
            _scene->getRootNode()->setBackground(_cameraBackground);
        }
    }


    // Always render so the live camera feed is visible even before tracking
    // converges (mirrors native ARCore, which shows the camera while waiting).
    // The pose is applied only once tracking is Normal; before that the scene
    // renders at the default point of view.
    VROFieldOfView fov;
    VROMatrix4f projection = camera->getProjection(viewport, kZNear,
                                                   _renderer->getFarClippingPlane(), &fov);
    VROMatrix4f rotation = VROMatrix4f::identity();
    if (camera->getTrackingState() == VROARTrackingState::Normal) {
        rotation = camera->getRotation();
        _cameraNode->getCamera()->setPosition(camera->getPosition());
    }

    _inputController->setRenderState(_renderer->getLookAtMatrix(), projection, _width, _height);

    _renderer->prepareFrame(_frame, viewport, fov, rotation, projection, _driver);
    glViewport(viewport.getX(), viewport.getY(), viewport.getWidth(), viewport.getHeight());
    _renderer->renderEye(VROEyeType::Monocular, _renderer->getLookAtMatrix(), projection, viewport, _driver);
    _renderer->renderHUD(VROEyeType::Monocular, VROMatrix4f::identity(), projection, _driver);
    _renderer->endFrame(_driver);
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

// --- Post-processing effects ---
//
// The renderer opens with all four on (VROSceneWeb's constructor asks for the
// full set and VROChoreographer degrades what the driver cannot do). The native
// scene navigators expose the same four as props and Studio switches HDR and
// bloom off, because Hable tone mapping renders pure white at about 0.77 and
// bright materials glow — neither of which the editor previews. Without these
// the web player was the only surface of the three still applying them.
//
// Each returns whether the effect is on after the call: asking for HDR on a
// driver without float colour buffers leaves it off, and the caller should know
// rather than assume.
static bool viroSetHDREnabled(bool enabled) {
    if (!sScene) return false;
    auto renderer = sScene->getRenderer();
    return renderer ? renderer->setHDREnabled(enabled) : false;
}
static bool viroSetBloomEnabled(bool enabled) {
    if (!sScene) return false;
    auto renderer = sScene->getRenderer();
    return renderer ? renderer->setBloomEnabled(enabled) : false;
}
static bool viroSetPBREnabled(bool enabled) {
    if (!sScene) return false;
    auto renderer = sScene->getRenderer();
    return renderer ? renderer->setPBREnabled(enabled) : false;
}
static bool viroSetShadowsEnabled(bool enabled) {
    if (!sScene) return false;
    auto renderer = sScene->getRenderer();
    return renderer ? renderer->setShadowsEnabled(enabled) : false;
}

// What this binary is: the virocore commit it was built from, whether that tree
// was dirty, and when. Compiled in (see CMakeLists) because by the time a report
// arrives the .wasm has been copied twice and carries no provenance otherwise.
#ifndef VIRO_WEB_BUILD_ID
#define VIRO_WEB_BUILD_ID "unknown"
#endif
static std::string viroGetBuildId() {
    return VIRO_WEB_BUILD_ID;
}

// The tone curve, separately from HDR. Studio switches this off and leaves HDR
// on, because isPBREnabled() is `_hdrEnabled && _pbrEnabled`: turning HDR off to
// lose the curve takes the whole PBR branch of VROShaderFactory with it, and a
// glTF material then falls back to Blinn — roughness, metalness and the AO map
// read by nothing, and the default specular washing the model out to white.
// Native has said this with ViroScene's `toneMappingEnabled` since the editor
// shipped; this is the same switch.
static void viroSetToneMappingEnabled(bool enabled) {
    if (!sScene) return;
    std::shared_ptr<VROScene> scene = sScene->getScene();
    if (!scene) return;
    scene->setToneMappingEnabled(enabled);
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
// Pre-override materials per node, dropped with the node. Declared up here
// because viroDestroyNode erases it; see "Shader overrides" for what it holds.
static std::unordered_map<int,
    std::unordered_map<VROGeometry *, std::vector<std::shared_ptr<VROMaterial>>>>
    sShaderOverrideBaselines;

// Defined with the physics section below; viroDestroyNode needs it before that.
static void viroClearPhysicsBody(int nodeHandle);
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
// --- Node rendering order, light masks, billboard and world position ---

// B3. Drawn-last wins among equal-depth fragments; the native nodes take the
// same int.
static void viroSetNodeRenderingOrder(int nodeHandle, int order) {
    if (auto n = getNode(nodeHandle)) n->setRenderingOrder(order);
}

// B4. The node half of light masking: a light lights a node only where their
// masks intersect. `recursive` matches VRONode's own parameter, which a loaded
// model needs — its geometry is on children, not on the handle the bridge owns.
static void viroSetNodeLightReceivingBitMask(int nodeHandle, int mask, bool recursive) {
    if (auto n = getNode(nodeHandle)) n->setLightReceivingBitMask(mask, recursive);
}
static void viroSetNodeShadowCastingBitMask(int nodeHandle, int mask, bool recursive) {
    if (auto n = getNode(nodeHandle)) n->setShadowCastingBitMask(mask, recursive);
}

// B2. Billboarding, as a constraint on the node — the same VROBillboardConstraint
// the native `transformBehaviors` prop installs. One per node: the map holds the
// live constraint so a change can remove the previous one rather than stacking a
// second that fights it.
static std::unordered_map<int, std::shared_ptr<VROBillboardConstraint>> sBillboards;

// axis: 0 = X, 1 = Y, 2 = Z, 3 = all; anything else removes the constraint.
static void viroSetNodeBillboard(int nodeHandle, int axis) {
    auto node = getNode(nodeHandle);
    if (!node) return;

    auto existing = sBillboards.find(nodeHandle);
    if (existing != sBillboards.end()) {
        node->removeConstraint(existing->second);
        sBillboards.erase(existing);
    }
    if (axis < 0 || axis > 3) {
        return;
    }

    VROBillboardAxis freeAxis;
    switch (axis) {
        case 0:  freeAxis = VROBillboardAxis::X; break;
        case 1:  freeAxis = VROBillboardAxis::Y; break;
        case 2:  freeAxis = VROBillboardAxis::Z; break;
        default: freeAxis = VROBillboardAxis::All; break;
    }
    auto constraint = std::make_shared<VROBillboardConstraint>(freeAxis);
    node->addConstraint(constraint);
    sBillboards[nodeHandle] = constraint;
}

// B7. A node's world position as [x, y, z].
//
// Anything measuring real distances needs this: inside a plane wrapper an
// asset's authored position is local to the plane, so the proximity runtime was
// comparing a camera world position against a plane-local one.
static emscripten::val viroGetNodeWorldPosition(int nodeHandle) {
    emscripten::val out = emscripten::val::array();
    auto node = getNode(nodeHandle);
    if (!node) return out;
    VROVector3f p = node->getWorldPosition();
    out.set(0, p.x);
    out.set(1, p.y);
    out.set(2, p.z);
    return out;
}

// Morph targets. A glTF or FBX model carries its blend shapes in the geometry
// virocore already loaded; nothing on web could name one or move it, so a face
// rig that animates on a phone sat at its rest pose in a browser.
//
// Both act on the node's whole subtree, as the native bridges do: a loaded model
// keeps its meshes on child nodes, so a target named on the root would otherwise
// reach nothing.
static void viroSetMorphTargetWeight(int nodeHandle, std::string target, float weight) {
    auto node = getNode(nodeHandle);
    if (!node) return;
    for (const std::shared_ptr<VROMorpher> &morpher : node->getMorphers(true)) {
        morpher->setWeightForTarget(target, weight);
    }
}

// The names this model's meshes morph by, sorted and deduplicated across them.
static emscripten::val viroGetMorphTargetKeys(int nodeHandle) {
    emscripten::val out = emscripten::val::array();
    auto node = getNode(nodeHandle);
    if (!node) return out;
    std::set<std::string> keys;
    for (const std::shared_ptr<VROMorpher> &morpher : node->getMorphers(true)) {
        std::set<std::string> morphKeys = morpher->getMorphTargetKeys();
        keys.insert(morphKeys.begin(), morphKeys.end());
    }
    int i = 0;
    for (const std::string &key : keys) {
        out.set(i++, key);
    }
    return out;
}

// "cpu", "gpu" or "hybrid", matching the strings the native bridges take.
// Returns whether every morpher accepted it: the GPU path needs vertex
// attributes a model may not have left, and virocore refuses rather than
// degrade silently.
static bool viroSetMorphMode(int nodeHandle, std::string mode) {
    auto node = getNode(nodeHandle);
    if (!node) return false;
    VROMorpher::ComputeLocation location = VROMorpher::ComputeLocation::CPU;
    if (VROStringUtil::strcmpinsensitive("gpu", mode)) {
        location = VROMorpher::ComputeLocation::GPU;
    } else if (VROStringUtil::strcmpinsensitive("hybrid", mode)) {
        location = VROMorpher::ComputeLocation::Hybrid;
    }
    bool ok = true;
    for (const std::shared_ptr<VROMorpher> &morpher : node->getMorphers(true)) {
        ok = morpher->setComputeLocation(location) && ok;
    }
    return ok;
}

static void viroDestroyNode(int node) {
    sNodes.erase(node);
    sNodeDelegates.erase(node);
    sNodeAnimations.erase(node);
    sShaderOverrideBaselines.erase(node);
    sBillboards.erase(node);
    viroClearPhysicsBody(node);
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
// B6. A surface with explicit UVs, so a caller can crop the texture into the
// quad instead of stretching it — what imageClipMode ClipToBounds does on a
// device. Separate from viroCreateSurface rather than extra parameters on it:
// a binary that predates this would silently read the extra arguments as
// garbage, where a missing function is something the caller can detect.
static int viroCreateSurfaceUV(float width, float height,
                               float u0, float v0, float u1, float v1) {
    int h = sNextHandle++;
    sGeometries[h] = VROSurface::createSurface(width, height, u0, v0, u1, v1);
    return h;
}

static int viroCreateSurface(float width, float height) {
    int h = sNextHandle++;
    sGeometries[h] = VROSurface::createSurface(width, height);
    return h;
}

// Minimal UTF-8 → wstring decoder (emscripten wchar_t is 32-bit / UTF-32).
// Handles the full BMP + astral planes; malformed bytes are skipped.
static std::wstring utf8ToWString(const std::string &s) {
    std::wstring out;
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char) s[i];
        uint32_t cp;
        int extra;
        if (c < 0x80) { cp = c; extra = 0; }
        else if ((c >> 5) == 0x6) { cp = c & 0x1F; extra = 1; }
        else if ((c >> 4) == 0xE) { cp = c & 0x0F; extra = 2; }
        else if ((c >> 3) == 0x1E) { cp = c & 0x07; extra = 3; }
        else { i++; continue; }
        if (i + extra >= n) break;
        for (int k = 0; k < extra; k++) {
            cp = (cp << 6) | ((unsigned char) s[i + 1 + k] & 0x3F);
        }
        out.push_back((wchar_t) cp);
        i += extra + 1;
    }
    return out;
}

// Create a text geometry. Alignment/linebreak/clip are int-coded to match
// VROText's enums. color is RGBA in [0,1]. Uses the preloaded system font.
// hAlign: 0 Left, 1 Right, 2 Center | vAlign: 0 Top, 1 Bottom, 2 Center
// lineBreak: 0 WordWrap, 1 CharWrap, 2 Justify, 3 None | clip: 0 ClipToBounds, 1 None
static int viroCreateText(std::string text, float width, float height, int fontSize,
                          int hAlign, int vAlign, int lineBreak, int clipMode, int maxLines,
                          float r, float g, float b, float a) {
    if (!sScene) return 0;

    VROTextHorizontalAlignment h;
    switch (hAlign) {
        case 1: h = VROTextHorizontalAlignment::Right; break;
        case 2: h = VROTextHorizontalAlignment::Center; break;
        default: h = VROTextHorizontalAlignment::Left; break;
    }
    VROTextVerticalAlignment v;
    switch (vAlign) {
        case 1: v = VROTextVerticalAlignment::Bottom; break;
        case 2: v = VROTextVerticalAlignment::Center; break;
        default: v = VROTextVerticalAlignment::Top; break;
    }
    VROLineBreakMode lb;
    switch (lineBreak) {
        case 1: lb = VROLineBreakMode::CharWrap; break;
        case 2: lb = VROLineBreakMode::Justify; break;
        case 3: lb = VROLineBreakMode::None; break;
        default: lb = VROLineBreakMode::WordWrap; break;
    }
    VROTextClipMode clip = (clipMode == 1) ? VROTextClipMode::None : VROTextClipMode::ClipToBounds;

    std::shared_ptr<VROText> textGeom = VROText::createText(
        utf8ToWString(text), "Helvetica", fontSize,
        VROFontStyle::Normal, VROFontWeight::Regular,
        {r, g, b, a}, 0 /*extrusion*/, width, height,
        h, v, lb, clip, maxLines, sScene->getDriver());

    int handle = sNextHandle++;
    sGeometries[handle] = textGeom;
    return handle;
}
// Create a polyline from a flat [x,y,z, x,y,z, …] point list, with a thickness.
static int viroCreatePolyline(emscripten::val points, float thickness) {
    std::vector<float> f = emscripten::convertJSArrayToNumberVector<float>(points);
    std::vector<VROVector3f> path;
    for (size_t i = 0; i + 2 < f.size(); i += 3) {
        path.push_back({ f[i], f[i + 1], f[i + 2] });
    }
    int h = sNextHandle++;
    sGeometries[h] = VROPolyline::createPolyline(path, thickness);
    return h;
}

// Create a filled polygon from a flat [x,y,z, …] perimeter point list.
static int viroCreatePolygon(emscripten::val points) {
    std::vector<float> f = emscripten::convertJSArrayToNumberVector<float>(points);
    std::vector<VROVector3f> path;
    for (size_t i = 0; i + 2 < f.size(); i += 3) {
        path.push_back({ f[i], f[i + 1], f[i + 2] });
    }
    int h = sNextHandle++;
    sGeometries[h] = VROPolygon::createPolygon(path);
    return h;
}

// Create a custom mesh. vertices/normals are flat [x,y,z,…]; texcoords are flat
// [u,v,…]; indices are a flat triangle-index list. normals/texcoords may be empty.
static int viroCreateGeometry(emscripten::val vertices, emscripten::val normals,
                              emscripten::val texcoords, emscripten::val indices) {
    std::vector<float> v = emscripten::convertJSArrayToNumberVector<float>(vertices);
    std::vector<float> n = emscripten::convertJSArrayToNumberVector<float>(normals);
    std::vector<float> t = emscripten::convertJSArrayToNumberVector<float>(texcoords);
    std::vector<uint32_t> idx = emscripten::convertJSArrayToNumberVector<uint32_t>(indices);
    if (v.empty() || idx.empty()) return 0;

    int vertexCount = (int) v.size() / 3;
    std::vector<std::shared_ptr<VROGeometrySource>> sources;
    std::vector<std::shared_ptr<VROGeometryElement>> elements;

    auto vData = std::make_shared<VROData>((void *) v.data(), (int)(v.size() * sizeof(float)));
    sources.push_back(std::make_shared<VROGeometrySource>(
        vData, VROGeometrySourceSemantic::Vertex, vertexCount, true, 3, 4, 0, 12));
    if (!n.empty()) {
        auto nData = std::make_shared<VROData>((void *) n.data(), (int)(n.size() * sizeof(float)));
        sources.push_back(std::make_shared<VROGeometrySource>(
            nData, VROGeometrySourceSemantic::Normal, (int) n.size() / 3, true, 3, 4, 0, 12));
    }
    if (!t.empty()) {
        auto tData = std::make_shared<VROData>((void *) t.data(), (int)(t.size() * sizeof(float)));
        sources.push_back(std::make_shared<VROGeometrySource>(
            tData, VROGeometrySourceSemantic::Texcoord, (int) t.size() / 2, true, 2, 4, 0, 8));
    }

    auto iData = std::make_shared<VROData>((void *) idx.data(), (int)(idx.size() * sizeof(uint32_t)));
    elements.push_back(std::make_shared<VROGeometryElement>(
        iData, VROGeometryPrimitiveType::Triangle, (int) idx.size() / 3, 4, false));

    int handle = sNextHandle++;
    sGeometries[handle] = std::make_shared<VROGeometry>(sources, elements);
    return handle;
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

// entryPoint: "geometry"|"vertex"|"surface"|"fragment"|"lightingModel"|"image".
// Mirrors parseShaderEntryPoint in capi/Material_JNI.cpp (kept local rather than
// reused across that file's JNI macros, since this file has no JNI dependency
// otherwise); unknown names fall back to Fragment, same as the JNI side.
static VROShaderEntryPoint webParseShaderEntryPoint(const std::string &name) {
    if (name == "geometry") return VROShaderEntryPoint::Geometry;
    if (name == "vertex") return VROShaderEntryPoint::Vertex;
    if (name == "surface") return VROShaderEntryPoint::Surface;
    if (name == "fragment") return VROShaderEntryPoint::Fragment;
    if (name == "lightingModel") return VROShaderEntryPoint::LightingModel;
    if (name == "image") return VROShaderEntryPoint::Image;
    pwarn("viroAddMaterialShaderModifier: unknown entry point [%s], defaulting to Fragment", name.c_str());
    return VROShaderEntryPoint::Fragment;
}

// shaderCode is the modifier body, with any `uniforms` block already prepended
// by the JS bridge (same convention as MaterialManager.java's
// parseShaderModifiers: uniforms + "\n" + body) — this API takes one blob of
// GLSL text and splits it into lines the way VROShaderModifier expects.
// varyings is an optional JS array of strings; pass undefined/null for none.
static void viroAddMaterialShaderModifier(int material, std::string entryPoint, std::string shaderCode,
                                           emscripten::val varyings,
                                           bool requiresSceneDepth, bool requiresCameraTexture) {
    auto m = getMaterial(material);
    if (!m) return;

    VROShaderEntryPoint entry = webParseShaderEntryPoint(entryPoint);

    std::vector<std::string> lines;
    std::stringstream ss(shaderCode);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
    }

    std::vector<std::string> varyingsVec;
    if (!varyings.isNull() && !varyings.isUndefined()) {
        int len = varyings["length"].as<int>();
        for (int i = 0; i < len; i++) {
            varyingsVec.push_back(varyings[i].as<std::string>());
        }
    }

    // Match the JNI side: shader modifiers are added synchronously, so disable
    // thread-restriction the same way the immutable-material constructor does.
    m->setThreadRestrictionEnabled(false);
    auto modifier = std::make_shared<VROShaderModifier>(entry, lines);
    if (!varyingsVec.empty()) {
        modifier->setVaryings(varyingsVec);
    }
    modifier->setRequiresSceneDepth(requiresSceneDepth);
    modifier->setRequiresCameraTexture(requiresCameraTexture);
    m->addShaderModifier(modifier);
    m->setThreadRestrictionEnabled(true);
}
static void viroRemoveAllMaterialShaderModifiers(int material) {
    if (auto m = getMaterial(material)) m->removeAllShaderModifiers();
}

// Dynamic shader-uniform updates (ViroMaterials.updateShaderUniform). Mirrors
// MaterialManager.java's updateShaderUniform: it does the type-string
// dispatch itself (float/vec3/vec4/mat4/sampler2D — there is no vec2 here,
// matching the native bridge, which has no VROMaterial vec2 overload either)
// and calls straight into VROMaterial::setShaderUniform. That just stores the
// value in a map on the material; VROMaterialShaderBinding::bindMaterialUniforms
// re-pushes every entry to the GL uniform of the same name every frame, so
// there's no per-frame work to do here and no thread hop needed (this file is
// synchronous, unlike the JNI/ObjC bridges these mirror).
static void viroSetMaterialShaderUniformFloat(int material, std::string name, float value) {
    if (auto m = getMaterial(material)) m->setShaderUniform(name, value);
}
static void viroSetMaterialShaderUniformVec2(int material, std::string name, float x, float y) {
    if (auto m = getMaterial(material)) m->setShaderUniform(name, VROVector2f(x, y));
}
static void viroSetMaterialShaderUniformVec3(int material, std::string name, float x, float y, float z) {
    if (auto m = getMaterial(material)) m->setShaderUniform(name, VROVector3f(x, y, z));
}
static void viroSetMaterialShaderUniformVec4(int material, std::string name, float x, float y, float z, float w) {
    if (auto m = getMaterial(material)) m->setShaderUniform(name, VROVector4f(x, y, z, w));
}
// matrix: JS array/typed array of 16 floats, matching VROMatrix4f's flat-array constructor.
static void viroSetMaterialShaderUniformMat4(int material, std::string name, emscripten::val matrix) {
    auto m = getMaterial(material);
    if (!m) return;
    std::vector<float> elements = emscripten::convertJSArrayToNumberVector<float>(matrix);
    if (elements.size() != 16) {
        pwarn("viroSetMaterialShaderUniformMat4: matrix must have 16 elements, got %zu", elements.size());
        return;
    }
    m->setShaderUniform(name, VROMatrix4f(elements.data()));
}

// --- Physics ---
//
// Bullet was compiled and linked into this binary from the start and nothing
// reached it: every scene on web ran with no physics at all, and the host said
// so through its capability report rather than because it had to.
//
// The scene switch is honoured here rather than left to the engine. VROScene
// creates a physics world on demand at its own -9.81 the first time anything
// asks for one, so a body attached while the scene's physics is off would
// simulate anyway — exactly the bug the native side had to fix once already.
// viroSetPhysicsWorld(enabled=false) is therefore a real instruction, not a
// no-op: it detaches every body it knows about.

static bool sPhysicsEnabled = false;

/** Reports a collision to JS as (viroTag of A, viroTag of B, point, normal). */
static emscripten::val sCollisionCallback = emscripten::val::undefined();
static void viroSetCollisionCallback(emscripten::val cb) {
    sCollisionCallback = cb;
}

class WebPhysicsDelegate : public VROPhysicsBodyDelegate {
public:
    explicit WebPhysicsDelegate(std::string tag) : _tag(std::move(tag)) {}
    void onCollided(std::string bodyBKey, VROPhysicsBody::VROCollision collision) override {
        if (sCollisionCallback.isUndefined() || sCollisionCallback.isNull()) {
            return;
        }
        sCollisionCallback(_tag, collision.collidedBodyTag,
                           collision.collidedPoint.x, collision.collidedPoint.y,
                           collision.collidedPoint.z,
                           collision.collidedNormal.x, collision.collidedNormal.y,
                           collision.collidedNormal.z);
    }
private:
    std::string _tag;
};

// Bodies by node handle, so a scene teardown or a switch-off can detach them.
static std::unordered_map<int, std::shared_ptr<VROPhysicsBody>> sPhysicsBodies;
static std::unordered_map<int, std::shared_ptr<WebPhysicsDelegate>> sPhysicsDelegates;

/**
 The scene's physics world.

 `create` is false everywhere the answer "there isn't one" is the right one:
 VROScene builds a world on demand and VROScene::computePhysics only steps when
 one exists, so asking for it is what starts the simulation. A scene whose author
 switched physics off must never reach the creating branch, or it simulates at
 virocore's own -9.81 regardless — the bug the native side already had to fix.
 */
static std::shared_ptr<VROPhysicsWorld> physicsWorld(bool create) {
    if (!sScene) return nullptr;
    std::shared_ptr<VROScene> scene = sScene->getScene();
    if (!scene) return nullptr;
    if (!create && !scene->hasPhysicsWorld()) return nullptr;
    return scene->getPhysicsWorld();
}

static void viroSetPhysicsWorld(bool enabled, float gx, float gy, float gz) {
    sPhysicsEnabled = enabled;
    // Only create a world when switching physics ON. Off means: if one exists,
    // empty it; if not, leave it that way and nothing ever steps.
    auto world = physicsWorld(enabled);
    if (!world) return;

    if (!enabled) {
        // Detach rather than merely stop: the world keeps stepping whatever it
        // holds, so leaving the bodies in would simulate a scene whose author
        // switched physics off.
        for (auto &entry : sPhysicsBodies) {
            world->removePhysicsBody(entry.second);
        }
        return;
    }
    world->setGravity({ gx, gy, gz });
    for (auto &entry : sPhysicsBodies) {
        // Remove first: this runs again on every scene change and on a hot
        // reload, and the world rejects a body it already holds ("Attempted to
        // add the same physics body twice"). Removing an absent one is a no-op,
        // so this is the idempotent order.
        world->removePhysicsBody(entry.second);
        world->addPhysicsBody(entry.second);
    }
}

// type: 0 static, 1 kinematic, 2 dynamic.  shapeType: -1 infer from geometry,
// 2 sphere, 3 box (VROShapeType). Box params are HALF spans, as virocore takes
// them and as the native bridges pass them through.
static void viroSetPhysicsBody(int nodeHandle, int type, float mass,
                               int shapeType, emscripten::val shapeParams,
                               std::string tag) {
    auto node = getNode(nodeHandle);
    if (!node) return;

    // Replacing a body rather than adding a second one: a changed collider
    // re-enters here, and the old body would otherwise stay in the world for
    // ever, colliding with things from a shape the author already changed.
    viroClearPhysicsBody(nodeHandle);

    VROPhysicsBody::VROPhysicsBodyType bodyType;
    switch (type) {
        case 1:  bodyType = VROPhysicsBody::VROPhysicsBodyType::Kinematic; break;
        case 2:  bodyType = VROPhysicsBody::VROPhysicsBodyType::Dynamic; break;
        default: bodyType = VROPhysicsBody::VROPhysicsBodyType::Static; break;
    }

    // No shape means "fit the geometry": virocore infers one from the node's
    // bounding box and applies its world scale, which is what the editors
    // measure. Sending a unit box instead is what used to stand a 0.4 m model
    // 0.3 m off the ground.
    std::shared_ptr<VROPhysicsShape> shape;
    if (shapeType >= 0) {
        std::vector<float> params =
            emscripten::convertJSArrayToNumberVector<float>(shapeParams);
        shape = std::make_shared<VROPhysicsShape>(
            (VROPhysicsShape::VROShapeType) shapeType, params);
    }

    // The tag lives on the node: VROPhysicsBody::getTag() reads it from there,
    // and it is what a collision reports as the other party.
    node->setTag(tag);
    auto body = node->initPhysicsBody(bodyType, mass, shape);
    if (!body) return;

    auto delegate = std::make_shared<WebPhysicsDelegate>(tag);
    body->setPhysicsDelegate(delegate);
    sPhysicsDelegates[nodeHandle] = delegate;
    sPhysicsBodies[nodeHandle] = body;

    // Only joins the world if the scene said physics is on.
    if (sPhysicsEnabled) {
        if (auto world = physicsWorld(true)) world->addPhysicsBody(body);
    }
}

static void viroSetPhysicsBodyProperties(int nodeHandle, float restitution,
                                         float friction, bool useGravity) {
    auto it = sPhysicsBodies.find(nodeHandle);
    if (it == sPhysicsBodies.end()) return;
    it->second->setRestitution(restitution);
    it->second->setFriction(friction);
    it->second->setUseGravity(useGravity);
}

// isConstant=false is the instant latch the next physics step consumes once.
// A constant velocity is reasserted on the rigid body every frame and gravity
// never gets a turn, which is why Studio sends `instantVelocity`.
static void viroSetPhysicsVelocity(int nodeHandle, float x, float y, float z,
                                   bool isConstant) {
    auto it = sPhysicsBodies.find(nodeHandle);
    if (it == sPhysicsBodies.end()) return;
    it->second->setVelocity({ x, y, z }, isConstant);
}

static void viroApplyPhysicsImpulse(int nodeHandle, float x, float y, float z) {
    auto it = sPhysicsBodies.find(nodeHandle);
    if (it == sPhysicsBodies.end()) return;
    it->second->applyImpulse({ x, y, z }, { 0, 0, 0 });
}

static void viroApplyPhysicsTorque(int nodeHandle, float x, float y, float z) {
    auto it = sPhysicsBodies.find(nodeHandle);
    if (it == sPhysicsBodies.end()) return;
    it->second->applyTorqueImpulse({ x, y, z });
}

static void viroClearPhysicsBody(int nodeHandle) {
    auto it = sPhysicsBodies.find(nodeHandle);
    if (it == sPhysicsBodies.end()) return;
    if (auto world = physicsWorld(false)) world->removePhysicsBody(it->second);
    sPhysicsBodies.erase(it);
    sPhysicsDelegates.erase(nodeHandle);
    if (auto node = getNode(nodeHandle)) node->clearPhysicsBody();
}

// --- Shader overrides (a registered material merged onto a loaded model) ---
//
// The web counterpart of VRTNode.mm's applyShaderOverridesRecursive, and
// deliberately the same merge: the model keeps its own colours and textures, and
// the override contributes the seven rendering properties plus its shader
// modifiers and uniforms. Copying the colour here would make a model read one way
// in a browser and another on a phone, which is the difference this exists to
// close; the shared defect is that neither surface honours an authored colour on
// a model, and that belongs to the native merge, not here.
//
// A node handed to the bridge usually draws nothing itself — a loaded model hangs
// its geometry off child nodes — so this walks the subtree rather than reading
// getGeometry() alone.

// sShaderOverrideBaselines (declared with the handle tables) holds, per node, the
// materials each of its geometries carried before the first override landed, so
// re-applying a changed override merges onto the model's own materials again
// rather than compounding onto the previous merge.

static void mergeShaderOverride(const std::shared_ptr<VROMaterial> &source,
                                const std::shared_ptr<VROMaterial> &dest) {
    dest->setLightingModel(source->getLightingModel());
    dest->setShininess(source->getShininess());
    dest->setBlendMode(source->getBlendMode());
    dest->setTransparencyMode(source->getTransparencyMode());
    dest->setCullMode(source->getCullMode());
    dest->setWritesToDepthBuffer(source->getWritesToDepthBuffer());
    dest->setReadsFromDepthBuffer(source->getReadsFromDepthBuffer());

    // Modifiers are added, not replaced: dest is a copy of the model's own
    // material and still carries the loader's modifiers, so clearing them would
    // stop a rigged mesh skinning. Each merge starts from the baseline, so
    // nothing accumulates across applications.
    dest->setThreadRestrictionEnabled(false);
    for (const auto &modifier : source->getShaderModifiers()) {
        dest->addShaderModifier(modifier);
    }
    for (const auto &u : source->getShaderUniformFloats())   dest->setShaderUniform(u.first, u.second);
    for (const auto &u : source->getShaderUniformVec2s())    dest->setShaderUniform(u.first, u.second);
    for (const auto &u : source->getShaderUniformVec3s())    dest->setShaderUniform(u.first, u.second);
    for (const auto &u : source->getShaderUniformVec4s())    dest->setShaderUniform(u.first, u.second);
    for (const auto &u : source->getShaderUniformMat4s())    dest->setShaderUniform(u.first, u.second);
    for (const auto &u : source->getShaderUniformTextures()) dest->setShaderUniform(u.first, u.second);
    dest->setThreadRestrictionEnabled(true);
}

static void applyShaderOverrideToNode(
    const std::shared_ptr<VRONode> &node,
    const std::shared_ptr<VROMaterial> &source,
    std::unordered_map<VROGeometry *, std::vector<std::shared_ptr<VROMaterial>>> &baselines) {

    std::shared_ptr<VROGeometry> geometry = node->getGeometry();
    // An empty material list means the loader has not populated this geometry
    // yet; skipping leaves the baseline unrecorded so a later call still sees the
    // model's own materials rather than adopting an empty set as the original.
    if (geometry && !geometry->getMaterials().empty()) {
        auto baseline = baselines.find(geometry.get());
        if (baseline == baselines.end()) {
            baseline = baselines.emplace(geometry.get(), geometry->getMaterials()).first;
        }
        std::vector<std::shared_ptr<VROMaterial>> merged;
        merged.reserve(baseline->second.size());
        for (const auto &original : baseline->second) {
            auto copy = std::make_shared<VROMaterial>(original);
            mergeShaderOverride(source, copy);
            merged.push_back(copy);
        }
        geometry->setMaterials(merged);
    }

    for (const auto &child : node->getChildNodes()) {
        applyShaderOverrideToNode(child, source, baselines);
    }
}

static void viroApplyShaderOverride(int nodeHandle, int materialHandle) {
    auto node = getNode(nodeHandle);
    auto material = getMaterial(materialHandle);
    if (!node || !material) {
        return;
    }
    applyShaderOverrideToNode(node, material, sShaderOverrideBaselines[nodeHandle]);
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
// sampler2D shader uniform. texture may be VIRO_INVALID_HANDLE (0) to clear it.
static void viroSetMaterialShaderUniformTexture(int material, std::string name, int texture) {
    auto m = getMaterial(material);
    if (!m) return;
    m->setShaderUniform(name, getTexture(texture));
}

// Create a cube texture from six RGBA8 faces (order: +X,-X,+Y,-Y,+Z,-Z), each
// width*height*4 bytes. Used for skybox backgrounds.
static int viroCreateTextureCubeRGBA(emscripten::val px, emscripten::val nx,
                                     emscripten::val py, emscripten::val ny,
                                     emscripten::val pz, emscripten::val nz,
                                     int width, int height) {
    auto toData = [](emscripten::val a) {
        std::vector<uint8_t> b = emscripten::convertJSArrayToNumberVector<uint8_t>(a);
        return std::make_shared<VROData>(b.data(), (int) b.size());
    };
    std::vector<std::shared_ptr<VROData>> faces = {
        toData(px), toData(nx), toData(py), toData(ny), toData(pz), toData(nz)
    };
    int h = sNextHandle++;
    sTextures[h] = std::make_shared<VROTexture>(
        VROTextureType::TextureCube, VROTextureFormat::RGBA8, VROTextureInternalFormat::RGBA8,
        true, VROMipmapMode::None, faces, width, height, std::vector<uint32_t>());
    return h;
}

// --- Lighting environment (IBL) ---

// Load a radiance .hdr file (already written to the virtual FS at `path`) into a
// texture usable as an image-based lighting environment.
static int viroLoadRadianceHDRTexture(std::string path) {
    std::shared_ptr<VROTexture> tex = VROHDRLoader::loadRadianceHDRTexture(path);
    if (!tex) return 0;
    int h = sNextHandle++;
    sTextures[h] = tex;
    return h;
}

// Apply (or clear, if 0) the scene's IBL environment.
static void viroSetLightingEnvironment(int textureHandle) {
    if (!sScene) return;
    if (textureHandle == 0) {
        sScene->getRootNode()->setLightingEnvironment(nullptr);
    } else if (std::shared_ptr<VROTexture> tex = getTexture(textureHandle)) {
        sScene->getRootNode()->setLightingEnvironment(tex);
    }
}

// --- Background (skybox / 360) ---

static void viroSetBackgroundSphere(int texture) {
    if (sScene) {
        auto t = getTexture(texture);
        if (t) sScene->getRootNode()->setBackgroundSphere(t);
    }
}
static void viroSetBackgroundCube(int texture) {
    if (sScene) {
        auto t = getTexture(texture);
        if (t) sScene->getRootNode()->setBackgroundCube(t);
    }
}
static void viroSetBackgroundRotation(float x, float y, float z) {
    if (sScene) {
        VROQuaternion q(x, y, z);
        sScene->getRootNode()->setBackgroundRotation(q);
    }
}

// --- Particle emitter ---

// Attach a particle emitter to a node. The sprite is a quad (particleW×particleH)
// textured with textureHandle. spawnShape: 0 Box, 1 Sphere, 2 Point (shapeParams:
// Box=[w,h,l], Sphere=[r], Point=[]). velocity is a random range [min,max].
// Returns 1 on success, 0 on failure.
static int viroCreateParticleEmitter(int nodeHandle, int textureHandle,
                                     float particleW, float particleH,
                                     int maxParticles, int emitRateMin, int emitRateMax,
                                     int lifetimeMin, int lifetimeMax,
                                     int spawnShape, float sp0, float sp1, float sp2,
                                     float velMinX, float velMinY, float velMinZ,
                                     float velMaxX, float velMaxY, float velMaxZ) {
    if (!sScene) return 0;
    std::shared_ptr<VRONode> node = getNode(nodeHandle);
    if (!node) return 0;

    std::shared_ptr<VROSurface> surface = VROSurface::createSurface(particleW, particleH);
    std::shared_ptr<VROMaterial> mat = surface->getMaterials()[0];
    mat->setLightingModel(VROLightingModel::Constant);
    mat->setBlendMode(VROBlendMode::Add);
    mat->setWritesToDepthBuffer(false);
    if (std::shared_ptr<VROTexture> tex = getTexture(textureHandle)) {
        mat->getDiffuse().setTexture(tex);
    }

    std::shared_ptr<VROParticleEmitter> emitter =
        std::make_shared<VROParticleEmitter>(sScene->getDriver(), surface);
    emitter->setMaxParticles(maxParticles);
    emitter->setEmissionRatePerSecond({ emitRateMin, emitRateMax });
    emitter->setParticleLifeTime({ lifetimeMin, lifetimeMax });

    VROParticleSpawnVolume volume;
    switch (spawnShape) {
        case 0: volume.shape = VROParticleSpawnVolume::Shape::Box; volume.shapeParams = { sp0, sp1, sp2 }; break;
        case 1: volume.shape = VROParticleSpawnVolume::Shape::Sphere; volume.shapeParams = { sp0 }; break;
        default: volume.shape = VROParticleSpawnVolume::Shape::Point; volume.shapeParams = {}; break;
    }
    volume.spawnOnSurface = false;
    emitter->setParticleSpawnVolume(volume);

    emitter->setVelocityModifier(std::make_shared<VROParticleModifier>(
        VROVector3f(velMinX, velMinY, velMinZ), VROVector3f(velMaxX, velMaxY, velMaxZ)));

    emitter->setRun(true);
    node->setParticleEmitter(emitter);
    return 1;
}

// B8. Acceleration on a running emitter, as a [min, max] range like velocity.
// Set after creation rather than as six more arguments to an emitter factory
// that already takes seventeen.
static void viroSetParticleAcceleration(int nodeHandle,
                                        float minX, float minY, float minZ,
                                        float maxX, float maxY, float maxZ) {
    auto node = getNode(nodeHandle);
    if (!node) return;
    auto emitter = node->getParticleEmitter();
    if (!emitter) return;
    emitter->setAccelerationmodifier(std::make_shared<VROParticleModifier>(
        VROVector3f(minX, minY, minZ), VROVector3f(maxX, maxY, maxZ)));
}

// The appearance modifiers: how a particle's colour, opacity, scale and rotation
// change over its life. Without them a web emitter drew every particle at full
// opacity and one size until it expired, so smoke never thinned and a spark
// never shrank — the two things an author uses particles for.
//
// One entry point for all four because virocore models them identically: an
// initial [min, max] range to randomise from, a reference factor, and a list of
// intervals to interpolate towards. `intervals` arrives flattened at five floats
// each — startFactor, endFactor, then the target x/y/z — since embind has no
// cheap way to hand over an array of structs, and a trailing partial entry is
// dropped rather than read past.
//
//   which:  0 alpha (x only), 1 colour (rgb), 2 scale (xyz), 3 rotation (xyz radians)
//   factor: 0 time, 1 distance, 2 velocity
static void viroSetParticleModifier(int nodeHandle, int which,
                                    float minX, float minY, float minZ,
                                    float maxX, float maxY, float maxZ,
                                    int factor, emscripten::val intervals) {
    auto node = getNode(nodeHandle);
    if (!node) return;
    auto emitter = node->getParticleEmitter();
    if (!emitter) return;

    VROParticleModifier::VROModifierFactor referenceFactor;
    switch (factor) {
        case 1:  referenceFactor = VROParticleModifier::VROModifierFactor::Distance; break;
        case 2:  referenceFactor = VROParticleModifier::VROModifierFactor::Velocity; break;
        default: referenceFactor = VROParticleModifier::VROModifierFactor::Time; break;
    }

    std::vector<float> flat = emscripten::convertJSArrayToNumberVector<float>(intervals);
    std::vector<VROParticleModifier::VROModifierInterval> points;
    const size_t stride = 5;
    for (size_t i = 0; i + stride <= flat.size(); i += stride) {
        VROParticleModifier::VROModifierInterval point;
        point.startFactor = flat[i];
        point.endFactor = flat[i + 1];
        point.targetedValue = VROVector3f(flat[i + 2], flat[i + 3], flat[i + 4]);
        points.push_back(point);
    }

    auto modifier = std::make_shared<VROParticleModifier>(
        VROVector3f(minX, minY, minZ), VROVector3f(maxX, maxY, maxZ),
        referenceFactor, points);

    switch (which) {
        case 0: emitter->setAlphaModifier(modifier); break;
        case 1: emitter->setColorModifier(modifier); break;
        case 2: emitter->setScaleModifier(modifier); break;
        case 3: emitter->setRotationModifier(modifier); break;
        default: break;
    }
}

// Toggle a node's emitter run/pause state.
static void viroSetParticleEmitterRun(int nodeHandle, bool run) {
    std::shared_ptr<VRONode> node = getNode(nodeHandle);
    if (node && node->getParticleEmitter()) {
        node->getParticleEmitter()->setRun(run);
    }
}

// --- Portals ---
// A portal scene (VROPortal) holds content seen through an entrance frame
// (VROPortalFrame). Both are VRONodes, so parenting uses the normal node API;
// these just create the right subclass and wire the entrance.

static int viroCreatePortalScene() {
    int h = sNextHandle++;
    sNodes[h] = std::make_shared<VROPortal>();
    return h;
}
static int viroCreatePortalFrame() {
    int h = sNextHandle++;
    sNodes[h] = std::make_shared<VROPortalFrame>();
    return h;
}
static void viroSetPortalEntrance(int portalScene, int frame) {
    std::shared_ptr<VROPortal> scene = std::dynamic_pointer_cast<VROPortal>(getNode(portalScene));
    std::shared_ptr<VROPortalFrame> f = std::dynamic_pointer_cast<VROPortalFrame>(getNode(frame));
    if (scene && f) {
        scene->setPortalEntrance(f);
    }
}
static void viroSetPortalPassable(int portalScene, bool passable) {
    std::shared_ptr<VROPortal> scene = std::dynamic_pointer_cast<VROPortal>(getNode(portalScene));
    if (scene) {
        scene->setPassable(passable);
    }
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
// B4. The light half of masking. Pairs with the node's lightReceivingBitMask:
// a light only lights a node where the two masks intersect.
static void viroSetLightInfluenceBitMask(int light, int mask) {
    if (auto l = getLight(light)) l->setInfluenceBitMask(mask);
}

// B5. Shadow tuning. `castsShadow` alone decides whether a light casts; these
// decide whether the result is usable — a map too small or a bias too low is
// the difference between a shadow and a field of acne.
//
// No shadowOrthographicPosition: the native navigators expose it but VROLight
// has no setter for it, so there is nothing to forward it to. Six of the seven.
static void viroSetLightShadowOpacity(int light, float opacity) {
    if (auto l = getLight(light)) l->setShadowOpacity(opacity);
}
static void viroSetLightShadowMapSize(int light, int size) {
    if (auto l = getLight(light)) l->setShadowMapSize(size);
}
static void viroSetLightShadowBias(int light, float bias) {
    if (auto l = getLight(light)) l->setShadowBias(bias);
}
static void viroSetLightShadowNearZ(int light, float nearZ) {
    if (auto l = getLight(light)) l->setShadowNearZ(nearZ);
}
static void viroSetLightShadowFarZ(int light, float farZ) {
    if (auto l = getLight(light)) l->setShadowFarZ(farZ);
}
static void viroSetLightShadowOrthographicSize(int light, float size) {
    if (auto l = getLight(light)) l->setShadowOrthographicSize(size);
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

// type: 0=perspective, 1=orthographic. VRORenderer::computeProjection reads these
// off the point-of-view camera, so the whole effect is in the two setters below.
static void viroSetCameraProjection(int node, int type) {
    auto n = getNode(node);
    if (!n || !n->getCamera()) {
        return;
    }
    n->getCamera()->setProjectionType(type == 1 ? VROCameraProjectionType::Orthographic
                                               : VROCameraProjectionType::Perspective);
}

// The full vertical height in world units; the width follows from the viewport
// aspect ratio, so only the height is set here.
static void viroSetCameraOrthographicScale(int node, float scale) {
    auto n = getNode(node);
    if (!n || !n->getCamera()) {
        return;
    }
    n->getCamera()->setOrthographicHeight(scale);
}

// --- Model loading (GLB / glTF / VRX / OBJ) ---

// cb(nodeHandle, success). Registered by the bridge to know when a load finishes.
static emscripten::val sModelLoadCallback = emscripten::val::undefined();
static void viroSetModelLoadCallback(emscripten::val callback) {
    sModelLoadCallback = callback;
}

// Loads a model at `path` (already written to the emscripten virtual FS by JS)
// into the node. format: 0=GLB, 1=glTF, 2=VRX, 3=OBJ. Self-contained assets
// (GLB/VRX) need only the single file; the VRX loader handles gzip.
//
// OBJ is the exception: it names a .mtl, which in turn names textures, and both
// are resolved against the *directory* of `path` — VROOBJLoader takes everything
// before the last '/' and tinyobj joins that with the referenced name. So the JS
// side writes an OBJ and its companions into a directory of their own. A flat
// layout at the FS root happens to resolve too (an empty base yields "/tex.png"
// for textures and a CWD-relative open for the .mtl), but then two models that
// both reference "wood.png" overwrite each other.
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

    if (format == 3) {
        VROOBJLoader::loadOBJFromResource(path, VROResourceType::LocalFile, node, driver, onFinish);
    } else if (format == 2) {
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

// --- Declarative animations (ViroAnimations: transform/opacity via transaction) ---
//
// The bridge wraps node property setters between begin/commit; the renderer
// interpolates from the current values to the new ones over `duration`. Reuses
// the existing viroSetNode* setters so any animatable property works.

// easing: 0=Linear,1=EaseIn,2=EaseOut,3=EaseInEaseOut,4=Bounce,5=PowerDecel
static VROTimingFunctionType easingValue(int easing) {
    switch (easing) {
        case 1: return VROTimingFunctionType::EaseIn;
        case 2: return VROTimingFunctionType::EaseOut;
        case 3: return VROTimingFunctionType::EaseInEaseOut;
        case 4: return VROTimingFunctionType::Bounce;
        case 5: return VROTimingFunctionType::PowerDecel;
        default: return VROTimingFunctionType::Linear;
    }
}

static void viroBeginAnimation(int nodeHandle, float duration, float delay, bool loop, int easing) {
    VROTransaction::begin();
    VROTransaction::setAnimationDuration(duration);
    if (delay > 0) {
        VROTransaction::setAnimationDelay(delay);
    }
    VROTransaction::setAnimationLoop(loop);
    VROTransaction::setTimingFunction(easingValue(easing));
    VROTransaction::setFinishCallback([nodeHandle](bool terminate) {
        if (!terminate) emitAnim(nodeHandle, 1);
    });
}
static void viroCommitAnimation() {
    VROTransaction::commit();
}

#pragma mark - AR

// Switch the scene into AR mode. After this, drawFrame() drives the camera from
// the pose injected via viroARSetPose and draws the camera background.
static void viroInitAR() {
    if (sScene) sScene->initAR();
}

// Inject a camera pose from JS (slam-wasm), already converted to virocore's
// Y-up/GL convention. Rotation as a quaternion (x,y,z,w); position in meters.
// trackingState: 1 = Unavailable, 2 = Limited, 3 = Normal.
static void viroARSetPose(float qx, float qy, float qz, float qw,
                          float px, float py, float pz, int trackingState) {
    if (!sScene) return;
    std::shared_ptr<VROARSessionWeb> session = sScene->getARSession();
    if (!session) return;

    VROMatrix4f rotation = VROQuaternion(qx, qy, qz, qw).getMatrix();
    VROARTrackingState state = VROARTrackingState::Normal;
    switch (trackingState) {
        case 1: state = VROARTrackingState::Unavailable; break;
        case 2: state = VROARTrackingState::Limited; break;
        default: state = VROARTrackingState::Normal; break;
    }
    session->setPose(rotation, VROVector3f(px, py, pz), state);
}

// Set the live camera feed texture (a handle from viroCreateTextureRGBA that JS
// re-uploads each frame from the <video> element).
static void viroARSetCameraBackground(int textureHandle) {
    if (!sScene) return;
    std::shared_ptr<VROARSessionWeb> session = sScene->getARSession();
    if (!session) return;
    session->setCameraBackground(getTexture(textureHandle));
}

// Report the camera image dimensions (used for projection/intrinsics).
static void viroARSetCameraImageSize(float width, float height) {
    if (!sScene) return;
    std::shared_ptr<VROARSessionWeb> session = sScene->getARSession();
    if (session) session->setCameraImageSize(width, height);
}

// The camera's real intrinsics. Prefer this over viroARSetCameraImageSize
// wherever the host knows them: without them the frustum is a fixed 60-degree
// vertical field of view, and 3-D content is then projected through a different
// camera from the one that produced the background image.
static void viroARSetCameraIntrinsics(float fx, float fy, float cx, float cy,
                                      float width, float height) {
    if (!sScene) return;
    std::shared_ptr<VROARSessionWeb> session = sScene->getARSession();
    if (session) session->setCameraIntrinsics(fx, fy, cx, cy, width, height);
}

EMSCRIPTEN_BINDINGS(viro_web) {
    emscripten::function("initViroScene", &initViroScene);
    emscripten::function("setViroSceneSize", &setViroSceneSize);
    emscripten::function("viroSetHDREnabled", &viroSetHDREnabled);
    emscripten::function("viroSetBloomEnabled", &viroSetBloomEnabled);
    emscripten::function("viroSetPBREnabled", &viroSetPBREnabled);
    emscripten::function("viroSetShadowsEnabled", &viroSetShadowsEnabled);
    emscripten::function("viroSetToneMappingEnabled", &viroSetToneMappingEnabled);
    emscripten::function("viroGetBuildId", &viroGetBuildId);

    // Physics
    emscripten::function("viroSetPhysicsWorld", &viroSetPhysicsWorld);
    emscripten::function("viroSetPhysicsBody", &viroSetPhysicsBody);
    emscripten::function("viroSetPhysicsBodyProperties", &viroSetPhysicsBodyProperties);
    emscripten::function("viroSetPhysicsVelocity", &viroSetPhysicsVelocity);
    emscripten::function("viroApplyPhysicsImpulse", &viroApplyPhysicsImpulse);
    emscripten::function("viroApplyPhysicsTorque", &viroApplyPhysicsTorque);
    emscripten::function("viroClearPhysicsBody", &viroClearPhysicsBody);
    emscripten::function("viroSetCollisionCallback", &viroSetCollisionCallback);
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
    emscripten::function("viroSetNodeRenderingOrder", &viroSetNodeRenderingOrder);
    emscripten::function("viroSetNodeLightReceivingBitMask", &viroSetNodeLightReceivingBitMask);
    emscripten::function("viroSetNodeShadowCastingBitMask", &viroSetNodeShadowCastingBitMask);
    emscripten::function("viroSetNodeBillboard", &viroSetNodeBillboard);
    emscripten::function("viroGetNodeWorldPosition", &viroGetNodeWorldPosition);
    emscripten::function("viroSetMorphTargetWeight", &viroSetMorphTargetWeight);
    emscripten::function("viroGetMorphTargetKeys", &viroGetMorphTargetKeys);
    emscripten::function("viroSetMorphMode", &viroSetMorphMode);

    emscripten::function("viroCreateBox", &viroCreateBox);
    emscripten::function("viroCreateSphere", &viroCreateSphere);
    emscripten::function("viroCreateSurface", &viroCreateSurface);
    emscripten::function("viroCreateSurfaceUV", &viroCreateSurfaceUV);
    emscripten::function("viroCreateText", &viroCreateText);
    emscripten::function("viroCreatePolyline", &viroCreatePolyline);
    emscripten::function("viroCreatePolygon", &viroCreatePolygon);
    emscripten::function("viroCreateGeometry", &viroCreateGeometry);
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
    emscripten::function("viroAddMaterialShaderModifier", &viroAddMaterialShaderModifier);
    emscripten::function("viroRemoveAllMaterialShaderModifiers", &viroRemoveAllMaterialShaderModifiers);
    emscripten::function("viroSetMaterialShaderUniformFloat", &viroSetMaterialShaderUniformFloat);
    emscripten::function("viroSetMaterialShaderUniformVec2", &viroSetMaterialShaderUniformVec2);
    emscripten::function("viroSetMaterialShaderUniformVec3", &viroSetMaterialShaderUniformVec3);
    emscripten::function("viroSetMaterialShaderUniformVec4", &viroSetMaterialShaderUniformVec4);
    emscripten::function("viroSetMaterialShaderUniformMat4", &viroSetMaterialShaderUniformMat4);
    emscripten::function("viroApplyShaderOverride", &viroApplyShaderOverride);

    emscripten::function("viroCreateTextureRGBA", &viroCreateTextureRGBA);
    emscripten::function("viroSetTextureWrap", &viroSetTextureWrap);
    emscripten::function("viroSetTextureFilter", &viroSetTextureFilter);
    emscripten::function("viroSetMaterialTexture", &viroSetMaterialTexture);
    emscripten::function("viroDestroyTexture", &viroDestroyTexture);
    emscripten::function("viroSetMaterialShaderUniformTexture", &viroSetMaterialShaderUniformTexture);
    emscripten::function("viroCreateTextureCubeRGBA", &viroCreateTextureCubeRGBA);
    emscripten::function("viroLoadRadianceHDRTexture", &viroLoadRadianceHDRTexture);
    emscripten::function("viroSetLightingEnvironment", &viroSetLightingEnvironment);
    emscripten::function("viroSetBackgroundSphere", &viroSetBackgroundSphere);
    emscripten::function("viroSetBackgroundCube", &viroSetBackgroundCube);
    emscripten::function("viroSetBackgroundRotation", &viroSetBackgroundRotation);
    emscripten::function("viroCreateParticleEmitter", &viroCreateParticleEmitter);
    emscripten::function("viroSetParticleEmitterRun", &viroSetParticleEmitterRun);
    emscripten::function("viroSetParticleAcceleration", &viroSetParticleAcceleration);
    emscripten::function("viroSetParticleModifier", &viroSetParticleModifier);
    emscripten::function("viroCreatePortalScene", &viroCreatePortalScene);
    emscripten::function("viroCreatePortalFrame", &viroCreatePortalFrame);
    emscripten::function("viroSetPortalEntrance", &viroSetPortalEntrance);
    emscripten::function("viroSetPortalPassable", &viroSetPortalPassable);

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
    emscripten::function("viroSetLightInfluenceBitMask", &viroSetLightInfluenceBitMask);
    emscripten::function("viroSetLightShadowOpacity", &viroSetLightShadowOpacity);
    emscripten::function("viroSetLightShadowMapSize", &viroSetLightShadowMapSize);
    emscripten::function("viroSetLightShadowBias", &viroSetLightShadowBias);
    emscripten::function("viroSetLightShadowNearZ", &viroSetLightShadowNearZ);
    emscripten::function("viroSetLightShadowFarZ", &viroSetLightShadowFarZ);
    emscripten::function("viroSetLightShadowOrthographicSize", &viroSetLightShadowOrthographicSize);
    emscripten::function("viroAddLightToNode", &viroAddLightToNode);
    emscripten::function("viroRemoveLightFromNode", &viroRemoveLightFromNode);
    emscripten::function("viroDestroyLight", &viroDestroyLight);

    emscripten::function("viroSetNodeCamera", &viroSetNodeCamera);
    emscripten::function("viroSetActiveCameraNode", &viroSetActiveCameraNode);
    emscripten::function("viroSetCameraProjection", &viroSetCameraProjection);
    emscripten::function("viroSetCameraOrthographicScale", &viroSetCameraOrthographicScale);

    emscripten::function("viroSetModelLoadCallback", &viroSetModelLoadCallback);
    emscripten::function("viroLoadModel", &viroLoadModel);

    emscripten::function("viroSetAnimationCallback", &viroSetAnimationCallback);
    emscripten::function("viroGetAnimationKeys", &viroGetAnimationKeys);
    emscripten::function("viroStartAnimation", &viroStartAnimation);
    emscripten::function("viroPauseAnimation", &viroPauseAnimation);
    emscripten::function("viroResumeAnimation", &viroResumeAnimation);
    emscripten::function("viroStopAnimation", &viroStopAnimation);

    emscripten::function("viroBeginAnimation", &viroBeginAnimation);
    emscripten::function("viroCommitAnimation", &viroCommitAnimation);

    emscripten::function("viroInitAR", &viroInitAR);
    emscripten::function("viroARSetPose", &viroARSetPose);
    emscripten::function("viroARSetCameraBackground", &viroARSetCameraBackground);
    emscripten::function("viroARSetCameraImageSize", &viroARSetCameraImageSize);
    emscripten::function("viroARSetCameraIntrinsics", &viroARSetCameraIntrinsics);
}

// The module has no work to do at startup — JS calls initViroScene() once the
// canvas exists. An empty main() keeps the default emscripten entry happy so we
// don't need --no-entry.
int main() {
    return 0;
}

