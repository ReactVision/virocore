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
#include "VROMaterial.h"
#include "VROTransaction.h"

static VROSceneWeb *sInstance = nullptr;

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

    _driver = std::make_shared<VRODriverOpenGLWasm>();
    _inputController = std::make_shared<VROInputControllerWasm>(_driver);

    // Minimal renderer config: no shadows/bloom/HDR/PBR so we stay on the
    // LinearSoftware path and avoid EXT_color_buffer_float dependencies for
    // this de-risking cube. These get re-enabled + validated in Phase 1.
    VRORendererConfiguration config;
    config.enableShadows = false;
    config.enableBloom = false;
    config.enableHDR = false;
    config.enablePBR = false;

    _renderer = std::make_shared<VRORenderer>(
        config, std::dynamic_pointer_cast<VROInputControllerBase>(_inputController));

    buildCubeScene();
    emscripten_set_main_loop(VROSceneWebMainLoop, 0, 0);
}

VROSceneWeb::~VROSceneWeb() {
    if (sInstance == this) {
        sInstance = nullptr;
    }
}

void VROSceneWeb::buildCubeScene() {
    std::shared_ptr<VROSceneController> sceneController = std::make_shared<VROSceneController>();
    std::shared_ptr<VROScene> scene = sceneController->getScene();
    std::shared_ptr<VROPortal> rootNode = scene->getRootNode();
    rootNode->setPosition({0, 0, 0});

    std::shared_ptr<VROLight> ambient = std::make_shared<VROLight>(VROLightType::Ambient);
    ambient->setColor({0.6, 0.6, 0.6});
    rootNode->addLight(ambient);

    std::shared_ptr<VROLight> directional = std::make_shared<VROLight>(VROLightType::Directional);
    directional->setColor({1.0, 1.0, 1.0});
    directional->setDirection({0.0, -0.5, -1.0});
    rootNode->addLight(directional);

    std::shared_ptr<VROBox> box = VROBox::createBox(2, 2, 2);
    box->setName("Cube");

    std::shared_ptr<VROMaterial> material = box->getMaterials()[0];
    material->setLightingModel(VROLightingModel::Blinn);
    material->getDiffuse().setColor({0.2, 0.6, 1.0, 1.0});

    _boxNode = std::make_shared<VRONode>();
    _boxNode->setGeometry(box);
    _boxNode->setPosition({0, 0, -5});
    rootNode->addChildNode(_boxNode);

    std::shared_ptr<VRONodeCamera> camera = std::make_shared<VRONodeCamera>();
    std::shared_ptr<VRONode> cameraNode = std::make_shared<VRONode>();
    cameraNode->setCamera(camera);
    rootNode->addChildNode(cameraNode);

    _renderer->setSceneController(sceneController, _driver);
    _renderer->setPointOfView(cameraNode);
}

void VROSceneWeb::drawFrame() {
    emscripten_webgl_make_context_current(_context);

    VROViewport viewport(0, 0, _width, _height);
    if (viewport.getWidth() == 0 || viewport.getHeight() == 0) {
        return;
    }

    // Spin the cube.
    _angle += 0.01f;
    if (_boxNode) {
        _boxNode->setRotationEuler({0, _angle, 0});
    }

    VROFieldOfView fov = _renderer->computeUserFieldOfView(viewport.getWidth(), viewport.getHeight());
    VROMatrix4f projection = fov.toPerspectiveProjection(kZNear, _renderer->getFarClippingPlane());

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

EMSCRIPTEN_BINDINGS(viro_web) {
    emscripten::function("initViroScene", &initViroScene);
    emscripten::function("setViroSceneSize", &setViroSceneSize);
}

// The module has no work to do at startup — JS calls initViroScene() once the
// canvas exists. An empty main() keeps the default emscripten entry happy so we
// don't need --no-entry.
int main() {
    return 0;
}

