// VROSceneRendererOpenXR.cpp
// ViroRenderer
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#include "VROSceneRendererOpenXR.h"

#include <android/log.h>
#include <sys/prctl.h>
#include <unistd.h>

#include "VRORendererConfiguration.h"
#include "VRODriverOpenGLAndroidOpenXR.h"
#include "VRODisplayOpenGLOpenXR.h"
#include "VROInputControllerOpenXR.h"
#include "VROLog.h"
#include "VROAllocationTracker.h"
#include "VROTime.h"
#include "VROThreadRestricted.h"
#include "VROPlatformUtil.h"

#define LOG_TAG "VRORendererOpenXR"
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR,   LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN,    LOG_TAG, __VA_ARGS__)
#define ALOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__)

// ──────────────────────────────────────────────────────────────────────────────
// Required extensions
// ──────────────────────────────────────────────────────────────────────────────

static const char *const kRequiredExtensions[] = {
    XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,   // GLES context binding (mandatory)
    XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
};
static constexpr uint32_t kRequiredExtensionCount =
    sizeof(kRequiredExtensions) / sizeof(kRequiredExtensions[0]);

static const char *const kOptionalExtensions[] = {
    XR_FB_PASSTHROUGH_EXTENSION_NAME,           // mixed reality (Quest 2 BW, Quest 3 color)
    XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME,  // 90 / 120 Hz mode
    XR_EXT_HAND_TRACKING_EXTENSION_NAME,        // M3: skeletal hand tracking (26 joints)
    XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME,     // M3: aim pose + pinch strength from runtime
};

// ──────────────────────────────────────────────────────────────────────────────
// Utility macros
// ──────────────────────────────────────────────────────────────────────────────

