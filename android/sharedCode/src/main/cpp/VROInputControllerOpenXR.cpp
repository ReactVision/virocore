// VROInputControllerOpenXR.cpp
// ViroRenderer
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#include "VROInputControllerOpenXR.h"
#include <android/log.h>
#include <cmath>
#include "VROLog.h"
#include "VROInputPresenterOpenXR.h"
#include "VROVector3f.h"
#include "VROCamera.h"
#include "VROInputType.h"

#undef  LOG_TAG
#define LOG_TAG "VROInputOpenXR"
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR,   LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN,    LOG_TAG, __VA_ARGS__)
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)

static constexpr float kTriggerThreshold   = 0.5f;
static constexpr float kGripThreshold      = 0.5f;
static constexpr float kThumbstickDeadZone = 0.15f;

// ──────────────────────────────────────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────────────────────────────────────

static VROVector3f xrVec3ToVRO(XrVector3f v) {
    return VROVector3f(v.x, v.y, v.z);
}

static VROQuaternion xrQuatToVRO(XrQuaternionf q) {
    return VROQuaternion(q.x, q.y, q.z, q.w);
}

static VROMatrix4f xrPoseToMatrix(const XrPosef &pose) {
    VROQuaternion q(pose.orientation.x, pose.orientation.y,
                    pose.orientation.z, pose.orientation.w);
    VROMatrix4f rot = q.getMatrix();
    rot[12] = pose.position.x;
    rot[13] = pose.position.y;
    rot[14] = pose.position.z;
    return rot;
}

// Derive the aim-forward direction (-Z column of the pose rotation matrix).
// OpenXR right-handed: -Z is the "look" direction.
static VROVector3f xrAimForward(const XrPosef &pose) {
    VROMatrix4f m = xrPoseToMatrix(pose);
    VROVector3f fwd(-m[8], -m[9], -m[10]);
    fwd.normalize();
    return fwd;
}

// ──────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ──────────────────────────────────────────────────────────────────────────────

VROInputControllerOpenXR::~VROInputControllerOpenXR() {
    destroySpaces();
    if (_actionSet != XR_NULL_HANDLE) {
        xrDestroyActionSet(_actionSet);
        _actionSet = XR_NULL_HANDLE;
    }
}

