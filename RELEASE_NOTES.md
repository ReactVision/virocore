# Release Notes

## v2.58.0

### Highlights

**VPS-Lite v1 — geo-anchored persistent AR**

- Room/building-scale cloud anchors, discoverable by GPS, with an optional attached mesh for occlusion + physics.
- New scan session API (`startScan`/`finishScan`) — no pre-placed anchor needed, no small-radius cap.
- Mesh snapshot/attach for occlusion + physics collision on resolve.
- `resolveCloudAnchor()` now returns `resolvedTransform`.
- GPS accuracy gating: `getEarthTrackingState()` reports a real `Localizing → Enabled` transition (15m horizontal accuracy threshold), gated identically on both the ReactVision and ARCore geospatial provider paths — previously the ARCore path reported `Enabled` immediately, regardless of fix quality.

**Web platform**

- virocore compiles to WebAssembly/WebGL2 (`viro-web.wasm`), shipping as the engine behind the new `@reactvision/viro-web-renderer` package — nodes, geometry, materials/textures, text, particles, portals, IBL, lights/camera, model loading, animation, and a JS-driven AR backend.
- AR projection now derived from the camera's real intrinsics (was a fixed 60° FOV).

**AR Session Recording**

- `VROARSession` gains an optional local recording surface (`startRecording`/`stopRecording`/`getRecordingStatus`) — `video.mp4` + `session.jsonl` (raw IMU + ground-truth pose), for offline analysis/replay via `tinyvio`. iOS (`AVAssetWriter`) and Android (`MediaCodec`/`MediaMuxer`) implementations, both with sorted, race-free sidecar/video finalization.

**Fixes**

- glTF: sparse accessors and non-indexed (draw-arrays) primitives no longer fail to load (VIRO-3664).

### Notes

- Rebuild the prebuilt AARs/frameworks before publishing — this release touches the Android/iOS AR session classes, the wasm build, and the glTF loader.
- The web platform is additive: it introduces no changes to the existing iOS/Android/Quest/visionOS renderers or their public API.
- Pairs with `@reactvision/react-viro` 2.58.0.

See [`CHANGELOG.md`](./CHANGELOG.md) for full detail.

---

## v2.57.5

### Highlights

- **Eye-gaze input source on Meta Quest (`XR_EXT_eye_gaze_interaction`).** When the runtime reports eye-tracking support (Quest Pro), the OpenXR renderer enables the extension and dispatches the gaze pose as an additional input source (`ViroOculus::EyeGaze`) through the existing hit-test/hover pipeline — surfaced in React as the new `onGaze` prop. `EYE_TRACKING` permission declared in the manifests. No-op on headsets without eye-tracking hardware.
- **Per-frame video watermark support (Android).** New `ViroMediaRecorder.setWatermark(bitmap, widthFraction, bottomMarginFraction)` composites a watermark as a GL quad into each recorded frame before the encoder swap (aspect-preserved, bottom-center), letting the bridge burn the free-tier watermark into video at parity with iOS's CoreImage compositing.
- **Crash loading animated glTF models with a zero-duration animation channel.** A single-keyframe / zero-duration skeletal channel (legal glTF) triggered a divide-by-zero → `NaN` keyframe times → `std::sort` heap corruption → `SIGSEGV` in `resampleSkeletalChannelsToCommonGrid`. The time normalizer now guards the zero duration and the resample skips non-finite values.
- **Media recording writes to app-specific storage (Android).** `ViroMediaRecorder` no longer writes to the public directory that fails with `EACCES` under scoped storage on API 29+, so recordings/screenshots are always produced; gallery publishing is handled scoped-safely by the bridge.
- **AR anchor use-after-free hardening.** `nativeCreateAnchoredNode` null-checks the scene-controller ref before dereferencing, so a stale/zeroed ref (anchor retry racing scene teardown) returns null instead of crashing.

### Notes

- Rebuild the prebuilt AARs before publishing.
- Eye gaze requires the `com.oculus.permission.EYE_TRACKING` runtime grant on Quest Pro; unsupported/ungranted degrades to a no-op (controllers/hands unaffected).
- Pairs with `@reactvision/react-viro` 2.57.5.

See [`CHANGELOG.md`](./CHANGELOG.md) for full detail.
