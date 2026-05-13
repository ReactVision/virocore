//
//  ViroKitVisionOS.h
//  ViroKit — visionOS umbrella header
//
//  Imports only what the Metal-only visionOS renderer supports in M0.
//  Deliberately excludes:
//    • UIKit / GLKit (not applicable on xros)
//    • ARKit iOS session / frame types
//    • Text rendering (VROText, VROTypeface, VROGlyph*) — freetype not yet ported
//    • Physics (bullet) — not yet recompiled for xros
//    • FBX loader (protobuf) — not yet recompiled for xros
//    • Audio — no-op in M0; override in VRODriverVisionOS
//    • OpenGL / Cardboard / GVR
//    • iOS-specific platform files (*iOS.h)
//
//  Render driver entry point: VRODriverVisionOS

#ifndef ViroKitVisionOS_h
#define ViroKitVisionOS_h

// ── Core ──────────────────────────────────────────────────────────────────────
#import <ViroKitVisionOS/VRODefines.h>
#import <ViroKitVisionOS/VRODriver.h>
#import <ViroKitVisionOS/VRORenderContext.h>
#import <ViroKitVisionOS/VRORenderParameters.h>
#import <ViroKitVisionOS/VRORendererConfiguration.h>
#import <ViroKitVisionOS/VROFrameListener.h>
#import <ViroKitVisionOS/VROFrameTimer.h>
#import <ViroKitVisionOS/VROFrameScheduler.h>
#import <ViroKitVisionOS/VROFrameSynchronizer.h>
#import <ViroKitVisionOS/VROChoreographer.h>
#import <ViroKitVisionOS/VRORenderPass.h>
#import <ViroKitVisionOS/VRORenderer.h>

// ── Scene graph ───────────────────────────────────────────────────────────────
#import <ViroKitVisionOS/VROScene.h>
#import <ViroKitVisionOS/VROSceneController.h>
#import <ViroKitVisionOS/VROCamera.h>
#import <ViroKitVisionOS/VRONodeCamera.h>
#import <ViroKitVisionOS/VRONode.h>
#import <ViroKitVisionOS/VROGeometry.h>
#import <ViroKitVisionOS/VROGeometryElement.h>
#import <ViroKitVisionOS/VROGeometrySource.h>
#import <ViroKitVisionOS/VROMaterial.h>
#import <ViroKitVisionOS/VROMaterialVisual.h>
#import <ViroKitVisionOS/VROTexture.h>
#import <ViroKitVisionOS/VROLight.h>
#import <ViroKitVisionOS/VROImage.h>
#import <ViroKitVisionOS/VROShaderModifier.h>
#import <ViroKitVisionOS/VROTransaction.h>
#import <ViroKitVisionOS/VROHitTestResult.h>
#import <ViroKitVisionOS/VROConstraint.h>
#import <ViroKitVisionOS/VROBillboardConstraint.h>

// ── Math ──────────────────────────────────────────────────────────────────────
#import <ViroKitVisionOS/VROQuaternion.h>
#import <ViroKitVisionOS/VROMatrix4f.h>
#import <ViroKitVisionOS/VROVector3f.h>
#import <ViroKitVisionOS/VROVector4f.h>
#import <ViroKitVisionOS/VROMath.h>
#import <ViroKitVisionOS/VROFrustum.h>
#import <ViroKitVisionOS/VROFrustumPlane.h>
#import <ViroKitVisionOS/VROFrustumBoxIntersectionMetadata.h>
#import <ViroKitVisionOS/VROBoundingBox.h>
#import <ViroKitVisionOS/VROPlane.h>
#import <ViroKitVisionOS/VROTriangle.h>

// ── Animation ─────────────────────────────────────────────────────────────────
#import <ViroKitVisionOS/VROAnimation.h>
#import <ViroKitVisionOS/VROAnimatable.h>
#import <ViroKitVisionOS/VROAnimationGroup.h>
#import <ViroKitVisionOS/VROAnimationChain.h>
#import <ViroKitVisionOS/VROExecutableAnimation.h>
#import <ViroKitVisionOS/VROPropertyAnimation.h>
#import <ViroKitVisionOS/VROMaterialAnimation.h>
#import <ViroKitVisionOS/VROTimingFunction.h>
#import <ViroKitVisionOS/VROTimingFunctionBounce.h>
#import <ViroKitVisionOS/VROTimingFunctionCubicBezier.h>
#import <ViroKitVisionOS/VROTimingFunctionEaseInEaseOut.h>
#import <ViroKitVisionOS/VROTimingFunctionEaseIn.h>
#import <ViroKitVisionOS/VROTimingFunctionEaseOut.h>
#import <ViroKitVisionOS/VROTimingFunctionLinear.h>
#import <ViroKitVisionOS/VROAction.h>
#import <ViroKitVisionOS/VROLazyMaterial.h>
#import <ViroKitVisionOS/VROMorpher.h>

// ── Shapes ────────────────────────────────────────────────────────────────────
#import <ViroKitVisionOS/VROBox.h>
#import <ViroKitVisionOS/VROSphere.h>
#import <ViroKitVisionOS/VROSurface.h>
#import <ViroKitVisionOS/VROPolygon.h>
#import <ViroKitVisionOS/VROPolyline.h>
#import <ViroKitVisionOS/VROShapeUtils.h>

// ── Model loaders (no static-lib deps) ────────────────────────────────────────
#import <ViroKitVisionOS/VROGLTFLoader.h>
#import <ViroKitVisionOS/VROOBJLoader.h>

// ── Util ──────────────────────────────────────────────────────────────────────
#import <ViroKitVisionOS/VROTime.h>
#import <ViroKitVisionOS/VROLog.h>
#import <ViroKitVisionOS/VROByteBuffer.h>
#import <ViroKitVisionOS/VROImageUtil.h>
#import <ViroKitVisionOS/VROData.h>
#import <ViroKitVisionOS/VROGeometryUtil.h>
#import <ViroKitVisionOS/VROTaskQueue.h>

// ── visionOS Metal driver ────────────────────────────────────────────────────
#import <ViroKitVisionOS/VRODriverMetal.h>
#import <ViroKitVisionOS/VRODriverVisionOS.h>
#import <ViroKitVisionOS/VRORenderTargetMetal.h>

#endif /* ViroKitVisionOS_h */
