// VRODriverOpenGLAndroidOpenXR.h
// ViroRenderer
//
// Android OpenGL driver for Meta Quest / OpenXR. Identical structure to
// VRODriverOpenGLAndroidOVR but returns a VRODisplayOpenGLOpenXR display
// and reports Linear color rendering (Quest uses sRGB swapchain images).
//
// EGL context ownership: the context is created in VROSceneRendererOpenXR
// before xrCreateSession and must remain current on the render thread.
// This driver does NOT create the EGL context; it only manages the Viro
// abstractions that sit above it.
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#ifndef ANDROID_VRODRIVEROPENGLANDROIDOPENXR_H
#define ANDROID_VRODRIVEROPENGLANDROIDOPENXR_H

#include "VRODriverOpenGLAndroid.h"
#include "VRODisplayOpenGLOpenXR.h"

class VRODriverOpenGLAndroidOpenXR : public VRODriverOpenGLAndroid {
public:

    VRODriverOpenGLAndroidOpenXR(std::shared_ptr<gvr::AudioApi> gvrAudio)
        : VRODriverOpenGLAndroid(gvrAudio) {}

    virtual ~VRODriverOpenGLAndroidOpenXR() {}

    /*
     * Quest uses an sRGB swapchain, so we can always render in linear space.
     */
    VROColorRenderingMode getColorRenderingMode() override {
        return VROColorRenderingMode::Linear;
    }

    /*
     * Returns the VRODisplayOpenGLOpenXR singleton. The renderer calls
     * display->setSwapchainImage() each frame before binding.
     */
    std::shared_ptr<VRORenderTarget> getDisplay() override {
        if (!_display) {
            std::shared_ptr<VRODriverOpenGL> driver = shared_from_this();
            _display = std::make_shared<VRODisplayOpenGLOpenXR>(driver);
        }
        return _display;
    }

    /*
     * Convenience cast used by the renderer to call setSwapchainImage().
     */
    std::shared_ptr<VRODisplayOpenGLOpenXR> getOpenXRDisplay() {
        return std::dynamic_pointer_cast<VRODisplayOpenGLOpenXR>(getDisplay());
    }

private:
    std::shared_ptr<VRORenderTarget> _display;
};

#endif  // ANDROID_VRODRIVEROPENGLANDROIDOPENXR_H
