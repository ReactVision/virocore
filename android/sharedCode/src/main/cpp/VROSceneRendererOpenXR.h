// VROSceneRendererOpenXR.h
// ViroRenderer
//
// Scene renderer for Meta Quest via the Khronos OpenXR API.
// Replaces the legacy VROSceneRendererOVR (VrApi) which is not supported
// on Quest 3 and newer hardware.
//
// Lifecycle managed by ViroViewOpenXR (Java) via JNI:
//   constructor  → xrCreateInstance, xrGetSystem, EGL context, xrCreateSession
//   onResume     → start render thread
//   onPause      → signal render thread to pause
//   onDestroy    → stop render thread, destroy session + instance
//
// Render thread loop:
//   xrPollEvent (session state machine)
//   xrWaitFrame / xrBeginFrame
//   for each eye: acquire swapchain → render → release
//   xrEndFrame with projection layer
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#ifndef ANDROID_VROSCENERENDEREROPENBXR_H
#define ANDROID_VROSCENERENDEREROPENBXR_H

#include "VROSceneRenderer.h"
#include <memory>
#include <thread>
#include <atomic>
#include <vector>

// Must be defined before openxr_platform.h to enable OpenGL ES + Android types
// (XrSwapchainImageOpenGLESKHR, XrGraphicsBindingOpenGLESAndroidKHR, etc.)
#define XR_USE_GRAPHICS_API_OPENGL_ES
#define XR_USE_PLATFORM_ANDROID

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

class VRORendererConfiguration;
class VRODriverOpenGLAndroidOpenXR;
class VROInputControllerOpenXR;
class VRODisplayOpenGLOpenXR;

namespace gvr { class AudioApi; }

// Per-eye swapchain state
struct VROOpenXRSwapchain {
    XrSwapchain                          handle   = XR_NULL_HANDLE;
    uint32_t                             width    = 0;
    uint32_t                             height   = 0;
    std::vector<XrSwapchainImageOpenGLESKHR> images;
};

class VROSceneRendererOpenXR : public VROSceneRenderer {
public:

    VROSceneRendererOpenXR(VRORendererConfiguration config,
                           std::shared_ptr<gvr::AudioApi> gvrAudio,
                           jobject view, jobject activity, JNIEnv *env);
    virtual ~VROSceneRendererOpenXR();

    // ── VROSceneRenderer interface ────────────────────────────────────────────
    void initGL()            {}  // OpenXR manages its own context; not used
    void onDrawFrame()       {}  // Render loop runs on dedicated thread; not used
    void onTouchEvent(int action, float x, float y);
    void onKeyEvent(int keyCode, int action);
    void setVRModeEnabled(bool enabled) {}
    void recenterTracking();

    // ── OpenXR extensions (callable from Java via JNI) ────────────────────────
    /**
     * Enable or disable XR_FB_passthrough mixed-reality mode.
     * No-op if the extension is unavailable on the current device.
     * Safe to call from any thread — changes take effect on the next frame.
     */
    void setPassthroughEnabled(bool enabled);
    void setHandTrackingEnabled(bool enabled);
    void onStart();
    void onResume();
    void onPause();
    void onStop();
    void onDestroy();
    void onSurfaceCreated(jobject surface)  {}  // OpenXR owns the display surface
    void onSurfaceChanged(jobject surface, VRO_INT w, VRO_INT h) {}
    void onSurfaceDestroyed() {}

private:

    // ── Runtime classification ────────────────────────────────────────────────
    // Populated once, immediately after xrCreateInstance succeeds, from
    // xrGetInstanceProperties + the negotiated extension set. Read-only after init.
    enum class VROOpenXRVendor { UNKNOWN, META, PICO, KHRONOS_OTHER };
    struct VROOpenXRRuntimeInfo {
        bool            valid              = false;
        VROOpenXRVendor vendor             = VROOpenXRVendor::UNKNOWN;
        char            runtimeName[XR_MAX_RUNTIME_NAME_SIZE] = {0};
        uint16_t        apiMajor           = 0;
        uint16_t        apiMinor           = 0;
        uint32_t        apiPatch           = 0;
        bool            androidCreateInstanceEnabled = false;
        bool            passthroughAvailable         = false;  // XR_FB_passthrough
        bool            displayRefreshRateAvailable  = false;  // XR_FB_display_refresh_rate
        bool            handTrackingAvailable        = false;  // XR_EXT_hand_tracking
        bool            handAimExtAvailable          = false;  // XR_FB_hand_tracking_aim
    };
    VROOpenXRRuntimeInfo _runtimeInfo;

public:
    const VROOpenXRRuntimeInfo &getRuntimeInfo() const { return _runtimeInfo; }

    // ── Foveation (XR_FB_foveation) ────────────────────────────────────────────
    enum class VROFoveationLevel { OFF = 0, LOW = 1, MEDIUM = 2, HIGH = 3 };
    // Apply a fixed-foveation level to both eye swapchains. dynamic=true lets the
    // runtime scale the level with GPU load. No-op (returns false) when
    // FB_foveation was not negotiated. Safe to call after session start.
    bool setFoveationLevel(VROFoveationLevel level, bool dynamic);
    bool isFoveationAvailable()           const { return _foveationAvailable; }
    bool isEyeTrackedFoveationAvailable() const { return _eyeTrackedFoveationAvailable; }

private:
    bool initFoveation();   // called once after swapchain creation