#define XR_CHECK(call)                                                           \
    do {                                                                         \
        XrResult _r = (call);                                                    \
        if (XR_FAILED(_r)) {                                                     \
            ALOGE("OpenXR call failed at %s:%d  result=%d  expr: %s",           \
                  __FILE__, __LINE__, (int)_r, #call);                           \
        }                                                                        \
    } while (0)

#define XR_RETURN_FALSE(call)                                                    \
    do {                                                                         \
        XrResult _r = (call);                                                    \
        if (XR_FAILED(_r)) {                                                     \
            ALOGE("OpenXR fatal at %s:%d  result=%d  expr: %s",                 \
                  __FILE__, __LINE__, (int)_r, #call);                           \
            return false;                                                        \
        }                                                                        \
    } while (0)

// ──────────────────────────────────────────────────────────────────────────────
// Conversion helpers
// ──────────────────────────────────────────────────────────────────────────────

static VROMatrix4f xrPoseToMatrix(const XrPosef &pose) {
    VROQuaternion q(pose.orientation.x, pose.orientation.y,
                    pose.orientation.z, pose.orientation.w);
    VROMatrix4f rot = q.getMatrix();
    rot[12] = pose.position.x;
    rot[13] = pose.position.y;
    rot[14] = pose.position.z;
    return rot;
}

// Compute a projection matrix from OpenXR FoV angles (tangent half-angles).
static VROMatrix4f xrFovToProjection(const XrFovf &fov,
                                      float nearZ = 0.1f,
                                      float farZ  = 1000.0f) {
    const float left   = tanf(fov.angleLeft);
    const float right  = tanf(fov.angleRight);
    const float down   = tanf(fov.angleDown);
    const float up     = tanf(fov.angleUp);

    const float w  =  right - left;
    const float h  =  up    - down;
    const float q  = -(farZ + nearZ) / (farZ - nearZ);
    const float qn = -2.0f * farZ * nearZ / (farZ - nearZ);

    float m[16] = {};
    m[0]  =  2.0f / w;
    m[5]  =  2.0f / h;
    m[8]  =  (right + left) / w;
    m[9]  =  (up    + down) / h;
    m[10] =  q;
    m[11] = -1.0f;
    m[14] =  qn;
    return VROMatrix4f(m);
}

// ──────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ──────────────────────────────────────────────────────────────────────────────

VROSceneRendererOpenXR::VROSceneRendererOpenXR(VRORendererConfiguration config,
                                               std::shared_ptr<gvr::AudioApi> gvrAudio,
                                               jobject view, jobject activity, JNIEnv *env)
    : _gvrAudio(gvrAudio)
{
    // Hold global refs so these objects live as long as the renderer
    _activity = env->NewGlobalRef(activity);
    _jview    = env->NewGlobalRef(view);
    env->GetJavaVM(&_jvm);

    if (!initOpenXR()) {
        ALOGE("initOpenXR() failed — Quest renderer will not function");
        return;
    }
    if (!createEGLContext()) {
        ALOGE("createEGLContext() failed");
        return;
    }
    if (!createSession()) {
        ALOGE("createSession() failed");
        return;
    }

    // Create the OpenXR driver and assign it to both the derived-class field
    // (for getOpenXRDisplay()) and the base-class _driver (for setSceneController etc.)
    _openxrDriver = std::make_shared<VRODriverOpenGLAndroidOpenXR>(gvrAudio);
    _driver = _openxrDriver;  // base class std::shared_ptr<VRODriverOpenGLAndroid>
    _inputController = std::make_shared<VROInputControllerOpenXR>(_openxrDriver);
    _inputController->createActionSet(_instance, _session);
    initHandTracking();  // no-op if XR_EXT_hand_tracking not available on this device

    // Create the shared VRORenderer and wire it to the input controller.
    // VROSceneRenderer::_renderer is null until explicitly set here — every other
    // platform (GVR, OVR) does the equivalent in their constructor.
    _renderer = std::make_shared<VRORenderer>(config, _inputController);

    // OpenXR owns its own render thread — bypass the GLSurfaceView dispatcher.
    // VROPlatformDrainRendererQueue() is called at the top of each renderFrame().
    VROPlatformSetUseDirectRendererQueue(true);

    // Release the EGL context from the main thread so the render thread can
    // take exclusive ownership via eglMakeCurrent in renderLoop().
    // EGL contexts can only be current on one thread at a time.
    eglMakeCurrent(_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

    ALOGV("VROSceneRendererOpenXR constructed successfully");
}

VROSceneRendererOpenXR::~VROSceneRendererOpenXR() {
    onDestroy();
}

// ──────────────────────────────────────────────────────────────────────────────
// OpenXR initialisation
// ──────────────────────────────────────────────────────────────────────────────

bool VROSceneRendererOpenXR::initOpenXR() {
    // ── Initialize loader (MUST be first OpenXR call on Android) ─────────────
    // Without this the loader cannot locate the Meta Quest runtime and
    // xrEnumerateInstanceExtensionProperties returns 0 extensions.
    {
        PFN_xrInitializeLoaderKHR pfnInitLoader = nullptr;
        xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                              (PFN_xrVoidFunction *)&pfnInitLoader);
        if (!pfnInitLoader) {
            ALOGE("xrInitializeLoaderKHR not available — OpenXR loader too old?");
            return false;
        }
        XrLoaderInitInfoAndroidKHR loaderInfo = {
            XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR
        };
        loaderInfo.applicationVM      = _jvm;
        loaderInfo.applicationContext = _activity;
        XrResult loaderResult = pfnInitLoader(
            (const XrLoaderInitInfoBaseHeaderKHR *)&loaderInfo);
        if (XR_FAILED(loaderResult)) {
            ALOGE("xrInitializeLoaderKHR failed: %d", (int)loaderResult);
            return false;
        }
        ALOGV("xrInitializeLoaderKHR OK");
    }

    // ── Enumerate available extensions ────────────────────────────────────────
    uint32_t extCount = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr);
    std::vector<XrExtensionProperties> availableExts(extCount,
        { XR_TYPE_EXTENSION_PROPERTIES });
    xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount,
                                            availableExts.data());

    // Verify required extensions are present
    for (uint32_t i = 0; i < kRequiredExtensionCount; ++i) {
        bool found = false;
        for (auto &ext : availableExts) {
            if (strcmp(ext.extensionName, kRequiredExtensions[i]) == 0) {
                found = true; break;
            }
        }
        if (!found) {
            ALOGE("Required OpenXR extension not available: %s",
                  kRequiredExtensions[i]);
            return false;
        }
    }

    // Build the extension list: required + available optionals
    std::vector<const char *> enabledExts(kRequiredExtensions,
                                           kRequiredExtensions + kRequiredExtensionCount);
    for (auto *optExt : kOptionalExtensions) {
        for (auto &ext : availableExts) {
            if (strcmp(ext.extensionName, optExt) == 0) {
                enabledExts.push_back(optExt);
                ALOGV("Optional extension enabled: %s", optExt);
                if (strcmp(optExt, XR_EXT_HAND_TRACKING_EXTENSION_NAME) == 0)
                    _handTrackingAvailable = true;
                if (strcmp(optExt, XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME) == 0)
                    _handAimExtAvailable = true;
                break;
            }
        }
    }

    // ── Create XrInstance ─────────────────────────────────────────────────────
    XrInstanceCreateInfoAndroidKHR androidInfo = {
        XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR
    };
    androidInfo.applicationActivity = _activity;
    androidInfo.applicationVM       = _jvm;

    XrApplicationInfo appInfo = {};
    strncpy(appInfo.applicationName, "ViroReact", XR_MAX_APPLICATION_NAME_SIZE);
    appInfo.applicationVersion = 1;
    strncpy(appInfo.engineName, "ViroRenderer", XR_MAX_ENGINE_NAME_SIZE);
    appInfo.engineVersion      = 1;
    appInfo.apiVersion         = XR_CURRENT_API_VERSION;

    XrInstanceCreateInfo createInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
    createInfo.next                    = &androidInfo;
    createInfo.applicationInfo         = appInfo;
    createInfo.enabledExtensionCount   = (uint32_t)enabledExts.size();
    createInfo.enabledExtensionNames   = enabledExts.data();

    XR_RETURN_FALSE(xrCreateInstance(&createInfo, &_instance));
    ALOGV("xrCreateInstance OK");

    // ── Get system (HMD) ──────────────────────────────────────────────────────
    XrSystemGetInfo sysInfo = { XR_TYPE_SYSTEM_GET_INFO };
    sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XR_RETURN_FALSE(xrGetSystem(_instance, &sysInfo, &_systemId));
    ALOGV("xrGetSystem OK  systemId=%llu", (unsigned long long)_systemId);

    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// EGL context
// ──────────────────────────────────────────────────────────────────────────────

bool VROSceneRendererOpenXR::createEGLContext() {
    _eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (_eglDisplay == EGL_NO_DISPLAY) {
        ALOGE("eglGetDisplay failed");
        return false;
    }

    EGLint major, minor;
    if (!eglInitialize(_eglDisplay, &major, &minor)) {
        ALOGE("eglInitialize failed");
        return false;
    }
    ALOGV("EGL %d.%d", major, minor);

    // Manual config selection — same approach as VROSceneRendererOVR to avoid
    // Android's forced MSAA injection via eglChooseConfig.
    const int MAX_CONFIGS = 1024;
    EGLConfig configs[MAX_CONFIGS];
    EGLint numConfigs = 0;
    eglGetConfigs(_eglDisplay, configs, MAX_CONFIGS, &numConfigs);

    const EGLint wanted[] = {
        EGL_RED_SIZE,   8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 0,
        EGL_NONE
    };

    for (int i = 0; i < numConfigs; ++i) {
        EGLint val = 0;
        eglGetConfigAttrib(_eglDisplay, configs[i], EGL_RENDERABLE_TYPE, &val);
        if (!(val & EGL_OPENGL_ES3_BIT_KHR)) continue;

        eglGetConfigAttrib(_eglDisplay, configs[i], EGL_SURFACE_TYPE, &val);
        if (!(val & EGL_PBUFFER_BIT)) continue;

        bool match = true;
        for (int j = 0; wanted[j] != EGL_NONE; j += 2) {
            eglGetConfigAttrib(_eglDisplay, configs[i], wanted[j], &val);
            if (val != wanted[j + 1]) { match = false; break; }
        }
        if (match) {
            _eglConfig = configs[i];
            break;
        }
    }
    if (!_eglConfig) {
        ALOGE("No suitable EGL config found");
        return false;
    }

    EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    _eglContext = eglCreateContext(_eglDisplay, _eglConfig, EGL_NO_CONTEXT, ctxAttribs);
    if (_eglContext == EGL_NO_CONTEXT) {
        ALOGE("eglCreateContext failed: 0x%x", eglGetError());
        return false;
    }

    // Tiny pbuffer surface so the context is current during session creation
    EGLint pbAttribs[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
    _eglSurface = eglCreatePbufferSurface(_eglDisplay, _eglConfig, pbAttribs);
    eglMakeCurrent(_eglDisplay, _eglSurface, _eglSurface, _eglContext);

    ALOGV("EGL context created and current");
    return true;
}

// ──────────────────────────────────────────────────────────────────────────────
// Session
// ──────────────────────────────────────────────────────────────────────────────

bool VROSceneRendererOpenXR::createSession() {
    // ── Graphics requirements (MANDATORY before xrCreateSession) ──────────────
    // The OpenXR spec requires xrGetOpenGLESGraphicsRequirementsKHR to be called
    // before xrCreateSession or the runtime returns XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING.
    {
        PFN_xrGetOpenGLESGraphicsRequirementsKHR pfnGetReqs = nullptr;
        xrGetInstanceProcAddr(_instance, "xrGetOpenGLESGraphicsRequirementsKHR",
                              (PFN_xrVoidFunction *)&pfnGetReqs);
        if (!pfnGetReqs) {
            ALOGE("xrGetOpenGLESGraphicsRequirementsKHR not found");
            return false;
        }
        XrGraphicsRequirementsOpenGLESKHR reqs = {
            XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR
        };
        XrResult r = pfnGetReqs(_instance, _systemId, &reqs);
        if (XR_FAILED(r)) {
            ALOGE("xrGetOpenGLESGraphicsRequirementsKHR failed: %d", (int)r);
            return false;
        }
        ALOGV("GLES requirements: min=%u.%u max=%u.%u",
              XR_VERSION_MAJOR(reqs.minApiVersionSupported),
              XR_VERSION_MINOR(reqs.minApiVersionSupported),
              XR_VERSION_MAJOR(reqs.maxApiVersionSupported),
              XR_VERSION_MINOR(reqs.maxApiVersionSupported));
    }

    // The EGL context must already be current (createEGLContext called first).
    XrGraphicsBindingOpenGLESAndroidKHR binding = {
        XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR
    };
    binding.display = _eglDisplay;
    binding.config  = _eglConfig;
    binding.context = _eglContext;

    XrSessionCreateInfo sessionInfo = { XR_TYPE_SESSION_CREATE_INFO };
    sessionInfo.next      = &binding;
    sessionInfo.systemId  = _systemId;

    XR_RETURN_FALSE(xrCreateSession(_instance, &sessionInfo, &_session));
    ALOGV("xrCreateSession OK");

    if (!createReferenceSpace()) return false;
    if (!createSwapchains())    return false;

    // Try to enable passthrough (optional — graceful degradation if unavailable)
    initPassthrough();

    return true;
}

bool VROSceneRendererOpenXR::initHandTracking() {
    if (!_handTrackingAvailable || !_inputController) return false;
    bool ok = _inputController->initHandTracking(_instance, _session, _handAimExtAvailable);
    ALOGV("Hand tracking init: %s (aim ext: %s)", ok ? "OK" : "FAILED",
          _handAimExtAvailable ? "yes" : "no");
    return ok;
}

bool VROSceneRendererOpenXR::createReferenceSpace() {
    // Use LOCAL space so the Viro world origin matches the eye level at session start.
    // STAGE space places Y=0 at the floor (~1.6 m below the eye), which causes objects
    // placed at Viro world (0,0,-2) to appear ~39° below the horizon — near or past
    // the bottom of the Quest 3's physical FOV. LOCAL space matches every other Viro
    // platform's convention (camera at scene origin, looking forward).
    // Floor-relative placement (STAGE) can be addressed in a dedicated M-series milestone.
    XrReferenceSpaceCreateInfo spaceInfo = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    spaceInfo.referenceSpaceType    = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace  = { {0, 0, 0, 1}, {0, 0, 0} };  // identity

    XrResult r = xrCreateReferenceSpace(_session, &spaceInfo, &_stageSpace);
    if (XR_FAILED(r)) {
        ALOGE("LOCAL reference space creation failed: %d", r);
        return false;
    }
    ALOGV("Reference space created (LOCAL — eye-level origin)");
    return true;
}

bool VROSceneRendererOpenXR::createSwapchains() {
    // Enumerate view configuration (stereo: 2 views)
    uint32_t viewCount = 0;
    xrEnumerateViewConfigurationViews(_instance, _systemId,
                                       XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                       0, &viewCount, nullptr);
    if (viewCount != 2) {
        ALOGE("Expected 2 views, got %u", viewCount);
        return false;
    }
    std::vector<XrViewConfigurationView> viewConfigs(viewCount,
        { XR_TYPE_VIEW_CONFIGURATION_VIEW });
    xrEnumerateViewConfigurationViews(_instance, _systemId,
                                       XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                       viewCount, &viewCount, viewConfigs.data());

    // Choose swapchain format: prefer sRGB, fall back to RGBA8
    uint32_t fmtCount = 0;
    xrEnumerateSwapchainFormats(_session, 0, &fmtCount, nullptr);
    std::vector<int64_t> formats(fmtCount);
    xrEnumerateSwapchainFormats(_session, fmtCount, &fmtCount, formats.data());

    int64_t chosenFormat = GL_RGBA8;
    for (int64_t f : formats) {
        if (f == GL_SRGB8_ALPHA8_EXT) { chosenFormat = f; break; }
    }
    ALOGV("Swapchain format: 0x%llx", (long long)chosenFormat);

    // Create one swapchain per eye
    for (uint32_t eye = 0; eye < 2; ++eye) {
        auto &view  = viewConfigs[eye];
        auto &sc    = _swapchains[eye];

        sc.width  = view.recommendedImageRectWidth;
        sc.height = view.recommendedImageRectHeight;

        XrSwapchainCreateInfo scInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
        scInfo.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                              XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        scInfo.format      = chosenFormat;
        scInfo.sampleCount = view.recommendedSwapchainSampleCount;
        scInfo.width       = sc.width;
        scInfo.height      = sc.height;
        scInfo.faceCount   = 1;
        scInfo.arraySize   = 1;
        scInfo.mipCount    = 1;

        XR_RETURN_FALSE(xrCreateSwapchain(_session, &scInfo, &sc.handle));

        // Enumerate swapchain images (GL textures)
        uint32_t imgCount = 0;
        xrEnumerateSwapchainImages(sc.handle, 0, &imgCount, nullptr);
        sc.images.resize(imgCount, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR });
        xrEnumerateSwapchainImages(sc.handle, imgCount, &imgCount,
            reinterpret_cast<XrSwapchainImageBaseHeader *>(sc.images.data()));

        ALOGV("Eye %u: swapchain %ux%u  %u images", eye, sc.width, sc.height, imgCount);
    }
    return true;
}

bool VROSceneRendererOpenXR::initPassthrough() {
    // Extension functions are NOT direct API calls — they must be loaded via
    // xrGetInstanceProcAddr. The extension guard (XR_FB_passthrough) was already
    // checked during instance creation; if we reach here it was enabled.

    auto loadFn = [&](const char *name, void **fn) -> bool {
        XrResult r = xrGetInstanceProcAddr(_instance, name, (PFN_xrVoidFunction *)fn);
        if (XR_FAILED(r)) {
            ALOGW("xrGetInstanceProcAddr('%s') failed: %d", name, (int)r);
            return false;
        }
        return (*fn != nullptr);
    };

    bool ok = true;
    ok &= loadFn("xrCreatePassthroughFB",       (void **)&_pfnCreatePassthrough);
    ok &= loadFn("xrDestroyPassthroughFB",      (void **)&_pfnDestroyPassthrough);
    ok &= loadFn("xrPassthroughStartFB",        (void **)&_pfnPassthroughStart);
    ok &= loadFn("xrPassthroughPauseFB",        (void **)&_pfnPassthroughPause);
    ok &= loadFn("xrCreatePassthroughLayerFB",  (void **)&_pfnCreatePassthroughLayer);
    ok &= loadFn("xrDestroyPassthroughLayerFB", (void **)&_pfnDestroyPassthroughLayer);
    ok &= loadFn("xrPassthroughLayerResumeFB",  (void **)&_pfnPassthroughLayerResume);
    ok &= loadFn("xrPassthroughLayerPauseFB",   (void **)&_pfnPassthroughLayerPause);

    if (!ok) {
        ALOGW("XR_FB_passthrough functions not fully available — passthrough disabled");
        return false;
    }

    // Create the XrPassthroughFB handle. Do NOT set the running-at-creation flag
    // so passthrough starts in paused state — enabled only on demand.
    XrPassthroughCreateInfoFB ptInfo = { XR_TYPE_PASSTHROUGH_CREATE_INFO_FB };
    ptInfo.flags = 0;
    XrResult r = _pfnCreatePassthrough(_session, &ptInfo, &_passthrough);
    if (XR_FAILED(r)) {
        ALOGE("xrCreatePassthroughFB failed: %d", (int)r);
        return false;
    }

    // Create a full-reconstruction layer. The layer itself starts running
    // (XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB) but we immediately pause it;
    // this avoids a stop/start round-trip on first enable.
    XrPassthroughLayerCreateInfoFB layerInfo = { XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB };
    layerInfo.passthrough = _passthrough;
    layerInfo.flags       = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
    layerInfo.purpose     = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
    r = _pfnCreatePassthroughLayer(_session, &layerInfo, &_passthroughLayer);
    if (XR_FAILED(r)) {
        ALOGE("xrCreatePassthroughLayerFB failed: %d", (int)r);
        _pfnDestroyPassthrough(_passthrough);
        _passthrough = XR_NULL_HANDLE;
        return false;
    }

    // Pause immediately — layer is ready but not composited until setPassthroughEnabled(true).
    _pfnPassthroughLayerPause(_passthroughLayer);

    ALOGV("XR_FB_passthrough initialised (paused — call setPassthroughEnabled(true) to enable)");
    return true;
}

void VROSceneRendererOpenXR::setPassthroughEnabled(bool enabled) {
    if (_passthrough == XR_NULL_HANDLE || _passthroughLayer == XR_NULL_HANDLE) {
        ALOGW("setPassthroughEnabled(%s): XR_FB_passthrough not available on this device",
              enabled ? "true" : "false");
        _passthroughEnabled = false;
        return;
    }

    if (enabled) {
        // Ensure the passthrough subsystem is running before resuming the layer.
        XR_CHECK(_pfnPassthroughStart(_passthrough));
        XR_CHECK(_pfnPassthroughLayerResume(_passthroughLayer));
    } else {
        // Pause the layer first, then pause the subsystem (saves power).
        XR_CHECK(_pfnPassthroughLayerPause(_passthroughLayer));
        XR_CHECK(_pfnPassthroughPause(_passthrough));
    }

    _passthroughEnabled = enabled;
    ALOGV("setPassthroughEnabled: %s", enabled ? "true" : "false");
}

void VROSceneRendererOpenXR::setHandTrackingEnabled(bool enabled) {
    if (_inputController) {
        _inputController->setHandTrackingEnabled(enabled);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Teardown
// ──────────────────────────────────────────────────────────────────────────────

void VROSceneRendererOpenXR::destroySwapchains() {
    for (auto &sc : _swapchains) {
        if (sc.handle != XR_NULL_HANDLE) {
            xrDestroySwapchain(sc.handle);
            sc.handle = XR_NULL_HANDLE;
        }
        sc.images.clear();
    }
}

void VROSceneRendererOpenXR::destroySession() {
    // Passthrough teardown — must use loaded function pointers, not direct calls.
    if (_passthroughLayer != XR_NULL_HANDLE && _pfnDestroyPassthroughLayer) {
        _pfnDestroyPassthroughLayer(_passthroughLayer);
        _passthroughLayer = XR_NULL_HANDLE;
    }
    if (_passthrough != XR_NULL_HANDLE && _pfnDestroyPassthrough) {
        _pfnDestroyPassthrough(_passthrough);
        _passthrough = XR_NULL_HANDLE;
    }
    destroySwapchains();
    if (_stageSpace != XR_NULL_HANDLE) {
        xrDestroySpace(_stageSpace);
        _stageSpace = XR_NULL_HANDLE;
    }
    if (_session != XR_NULL_HANDLE) {
        xrDestroySession(_session);
        _session = XR_NULL_HANDLE;
    }
}

void VROSceneRendererOpenXR::destroyEGLContext() {
    if (_eglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (_eglSurface  != EGL_NO_SURFACE) eglDestroySurface(_eglDisplay, _eglSurface);
        if (_eglContext  != EGL_NO_CONTEXT) eglDestroyContext(_eglDisplay, _eglContext);
        eglTerminate(_eglDisplay);
    }
    _eglDisplay = EGL_NO_DISPLAY;
    _eglContext = EGL_NO_CONTEXT;
    _eglSurface = EGL_NO_SURFACE;
}

void VROSceneRendererOpenXR::destroyOpenXR() {
    if (_instance != XR_NULL_HANDLE) {
        xrDestroyInstance(_instance);
        _instance = XR_NULL_HANDLE;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// VROSceneRenderer lifecycle
// ──────────────────────────────────────────────────────────────────────────────

void VROSceneRendererOpenXR::onStart() {
    ALOGV("onStart");
}

void VROSceneRendererOpenXR::onResume() {
    // Idempotent: must be safe to call multiple times. The Java side may end up
    // calling this from both onAttachedToWindow (manual catch-up for a missed
    // onResume) AND a later RN LifecycleEventListener.onHostResume firing.
    // std::thread::operator= terminates the process if the LHS is joinable, so
    // assigning a new thread on top of an already-running render thread aborts
    // the app. Re-entering when already running just clears the paused flag.
    if (_running) {
        ALOGV("onResume — already running, clearing paused flag");
        _paused = false;
        return;
    }
    ALOGV("onResume — starting render thread");
    _paused  = false;
    _running = true;
    _renderThread = std::thread(&VROSceneRendererOpenXR::renderLoop, this);
}

void VROSceneRendererOpenXR::onPause() {
    if (!_running || _paused) {
        ALOGV("onPause — already paused or not running, skipping");
        return;
    }
    ALOGV("onPause — pausing render thread");
    _paused = true;
    // The render loop checks _paused and idles; session state will transition
    // to VISIBLE or IDLE via xrPollEvent naturally.
}

void VROSceneRendererOpenXR::onStop() {
    ALOGV("onStop — stopping render thread");
    _running = false;
    if (_renderThread.joinable()) {
        _renderThread.join();
    }
}

void VROSceneRendererOpenXR::onDestroy() {
    onStop();
    if (_inputController) {
        _inputController->destroyHandTrackers();
        _inputController->destroySpaces();
    }
    destroySession();
    destroyEGLContext();
    destroyOpenXR();

    // Release global refs after the render thread is fully stopped.
    //
    // onDestroy is invoked from `Java_com_viro_core_Renderer_nativeDestroyRenderer`,
    // i.e. the JVM main thread already attached by the JNI bridge. Calling
    // DetachCurrentThread on a thread that's mid-JNI-call aborts ART with
    // "attempting to detach while still running code" (SIGABRT). Use GetEnv
    // to detect attached state and only Attach/Detach if we were actually
    // running on a non-attached thread.
    if (_jvm && _jview) {
        JNIEnv *env       = nullptr;
        bool    attached  = false;
        jint    res       = _jvm->GetEnv((void **) &env, JNI_VERSION_1_6);
        if (res == JNI_EDETACHED) {
            if (_jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
                attached = true;
            } else {
                env = nullptr;
            }
        }
        if (env) {
            env->DeleteGlobalRef(_jview);
            env->DeleteGlobalRef(_activity);
        }
        if (attached) {
            _jvm->DetachCurrentThread();
        }
        _jview    = nullptr;
        _activity = nullptr;
    }
    ALOGV("VROSceneRendererOpenXR destroyed");
}

void VROSceneRendererOpenXR::onTouchEvent(int /*action*/, float /*x*/, float /*y*/) {
    // Quest has no touchscreen — controller events handled in input controller
}

void VROSceneRendererOpenXR::onKeyEvent(int /*keyCode*/, int /*action*/) {}

void VROSceneRendererOpenXR::recenterTracking() {
    if (_session == XR_NULL_HANDLE) return;

    // Create a temporary VIEW space to locate the head pose.
    XrReferenceSpaceCreateInfo viewSpaceInfo = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    viewSpaceInfo.referenceSpaceType    = XR_REFERENCE_SPACE_TYPE_VIEW;
    viewSpaceInfo.poseInReferenceSpace  = { {0, 0, 0, 1}, {0, 0, 0} };
    XrSpace viewSpace = XR_NULL_HANDLE;
    if (XR_FAILED(xrCreateReferenceSpace(_session, &viewSpaceInfo, &viewSpace))) {
        ALOGE("recenterTracking: failed to create VIEW space");
        return;
    }

    // Locate head (VIEW) relative to the current stage space.
    // Use _lastPredictedDisplayTime so the pose is from the most recent frame.
    XrSpaceLocation headLoc = { XR_TYPE_SPACE_LOCATION };
    xrLocateSpace(viewSpace, _stageSpace, _lastPredictedDisplayTime, &headLoc);
    xrDestroySpace(viewSpace);

    if (!(headLoc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
        ALOGW("recenterTracking: head orientation not valid — skipped");
        return;
    }

    // Extract yaw from head orientation (keep scene upright — Y-axis rotation only).
    XrQuaternionf &q = headLoc.pose.orientation;
    float yaw = atan2f(2.0f * (q.w * q.y + q.x * q.z),
                       1.0f - 2.0f * (q.y * q.y + q.z * q.z));

    // Create a new LOCAL space centered at current head XZ position, facing forward.
    XrReferenceSpaceCreateInfo spaceInfo = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace.position    = { headLoc.pose.position.x,
                                                    0.0f,
                                                    headLoc.pose.position.z };
    spaceInfo.poseInReferenceSpace.orientation = { 0.0f,
                                                    sinf(yaw * 0.5f),
                                                    0.0f,
                                                    cosf(yaw * 0.5f) };

    XrSpace newSpace = XR_NULL_HANDLE;
    if (XR_SUCCEEDED(xrCreateReferenceSpace(_session, &spaceInfo, &newSpace))) {
        xrDestroySpace(_stageSpace);
        _stageSpace = newSpace;
        ALOGV("recenterTracking: recentered (yaw=%.2f rad)", yaw);
    } else {
        ALOGE("recenterTracking: xrCreateReferenceSpace failed");
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Render thread
// ──────────────────────────────────────────────────────────────────────────────

void VROSceneRendererOpenXR::renderLoop() {
    prctl(PR_SET_NAME, "VROOpenXRRender", 0, 0, 0);
    ALOGV("Render thread started");

    // Make the EGL context current on this thread (main thread released it in constructor)
    EGLBoolean eglOk = eglMakeCurrent(_eglDisplay, _eglSurface, _eglSurface, _eglContext);
    if (!eglOk) {
        ALOGE("eglMakeCurrent failed on render thread: 0x%x", eglGetError());
        return;
    }
    ALOGV("EGL context current on render thread");

    // Mark this thread as the Viro renderer thread so passert_thread() checks pass.
    VROThreadRestricted::setThread(VROThreadName::Renderer);

    // Attach to JVM so we can call back into Java each frame (drains mRenderQueue /
    // FrameListeners registered by the user from the React Native side).
    JNIEnv *env = nullptr;
    _jvm->AttachCurrentThread(&env, nullptr);
    jclass   viewCls       = env->GetObjectClass(_jview);
    jmethodID onDrawFrameM = env->GetMethodID(viewCls, "onDrawFrame", "()V");
    env->DeleteLocalRef(viewCls);

    while (_running) {
        pollEvents();

        if (_paused || _sessionState == XR_SESSION_STATE_IDLE ||
                       _sessionState == XR_SESSION_STATE_UNKNOWN) {
            usleep(100000);  // 100ms — don't burn CPU when idle/paused
            continue;
        }

        // OpenXR spec: the frame loop (xrWaitFrame/xrBeginFrame/xrEndFrame) must run
        // as soon as the session is running (_sessionRunning=true, set in READY handler
        // when xrBeginSession succeeds). The runtime only transitions from READY →
        // SYNCHRONIZED after it sees the first completed frame loop iteration.
        if (_sessionRunning) {
            renderFrame();

            // Drive Java FrameListeners / PlatformUtil render-thread callbacks
            // only when the app has visible content (VISIBLE or FOCUSED states).
            if (_sessionState == XR_SESSION_STATE_VISIBLE ||
                _sessionState == XR_SESSION_STATE_FOCUSED) {
                env->CallVoidMethod(_jview, onDrawFrameM);
            }
        }
    }

    _jvm->DetachCurrentThread();
    eglMakeCurrent(_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    ALOGV("Render thread exited");
}

void VROSceneRendererOpenXR::pollEvents() {
    XrEventDataBuffer event = { XR_TYPE_EVENT_DATA_BUFFER };
    while (xrPollEvent(_instance, &event) == XR_SUCCESS) {
        switch (event.type) {
            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                auto *stateEvent =
                    reinterpret_cast<XrEventDataSessionStateChanged *>(&event);
                handleSessionStateChange(stateEvent);
                break;
            }
            case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
                ALOGV("Reference space change pending — content may shift");
                break;
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
                ALOGE("Instance loss pending — shutting down");
                _running = false;
                break;
            default:
                break;
        }
        event = { XR_TYPE_EVENT_DATA_BUFFER };
    }
}

void VROSceneRendererOpenXR::handleSessionStateChange(
        XrEventDataSessionStateChanged *event) {

    _sessionState = event->state;
    ALOGV("Session state → %d", (int)_sessionState);

    switch (_sessionState) {
        case XR_SESSION_STATE_READY: {
            XrSessionBeginInfo beginInfo = { XR_TYPE_SESSION_BEGIN_INFO };
            beginInfo.primaryViewConfigurationType =
                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
            XR_CHECK(xrBeginSession(_session, &beginInfo));
            _sessionRunning = true;
            ALOGV("Session began");
            break;
        }
        case XR_SESSION_STATE_STOPPING:
            XR_CHECK(xrEndSession(_session));
            _sessionRunning = false;
            ALOGV("Session ended");
            break;
        case XR_SESSION_STATE_LOSS_PENDING:
        case XR_SESSION_STATE_EXITING:
            _running = false;
            break;
        default:
            break;
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Frame render
// ──────────────────────────────────────────────────────────────────────────────

void VROSceneRendererOpenXR::renderFrame() {
    // Drain pending renderer tasks (setSceneController, texture uploads, etc.)
    // submitted via VROPlatformDispatchAsyncRenderer from any thread.
    VROPlatformDrainRendererQueue();

    // ── Wait for the display ──────────────────────────────────────────────────
    XrFrameWaitInfo waitInfo  = { XR_TYPE_FRAME_WAIT_INFO };
    XrFrameState    frameState= { XR_TYPE_FRAME_STATE };
    XR_CHECK(xrWaitFrame(_session, &waitInfo, &frameState));
    _lastPredictedDisplayTime = frameState.predictedDisplayTime;

    // ── Begin frame ───────────────────────────────────────────────────────────
    XrFrameBeginInfo beginInfo = { XR_TYPE_FRAME_BEGIN_INFO };
    XR_CHECK(xrBeginFrame(_session, &beginInfo));

    // Locate eye views
    XrViewLocateInfo locateInfo = { XR_TYPE_VIEW_LOCATE_INFO };
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime            = frameState.predictedDisplayTime;
    locateInfo.space                  = _stageSpace;

    XrViewState viewState = { XR_TYPE_VIEW_STATE };
    uint32_t    viewCount = 2;
    XrView      views[2]  = { { XR_TYPE_VIEW }, { XR_TYPE_VIEW } };
    XR_CHECK(xrLocateViews(_session, &locateInfo, &viewState, 2, &viewCount, views));

    // Render each eye if views are valid
    std::vector<XrCompositionLayerProjectionView> projViews(2);

    bool viewsValid =
        (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) &&
        (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT);

    if (viewsValid && frameState.shouldRender) {
        // ── Prepare the Viro renderer (once per frame, before any eye render) ─
        // Use the left eye pose/fov to drive scene-level preparation (culling,
        // animation ticks, physics, etc.). Per-eye projection is applied in renderEye.
        if (_renderer && _renderer->hasRenderContext()) {
            VROViewport leftViewport(0, 0, _swapchains[0].width, _swapchains[0].height);

            // Convert OpenXR half-angle tangents (radians, signed) → degrees, positive
            constexpr float kRad2Deg = 180.0f / M_PI;
            const XrFovf &fov0 = views[0].fov;
            VROFieldOfView viroFov(
                -fov0.angleLeft  * kRad2Deg,   // left  half-angle (positive)
                 fov0.angleRight * kRad2Deg,   // right half-angle
                -fov0.angleDown  * kRad2Deg,   // bottom half-angle
                 fov0.angleUp    * kRad2Deg    // top half-angle
            );
            // headRotation must be rotation-only. VROMatrix4f::multiply(VROVector3f) always
            // adds the matrix's translation column (m[12..14]), so passing the full pose
            // matrix shifts the computed camera forward/up vectors by the head's stage-space
            // position (~1.6 m on Y), tilting the frustum upward and culling objects placed
            // at (0, 0, -2). Strip translation so only orientation drives the frustum.
            // Per-eye view matrices (which include head position) are computed in renderEye().
            VROMatrix4f headPoseMatrix  = xrPoseToMatrix(views[0].pose);
            headPoseMatrix[12] = 0.0f;
            headPoseMatrix[13] = 0.0f;
            headPoseMatrix[14] = 0.0f;
            VROMatrix4f leftProjMatrix  = xrFovToProjection(fov0);

            _renderer->prepareFrame(_frame++, leftViewport, viroFov,
                                    headPoseMatrix, leftProjMatrix, _driver);
        }

        // Process input after prepareFrame so the camera is valid for hit-testing.
        if (_inputController && _renderer && _renderer->hasRenderContext()) {
            _inputController->onProcess(_session, _stageSpace,
                                         frameState.predictedDisplayTime,
                                         _renderer->getCamera());
        }

        for (uint32_t eye = 0; eye < 2; ++eye) {
            renderEye((int)eye, views[eye], _swapchains[eye]);

            projViews[eye]                  = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
            projViews[eye].pose             = views[eye].pose;
            projViews[eye].fov              = views[eye].fov;
            projViews[eye].subImage.swapchain              = _swapchains[eye].handle;
            projViews[eye].subImage.imageRect.offset       = { 0, 0 };
            projViews[eye].subImage.imageRect.extent       = {
                (int32_t)_swapchains[eye].width,
                (int32_t)_swapchains[eye].height
            };
            projViews[eye].subImage.imageArrayIndex = 0;
        }

        // Tick the frame scheduler so that queued tasks (texture hydration,
        // model load callbacks, etc.) are processed. Must be called after
        // prepareFrame() and after all eye rendering is done.
        if (_renderer && _renderer->hasRenderContext()) {
            _renderer->endFrame(_driver);
        }
    }

    // ── End frame — assemble layer list ──────────────────────────────────────
    std::vector<const XrCompositionLayerBaseHeader *> layers;

    // Passthrough layer (behind projection)
    XrCompositionLayerPassthroughFB ptLayerComp = {
        XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB
    };
    if (_passthroughEnabled && _passthroughLayer != XR_NULL_HANDLE) {
        ptLayerComp.layerHandle = _passthroughLayer;
        ptLayerComp.flags       = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader *>(&ptLayerComp));
    }

    // Projection layer (3D scene)
    XrCompositionLayerProjection projLayer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    if (viewsValid && frameState.shouldRender) {
        projLayer.space      = _stageSpace;
        projLayer.viewCount  = 2;
        projLayer.views      = projViews.data();
        layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader *>(&projLayer));
    }

    XrFrameEndInfo endInfo = { XR_TYPE_FRAME_END_INFO };
    endInfo.displayTime          = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = _passthroughEnabled
                                       ? XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND
                                       : XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount  = (uint32_t)layers.size();
    // OpenXR spec: layers must be NULL when layerCount==0
    endInfo.layers      = layers.empty() ? nullptr : layers.data();

    static uint32_t sFrameLog = 0;
    if (sFrameLog++ < 10 || (sFrameLog % 90 == 0)) {
        ALOGV("xrEndFrame: state=%d shouldRender=%d viewsValid=%d layerCount=%u",
              (int)_sessionState, (int)frameState.shouldRender,
              (int)viewsValid, endInfo.layerCount);
    }

    XrResult endResult = xrEndFrame(_session, &endInfo);
    if (XR_FAILED(endResult)) {
        ALOGE("xrEndFrame FAILED: result=%d state=%d shouldRender=%d layerCount=%u",
              (int)endResult, (int)_sessionState,
              (int)frameState.shouldRender, endInfo.layerCount);
    }
}

void VROSceneRendererOpenXR::renderEye(int eyeIndex,
                                        const XrView &view,
                                        VROOpenXRSwapchain &swapchain) {
    // ── Acquire swapchain image ───────────────────────────────────────────────
    XrSwapchainImageAcquireInfo acquireInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    uint32_t imageIndex = 0;
    XR_CHECK(xrAcquireSwapchainImage(swapchain.handle, &acquireInfo, &imageIndex));

    XrSwapchainImageWaitInfo waitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    waitInfo.timeout = XR_INFINITE_DURATION;
    XR_CHECK(xrWaitSwapchainImage(swapchain.handle, &waitInfo));

    // ── Bind the FBO for this eye ─────────────────────────────────────────────
    GLuint colorTex = swapchain.images[imageIndex].image;

    auto display = _openxrDriver->getOpenXRDisplay();
    display->setSwapchainImage(colorTex,
                                (GLsizei)swapchain.width,
                                (GLsizei)swapchain.height);
    VROViewport viewport(0, 0, swapchain.width, swapchain.height);
    display->setViewport(viewport);
    display->bind();

    // ── Compute view + projection matrices ───────────────────────────────────
    // viewMatrix = inverted pose (world→eye transform for rendering)
    VROMatrix4f viewMatrix = xrPoseToMatrix(view.pose).invert();
    VROMatrix4f projMatrix = xrFovToProjection(view.fov);

    // ── Render the Viro scene for this eye ───────────────────────────────────
    if (_renderer && _renderer->hasRenderContext()) {
        VROEyeType eyeType = (eyeIndex == 0) ? VROEyeType::Left : VROEyeType::Right;
        _renderer->renderEye(eyeType, viewMatrix, projMatrix, viewport, _driver);
    } else {
        // Renderer not yet initialized — clear to black so the swapchain is valid.
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    // ── Release swapchain image ───────────────────────────────────────────────
    XrSwapchainImageReleaseInfo releaseInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    XR_CHECK(xrReleaseSwapchainImage(swapchain.handle, &releaseInfo));
}
