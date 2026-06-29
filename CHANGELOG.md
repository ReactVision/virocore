# CHANGELOG

## v2.57.2 — 29 June 2026

### Fixed

- **16 KB page-size alignment completed for all bundled native libraries (Android).** v2.57.0 aligned `libopenxr_loader.so`, but `libc++_shared.so` was still 4 KB-aligned (`2**12`) and continued to fail the 16 KB memory-page requirement for Android 15+ / Google Play. Unlike the libraries Viro compiles — which already honour `-Wl,-z,max-page-size=16384` — `libc++_shared.so` is a prebuilt copied verbatim from the NDK sysroot, so the linker flag can't re-align it, and NDK r21–r26 ship it at 4 KB. The Android NDK was bumped from r25 (`25.2.9519653`) to r27 (`27.1.12297006`), whose `libc++_shared.so` is 16 KB-aligned, across all native modules (`sharedCode`, `viroreact`, `viroar`, `virocore`, and `reactvisioncca`). Every 64-bit (`arm64-v8a`) library now reports ≥ 16 KB segment alignment — `libc++_shared.so` plus the Viro / Bullet / freetype / ARCore / OpenXR libraries at 16 KB (`2**14`), and `libvrapi.so` / `libgvr*.so` at 64 KB (`2**16`) — verified with Google's `check_elf_alignment.sh` (0 unaligned). Apps bundling ViroCore can now pass Google Play's 16 KB device requirement.

---

## v2.57.1 — 27 June 2026

### Added

- **Mixed Reality on Meta Quest — `VROARSessionOpenXR`.** A new `VROARSession` subclass brings the AR component API (`VROARScene` / `VROARPlaneAnchor` / the anchor delegate) to the OpenXR backend, mirroring `VROARSessionARCore` so the entire anchor → delegate → JNI → `ARScene.Listener` → JS chain is reused unchanged. Plane detection is sourced from the Quest **room model** via `XR_FB_scene` / `XR_FB_spatial_entity` / `XR_FB_spatial_entity_query`: an async `xrQuerySpacesFB` finds room spaces, `xrSetSpaceComponentStatusFB` enables the `LOCATABLE` component, and each `BOUNDED_2D` space is turned into a `VROARPlaneAnchor` via `xrGetSpaceBoundingBox2DFB` + `xrGetSpaceSemanticLabelsFB` + `xrGetSpaceBoundary2DFB` + `xrLocateSpace`. `VROSceneRendererOpenXR` owns the session, drives it each frame after `xrLocateViews`, and wires it to the scene's AR delegate when a `VROARScene` is set (auto-enabling passthrough). Requires the `horizonos.permission.USE_ANCHOR_API` Spatial Data permission and a completed Space Setup; `XR_EXT_plane_detection` is also wired as a graceful fallback for runtimes that expose it. Semantic labels map to Viro classifications (`FLOOR`→Floor, `WALL_FACE`→Wall, `CEILING`→Ceiling, `DESK`/`TABLE`→Table, …).

- **Passthrough layer styling.** `VROSceneRendererOpenXR::setPassthroughStyle(opacity, edgeRGBA)` loads `xrPassthroughLayerSetStyleFB` and applies an `XrPassthroughStyleFB` (texture opacity factor + edge colour) to the passthrough layer. Surfaced through `Renderer.nativeSetPassthroughStyle` and `ViroViewOpenXR.setPassthroughStyle`.

### Fixed

- **Passthrough black background on Quest.** A mixed-reality `VROARScene` has neither a skybox nor an ARCore camera quad to fill the view, so the swapchain kept stale opaque content and the projection layer hid the passthrough layer beneath it. `VRODisplayOpenGLOpenXR::bind()` now clears with a configurable `_clearAlpha` (0 for passthrough, 1 for opaque VR), set from `setPassthroughEnabled`; the projection composition layer is submitted with `XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT` (+ unpremultiplied) over an OPAQUE environment blend mode with the passthrough layer as an underlay. Empty regions now reveal the room.

