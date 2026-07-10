//
//  VROSceneWeb.h
//  ViroRenderer — Web (WASM) library entry point
//
//  Phase 0 de-risking spike: a minimal, self-contained scene host that does
//  NOT depend on the VRORendererTestHarness (which pulls in every VRO*Test.cpp,
//  the FBX/HDR loaders, and per-test preload assets). It builds a single
//  spinning cube so we can prove the WASM+WebGL2 pipeline end-to-end.
//
//  The public surface is exposed to JS via EMSCRIPTEN_BINDINGS in
//  VROSceneWeb.cpp — there is no SDL `main()` driving this.
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
class VROEventDelegate;

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

private:
    void buildCubeScene();

    int _frame;
    int _width, _height;
    float _angle;

    std::string _canvasSelector;

    std::shared_ptr<VRORenderer> _renderer;
    std::shared_ptr<VROInputControllerWasm> _inputController;
    std::shared_ptr<VRODriverOpenGLWasm> _driver;

    // The node we spin every frame.
    std::shared_ptr<VRONode> _boxNode;

    // Retained because VRONode holds the event delegate only weakly.
    std::shared_ptr<VROEventDelegate> _cubeDelegate;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE _context;
};

#endif /* VROSceneWeb_h */
