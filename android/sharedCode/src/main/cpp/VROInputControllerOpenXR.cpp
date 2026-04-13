// VROInputControllerOpenXR.cpp
// ViroRenderer
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#include "VROInputControllerOpenXR.h"
#include <android/log.h>
#include "VROLog.h"
#include "VROInputPresenterOVR.h"   // Reuse OVR presenter (no-op visuals) until M2
#include "VROVector3f.h"
#include "VROCamera.h"

#define LOG_TAG "VROInputOpenXR"
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)

// ──────────────────────────────────────────────────────────────────────────────
// Helpers
// ──────────────────────────────────────────────────────────────────────────────
// Note: XR_SUCCEEDED / XR_FAILED are macros defined in openxr.h — do not redefine.

static VROVector3f xrVec3ToVRO(XrVector3f v) {
    return VROVector3f(v.x, v.y, v.z);
}

static VROQuaternion xrQuatToVRO(XrQuaternionf q) {
    return VROQuaternion(q.x, q.y, q.z, q.w);
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
    strncpy(asInfo.actionSetName,       "viro_gameplay",      XR_MAX_ACTION_SET_NAME_SIZE);
    strncpy(asInfo.localizedActionSetName, "Viro Gameplay",   XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE);
    asInfo.priority = 0;

    XrResult result = xrCreateActionSet(instance, &asInfo, &_actionSet);
    if (!XR_SUCCEEDED(result)) {
        ALOGE("xrCreateActionSet failed: %d", result);
        return false;
    }

    // ── 2. Declare actions ────────────────────────────────────────────────────
    _leftAimPoseAction  = createAction(_actionSet, XR_ACTION_TYPE_POSE_INPUT,
                                       "left_aim",  "Left Aim Pose");
    _rightAimPoseAction = createAction(_actionSet, XR_ACTION_TYPE_POSE_INPUT,
                                       "right_aim", "Right Aim Pose");
    _selectAction       = createAction(_actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT,
                                       "select",    "Select / Trigger Click");
    _menuAction         = createAction(_actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT,
                                       "menu",      "Menu");
    _triggerValueAction = createAction(_actionSet, XR_ACTION_TYPE_FLOAT_INPUT,
                                       "trigger_value", "Trigger Value");
    _squeezeValueAction = createAction(_actionSet, XR_ACTION_TYPE_FLOAT_INPUT,
                                       "squeeze_value", "Squeeze / Grip Value");
    _thumbstickAction   = createAction(_actionSet, XR_ACTION_TYPE_VECTOR2F_INPUT,
                                       "thumbstick", "Thumbstick");
    _vibrateAction      = createAction(_actionSet, XR_ACTION_TYPE_VIBRATION_OUTPUT,
                                       "vibrate",   "Vibrate");

    if (!_leftAimPoseAction || !_rightAimPoseAction || !_selectAction ||
        !_menuAction || !_triggerValueAction || !_squeezeValueAction ||
        !_thumbstickAction || !_vibrateAction) {
        ALOGE("One or more OpenXR actions failed to create");
        return false;
    }

    // ── 3. Suggest interaction profile bindings (Touch / Touch Plus) ──────────
    //    Profile: /interaction_profiles/oculus/touch_controller
    //    Also covers Touch Plus (Quest 3 default controllers).

    XrPath touchProfile;
    xrStringToPath(instance,
                   "/interaction_profiles/oculus/touch_controller",
                   &touchProfile);

    auto makePath = [&](const char *str) -> XrPath {
        XrPath p;
        xrStringToPath(instance, str, &p);
        return p;
    };

    const XrActionSuggestedBinding bindings[] = {
        { _leftAimPoseAction,   makePath("/user/hand/left/input/aim/pose")       },
        { _rightAimPoseAction,  makePath("/user/hand/right/input/aim/pose")      },
        { _selectAction,        makePath("/user/hand/left/input/trigger/value")  },
        { _selectAction,        makePath("/user/hand/right/input/trigger/value") },
        { _menuAction,          makePath("/user/hand/left/input/menu/click")     },
        { _triggerValueAction,  makePath("/user/hand/left/input/trigger/value")  },
        { _triggerValueAction,  makePath("/user/hand/right/input/trigger/value") },
        { _squeezeValueAction,  makePath("/user/hand/left/input/squeeze/value")  },
        { _squeezeValueAction,  makePath("/user/hand/right/input/squeeze/value") },
        { _thumbstickAction,    makePath("/user/hand/left/input/thumbstick")     },
        { _thumbstickAction,    makePath("/user/hand/right/input/thumbstick")    },
        { _vibrateAction,       makePath("/user/hand/left/output/haptic")        },
        { _vibrateAction,       makePath("/user/hand/right/output/haptic")       },
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
    XrSessionActionSetsAttachInfo attachInfo = {
        XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO
    };
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

    ALOGV("OpenXR action set created and attached");
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
    strncpy(info.actionName,            name,          XR_MAX_ACTION_NAME_SIZE);
    strncpy(info.localizedActionName,   localizedName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE);
    // Bind to both hands where applicable; caller controls via interaction profile bindings
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
// Per-frame processing  (Week 3 / M2: wire to Viro events)
// ──────────────────────────────────────────────────────────────────────────────

void VROInputControllerOpenXR::onProcess(XrSession session, XrSpace baseSpace,
                                          XrTime time, const VROCamera &camera) {
    // Sync active action set
    XrActiveActionSet activeSet = { _actionSet, XR_NULL_PATH };
    XrActionsSyncInfo syncInfo  = { XR_TYPE_ACTIONS_SYNC_INFO };
    syncInfo.activeActionSets      = &activeSet;
    syncInfo.countActiveActionSets = 1;
    xrSyncActions(session, &syncInfo);

    // ── Query right controller aim pose ───────────────────────────────────────
    if (_rightSpace != XR_NULL_HANDLE) {
        XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
        XrResult r = xrLocateSpace(_rightSpace, baseSpace, time, &loc);
        if (XR_SUCCEEDED(r) &&
            (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
            (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {

            VROVector3f    pos = xrVec3ToVRO(loc.pose.position);
            VROQuaternion  rot = xrQuatToVRO(loc.pose.orientation);

            // TODO (M2): update right controller ray origin + orientation in
            //            VROInputControllerBase and emit hover events
            (void)pos; (void)rot;
        }
    }

    // ── Query select (trigger click) ─────────────────────────────────────────
    {
        XrActionStateBoolean state = { XR_TYPE_ACTION_STATE_BOOLEAN };
        XrActionStateGetInfo getInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
        getInfo.action = _selectAction;
        xrGetActionStateBoolean(session, &getInfo, &state);

        if (state.isActive) {
            bool pressed = (state.currentState == XR_TRUE);
            if (pressed && !_prevSelectRight) {
                // TODO (M2): onButtonEvent(ViroControllerRight, VROEventDelegate::EventAction::ClickDown)
            } else if (!pressed && _prevSelectRight) {
                // TODO (M2): onButtonEvent(ViroControllerRight, VROEventDelegate::EventAction::ClickUp)
            }
            _prevSelectRight = pressed;
        }
    }
}

void VROInputControllerOpenXR::triggerHaptic(XrSession session, int hand,
                                               float amplitude, float durationSec) {
    XrHapticVibration vibration = { XR_TYPE_HAPTIC_VIBRATION };
    vibration.amplitude  = amplitude;
    vibration.duration   = (XrDuration)(durationSec * 1e9f);  // nanoseconds
    vibration.frequency  = XR_FREQUENCY_UNSPECIFIED;

    XrHapticActionInfo info = { XR_TYPE_HAPTIC_ACTION_INFO };
    info.action = _vibrateAction;
    xrApplyHapticFeedback(session, &info, (const XrHapticBaseHeader *)&vibration);
}

VROVector3f VROInputControllerOpenXR::getDragForwardOffset() {
    return VROVector3f(0, 0, 0);
}

std::shared_ptr<VROInputPresenter>
VROInputControllerOpenXR::createPresenter(std::shared_ptr<VRODriver> driver) {
    // Reuse OVR no-op presenter until M2 builds a dedicated Quest presenter
    return std::make_shared<VROInputPresenterOVR>();
}
