// VRODisplayOpenGLOpenXR.h
// ViroRenderer
//
// OpenXR display (render target) for one eye. Unlike OVR's ovrFramebuffer,
// OpenXR returns raw GL textures from the swapchain. We wrap each one in an
// FBO and bind it here when the eye render begins.
//
// Copyright © 2026 ReactVision. All rights reserved.
// MIT License — see LICENSE file.

#ifndef ANDROID_VRODISPLAYOPENGLOPENXR_H
#define ANDROID_VRODISPLAYOPENGLOPENXR_H

#include <memory>
#include <vector>
#include <GLES3/gl3.h>
#include <android/log.h>
#include "VROOpenGL.h"
#include "VRODisplayOpenGL.h"

#define XRDISPLAY_TAG "VRODisplayOpenXR"
#define XRDLOG(...) __android_log_print(ANDROID_LOG_VERBOSE, XRDISPLAY_TAG, __VA_ARGS__)
#define XRELOG(...) __android_log_print(ANDROID_LOG_ERROR,   XRDISPLAY_TAG, __VA_ARGS__)

class VRODriverOpenGL;

/*
 * Wraps the OpenXR swapchain images as a Viro render target. One instance
 * serves both eyes: before rendering each, call setSwapchainImage() with the
 * GL texture ID returned by XrSwapchainImageOpenGLESKHR, then bind().
 */
class VRODisplayOpenGLOpenXR : public VRODisplayOpenGL {
public:

    VRODisplayOpenGLOpenXR(std::shared_ptr<VRODriverOpenGL> driver)
        : VRODisplayOpenGL(0, driver),
          _fbo(0) {
    }

    virtual ~VRODisplayOpenGLOpenXR() {
        for (const SwapchainFramebuffer &framebuffer : _framebuffers) {
            glDeleteFramebuffers(1, &framebuffer.fbo);
        }
        for (const DepthBuffer &depth : _depthBuffers) {
            glDeleteRenderbuffers(1, &depth.rbo);
        }
    }

    /*
     * Called once per eye per frame, after xrAcquireSwapchainImage, with the
     * GL texture from XrSwapchainImageOpenGLESKHR.image. Each swapchain image
     * gets its FBO the first time it is seen, and keeps it: the swapchains
     * live as long as this display. FBOs of one size share a depth buffer,
     * which bind() clears for every eye.
     */
    void setSwapchainImage(GLuint colorTex, GLsizei width, GLsizei height) {
        for (const SwapchainFramebuffer &framebuffer : _framebuffers) {
            if (framebuffer.colorTex == colorTex) {
                _fbo = framebuffer.fbo;
                return;
            }
        }

        glGenFramebuffers(1, &_fbo);
        _framebuffers.push_back({ colorTex, _fbo });
        glBindFramebuffer(GL_FRAMEBUFFER, _fbo);

        // Try GL_TEXTURE_2D first; Quest may use GL_TEXTURE_2D_ARRAY even for arraySize=1
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, colorTex, 0);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            // Quest may use GL_TEXTURE_2D_ARRAY even for arraySize=1.
            // glFramebufferTextureLayer is GLES3.0 core — attach layer 0.
            XRELOG("GL_TEXTURE_2D attachment incomplete (0x%x), retrying as array layer 0", status);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, colorTex, 0, 0);
        }

        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, getDepthBuffer(width, height));

        status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            XRELOG("OpenXR: FBO still incomplete after retry, status=0x%x  tex=%u  %dx%d",
                   status, colorTex, (int)width, (int)height);
            // Do NOT abort — log and continue so we can see the status code on device
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // For mixed-reality (passthrough) the swapchain must be cleared TRANSPARENT
    // (alpha 0) so empty regions reveal the passthrough layer beneath. For fully
    // virtual VR it must be OPAQUE (alpha 1) so the background is solid black.
    // The renderer sets this when passthrough is toggled.
    void setClearAlpha(float alpha) { _clearAlpha = alpha; }

    void bind() {
        glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
        glViewport(_viewport.getX(), _viewport.getY(),
                   _viewport.getWidth(), _viewport.getHeight());
        glScissor(_viewport.getX(), _viewport.getY(),
                  _viewport.getWidth(), _viewport.getHeight());
        // Force the colour write mask fully on (incl. alpha) — Viro's cached state
        // can leave the alpha channel masked off, which would skip the alpha clear.
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0.0f, 0.0f, 0.0f, _clearAlpha);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        // Through the driver, which turns depth writes back on first. glClear
        // skips a buffer whose writes are masked, and the left eye's last
        // material (the aim laser, say) can leave them off, so the right eye
        // drew against stale depth.
        clearDepth();
    }

    /*
     * Called once an eye is drawn. Nothing reads its depth and stencil after
     * that (no depth layer goes to the compositor, and the next eye clears
     * them), so the GPU need not write them from tile memory back to main
     * memory. Not invalidate(): the driver also calls that when it switches
     * render targets partway through an eye.
     */
    void discardDepth() {
        glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
        const GLenum attachments[] = { GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT };
        glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, attachments);
    }

private:

    struct SwapchainFramebuffer {
        GLuint colorTex;
        GLuint fbo;
    };

    struct DepthBuffer {
        GLsizei width;
        GLsizei height;
        GLuint  rbo;
    };

    GLuint _fbo;  // the current eye's, one of _framebuffers
    std::vector<SwapchainFramebuffer> _framebuffers;
    std::vector<DepthBuffer> _depthBuffers;
    float  _clearAlpha = 1.0f;  // 0 for MR/passthrough, 1 for opaque VR

    GLuint getDepthBuffer(GLsizei width, GLsizei height) {
        for (const DepthBuffer &depth : _depthBuffers) {
            if (depth.width == width && depth.height == height) {
                return depth.rbo;
            }
        }
        GLuint rbo = 0;
        glGenRenderbuffers(1, &rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        _depthBuffers.push_back({ width, height, rbo });
        return rbo;
    }
};

#endif  // ANDROID_VRODISPLAYOPENGLOPENXR_H
