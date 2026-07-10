//
//  VROInputControllerWasm.h
//  ViroRenderer
//
//  Copyright © 2018 Viro Media. All rights reserved.
//

#ifndef VROInputControllerWasm_H
#define VROInputControllerWasm_H

#include <memory>
#include "VRORenderContext.h"
#include "VROInputControllerBase.h"
#include "VROInputPresenterWasm.h"
#include "VROCamera.h"
#include "VROMatrix4f.h"

class VROInputControllerWasm : public VROInputControllerBase {

public:
    VROInputControllerWasm(std::shared_ptr<VRODriver> driver) : VROInputControllerBase(driver) {}
    virtual ~VROInputControllerWasm() {}
    virtual VROVector3f getDragForwardOffset();
    virtual void onProcess(const VROCamera &camera);

    virtual std::string getHeadset() {
        return "cardboard";
    }
    virtual std::string getController() {
        return "cardboard";
    }

    // Feed a DOM pointer/touch event. action: 0 = down, 1 = move, 2 = up.
    // x/y are in device pixels with a top-left origin (as DOM events report,
    // scaled to the canvas backing store); the y-flip to GL's bottom-left
    // origin is handled internally.
    void onScreenTouch(int action, float x, float y);

    // Called once per frame by the view with the same view/projection/viewport
    // used for rendering, so touch rays can be unprojected consistently.
    void setRenderState(VROMatrix4f view, VROMatrix4f projection,
                        int viewportWidth, int viewportHeight);

    // Legacy no-arg-position entry: treats the touch as hitting the screen
    // center. Retained for compatibility with the original stub.
    void updateScreenTouch(int touchAction);

protected:

    std::shared_ptr<VROInputPresenter> createPresenter(std::shared_ptr<VRODriver> driver) {
        return std::make_shared<VROInputPresenterWasm>();
    }

private:

    void updateOrientation(const VROCamera &camera);
    VROVector3f calculateCameraRay(float x, float y);

    VROCamera _latestCamera;
    VROMatrix4f _view;
    VROMatrix4f _projection;
    int _viewportWidth = 0;
    int _viewportHeight = 0;

};
#endif
