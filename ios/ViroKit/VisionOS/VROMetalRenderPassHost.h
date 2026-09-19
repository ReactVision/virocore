//
//  VROMetalRenderPassHost.h
//  ViroKit — visionOS
//
//  Copyright © 2026 ReactVision. All rights reserved.
//
//  The seam between a render target and the object that owns the frame's Metal
//  command buffer.
//
//  OpenGL lets a render target bind itself: glBindFramebuffer is global state, so
//  VRORenderTarget::bind() needs no collaborator. Metal has no such global state —
//  binding a target means beginning a MTLRenderCommandEncoder from the frame's
//  command buffer with that target's MTLRenderPassDescriptor. VRORenderTarget::bind()
//  takes no driver argument, so the target needs a way back to the command buffer.
//
//  VRODriverVisionOS implements this interface and hands itself to every render
//  target it creates.

#ifndef VROMetalRenderPassHost_h
#define VROMetalRenderPassHost_h

#include "VRODefines.h"
#if VRO_METAL

#include <Metal/Metal.h>
#include <memory>

class VRODriver;
class VROMetalFrameTimer;

class VROMetalRenderPassHost {
public:

    virtual ~VROMetalRenderPassHost() {}

    /*
     The driver itself, for the few VRORenderTarget entry points that need one to
     reach a texture's substrate.
     */
    virtual std::shared_ptr<VRODriver> getRenderPassDriver() = 0;

    /*
     The command buffer for the frame currently being encoded, or nil outside a
     frame. Offscreen passes append their encoders to this buffer, so they are
     submitted in the same commit as the display pass.
     */
    virtual id <MTLCommandBuffer> getFrameCommandBuffer() = 0;

    /*
     Report that a render target began a new encoder, so the driver can route
     subsequent state changes (cull mode, viewport) to it.
     */
    virtual void onRenderTargetEncoderBegan(id <MTLRenderCommandEncoder> encoder) = 0;

    /*
     End the encoder currently in flight. Called before a target begins its own:
     Metal permits only one render command encoder per command buffer at a time,
     so every target switch is an end-then-begin.
     */
    virtual void endActiveEncoder() = 0;

    /*
     The frame timer, or null when frame timing is off. Render targets register their pass
     with it so the report can attribute GPU time per pass rather than only per frame.
     */
    virtual VROMetalFrameTimer *getFrameTimer() = 0;
};

#endif  // VRO_METAL
#endif  // VROMetalRenderPassHost_h