    bool _foveationAvailable            = false;  // XR_FB_foveation present + fns loaded
    bool _eyeTrackedFoveationAvailable  = false;  // XR_META_foveation_eye_tracked present
    bool _swapchainUpdateStateAvailable = false;  // XR_FB_swapchain_update_state present
    PFN_xrCreateFoveationProfileFB  _pfnCreateFoveationProfile  = nullptr;
    PFN_xrDestroyFoveationProfileFB _pfnDestroyFoveationProfile = nullptr;
    PFN_xrUpdateSwapchainFB         _pfnUpdateSwapchain         = nullptr;

    // ── OpenXR core handles ───────────────────────────────────────────────────
    XrInstance      _instance   = XR_NULL_HANDLE;
    XrSystemId      _systemId   = XR_NULL_SYSTEM_ID;
    XrSession       _session    = XR_NULL_HANDLE;
    XrSpace         _stageSpace = XR_NULL_HANDLE;  // XR_REFERENCE_SPACE_TYPE_STAGE

    XrSessionState  _sessionState             = XR_SESSION_STATE_UNKNOWN;
    XrTime          _lastPredictedDisplayTime = 0;  // updated each frame; used by recenterTracking()
    bool            _sessionRunning        = false;
    bool            _passthroughEnabled    = false;
    bool            _handTrackingAvailable = false;  // XR_EXT_hand_tracking present
    bool            _handAimExtAvailable   = false;  // XR_FB_hand_tracking_aim present

    // Per-eye swapchains (index 0 = left, 1 = right)
    VROOpenXRSwapchain _swapchains[2];

    // Passthrough extension handles (XR_FB_passthrough)
    XrPassthroughFB      _passthrough      = XR_NULL_HANDLE;
    XrPassthroughLayerFB _passthroughLayer = XR_NULL_HANDLE;

    // Extension function pointers — loaded in initPassthrough() via xrGetInstanceProcAddr.
    // All are null until XR_FB_passthrough is confirmed available and initialised.
    PFN_xrCreatePassthroughFB       _pfnCreatePassthrough       = nullptr;
    PFN_xrDestroyPassthroughFB      _pfnDestroyPassthrough      = nullptr;
    PFN_xrPassthroughStartFB        _pfnPassthroughStart        = nullptr;
    PFN_xrPassthroughPauseFB        _pfnPassthroughPause        = nullptr;
    PFN_xrCreatePassthroughLayerFB  _pfnCreatePassthroughLayer  = nullptr;
    PFN_xrDestroyPassthroughLayerFB _pfnDestroyPassthroughLayer = nullptr;
    PFN_xrPassthroughLayerResumeFB  _pfnPassthroughLayerResume  = nullptr;
    PFN_xrPassthroughLayerPauseFB   _pfnPassthroughLayerPause   = nullptr;

    // ── EGL ──────────────────────────────────────────────────────────────────
    EGLDisplay  _eglDisplay  = EGL_NO_DISPLAY;
    EGLConfig   _eglConfig   = nullptr;
    EGLContext  _eglContext  = EGL_NO_CONTEXT;
    EGLSurface  _eglSurface  = EGL_NO_SURFACE;  // tiny pbuffer surface

    // ── Render thread ─────────────────────────────────────────────────────────
    std::thread       _renderThread;
    std::atomic<bool> _running  { false };
    std::atomic<bool> _paused   { true  };

    // ── Viro subsystems ───────────────────────────────────────────────────────
    // _openxrDriver holds the derived type for getOpenXRDisplay(); base class
    // _driver (VRODriverOpenGLAndroid) is also set to the same shared_ptr in the
    // constructor so that VROSceneRenderer methods (setSceneController, etc.) work.
    std::shared_ptr<VRODriverOpenGLAndroidOpenXR> _openxrDriver;
    std::shared_ptr<VROInputControllerOpenXR>     _inputController;
    std::shared_ptr<gvr::AudioApi>                _gvrAudio;
    jobject                                        _activity = nullptr;

    // ── Java callback (onDrawFrame) ───────────────────────────────────────────
    JavaVM  *_jvm      = nullptr;
    jobject  _jview    = nullptr;  // global ref to ViroViewOpenXR instance

    // ── Init / teardown helpers ───────────────────────────────────────────────
    bool initOpenXR();           // xrCreateInstance, xrGetSystem
    bool createEGLContext();
    bool createSession();        // xrCreateSession with GLES binding
    bool createReferenceSpace();
    bool createSwapchains();
    bool initPassthrough();
    bool initHandTracking();     // XR_EXT_hand_tracking — no-op if extension unavailable
    void destroySwapchains();
    void destroySession();
    void destroyEGLContext();
    void destroyOpenXR();

    // ── Render thread ─────────────────────────────────────────────────────────
    void renderLoop();
    void pollEvents();
    void handleSessionStateChange(XrEventDataSessionStateChanged *event);
    void renderFrame();

    // ── Per-eye render ────────────────────────────────────────────────────────
    void renderEye(int eyeIndex,
                   const XrView &view,
                   VROOpenXRSwapchain &swapchain);
};

#endif  // ANDROID_VROSCENERENDEREROPENBXR_H