bool VROInputControllerOpenXR::createActionSet(XrInstance instance, XrSession session) {
    // ── 1. Create action set ──────────────────────────────────────────────────
    XrActionSetCreateInfo asInfo = { XR_TYPE_ACTION_SET_CREATE_INFO };
    strncpy(asInfo.actionSetName,          "viro_gameplay", XR_MAX_ACTION_SET_NAME_SIZE);
    strncpy(asInfo.localizedActionSetName, "Viro Gameplay", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE);
    asInfo.priority = 0;

    XrResult result = xrCreateActionSet(instance, &asInfo, &_actionSet);
    if (!XR_SUCCEEDED(result)) {
        ALOGE("xrCreateActionSet failed: %d", result);
        return false;
    }

    // ── 2. Declare all actions ────────────────────────────────────────────────
    _leftAimPoseAction  = createAction(_actionSet, XR_ACTION_TYPE_POSE_INPUT,
                                       "left_aim",  "Left Aim Pose");
    _rightAimPoseAction = createAction(_actionSet, XR_ACTION_TYPE_POSE_INPUT,
                                       "right_aim", "Right Aim Pose");

    _leftTriggerAction  = createAction(_actionSet, XR_ACTION_TYPE_FLOAT_INPUT,
                                       "left_trigger",  "Left Trigger");
    _rightTriggerAction = createAction(_actionSet, XR_ACTION_TYPE_FLOAT_INPUT,
                                       "right_trigger", "Right Trigger");

    _leftGripAction  = createAction(_actionSet, XR_ACTION_TYPE_FLOAT_INPUT,
                                    "left_grip",  "Left Grip");
    _rightGripAction = createAction(_actionSet, XR_ACTION_TYPE_FLOAT_INPUT,
                                    "right_grip", "Right Grip");

    _aButtonAction = createAction(_actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT,
                                  "a_button", "A Button");
    _bButtonAction = createAction(_actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT,
                                  "b_button", "B Button");
    _xButtonAction = createAction(_actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT,
                                  "x_button", "X Button");
    _yButtonAction = createAction(_actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT,
                                  "y_button", "Y Button");
    _menuAction    = createAction(_actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT,
                                  "menu",     "Menu");

    _leftThumbstickAction  = createAction(_actionSet, XR_ACTION_TYPE_VECTOR2F_INPUT,
                                          "left_thumbstick",  "Left Thumbstick");
    _rightThumbstickAction = createAction(_actionSet, XR_ACTION_TYPE_VECTOR2F_INPUT,
                                          "right_thumbstick", "Right Thumbstick");

    _leftVibrateAction  = createAction(_actionSet, XR_ACTION_TYPE_VIBRATION_OUTPUT,
                                       "vibrate_left",  "Vibrate Left");
    _rightVibrateAction = createAction(_actionSet, XR_ACTION_TYPE_VIBRATION_OUTPUT,
                                       "vibrate_right", "Vibrate Right");

    if (!_leftAimPoseAction  || !_rightAimPoseAction ||
        !_leftTriggerAction  || !_rightTriggerAction ||
        !_leftGripAction     || !_rightGripAction    ||
        !_aButtonAction      || !_bButtonAction      ||
        !_xButtonAction      || !_yButtonAction      || !_menuAction ||
        !_leftThumbstickAction || !_rightThumbstickAction ||
        !_leftVibrateAction  || !_rightVibrateAction) {
        ALOGE("One or more OpenXR actions failed to create");
        return false;
    }

    // ── 3. Suggest interaction profile bindings ───────────────────────────────
    // Covers Touch controllers (Quest 2) and Touch Plus (Quest 3 default).
    XrPath touchProfile;
    xrStringToPath(instance, "/interaction_profiles/oculus/touch_controller", &touchProfile);

    auto makePath = [&](const char *str) -> XrPath {
        XrPath p;
        xrStringToPath(instance, str, &p);
        return p;
    };

    const XrActionSuggestedBinding bindings[] = {
        { _leftAimPoseAction,     makePath("/user/hand/left/input/aim/pose")       },
        { _rightAimPoseAction,    makePath("/user/hand/right/input/aim/pose")      },
        { _leftTriggerAction,     makePath("/user/hand/left/input/trigger/value")  },
        { _rightTriggerAction,    makePath("/user/hand/right/input/trigger/value") },
        { _leftGripAction,        makePath("/user/hand/left/input/squeeze/value")  },
        { _rightGripAction,       makePath("/user/hand/right/input/squeeze/value") },
        { _aButtonAction,         makePath("/user/hand/right/input/a/click")       },
        { _bButtonAction,         makePath("/user/hand/right/input/b/click")       },
        { _xButtonAction,         makePath("/user/hand/left/input/x/click")        },
        { _yButtonAction,         makePath("/user/hand/left/input/y/click")        },
        { _menuAction,            makePath("/user/hand/left/input/menu/click")     },
        { _leftThumbstickAction,  makePath("/user/hand/left/input/thumbstick")     },
        { _rightThumbstickAction, makePath("/user/hand/right/input/thumbstick")    },
        { _leftVibrateAction,     makePath("/user/hand/left/output/haptic")        },
        { _rightVibrateAction,    makePath("/user/hand/right/output/haptic")       },
    };

    XrInteractionProfileSuggestedBinding suggestion = {
        XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING
    };
    suggestion.interactionProfile     = touchProfile;
    suggestion.suggestedBindings      = bindings;
    suggestion.countSuggestedBindings = (uint32_t)(sizeof(bindings) / sizeof(bindings[0]));

    result = xrSuggestInteractionProfileBindings(instance, &suggestion);
    if (!XR_SUCCEEDED(result)) {
        ALOGE("xrSuggestInteractionProfileBindings failed: %d", result);
        return false;
    }

    // ── 4. Attach action set to session ───────────────────────────────────────
    XrSessionActionSetsAttachInfo attachInfo = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    attachInfo.actionSets      = &_actionSet;
    attachInfo.countActionSets = 1;

    result = xrAttachSessionActionSets(session, &attachInfo);
    if (!XR_SUCCEEDED(result)) {
        ALOGE("xrAttachSessionActionSets failed: %d", result);
        return false;
    }

    // ── 5. Create action spaces for aim poses ─────────────────────────────────
    if (!createActionSpaces(session)) {
        return false;
    }

    ALOGV("OpenXR action set created and attached (M2 — full dual-controller)");
    return true;
}

void VROInputControllerOpenXR::destroySpaces() {
    if (_leftSpace  != XR_NULL_HANDLE) { xrDestroySpace(_leftSpace);  _leftSpace  = XR_NULL_HANDLE; }
    if (_rightSpace != XR_NULL_HANDLE) { xrDestroySpace(_rightSpace); _rightSpace = XR_NULL_HANDLE; }
}

bool VROInputControllerOpenXR::createActionSpaces(XrSession session) {
    XrActionSpaceCreateInfo spaceInfo = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
    spaceInfo.poseInActionSpace = { {0, 0, 0, 1}, {0, 0, 0} };  // identity

    spaceInfo.action = _leftAimPoseAction;
    XrResult r = xrCreateActionSpace(session, &spaceInfo, &_leftSpace);
    if (!XR_SUCCEEDED(r)) { ALOGE("xrCreateActionSpace left failed: %d", r); return false; }

    spaceInfo.action = _rightAimPoseAction;
    r = xrCreateActionSpace(session, &spaceInfo, &_rightSpace);
    if (!XR_SUCCEEDED(r)) { ALOGE("xrCreateActionSpace right failed: %d", r); return false; }

    return true;
}

