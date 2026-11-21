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

    // Top-left origin in, as DOM events arrive: VROProjector::unproject flips Y
    // itself, the same convention VROInputControllerAR relies on. Flipping here
    // too cancelled that out and mirrored every tap about the horizontal midline.
    VROVector3f resultNear, resultFar;
    VROProjector::unproject(VROVector3f(x, y, 0), mvp.getArray(), viewport, &resultNear);
    VROProjector::unproject(VROVector3f(x, y, 1), mvp.getArray(), viewport, &resultFar);

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
    } else if (action == 3) {
        VROInputControllerBase::cancelButtonEvent(ViroCardBoard::ViewerButton);
    }
    // action == 1 (move): the hit node is refreshed above; drag/hover
    // propagation via onMove is a follow-up once components consume it.
}

// The start hit-tests at the midpoint and starts the gesture in one call:
// updateOrientation replaces the hit with the gaze ray every frame.
void VROInputControllerWasm::onPinch(int state, float scale, float x, float y) {
    if (state == VROEventDelegate::PinchState::PinchStart) {
        if (_viewportWidth == 0 || _viewportHeight == 0) {
            return;
        }
        VROInputControllerBase::updateHitNode(_latestCamera, _latestCamera.getPosition(), calculateCameraRay(x, y));
    }
    VROInputControllerBase::onPinch(ViroCardBoard::InputSource::Controller, scale,
                                    (VROEventDelegate::PinchState) state);
}

void VROInputControllerWasm::onRotate(int state, float radians, float x, float y) {
    if (state == VROEventDelegate::RotateState::RotateStart) {
        if (_viewportWidth == 0 || _viewportHeight == 0) {
            return;
        }
        VROInputControllerBase::updateHitNode(_latestCamera, _latestCamera.getPosition(), calculateCameraRay(x, y));
    }
    VROInputControllerBase::onRotate(ViroCardBoard::InputSource::Controller, radians,
                                     (VROEventDelegate::RotateState) state);
}

void VROInputControllerWasm::setPointerHover(float x, float y) {
    _pointerHover = true;
    _pointerX = x;
    _pointerY = y;
}

void VROInputControllerWasm::clearPointerHover() {
    _pointerHover = false;
}

void VROInputControllerWasm::updateScreenTouch(int touchAction) {
    // Legacy entry point: treat as a touch at the screen center.
    onScreenTouch(touchAction, _viewportWidth / 2.0f, _viewportHeight / 2.0f);
}

void VROInputControllerWasm::updateOrientation(const VROCamera &camera) {
    // Grab controller orientation
    VROQuaternion rotation = camera.getRotation();
    VROVector3f controllerForward = rotation.getMatrix().multiply(kBaseForward);

    // Perform hit test. Re-cast every frame, not only on mouse moves, so the
    // hover follows a camera that moves under a still mouse. Fuse reads the
    // same hit, so on desktop it follows the mouse too.
    VROVector3f hitRay = controllerForward;
    if (_pointerHover && _viewportWidth > 0 && _viewportHeight > 0) {
        hitRay = calculateCameraRay(_pointerX, _pointerY);
    }
    VROInputControllerBase::updateHitNode(camera, camera.getPosition(), hitRay);

    // Process orientation and update delegates
    VROInputControllerBase::onMove(ViroCardBoard::InputSource::Controller, camera.getPosition(), rotation, controllerForward);
    VROInputControllerBase::processGazeEvent(ViroOculus::InputSource::Controller);
}
