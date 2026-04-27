# CHANGELOG

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
