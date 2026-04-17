// VROInputControllerOpenXR.h
// ViroRenderer
//
// OpenXR input controller for Meta Quest Touch / Touch Plus controllers.
// Uses the OpenXR action-based input system:
//   - Actions are declared at session creation time (createActionSet)
//   - Bindings are suggested for /interaction_profiles/oculus/touch_controller
//   - Per-frame: xrSyncActions → query poses, buttons, axes
//
// M2 coverage:
//   Right controller  → ViroOculus::Controller  (primary ray, hover, trigger click)
//   Left controller   → ViroOculus::LeftController (pose + trigger click)
//   Right grip        → ViroOculus::RightGrip   (click on ≥0.5 squeeze)
//   Left grip         → ViroOculus::LeftGrip
//   A button (right)  → ViroOculus::AButton
//   B button (right)  → ViroOculus::BackButton  (back navigation, same as menu)
//   X button (left)   → ViroOculus::XButton
//   Y button (left)   → ViroOculus::YButton
//   Menu (left)       → ViroOculus::BackButton
//   Right thumbstick  → ViroOculus::RightThumbstick via onScroll
//   Left thumbstick   → ViroOculus::LeftThumbstick  via onScroll
//   Haptics           → triggerHaptic(session, hand, amplitude, durationSec)
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
        : VROInputControllerBase(driver) {}

    virtual ~VROInputControllerOpenXR();

    /*
     * Called once after xrCreateSession. Creates the action set, declares all
     * actions, suggests interaction profile bindings, and calls
     * xrAttachSessionActionSets. Must be called before the session enters
     * XR_SESSION_STATE_READY.
     */
    bool createActionSet(XrInstance instance, XrSession session);

    /*
     * Destroy controller action spaces. Call before xrDestroySession.
     */
    void destroySpaces();

    /*
     * Initialize XR_EXT_hand_tracking. Loads function pointers, creates left/right
     * hand trackers. No-op (returns false) if the extension was not enabled at
     * instance creation time.
     *
     * @param aimExtAvail  true if XR_FB_hand_tracking_aim was also enabled
     */
    bool initHandTracking(XrInstance instance, XrSession session, bool aimExtAvail);

    /*
     * Destroy hand trackers. Call before xrDestroySession.
     */
    void destroyHandTrackers();

    /*
     * Called every frame after prepareFrame (VRORenderer already set camera).
     * Syncs actions and emits Viro input events.
     *
     * @param session    Active XrSession
     * @param baseSpace  Reference space used for pose location (stage/local)
     * @param time       Predicted display time from XrFrameState
     * @param camera     Current Viro camera (for gaze / forward direction)
     */
    void onProcess(XrSession session, XrSpace baseSpace,
                   XrTime time, const VROCamera &camera);

    /*
     * Trigger haptic feedback on the given hand (0 = left, 1 = right).
     * No-op if the vibration action was not successfully created.
     */
    void triggerHaptic(XrSession session, int hand,
                       float amplitude = 0.5f, float durationSec = 0.05f);

    VROVector3f getDragForwardOffset() override;

    std::string getHeadset()    override { return "quest"; }
    std::string getController() override { return "touch"; }

protected:
    std::shared_ptr<VROInputPresenter> createPresenter(
        std::shared_ptr<VRODriver> driver) override;

private:
    // ── Action set ────────────────────────────────────────────────────────────
    XrActionSet _actionSet = XR_NULL_HANDLE;

    // ── Aim pose actions (one per hand) ──────────────────────────────────────
    XrAction _leftAimPoseAction  = XR_NULL_HANDLE;
    XrAction _rightAimPoseAction = XR_NULL_HANDLE;

    // ── Trigger (float, per hand — click detected at ≥0.5) ───────────────────
    XrAction _leftTriggerAction  = XR_NULL_HANDLE;
    XrAction _rightTriggerAction = XR_NULL_HANDLE;

    // ── Grip / squeeze (float, per hand — click detected at ≥0.5) ────────────
    XrAction _leftGripAction  = XR_NULL_HANDLE;
    XrAction _rightGripAction = XR_NULL_HANDLE;

    // ── Face buttons ─────────────────────────────────────────────────────────
    XrAction _aButtonAction = XR_NULL_HANDLE;  // right hand A
    XrAction _bButtonAction = XR_NULL_HANDLE;  // right hand B  (→ BackButton)
    XrAction _xButtonAction = XR_NULL_HANDLE;  // left  hand X
    XrAction _yButtonAction = XR_NULL_HANDLE;  // left  hand Y
    XrAction _menuAction    = XR_NULL_HANDLE;  // left  hand Menu (→ BackButton)

    // ── Thumbstick axes (vector2f, per hand) ─────────────────────────────────
    XrAction _leftThumbstickAction  = XR_NULL_HANDLE;
    XrAction _rightThumbstickAction = XR_NULL_HANDLE;

    // ── Haptic output (one per hand) ─────────────────────────────────────────
    XrAction _leftVibrateAction  = XR_NULL_HANDLE;
    XrAction _rightVibrateAction = XR_NULL_HANDLE;

    // ── Action spaces for aim poses ───────────────────────────────────────────
    XrSpace _leftSpace  = XR_NULL_HANDLE;
    XrSpace _rightSpace = XR_NULL_HANDLE;

    // ── Edge-detection state (previous frame) ────────────────────────────────
    bool _prevTriggerLeft  = false;
    bool _prevTriggerRight = false;
    bool _prevGripLeft     = false;
    bool _prevGripRight    = false;
    bool _prevAButton      = false;
    bool _prevBButton      = false;
    bool _prevXButton      = false;
    bool _prevYButton      = false;
    bool _prevMenuButton   = false;

    // ── Hand tracking (XR_EXT_hand_tracking) ─────────────────────────────────
    PFN_xrCreateHandTrackerEXT  _pfnCreateHandTracker  = nullptr;
    PFN_xrDestroyHandTrackerEXT _pfnDestroyHandTracker = nullptr;
    PFN_xrLocateHandJointsEXT   _pfnLocateHandJoints   = nullptr;

    XrHandTrackerEXT _leftHandTracker  = XR_NULL_HANDLE;
    XrHandTrackerEXT _rightHandTracker = XR_NULL_HANDLE;
    bool             _aimExtEnabled    = false;

    // Per-hand gesture state (edge detection)
    bool _prevPinchLeft  = false;
    bool _prevPinchRight = false;
    bool _prevGrabLeft   = false;
    bool _prevGrabRight  = false;

    // ── Private helpers ───────────────────────────────────────────────────────
    XrAction createAction(XrActionSet actionSet, XrActionType type,
                          const char *name, const char *localizedName);
    bool createActionSpaces(XrSession session);
    void processHands(XrSpace baseSpace, XrTime time, const VROCamera &camera);
};

#endif  // ANDROID_VROINPUTCONTROLLEROPENXR_H
