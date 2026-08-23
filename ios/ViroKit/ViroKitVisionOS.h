//
//  ViroKitVisionOS.h
//  ViroKit — visionOS umbrella header
//
//  Public surface of the Metal-only visionOS renderer.
//
//  Includes are quoted, not <ViroKitVisionOS/...>: this ships inside a static-library
//  xcframework whose Headers/ directory is flat, so there is no framework module for angle
//  brackets to resolve against. build_visionos.sh also stages this file as ViroKit/ViroKit.h,
//  because 32 files in ios/ViroReact import <ViroKit/ViroKit.h> — the name the iOS framework
//  uses. Until 2026-08-23 neither form resolved and nothing compiled this header at all.
//
//  Deliberately excludes:
//    • UIKit / GLKit (not applicable on xros)
//    • ARKit iOS session / frame types — the AR subsystem is not in this target
//    • FBX loader (protobuf) — not recompiled for xros
//    • Audio — no backend on visionOS; VRODriverVisionOS overrides it
//
//  Text and physics ARE included: freetype and bullet were built for xros in M5.
//    • OpenGL / Cardboard / GVR
//    • iOS-specific platform files (*iOS.h)
//
//  Render driver entry point: VRODriverVisionOS

#ifndef ViroKitVisionOS_h
#define ViroKitVisionOS_h

// ── Core ──────────────────────────────────────────────────────────────────────
#import "VRODefines.h"
#import "VRODriver.h"
#import "VRORenderContext.h"
#import "VRORenderParameters.h"
#import "VRORendererConfiguration.h"
#import "VROFrameListener.h"
#import "VROFrameTimer.h"
#import "VROFrameScheduler.h"
#import "VROFrameSynchronizer.h"
#import "VROChoreographer.h"
#import "VRORenderPass.h"
#import "VRORenderer.h"

// ── Scene graph ───────────────────────────────────────────────────────────────
#import "VROScene.h"
#import "VROSceneController.h"
#import "VROCamera.h"
#import "VRONodeCamera.h"
#import "VRONode.h"
#import "VROGeometry.h"
#import "VROGeometryElement.h"
#import "VROGeometrySource.h"
#import "VROMaterial.h"
#import "VROMaterialVisual.h"
#import "VROTexture.h"
#import "VROLight.h"
#import "VROImage.h"
#import "VROShaderModifier.h"
#import "VROTransaction.h"
#import "VROHitTestResult.h"
#import "VROConstraint.h"
#import "VROBillboardConstraint.h"

// ── Math ──────────────────────────────────────────────────────────────────────
#import "VROQuaternion.h"
#import "VROMatrix4f.h"
#import "VROVector3f.h"
#import "VROVector4f.h"
#import "VROMath.h"
#import "VROFrustum.h"
#import "VROFrustumPlane.h"
#import "VROFrustumBoxIntersectionMetadata.h"
#import "VROBoundingBox.h"
#import "VROPlane.h"
#import "VROTriangle.h"

// ── Animation ─────────────────────────────────────────────────────────────────
#import "VROAnimation.h"
#import "VROAnimatable.h"
#import "VROAnimationGroup.h"
#import "VROAnimationChain.h"
#import "VROExecutableAnimation.h"
#import "VROPropertyAnimation.h"
#import "VROMaterialAnimation.h"
#import "VROTimingFunction.h"
#import "VROTimingFunctionBounce.h"
#import "VROTimingFunctionCubicBezier.h"
#import "VROTimingFunctionEaseInEaseOut.h"
#import "VROTimingFunctionEaseIn.h"
#import "VROTimingFunctionEaseOut.h"
#import "VROTimingFunctionLinear.h"
#import "VROAction.h"
#import "VROLazyMaterial.h"
#import "VROMorpher.h"

// ── Shapes ────────────────────────────────────────────────────────────────────
#import "VROBox.h"
#import "VROSphere.h"
#import "VROSurface.h"
#import "VROPolygon.h"
#import "VROPolyline.h"
#import "VROShapeUtils.h"

// ── Input and events ──────────────────────────────────────────────────────────
// Required by the VRT layer: VRTNode and friends declare VROEventDelegateProtocol
// conformance, so without these the whole view layer fails to compile even though
// nothing yet feeds the input controller an event.
#import "VROInputState.h"
#import "VROEventDelegate.h"