- **Passthrough / hand-tracking state lost before renderer init (`ViroViewOpenXR`).** The native `Renderer` is created lazily (deferred to the host Activity's first resume), so `setPassthroughEnabled` / `setHandTrackingEnabled` / `setPassthroughStyle` calls that arrived during view mount were silently dropped. The desired values are now cached and re-applied inside `initRenderer()`, mirroring how the pending scene is deferred.

### Improved

- **OpenXR renderer diagnostics cleanup.** Removed the bring-up-era diagnostics from `VROSceneRendererOpenXR` (per-frame alpha read-back probe, environment-blend-mode enumeration log, passthrough-only debug flag, redundant per-frame clear-colour re-assert) now that the passthrough composite path is validated.

---

## v2.57.0 — 19 June 2026

### Added

- **`ViroObjectDetector` — on-device object detection.** A new component that runs open-vocabulary [YOLOE](https://docs.ultralytics.com/models/yoloe/) detection through ONNX Runtime, on-device and offline. It runs inside an AR session — sharing the enclosing `ViroARSceneNavigator`'s camera feed — and fires `onDetection` with labels, confidences, and bounding boxes. Each detection also carries a `screenBoundingBox` in density-independent points (dp), aligned to the on-screen camera preview, so boxes drop straight into an absolutely-positioned overlay. iOS and Android reach parity for detection and the 2D overlay (`worldPosition` 3D raycast remains iOS-only for now). Inference is delegated to the companion package **`@reactvision/react-viro-onnx`** (Android uses the NNAPI execution provider with FP16); there is no built-in fallback — without the provider the detector emits no detections and fires `onError`. See `docs/ViroObjectDetector.md`.

- **`onDepthReady` event on `ViroARScene`.** Fires once, when AR depth first becomes available, on both iOS and Android. Use it to gate depth-dependent features (occlusion, hit-testing) until the depth subsystem is actually producing data.

- **Expo SDK 56 support.** The config plugin and prebuilt artifacts now build against Expo 56 / React Native's new architecture.

### Improved

- **iOS depth precision.** Depth points are now derived directly from the AR depth map rather than approximated, and the monocular depth model is warmed up ahead of first use — eliminating inaccurate/late depth on the first frames and tightening occlusion and hit-test accuracy on non-LiDAR devices.

### Fixed

- **Layered / stacked GLB animations freezing instead of playing (VIRO-5741).** glTF skeletal clips whose channels mixed STEP and LINEAR interpolation or sat on multiple independent time-grids (common in Blender exports with layered animations) were being dropped or flattened, causing the animation to freeze. Channels are now resampled onto a single common time-grid and merged per joint into one index-aligned keyframe animation, with a per-frame density cap to bound skinning cost on pathological assets. Affected clips now play through fully.

- **16 KB page-size alignment for `libopenxr_loader.so` (Android).** The bundled OpenXR loader library, `libopenxr_loader.so`, previously used 4 KB alignment and failed the 16 KB memory-page requirement for Android 15+ devices, blocking Google Play / Meta Quest Store submission. The loader was updated from 1.1.38 to 1.1.49, which is 16 KB-page aligned. Apps that don't use XR are unaffected.

- **VR controller input** — upgraded the VR event listener path to the new architecture, restoring controller controls in VR.

- **`onDrag` in `StudioSceneNavigator`** — drag events now fire correctly.

- **iOS pod build** — visionOS-only sources are now excluded from the iOS CocoaPods build, fixing compile errors in iOS-only targets.

### Experimental / Preview

- **visionOS renderer.** Initial visionOS support landed: a Metal-based renderer/driver, the React Native bindings, and the renderer bridge. This is a work-in-progress foundation and not yet production-ready.

---

## v2.56.0 — 04 June 2026

### Added

- **Dynamic mesh node** — `VRODynamicMeshNode` and its backing `VRODynamicGeometry` allow vertex buffers (positions, normals, UVs, vertex colors) to be replaced every frame without recreating the node or its GPU resources. `VROVertexBuffer` gains a usage hint (`Static` / `Dynamic` / `Stream`) and an in-place `updateData()` path, implemented in `VROVertexBufferOpenGL` via buffer orphaning and `glBufferSubData`.

- **Virtual controller registry** — `VROInputState` and `VROVirtualControllerRegistry` provide a process-wide, thread-safe input state layer for game-style applications. Adapters write stick, trigger, and button state; C++ game-loop consumers read an atomic snapshot at frame rate with no JS bridge involvement.

- **PCM audio streaming** — a lock-free SPSC ring buffer (`VROPCMRingBuffer`) feeds an `AVAudioSourceNode` render block on iOS and an `AudioTrack MODE_STREAM` path on Android, enabling runtime PCM push without loading a complete audio file. `VROAudioPlayer` gains `beginStreaming` / `pushSamples` / `isStreaming` virtuals; `VRODriver` gains a `newStreamingAudioPlayer()` factory.

- **AR World Mesh — public subscriber API** — `VROARWorldMesh` becomes a general-purpose multi-consumer mesh provider. `subscribe(callback, options)` / `unsubscribe(id)` deliver full `VROARDepthMesh` geometry, a source enum (`LiDAR` / `Monocular` / `Plane`), and stats to each registered consumer. Additional work in this release: persistent mesh sourcing from `ARMeshAnchor` (iOS LiDAR), plane-based fallback for non-LiDAR/non-Depth-API devices, per-consumer triangle decimation (`VROWorldMeshSubscriberOptions::maxTriangles`), asynchronous Bullet BVH construction to prevent render-thread stalls, vertex clustering (`clusterMesh`) for gap-free collision meshes, depth-tested debug wireframe, smart-pointer lifecycle for Bullet rigid bodies, and full Android end-to-end repair for ARCore depth activation and plane mesh generation.

- **Game loop** — `VROGameLoopListener` wraps `VROFrameListener` and exposes variable-dt, late-update, and deterministic fixed-step callbacks. `setFixedHz(hz)` installs a dt accumulator that fires at an exact interval regardless of render frame rate.

- **Particle alpha fix** — the particle shader now reads `particleColor.w` for source alpha instead of a hardcoded `0.5`, unblocking correct transparency for all particle effects.

### Improved

- **Monocular depth pipeline** (iOS) — the depth estimation model has been upgraded to Depth Anything V2 (metric, indoor), which outputs depth in meters and significantly improves occlusion accuracy on non-LiDAR devices. Additional improvements: in-place GPU texture updates via `glTexSubImage2D` (eliminates per-frame `VROTexture` allocation), temporal confidence synthesis gating hit-test depth upgrades, calibration modes (`None` / `Manual` / `LiDARReference` stub), full orientation support in `getImageOrientation()` and `getDepthTextureTransform()`, monocular depth wired into the world mesh generator as a fallback source, thermal throttling via UTILITY QoS queue and adaptive FPS cap, and per-source occlusion bias (LiDAR 8%, monocular 0%).

### Fixed

- **SIGABRT on Android 14+ (API 34)** — `ARUtilsCreateJavaARAnchorFromAnchor` called `NewStringUTF` with a non-Modified-UTF-8 ARCore anchor ID, causing a hard crash on Android 14 and above.

---

## v2.55.0 — 27 April 2026

> **Install path.** Bare React Native is not tested for this release. It
> should work but will require a substantial amount of manual wiring (the
> `VRActivity` Android Activity, Quest manifest features, package
> registration in `MainApplication.kt`, iOS Podfile entries). For now we
> recommend the **Expo Dev Client**, where the Viro Expo plugin emits all
> of the above automatically. Bare RN support will be revisited in a
> follow-up release.

### Added

- **Meta Horizon OS support — full OpenXR backend**

  Native VR rendering on Meta Quest 3 / Quest Pro / Quest 2 / Quest 1 via
  Khronos OpenXR. Validated end-to-end at 90Hz on Quest 3 with App=5–6 ms
  frame time. The integration spans renderer, view lifecycle, input system,
  and presenter:

  - **`ViroViewOpenXR.java`** — Android view selected by
    `VRTVRSceneNavigator` when the package was registered with
    `ViroPlatform.QUEST`. Defers `Renderer` construction until
    `onAttachedToWindow` so `xrCreateSession` binds to the Activity that
    actually owns the surface (resolved via window-token matching against
    live activities, the only signal stable under Android's multi-resumed
    mode). Idempotent `onResume` / `onPause` (`mResumed` boolean) to
    tolerate the dual-source RN lifecycle. Subscribes to
    `Application.ActivityLifecycleCallbacks` directly because RN's
    `LifecycleEventListener.onHostResume` is binary across the dual-Activity
    setup. Null-guards every base-class setter / getter that touches the
    deferred renderer.

  - **`VROSceneRendererOpenXR.cpp`** — C++ scene renderer. OpenXR session
    state machine (`XR_SESSION_STATE_IDLE` → `READY` → `SYNCHRONIZED` →
    `VISIBLE` → `FOCUSED`), per-frame loop with `xrWaitFrame` /
    `xrBeginFrame` / `xrEndFrame`, projection-layer compositor submission,
    swapchain image acquisition / release. Idempotent `onResume` /
    `onPause` (early-return when already in target state) to prevent the
    `std::thread` reassignment SIGABRT when lifecycle events fire from
    multiple sources.

  - **`VROInputControllerOpenXR.{h,cpp}`** — action-based input controller.
    Per-frame `xrSyncActions` for Touch controllers (triggers, grips,
    A/B/X/Y, menu, thumbsticks, haptics) and per-hand `xrLocateHandJointsEXT`
    + `XR_FB_hand_tracking_aim` for fingertip-aimed pointing. Pinch-to-
    click + grip-to-grab edge detection per hand. **Two-pointer dispatch**:
    captures right and left aim poses without dispatching, then dispatches
    once per side via a `dispatchSide` lambda — `updateHitNode(source, …)`
    → `onMove(source, …)` → `processGazeEvent(source)` → laser update.
    Per-source pose hysteresis (`stickyPose` cache, ~5-frame survival) so
    the aim source doesn't flip between controller and synthesized hand
    pose on transient single-frame `xrLocateSpace` invalidity.

  - **`VROInputPresenterOpenXR.h`** — visual presenter. World-space
    (non-headlocked) reticle + per-source `unordered_map<int, Laser>` of
    cyan `VROPolyline` children under the presenter `_rootNode`, lazy-
    initialized on first dispatch and hidden when no aim is available.
    Both right and left lasers can be visible simultaneously.

  - **OpenXR extensions used:** `XR_KHR_opengl_es_enable`,
    `XR_KHR_android_create_instance`, `XR_FB_passthrough`,
    `XR_FB_display_refresh_rate`, `XR_EXT_hand_tracking`,
    `XR_FB_hand_tracking_aim`. `com.oculus.feature.PASSTHROUGH` must be
    declared in the manifest for `XR_FB_passthrough` to be advertised by
    the runtime.

- **Per-source hit / hover / click state in `VROInputControllerBase`**

  New maps `_hitResultsBySource`, `_lastHoveredNodesBySource`,
  `_lastClickedNodesBySource` keyed by input source ID. New overload
  `updateHitNode(int source, camera, origin, ray)` writes both the source
  map and the legacy single-source `_hitResult` for backward compat. New
  helper `getHitResultForSource(source)` falls back to the legacy field
  when no per-source entry exists. `processGazeEvent` and `onButtonEvent`
  use the source-aware lookups when an entry is present; otherwise behave
  exactly as before.

  **Backward compatible.** iOS / AR / Cardboard / OVR / Daydream / Wasm
  backends — which all call `updateHitNode(camera, origin, ray)` without a
  source — see zero behavior change. Only the OpenXR backend opts into
  per-source state by calling the new overload.

- **Studio scenes API in `libreactvisioncca`**

  Two new endpoints in the Cloud Anchor SDK that power the Studio scene
  fetcher in viro:

  - `getScene(sceneId, callback)` — returns the full scene response:
    metadata, asset list, animations, collision bindings, scene functions,
    project info. Wraps `GET /functions/v1/scenes/{scene_id}`.
  - `getSceneAssets(sceneId, callback)` — asset-list-only variant for
    clients with cached scene metadata. Wraps
    `GET /functions/v1/scenes/{scene_id}/assets`.

  Both authenticate via the project API key and return the response inside
  the existing `ApiResult<T>` wrapper. The full asset shape
  (`SceneAPIAsset`, `SceneData`) is added to `RVCCAApiTypes.h`.

  In viro, exposed to JS via
  `ViroARSceneNavigator.rvGetScene(sceneId)` and
  `ViroARSceneNavigator.rvGetSceneAssets(sceneId)`. Existing Cloud Anchor
  and Geospatial endpoints are unchanged.

### Fixed

- **16 KB `.so` page alignment for `libvrapi.so`** *(Issue A)*

  The Android 2025 ABI requires all shipped `.so` files to align to 16 KB
  pages. `libvrapi.so` is now repackaged with
  `-Wl,-z,max-page-size=16384`, resolving load failures on devices with the
  new page size.

- **AR Image Markers — children pin to screen coordinates after re-detection**
  *(Android, [GitHub viro#465](https://github.com/ReactVision/viro/issues/465))*

  Models parented to a `ViroARImageMarker` no longer become fixed at screen
  coordinates after the target was lost and re-acquired in v2.54.0. Markers
  re-anchor cleanly to the detected world pose every time, including
  subsequent re-detection.

- **iOS `ViroPortalScene` portal-tree stability**
  *([GitHub viro#452](https://github.com/ReactVision/viro/issues/452))*

  Continued portal-render-pass hardening on top of the v2.54.0 fix
  (`VROPortalTreeRenderPass.cpp` + `VROPortal.cpp`):
  - Portal stencil silhouette no longer drops transparent entry fragments
    before the alpha-discard modifier runs
    (`_silhouetteMaterial->setAlphaCutoff(0.0f)`).
  - 360° background inside a portal is no longer overwritten by the AR
    camera background drawn afterwards (depth=0.9999 + depth-write enabled
    on the portal background sphere/cube via
    `setBackgroundSphere`/`setBackgroundCube`).
  - The interior of a portal hole no longer reveals the portal interior
    when the user is *outside* a nested exit-frame portal — skip stencil
    DECR for `isExit=true`, use `Equal` stencil function when
    `anyChildIsExit=true`.
  - AR occlusion is disabled inside the portal interior (`recursionLevel > 0`)
    so virtual content is no longer discarded by depth-based occlusion in
    nested portals.

- **OpenXR input dispatch — stable hover and click on Quest**

  - Eliminated the dual hover / laser dispatch on the right side that caused
    `onHover` enter/exit oscillation when the controller pose and the
    synthesized hand pose both reported valid in the same frame.
    `processHands` no longer runs hit-test or laser updates internally;
    `onProcess` consolidates them into a single dispatch per source per
    frame.
  - `updateAimRay(source, …, visible=false)` is now called every frame on
    sides with no valid aim, so the laser hides cleanly instead of freezing
    at the last valid pose when tracking momentarily drops.
  - Pose hysteresis caches each source's last valid pose for ~5 frames so
    the aim source no longer flips between controller and synthesized
    hand pose on transient one-frame `xrLocateSpace` invalidity.

- **OpenXR teardown — clean VR exit**

  `VROSceneRendererOpenXR::onDestroy` no longer aborts on
  `DetachCurrentThread` when invoked from the JVM main thread via the JNI
  bridge. The teardown path now probes attachment state with `GetEnv`
  first and only Attach/Detach when the thread was genuinely detached,
  fixing the SIGABRT that previously fired on every VR Activity exit.
