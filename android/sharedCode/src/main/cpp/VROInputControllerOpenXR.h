// VROInputControllerOpenXR.h
// ViroRenderer
//
// OpenXR input controller for Meta Quest Touch / Touch Pro controllers.
// Uses the OpenXR action-based input system:
//   - Actions are declared at session creation time (createActionSet)
//   - Bindings are suggested for the oculus/touch_controller interaction profile
//   - Per-frame: xrSyncActions → query poses + button state
//
// Wiring to the render loop (Week 3 / M2):
//   1. After xrCreateSession: call createActionSet(session)
//   2. Per frame (after xrSyncActions): call onProcess(camera)
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#ifndef ANDROID_VROINPUTCONTROLLEROPENXR_H
#define ANDROID_VROINPUTCONTROLLEROPENXR_H

#include <memory>
#include <openxr/openxr.h>
#include "VROInputControllerBase.h"

class VROInputControllerOpenXR : public VROInputControllerBase {
public:
    explicit VROInputControllerOpenXR(std::shared_ptr<VRODriver> driver)
        : VROInputControllerBase(driver),
          _actionSet(XR_NULL_HANDLE),
          _leftAimPoseAction(XR_NULL_HANDLE),
          _rightAimPoseAction(XR_NULL_HANDLE),
          _selectAction(XR_NULL_HANDLE),
          _menuAction(XR_NULL_HANDLE),
          _triggerValueAction(XR_NULL_HANDLE),
          _squeezeValueAction(XR_NULL_HANDLE),
          _thumbstickAction(XR_NULL_HANDLE),
          _vibrateAction(XR_NULL_HANDLE),
          _leftSpace(XR_NULL_HANDLE),
          _rightSpace(XR_NULL_HANDLE) {
    }

    virtual ~VROInputControllerOpenXR();

    /*
     * Called once after xrCreateSession. Creates the action set, declares all
     * actions, suggests interaction profile bindings, and calls
     * xrAttachSessionActionSets. Must be called before the session enters
     * XR_SESSION_STATE_READY.
     */
    bool createActionSet(XrInstance instance, XrSession session);

    /*
     * Destroy action spaces. Call before xrDestroySession.
     */
    void destroySpaces();

    /*
     * Called every frame after xrSyncActions. Queries controller poses and
     * button state, emits Viro input events.
     *
     * @param session    Active XrSession
     * @param baseSpace  Stage/local reference space for pose location
     * @param time       Predicted display time from XrFrameState
     * @param camera     Current Viro camera (for gaze / forward direction)
     */
    void onProcess(XrSession session, XrSpace baseSpace,
                   XrTime time, const VROCamera &camera);

    /*
     * Trigger haptic feedback on the given hand (0 = left, 1 = right).
     */
    void triggerHaptic(XrSession session, int hand,
                       float amplitude = 0.5f, float durationSec = 0.05f);

    VROVector3f getDragForwardOffset() override;

    std::string getHeadset() override  { return "quest"; }
    std::string getController() override { return "touch"; }

protected:
    std::shared_ptr<VROInputPresenter> createPresenter(std::shared_ptr<VRODriver> driver) override;

private:
    XrActionSet _actionSet;

    // Pose actions (aim ray origin + orientation)
    XrAction _leftAimPoseAction;
    XrAction _rightAimPoseAction;

    // Boolean actions
    XrAction _selectAction;   // trigger click (maps to Viro CLICK_DOWN/UP)
    XrAction _menuAction;     // menu / B button

    // Float actions
    XrAction _triggerValueAction;
    XrAction _squeezeValueAction;
    XrAction _thumbstickAction;   // XrVector2f

    // Haptic output
    XrAction _vibrateAction;

    // Action spaces for each controller (aim pose)
    XrSpace _leftSpace;
    XrSpace _rightSpace;

    // Last known button state (for edge detection)
    bool _prevSelectLeft  = false;
    bool _prevSelectRight = false;

    XrAction createAction(XrActionSet actionSet, XrActionType type,
                          const char *name, const char *localizedName);
    bool createActionSpaces(XrSession session);
};

#endif  // ANDROID_VROINPUTCONTROLLEROPENXR_H