XrAction VROInputControllerOpenXR::createAction(XrActionSet actionSet,
                                                  XrActionType type,
                                                  const char *name,
                                                  const char *localizedName) {
    XrActionCreateInfo info = { XR_TYPE_ACTION_CREATE_INFO };
    info.actionType = type;
    strncpy(info.actionName,          name,          XR_MAX_ACTION_NAME_SIZE);
    strncpy(info.localizedActionName, localizedName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE);
    info.countSubactionPaths = 0;

    XrAction action = XR_NULL_HANDLE;
    XrResult r = xrCreateAction(actionSet, &info, &action);
    if (!XR_SUCCEEDED(r)) {
        ALOGE("xrCreateAction '%s' failed: %d", name, r);
        return XR_NULL_HANDLE;
    }
    return action;
}

// ──────────────────────────────────────────────────────────────────────────────
// Per-frame processing
// ──────────────────────────────────────────────────────────────────────────────

void VROInputControllerOpenXR::onProcess(XrSession session, XrSpace baseSpace,
                                          XrTime time, const VROCamera &camera) {
    // ── Sync active action set ────────────────────────────────────────────────
    XrActiveActionSet activeSet = { _actionSet, XR_NULL_PATH };
    XrActionsSyncInfo syncInfo  = { XR_TYPE_ACTIONS_SYNC_INFO };
    syncInfo.activeActionSets      = &activeSet;
    syncInfo.countActiveActionSets = 1;
    xrSyncActions(session, &syncInfo);

    // ── Capture poses for both hands (no dispatch yet) ───────────────────────
    // Each hand can be supplied by either a held controller or by tracked
    // hand joints. We probe controllers first, and the hand-tracking step
    // below fills in any side that didn't get a controller pose. All
    // hit-test / processGazeEvent / laser-viz dispatches happen in a single
    // pass at end-of-frame, once per source — preventing the dual-dispatch
    // hover oscillation that the previous single-aim implementation hit.
    bool          rightValid   = false;
    VROVector3f   rightPos;
    VROQuaternion rightRot;
    VROVector3f   rightForward;
    if (_rightSpace != XR_NULL_HANDLE) {
        XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
        XrResult r = xrLocateSpace(_rightSpace, baseSpace, time, &loc);
        if (XR_SUCCEEDED(r) &&
            (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
            (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
            rightPos     = xrVec3ToVRO(loc.pose.position);
            rightRot     = xrQuatToVRO(loc.pose.orientation);
            rightForward = xrAimForward(loc.pose);
            rightValid   = true;
        }
    }

    bool          leftValid   = false;
    VROVector3f   leftPos;
    VROQuaternion leftRot;
    VROVector3f   leftForward;
    if (_leftSpace != XR_NULL_HANDLE) {
        XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
        XrResult r = xrLocateSpace(_leftSpace, baseSpace, time, &loc);
        if (XR_SUCCEEDED(r) &&
            (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
            (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
            leftPos     = xrVec3ToVRO(loc.pose.position);
            leftRot     = xrQuatToVRO(loc.pose.orientation);
            leftForward = xrAimForward(loc.pose);
            leftValid   = true;
        }
    }

    // Apply per-source hysteresis (B18) so a single bad probe doesn't flip
    // the aim source over to hand tracking and back, which previously caused
    // hover ENTER/EXIT oscillation.
    rightValid = stickyPose(_rightCtrlAim, rightValid, rightPos, rightRot, rightForward);
    leftValid  = stickyPose(_leftCtrlAim,  leftValid,  leftPos,  leftRot,  leftForward);

    // ── Right trigger ─────────────────────────────────────────────────────────
    {
        XrActionStateFloat state   = { XR_TYPE_ACTION_STATE_FLOAT };
        XrActionStateGetInfo info  = { XR_TYPE_ACTION_STATE_GET_INFO };
        info.action = _rightTriggerAction;
        xrGetActionStateFloat(session, &info, &state);
        if (state.isActive) {
            bool pressed = (state.currentState >= kTriggerThreshold);
            if (pressed && !_prevTriggerRight)
                VROInputControllerBase::onButtonEvent(ViroOculus::Controller, VROEventDelegate::ClickState::ClickDown);
            else if (!pressed && _prevTriggerRight)
                VROInputControllerBase::onButtonEvent(ViroOculus::Controller, VROEventDelegate::ClickState::ClickUp);
            _prevTriggerRight = pressed;
        }
    }

    // ── Left trigger ──────────────────────────────────────────────────────────
    {
        XrActionStateFloat state   = { XR_TYPE_ACTION_STATE_FLOAT };
        XrActionStateGetInfo info  = { XR_TYPE_ACTION_STATE_GET_INFO };
        info.action = _leftTriggerAction;
        xrGetActionStateFloat(session, &info, &state);
        if (state.isActive) {
            bool pressed = (state.currentState >= kTriggerThreshold);
            if (pressed && !_prevTriggerLeft)
                VROInputControllerBase::onButtonEvent(ViroOculus::LeftController, VROEventDelegate::ClickState::ClickDown);
            else if (!pressed && _prevTriggerLeft)
                VROInputControllerBase::onButtonEvent(ViroOculus::LeftController, VROEventDelegate::ClickState::ClickUp);
            _prevTriggerLeft = pressed;
        }
    }

    // ── Right grip ────────────────────────────────────────────────────────────
    {
        XrActionStateFloat state   = { XR_TYPE_ACTION_STATE_FLOAT };
        XrActionStateGetInfo info  = { XR_TYPE_ACTION_STATE_GET_INFO };
        info.action = _rightGripAction;
        xrGetActionStateFloat(session, &info, &state);
        if (state.isActive) {
            bool pressed = (state.currentState >= kGripThreshold);
            if (pressed && !_prevGripRight)
                VROInputControllerBase::onButtonEvent(ViroOculus::RightGrip, VROEventDelegate::ClickState::ClickDown);
            else if (!pressed && _prevGripRight)
                VROInputControllerBase::onButtonEvent(ViroOculus::RightGrip, VROEventDelegate::ClickState::ClickUp);
            _prevGripRight = pressed;
        }
    }

    // ── Left grip ─────────────────────────────────────────────────────────────
    {
        XrActionStateFloat state   = { XR_TYPE_ACTION_STATE_FLOAT };
        XrActionStateGetInfo info  = { XR_TYPE_ACTION_STATE_GET_INFO };
        info.action = _leftGripAction;
        xrGetActionStateFloat(session, &info, &state);
        if (state.isActive) {
            bool pressed = (state.currentState >= kGripThreshold);
            if (pressed && !_prevGripLeft)
                VROInputControllerBase::onButtonEvent(ViroOculus::LeftGrip, VROEventDelegate::ClickState::ClickDown);
            else if (!pressed && _prevGripLeft)
                VROInputControllerBase::onButtonEvent(ViroOculus::LeftGrip, VROEventDelegate::ClickState::ClickUp);
            _prevGripLeft = pressed;
        }
    }

    // ── A button (right hand) ─────────────────────────────────────────────────
    {
        XrActionStateBoolean state  = { XR_TYPE_ACTION_STATE_BOOLEAN };
        XrActionStateGetInfo info   = { XR_TYPE_ACTION_STATE_GET_INFO };
        info.action = _aButtonAction;
        xrGetActionStateBoolean(session, &info, &state);
        if (state.isActive) {
            bool pressed = (state.currentState == XR_TRUE);
            if (pressed && !_prevAButton)
                VROInputControllerBase::onButtonEvent(ViroOculus::AButton, VROEventDelegate::ClickState::ClickDown);
            else if (!pressed && _prevAButton)
                VROInputControllerBase::onButtonEvent(ViroOculus::AButton, VROEventDelegate::ClickState::ClickUp);
            _prevAButton = pressed;
        }
    }

    // ── B button (right hand → BackButton) ───────────────────────────────────
    {
        XrActionStateBoolean state  = { XR_TYPE_ACTION_STATE_BOOLEAN };
        XrActionStateGetInfo info   = { XR_TYPE_ACTION_STATE_GET_INFO };
        info.action = _bButtonAction;
        xrGetActionStateBoolean(session, &info, &state);
        if (state.isActive) {
            bool pressed = (state.currentState == XR_TRUE);
            if (pressed && !_prevBButton)
                VROInputControllerBase::onButtonEvent(ViroOculus::BackButton, VROEventDelegate::ClickState::ClickDown);
            else if (!pressed && _prevBButton)
                VROInputControllerBase::onButtonEvent(ViroOculus::BackButton, VROEventDelegate::ClickState::ClickUp);
            _prevBButton = pressed;
        }
    }

    // ── X button (left hand) ─────────────────────────────────────────────────
    {
        XrActionStateBoolean state  = { XR_TYPE_ACTION_STATE_BOOLEAN };
        XrActionStateGetInfo info   = { XR_TYPE_ACTION_STATE_GET_INFO };
        info.action = _xButtonAction;
        xrGetActionStateBoolean(session, &info, &state);
        if (state.isActive) {
            bool pressed = (state.currentState == XR_TRUE);
            if (pressed && !_prevXButton)
                VROInputControllerBase::onButtonEvent(ViroOculus::XButton, VROEventDelegate::ClickState::ClickDown);
            else if (!pressed && _prevXButton)
                VROInputControllerBase::onButtonEvent(ViroOculus::XButton, VROEventDelegate::ClickState::ClickUp);
            _prevXButton = pressed;
        }
    }

    // ── Y button (left hand) ─────────────────────────────────────────────────
    {
        XrActionStateBoolean state  = { XR_TYPE_ACTION_STATE_BOOLEAN };
        XrActionStateGetInfo info   = { XR_TYPE_ACTION_STATE_GET_INFO };
        info.action = _yButtonAction;
        xrGetActionStateBoolean(session, &info, &state);
        if (state.isActive) {
            bool pressed = (state.currentState == XR_TRUE);
            if (pressed && !_prevYButton)
                VROInputControllerBase::onButtonEvent(ViroOculus::YButton, VROEventDelegate::ClickState::ClickDown);
            else if (!pressed && _prevYButton)
                VROInputControllerBase::onButtonEvent(ViroOculus::YButton, VROEventDelegate::ClickState::ClickUp);
            _prevYButton = pressed;
        }
    }

    // ── Menu button (left hand → BackButton) ─────────────────────────────────
    {
        XrActionStateBoolean state  = { XR_TYPE_ACTION_STATE_BOOLEAN };
        XrActionStateGetInfo info   = { XR_TYPE_ACTION_STATE_GET_INFO };
        info.action = _menuAction;
        xrGetActionStateBoolean(session, &info, &state);
        if (state.isActive) {
            bool pressed = (state.currentState == XR_TRUE);
            if (pressed && !_prevMenuButton)
                VROInputControllerBase::onButtonEvent(ViroOculus::BackButton, VROEventDelegate::ClickState::ClickDown);
            else if (!pressed && _prevMenuButton)
                VROInputControllerBase::onButtonEvent(ViroOculus::BackButton, VROEventDelegate::ClickState::ClickUp);
            _prevMenuButton = pressed;
        }
    }

    // ── Right thumbstick → scroll events ─────────────────────────────────────
    {
        XrActionStateVector2f state  = { XR_TYPE_ACTION_STATE_VECTOR2F };
        XrActionStateGetInfo  info   = { XR_TYPE_ACTION_STATE_GET_INFO };
        info.action = _rightThumbstickAction;
        xrGetActionStateVector2f(session, &info, &state);
        if (state.isActive) {
            float x = state.currentState.x;
            float y = state.currentState.y;
            if (sqrtf(x * x + y * y) > kThumbstickDeadZone) {
                VROInputControllerBase::onScroll(ViroOculus::RightThumbstick, x, y);
            }
        }
    }

    // ── Left thumbstick → scroll events ──────────────────────────────────────
    {
        XrActionStateVector2f state  = { XR_TYPE_ACTION_STATE_VECTOR2F };
        XrActionStateGetInfo  info   = { XR_TYPE_ACTION_STATE_GET_INFO };
        info.action = _leftThumbstickAction;
        xrGetActionStateVector2f(session, &info, &state);
        if (state.isActive) {
            float x = state.currentState.x;
            float y = state.currentState.y;
            if (sqrtf(x * x + y * y) > kThumbstickDeadZone) {
                VROInputControllerBase::onScroll(ViroOculus::LeftThumbstick, x, y);
            }
        }
    }

    // ── Hand tracking — gestures + fill missing-side aim from joints ────────
    bool          rightHandValid = false;
    VROVector3f   rightHandPos;
    VROQuaternion rightHandRot;
    VROVector3f   rightHandForward;
    bool          leftHandValid  = false;
    VROVector3f   leftHandPos;
    VROQuaternion leftHandRot;
    VROVector3f   leftHandForward;
    processHands(baseSpace, time, camera,
                 /*skipRight=*/rightValid, /*skipLeft=*/leftValid,
                 rightHandValid, rightHandPos, rightHandRot, rightHandForward,
                 leftHandValid,  leftHandPos,  leftHandRot,  leftHandForward);

    // Hysteresis on hand-tracking poses too — they flicker more than
    // controllers (joint visibility depends on hand orientation).
    rightHandValid = stickyPose(_rightHandAim, rightHandValid,
                                rightHandPos, rightHandRot, rightHandForward);
    leftHandValid  = stickyPose(_leftHandAim,  leftHandValid,
                                leftHandPos,  leftHandRot,  leftHandForward);

    // Fill in any side that controller didn't provide, from hand tracking.
    if (!rightValid && rightHandValid) {
        rightPos     = rightHandPos;
        rightRot     = rightHandRot;
        rightForward = rightHandForward;
        rightValid   = true;
    }
    if (!leftValid && leftHandValid) {
        leftPos     = leftHandPos;
        leftRot     = leftHandRot;
        leftForward = leftHandForward;
        leftValid   = true;
    }

    // ── Per-source dispatch — both hands handled independently ──────────────
    // Each side calls the source-aware updateHitNode so its hit/hover/click
    // state is tracked separately in VROInputControllerBase. Pinch / trigger
    // edge events emitted earlier in this frame already carry the correct
    // source, so onButtonEvent will resolve against this side's hit result.
    auto dispatchSide = [this, &camera](
        bool valid, int source,
        const VROVector3f &pos, const VROQuaternion &rot, const VROVector3f &fwd) {
        if (valid) {
            VROInputControllerBase::updateHitNode(source, camera, pos, fwd);
            VROInputControllerBase::onMove(source, pos, rot, fwd);
            VROInputControllerBase::processGazeEvent(source);
            updateLaserViz(source, pos, fwd, /*visible=*/true);
        } else {
            updateLaserViz(source, {}, {}, /*visible=*/false);
        }
    };
    dispatchSide(rightValid, ViroOculus::Controller,     rightPos, rightRot, rightForward);
    dispatchSide(leftValid,  ViroOculus::LeftController, leftPos,  leftRot,  leftForward);
}

// ──────────────────────────────────────────────────────────────────────────────
// Haptic feedback
// ──────────────────────────────────────────────────────────────────────────────

void VROInputControllerOpenXR::triggerHaptic(XrSession session, int hand,
                                               float amplitude, float durationSec) {
    XrAction vibrateAction = (hand == 0) ? _leftVibrateAction : _rightVibrateAction;
    if (vibrateAction == XR_NULL_HANDLE) return;

    XrHapticVibration vibration = { XR_TYPE_HAPTIC_VIBRATION };
    vibration.amplitude  = amplitude;
    vibration.duration   = (XrDuration)(durationSec * 1e9f);  // seconds → nanoseconds
    vibration.frequency  = XR_FREQUENCY_UNSPECIFIED;

    XrHapticActionInfo info = { XR_TYPE_HAPTIC_ACTION_INFO };
    info.action = vibrateAction;
    xrApplyHapticFeedback(session, &info, (const XrHapticBaseHeader *)&vibration);
}

// ──────────────────────────────────────────────────────────────────────────────
// Hand tracking  (XR_EXT_hand_tracking + XR_FB_hand_tracking_aim)
// ──────────────────────────────────────────────────────────────────────────────

bool VROInputControllerOpenXR::initHandTracking(XrInstance instance, XrSession session,
                                                 bool aimExtAvail) {
    auto loadFn = [&](const char *name, void **fn) -> bool {
        XrResult r = xrGetInstanceProcAddr(instance, name, (PFN_xrVoidFunction *)fn);
        return XR_SUCCEEDED(r) && (*fn != nullptr);
    };

    if (!loadFn("xrCreateHandTrackerEXT",  (void **)&_pfnCreateHandTracker)  ||
        !loadFn("xrDestroyHandTrackerEXT", (void **)&_pfnDestroyHandTracker) ||
        !loadFn("xrLocateHandJointsEXT",   (void **)&_pfnLocateHandJoints)) {
        ALOGW("XR_EXT_hand_tracking functions not found — hand tracking disabled");
        return false;
    }

    XrHandTrackerCreateInfoEXT info = { XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT };
    info.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;

    info.hand = XR_HAND_LEFT_EXT;
    XrResult r = _pfnCreateHandTracker(session, &info, &_leftHandTracker);
    if (XR_FAILED(r)) {
        ALOGE("xrCreateHandTrackerEXT (left) failed: %d", r);
        return false;
    }

    info.hand = XR_HAND_RIGHT_EXT;
    r = _pfnCreateHandTracker(session, &info, &_rightHandTracker);
    if (XR_FAILED(r)) {
        ALOGE("xrCreateHandTrackerEXT (right) failed: %d", r);
        _pfnDestroyHandTracker(_leftHandTracker);
        _leftHandTracker = XR_NULL_HANDLE;
        return false;
    }

    _aimExtEnabled = aimExtAvail;
    ALOGV("Hand trackers created (aim ext: %s)", _aimExtEnabled ? "yes" : "no");
    return true;
}

void VROInputControllerOpenXR::destroyHandTrackers() {
    if (_pfnDestroyHandTracker) {
        if (_leftHandTracker  != XR_NULL_HANDLE) {
            _pfnDestroyHandTracker(_leftHandTracker);
            _leftHandTracker = XR_NULL_HANDLE;
        }
        if (_rightHandTracker != XR_NULL_HANDLE) {
            _pfnDestroyHandTracker(_rightHandTracker);
            _rightHandTracker = XR_NULL_HANDLE;
        }
    }
}

void VROInputControllerOpenXR::processHands(XrSpace baseSpace, XrTime time,
                                              const VROCamera &camera,
                                              bool skipRight, bool skipLeft,
                                              bool &rightAimValidOut,
                                              VROVector3f &rightAimPosOut,
                                              VROQuaternion &rightAimRotOut,
                                              VROVector3f &rightAimForwardOut,
                                              bool &leftAimValidOut,
                                              VROVector3f &leftAimPosOut,
                                              VROQuaternion &leftAimRotOut,
                                              VROVector3f &leftAimForwardOut) {
    rightAimValidOut = false;
    leftAimValidOut  = false;
    if (!_pfnLocateHandJoints || !_handTrackingEnabled) return;

    for (int hand = 0; hand < 2; ++hand) {
        XrHandTrackerEXT tracker = (hand == 0) ? _leftHandTracker : _rightHandTracker;
        if (tracker == XR_NULL_HANDLE) continue;

        // ── Locate all 26 joints ──────────────────────────────────────────────
        XrHandJointLocationEXT jointLocs[XR_HAND_JOINT_COUNT_EXT];
        memset(jointLocs, 0, sizeof(jointLocs));

        XrHandJointLocationsEXT locations = { XR_TYPE_HAND_JOINT_LOCATIONS_EXT };
        locations.jointCount     = XR_HAND_JOINT_COUNT_EXT;
        locations.jointLocations = jointLocs;

        // Optionally chain FB aim state for pinch strength + aim pose
        XrHandTrackingAimStateFB aimState = { XR_TYPE_HAND_TRACKING_AIM_STATE_FB };
        if (_aimExtEnabled) {
            locations.next = &aimState;
        }

        XrHandJointsLocateInfoEXT locateInfo = { XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT };
        locateInfo.baseSpace = baseSpace;
        locateInfo.time      = time;

        XrResult r = _pfnLocateHandJoints(tracker, &locateInfo, &locations);
        if (!XR_SUCCEEDED(r) || !locations.isActive) continue;

        // ── Source IDs for this hand ──────────────────────────────────────────
        int  source     = (hand == 0) ? ViroOculus::LeftController : ViroOculus::Controller;
        int  gripSource = (hand == 0) ? ViroOculus::LeftGrip       : ViroOculus::RightGrip;
        bool &prevPinch = (hand == 0) ? _prevPinchLeft  : _prevPinchRight;
        bool &prevGrab  = (hand == 0) ? _prevGrabLeft   : _prevGrabRight;

        // ── Aim pose extraction (FB aim ext preferred, joint-derived fallback)
        // Hit-test, processGazeEvent, laser update and onMove are NOT done
        // here — `onProcess` consolidates them after this method returns,
        // dispatching once per side (right + left) so each hand has
        // independent hover/click/laser state. We only stash this hand's
        // aim pose in the matching out-params for the caller to use.
        bool aimComputed = _aimExtEnabled &&
                           (aimState.status & XR_HAND_TRACKING_AIM_COMPUTED_BIT_FB);
        bool          handAimValid   = false;
        VROVector3f   handAimPos;
        VROQuaternion handAimRot;
        VROVector3f   handAimForward;
        if (aimComputed) {
            handAimPos     = xrVec3ToVRO(aimState.aimPose.position);
            handAimRot     = xrQuatToVRO(aimState.aimPose.orientation);
            handAimForward = xrAimForward(aimState.aimPose);
            handAimValid   = true;
        } else {
            // Fallback: derive aim from index proximal → tip direction,
            // use wrist joint for position/orientation.
            auto &indexTip  = jointLocs[XR_HAND_JOINT_INDEX_TIP_EXT];
            auto &indexProx = jointLocs[XR_HAND_JOINT_INDEX_PROXIMAL_EXT];
            auto &wrist     = jointLocs[XR_HAND_JOINT_WRIST_EXT];

            bool tipValid   = (indexTip.locationFlags  & XR_SPACE_LOCATION_POSITION_VALID_BIT);
            bool proxValid  = (indexProx.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT);
            bool wristValid = (wrist.locationFlags     & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT);

            if (tipValid && proxValid && wristValid) {
                VROVector3f tipPos  = xrVec3ToVRO(indexTip.pose.position);
                VROVector3f proxPos = xrVec3ToVRO(indexProx.pose.position);
                handAimForward = (tipPos - proxPos);
                handAimForward.normalize();
                handAimPos     = xrVec3ToVRO(wrist.pose.position);
                handAimRot     = xrQuatToVRO(wrist.pose.orientation);
                handAimValid   = true;
            }
        }

        if (handAimValid) {
            if (hand == 0 && !skipLeft) {
                // Left hand: only relevant when the left controller didn't
                // provide an aim. Caller will dispatch via LeftController source.
                leftAimValidOut    = true;
                leftAimPosOut      = handAimPos;
                leftAimRotOut      = handAimRot;
                leftAimForwardOut  = handAimForward;
            } else if (hand == 1 && !skipRight) {
                rightAimValidOut   = true;
                rightAimPosOut     = handAimPos;
                rightAimRotOut     = handAimRot;
                rightAimForwardOut = handAimForward;
            }
        }

        // ── Pinch detection ───────────────────────────────────────────────────
        // Prefer FB pinch strength (continuous 0-1) over raw tip distance.
        bool pinched = false;
        if (aimComputed) {
            pinched = (aimState.pinchStrengthIndex >= 0.7f);
        } else {
            auto &thumbTip = jointLocs[XR_HAND_JOINT_THUMB_TIP_EXT];
            auto &indexTip = jointLocs[XR_HAND_JOINT_INDEX_TIP_EXT];
            if ((thumbTip.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
                (indexTip.locationFlags  & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
                float dx = thumbTip.pose.position.x - indexTip.pose.position.x;
                float dy = thumbTip.pose.position.y - indexTip.pose.position.y;
                float dz = thumbTip.pose.position.z - indexTip.pose.position.z;
                pinched = (sqrtf(dx*dx + dy*dy + dz*dz) < 0.02f);
            }
        }
        if (pinched && !prevPinch)
            VROInputControllerBase::onButtonEvent(source, VROEventDelegate::ClickState::ClickDown);
        else if (!pinched && prevPinch)
            VROInputControllerBase::onButtonEvent(source, VROEventDelegate::ClickState::ClickUp);
        prevPinch = pinched;

        // ── Grab detection (middle tip to palm distance) ──────────────────────
        auto &palm      = jointLocs[XR_HAND_JOINT_PALM_EXT];
        auto &middleTip = jointLocs[XR_HAND_JOINT_MIDDLE_TIP_EXT];
        if ((palm.locationFlags      & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
            (middleTip.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
            float dx = middleTip.pose.position.x - palm.pose.position.x;
            float dy = middleTip.pose.position.y - palm.pose.position.y;
            float dz = middleTip.pose.position.z - palm.pose.position.z;
            bool grabbed = (sqrtf(dx*dx + dy*dy + dz*dz) < 0.06f);

            if (grabbed && !prevGrab)
                VROInputControllerBase::onButtonEvent(gripSource, VROEventDelegate::ClickState::ClickDown);
            else if (!grabbed && prevGrab)
                VROInputControllerBase::onButtonEvent(gripSource, VROEventDelegate::ClickState::ClickUp);
            prevGrab = grabbed;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// VROInputControllerBase overrides
// ──────────────────────────────────────────────────────────────────────────────

VROVector3f VROInputControllerOpenXR::getDragForwardOffset() {
    return VROVector3f(0, 0, 0);
}

bool VROInputControllerOpenXR::stickyPose(PersistentAim &state, bool currentValid,
                                          VROVector3f &pos, VROQuaternion &rot,
                                          VROVector3f &forward) {
    if (currentValid) {
        state.haveCached  = true;
        state.pos         = pos;
        state.rot         = rot;
        state.forward     = forward;
        state.framesStale = 0;
        return true;
    }
    if (state.haveCached && state.framesStale < kMaxStaleFrames) {
        state.framesStale++;
        pos     = state.pos;
        rot     = state.rot;
        forward = state.forward;
        return true;
    }
    // Aged out — drop the cache so a long absence doesn't leave stale data
    // hanging around.
    if (state.framesStale >= kMaxStaleFrames) {
        state.haveCached = false;
    }
    state.framesStale++;
    return false;
}

std::shared_ptr<VROInputPresenter>
VROInputControllerOpenXR::createPresenter(std::shared_ptr<VRODriver> driver) {
    return std::make_shared<VROInputPresenterOpenXR>();
}

void VROInputControllerOpenXR::updateLaserViz(int source,
                                              const VROVector3f &origin,
                                              const VROVector3f &forward,
                                              bool visible) {
    auto presenter = std::dynamic_pointer_cast<VROInputPresenterOpenXR>(getPresenter());
    if (!presenter) return;

    if (!visible) {
        presenter->updateAimRay(source, {}, {}, false);
        return;
    }

    // Hide on degenerate forward — happens transiently if a pose is reported
    // valid but the orientation hasn't been computed yet (zero quaternion),
    // which would produce an invisible zero-length polyline anyway.
    if (forward.magnitude() < 0.001f) {
        presenter->updateAimRay(source, {}, {}, false);
        return;
    }

    // Use this source's hit result (per-source via base class) if any;
    // otherwise extend a fixed forward distance so the user always sees
    // where they're aiming.
    constexpr float kNoHitRange = 4.0f;  // meters
    VROVector3f hitPoint;
    auto hit = getHitResultForSource(source);
    if (hit) {
        hitPoint = hit->getLocation();
    } else {
        hitPoint = origin + forward.scale(kNoHitRange);
    }
    presenter->updateAimRay(source, origin, hitPoint, true /* visible */);
}
