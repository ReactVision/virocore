# Release Notes

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
