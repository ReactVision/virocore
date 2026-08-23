//
//  VROInputControllerWasm.cpp
//  ViroRenderer
//
//  Copyright © 2018 Viro Media. All rights reserved.
//
#include "VROInputControllerWasm.h"
#include "VROProjector.h"
#include "VROInputType.h"

VROVector3f VROInputControllerWasm::getDragForwardOffset() {
    // since the controller is the same as the camera, there's no offset.
    return VROVector3f();
}

void VROInputControllerWasm::onProcess(const VROCamera &camera) {
    _latestCamera = camera;
    updateOrientation(camera);
}

void VROInputControllerWasm::setRenderState(VROMatrix4f view, VROMatrix4f projection,
                                            int viewportWidth, int viewportHeight) {
    _view = view;
    _projection = projection;
    _viewportWidth = viewportWidth;
    _viewportHeight = viewportHeight;
}

VROVector3f VROInputControllerWasm::calculateCameraRay(float x, float y) {
    int viewport[4] = { 0, 0, _viewportWidth, _viewportHeight };
    VROMatrix4f mvp = _projection.multiply(_view);

    // DOM events use a top-left origin; GL/unproject expect bottom-left.
    float glY = (float) _viewportHeight - y;

    VROVector3f resultNear, resultFar;
    VROProjector::unproject(VROVector3f(x, glY, 0), mvp.getArray(), viewport, &resultNear);
    VROProjector::unproject(VROVector3f(x, glY, 1), mvp.getArray(), viewport, &resultFar);

    return (resultFar - resultNear).normalize();
}

void VROInputControllerWasm::onScreenTouch(int action, float x, float y) {
    if (_viewportWidth == 0 || _viewportHeight == 0) {
        return;
    }

    VROVector3f ray = calculateCameraRay(x, y);
    VROInputControllerBase::updateHitNode(_latestCamera, _latestCamera.getPosition(), ray);

    if (action == 0) {
        VROInputControllerBase::onButtonEvent(ViroCardBoard::ViewerButton,
                                              VROEventDelegate::ClickState::ClickDown);
    } else if (action == 2) {
        VROInputControllerBase::onButtonEvent(ViroCardBoard::ViewerButton,
                                              VROEventDelegate::ClickState::ClickUp);
    }
    // action == 1 (move): the hit node is refreshed above; drag/hover
    // propagation via onMove is a follow-up once components consume it.
}

void VROInputControllerWasm::updateScreenTouch(int touchAction) {
    // Legacy entry point: treat as a touch at the screen center.
    onScreenTouch(touchAction, _viewportWidth / 2.0f, _viewportHeight / 2.0f);
}

void VROInputControllerWasm::updateOrientation(const VROCamera &camera) {
    // Grab controller orientation
    VROQuaternion rotation = camera.getRotation();
    VROVector3f controllerForward = rotation.getMatrix().multiply(kBaseForward);

    // Perform hit test
    VROInputControllerBase::updateHitNode(camera, camera.getPosition(), controllerForward);

    // Process orientation and update delegates
    VROInputControllerBase::onMove(ViroCardBoard::InputSource::Controller, camera.getPosition(), rotation, controllerForward);
    VROInputControllerBase::processGazeEvent(ViroOculus::InputSource::Controller);
}