// ── Video texture (interface only) ────────────────────────────────────────────
// VROVideoTexture.h is in this target; VROVideoTextureiOS.cpp is NOT, so the VRT
// video views compile against this and then fail to link. Declared here so the
// failure is a missing symbol at link time rather than a missing header, which is
// the more honest error: the interface exists, the backend does not.
#import "VROVideoTexture.h"

// ── Model loaders (no static-lib deps) ────────────────────────────────────────
#import "VROGLTFLoader.h"
#import "VROOBJLoader.h"
#import "VROHDRLoader.h"

// ── Util ──────────────────────────────────────────────────────────────────────
#import "VROTime.h"
#import "VROLog.h"
#import "VROByteBuffer.h"
#import "VROImageUtil.h"
#import "VROData.h"
#import "VROGeometryUtil.h"
#import "VROTaskQueue.h"

// ── visionOS Metal driver ────────────────────────────────────────────────────
#import "VRODriverMetal.h"
#import "VRODriverVisionOS.h"
#import "VRORenderTargetMetal.h"

// ── Text (freetype, built for xros in M5) ────────────────────────────────────
// 59 symbols in the archive. The enums the VRT layer needs — VROTextHorizontalAlignment,
// VROTextClipMode, VROLineBreakMode, VROTextOuterStroke — come from these two.
#import "VROText.h"
#import "VROTextFormatter.h"
// VRODriver.h forward-declares VROFontStyle and VROFontWeight; VROTypeface.h defines them.
// Without this, VRTText fails on "incomplete type named in nested name specifier" — which
// reads like a broken enum rather than a missing include.
#import "VROTypeface.h"
#import "VROTypefaceCollection.h"

// ── Particles ─────────────────────────────────────────────────────────────────
// VROParticleSpawnVolume is declared in VROParticleEmitter.h, not in the modifier header.
#import "VROParticleEmitter.h"
#import "VROParticleModifier.h"

// ── FBX loader (fails cleanly here, but declared) ────────────────────────────
// Same reasoning as VROARShadow below: VRT3DObject calls VROFBXLoader on its .vrx branch,
// and Viro3DObject works here for GLB/GLTF. The stub in VROVisionOSRenderStubs.cpp reports
// failure through the callback, so only .vrx sources fail — not the component.
#import "VROFBXLoader.h"

// ── AR shadow (no-op here, but declared) ──────────────────────────────────────
// The only AR header in this umbrella, and it earns its place: VRTQuad and VRTPolygon call
// VROARShadow::apply() for the arShadowReceiver property, and both ViroQuad and ViroPolygon
// work fine on visionOS otherwise. VROVisionOSRenderStubs.cpp gives it a no-op body, so the
// declaration and the symbol agree — the property simply does nothing here.
#import "VROARShadow.h"

// ── Image and texture utilities ───────────────────────────────────────────────
#import "VROImageiOS.h"
#import "VROTextureUtil.h"

// ── Delegate protocols (header-only) ─────────────────────────────────────────
// ObjC protocol declarations with inline C++ adapters and no implementation in this
// target. Every VRT view declares conformance to VROEventDelegateProtocol and
// VROTransformDelegateProtocol, so omitting these fails the whole React view layer on a
// missing type rather than on anything to do with the renderer.
//
// The AR delegate protocols are absent on purpose — the AR subsystem is not in this
// target, so those views cannot link here anyway.
// The C++ bases first: each *DelegateiOS below derives from one of these, and VRONode.h
// only forward-declares them. Getting this order wrong fails with "base class has
// incomplete type", which reads like a corrupt header rather than a missing import.
#import "VROTransformDelegate.h"
#import "VROPortalDelegate.h"
#import "VROPhysicsBodyDelegate.h"
#import "VROVideoDelegateInternal.h"

// The renderer-host protocol. VRORendererBridge is what implements this role on visionOS;
// the protocol itself is what the React view layer declares its properties against.
#import "VROView.h"

#import "VROEventDelegateiOS.h"
#import "VROTransformDelegateiOS.h"
#import "VROSceneDelegateiOS.h"
#import "VROPortalDelegateiOS.h"
#import "VROPhysicsBodyDelegateiOS.h"
#import "VRORenderDelegate.h"
#import "VROVideoDelegateiOS.h"

#endif /* ViroKitVisionOS_h */
