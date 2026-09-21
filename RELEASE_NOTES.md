# Release Notes

## v3.0.1

Four fixes to shared coordinate frames on Meta Quest, all of them in the same call path, each one hidden behind the last. The renderer ships inside `@reactvision/react-viro` as prebuilt binaries; upgrade that package to 3.0.1 to get these.

### Fixed

- **Four OpenXR extensions are now requested at instance creation.** `XR_FB_spatial_entity_storage`, `XR_FB_spatial_entity_sharing`, `XR_META_spatial_entity_sharing` and `XR_META_spatial_entity_group_sharing`. `xrGetInstanceProcAddr` refuses a function whose extension was not enabled, so `xrShareSpacesMETA` never loaded, the capability flag stayed false, and every shared-frame call reported the feature unavailable on hardware that supports it.

  The last two are different extensions and the distinction is easy to miss: `xrShareSpacesMETA` is declared by `XR_META_spatial_entity_sharing`, while `XR_META_spatial_entity_group_sharing` only adds the group-uuid recipient and filter. Enabling the group one alone loads no function and reports *"XR_META_spatial_entity_group_sharing not present"* — naming the extension that is present rather than the one that is not. `static_assert`s now fail the build if any of the four leaves the list.

- **The event loop forwards `XR_TYPE_EVENT_DATA_SPATIAL_ANCHOR_CREATE_COMPLETE_FB` and `XR_TYPE_EVENT_DATA_SHARE_SPACES_COMPLETE_META`.** `onSpatialEvent` had handlers for both; `xrPollEvent`'s switch forwarded three of the five types it handles and dropped these two into `default`. Every spatial-entity call is asynchronous and answers through an event, so an event that is not forwarded is a call that never completes — no error, no timeout, just a callback that is never invoked. On device the anchor was created successfully and nothing was ever told about it.

- **The group filter chains on `XrSpaceQueryInfoFB::next`, not `::filter`.** `filter` takes the FB filter types — uuid, component — and the runtime rejects an unrecognised one with `XR_ERROR_VALIDATION_FAILURE`. The group filter is an extension struct and chains like every other META addition to an FB call.

Verified on a Quest 3: `xrCreateSpatialAnchorFB` → STORABLE and SHARABLE → `xrShareSpacesMETA` round-trips, and a group query recovers the frame.

Requires `horizonos.permission.IMPORT_EXPORT_IOT_MAP_DATA` in the app manifest, which `@reactvision/react-viro` 3.0.1's config plugin adds. Without it the Meta runtime hides the group-sharing extension from enumeration rather than failing the call, so the headset appears not to support shared anchors.

## v3.0.0

The renderer behind ViroReact 3.0.0. ViroCore ships inside `@reactvision/react-viro` as prebuilt binaries — the Android AARs and `ViroKit.framework` — rather than as a package of its own, so there is nothing to install separately.

One renderer now covers five targets: OpenGL ES on Android, Metal on iOS, Metal into a CompositorServices immersive space on **Apple Vision Pro**, OpenXR on **Meta Quest**, and WebGL2 through WebAssembly on **the web**.

Behind ViroReact 3.0.0's headline capabilities, the engine work is the world mesh and scan pipeline that VPS is built on, the shared coordinate frames co-location needs, and mixed reality on Quest.

### Added

- **World mesh and room-scale scanning.** The depth-derived mesh that VPS snapshots and reattaches — accumulated from ARKit's LiDAR and ARCore's Depth API, serialised into a scan's own location frame, and restored against a resolved one for occlusion and physics. `VROARScene` owns the mesh, its configuration and its lifetime.
- **The web target.** The WebAssembly build under `wasm/`: the C API that `@reactvision/viro-web-renderer` drives, the scene bindings, and the renderer work behind them.
- **Co-location.** `VROColocationSession` as the platform-neutral holder, `Colocation_JNI` on Android, `VROColocationBridge` for iOS and visionOS, and shared spatial anchors on Quest through `xrShareSpacesMETA`.
- **Quest.** Plane detection through `XR_EXT_plane_detection`, passthrough compositing, per-ray drag ownership and the input controller work behind mixed reality.
- **Scan and world-mesh state are readable from the bridge.** `rvGetScanStatusJson()` and `rvGetScanDiagnosticsJson()` on `VROARSession` report how a scan in progress is doing and why the last one ended as it did; `nativeRvGetWorldMeshStats` does the same for the world mesh over JNI. The JSON shape is defined once on `VROARSession` rather than restated in each bridge, so the two platforms cannot drift. All three are read-only snapshots taken on the render thread and are cheap enough to poll.

### Changed

- **Web text renders in Roboto.** The preloaded typeface is now Apache 2.0 licensed and matches the face already used on Android and Quest, so the three platforms agree. The preloaded data drops from 2.3 MB to 389 KB.
- **Android shader assets are generated at build time.** `android/shaders.gradle` regenerates `sharedCode/src/main/assets/*.glsl` from `ViroRenderer/` during `preBuild`, keeping the shipped copies in step with the sources automatically. The generated files stay committed, so a build after a shader change will modify the working tree.
- **iOS pods build at deployment target 15.0.** A `post_install` hook rewrites every pod target, as Xcode 26 and later reject anything lower.

### Fixed

- Compound physics shapes distribute mass by child volume, and a compound built from given parts no longer drifts on every physics update.
- Alpha cutoff binds only for `Mask` transparency mode; every other mode fades continuously instead of vanishing at half alpha.
- Vertex colours and glTF `doubleSided` are honoured.
- A `FixedToPlane` drag no longer snaps to the aim point on float noise, and aiming square at the plane no longer produces a NaN world transform.
- Calls made before the AR session exists are queued and flushed in order rather than dropped without a callback.
- On visionOS the renderer archive now carries the ReactVisionCCA slices it references, so an application no longer fails to link on symbols it never used.
