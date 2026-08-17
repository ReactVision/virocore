//
//  VROSceneWeb.h
//  ViroRenderer — Web (WASM) library entry point
//
//  Hosts the renderer, driver, input controller, and the active scene. The
//  scene starts empty and is built up imperatively from JS via the web C API
//  (see VROSceneWebAPI.cpp), which the TS bridge drives to translate the Viro
//  component tree into VRONode/VROGeometry/VROMaterial objects.
//
//  A demo cube can still be built via viroBuildDemoCube() for smoke testing.
//
//  The public surface is exposed to JS via EMSCRIPTEN_BINDINGS — there is no
//  SDL `main()` driving this.
//

#ifndef VROSceneWeb_h
#define VROSceneWeb_h

#include <memory>
#include <string>
#include "emscripten.h"
#include "emscripten/html5.h"

class VRORenderer;
class VROInputControllerWasm;
class VRODriverOpenGLWasm;
class VRONode;
class VROPortal;
class VROScene;
class VROSceneController;
class VROEventDelegate;
class VROARSessionWeb;
class VROViewport;
class VROSurface;

class VROSceneWeb {
public:
    // canvasSelector is a CSS selector for the target <canvas>, e.g. "#viroCanvas".
    VROSceneWeb(std::string canvasSelector, int width, int height);
    virtual ~VROSceneWeb();

    void drawFrame();
    void setSize(int width, int height);

    // Forward a DOM pointer/touch event into the input controller.
    // action: 0 = down, 1 = move, 2 = up. x/y in device pixels, top-left origin.
    void onTouch(int action, float x, float y);

    // Root node of the active scene — the C API attaches bridge-created nodes here.
    std::shared_ptr<VROPortal> getRootNode();

    // Make the given node's camera the renderer's point of view.
    void setActiveCameraNode(std::shared_ptr<VRONode> node);

    // Driver, needed by the model loaders (GLTF/FBX) invoked from the C API.
    std::shared_ptr<VRODriverOpenGLWasm> getDriver();

    // Switch the render loop into AR mode: creates a VROARSessionWeb whose pose /
    // camera background are injected from JS (slam-wasm). The scene graph is
    // reused; drawFrame() then drives the camera from the AR pose.
    void initAR();
    std::shared_ptr<VROARSessionWeb> getARSession();

    // Build a hardcoded spinning cube demo (smoke test; not used by the bridge).
    void buildCubeScene();

private:
    // Creates an empty scene (root node + camera + default lights) ready to be
    // populated by the web C API.
    void buildEmptyScene();
    // AR render loop (used when _arSession is set); mirrors the native ARCore path.
    void drawFrameAR(VROViewport viewport);

    int _frame;
    int _width, _height;
    float _angle;

    std::string _canvasSelector;

    std::shared_ptr<VRORenderer> _renderer;
    std::shared_ptr<VROInputControllerWasm> _inputController;
    std::shared_ptr<VRODriverOpenGLWasm> _driver;

    std::shared_ptr<VROSceneController> _sceneController;
    std::shared_ptr<VROScene> _scene;

    // The point-of-view camera node; in AR its position is driven by the pose.
    std::shared_ptr<VRONode> _cameraNode;
    // Non-null when in AR mode (pose/background injected from JS).
    std::shared_ptr<VROARSessionWeb> _arSession;
    // Screen-space surface that draws the live camera feed behind the scene.
    // Created lazily on the first AR frame; its diffuse is the JS-uploaded texture.
    std::shared_ptr<VROSurface> _cameraBackground;

    // Demo-only: the cube node spun each frame by buildCubeScene().
    std::shared_ptr<VRONode> _boxNode;
    // Retained because VRONode holds the event delegate only weakly.
    std::shared_ptr<VROEventDelegate> _cubeDelegate;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE _context;
};

#endif /* VROSceneWeb_h */
