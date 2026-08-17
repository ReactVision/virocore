# Release Notes

## v2.58.0

### Highlights

**AR Session Recording**

- `VROARSession` gains an optional local recording surface (`startRecording`/`stopRecording`/`getRecordingStatus`) — `video.mp4` + `session.jsonl` (raw IMU + ground-truth pose), for offline analysis/replay via `tinyvio`. iOS (`AVAssetWriter`) and Android (`MediaCodec`/`MediaMuxer`) implementations, both with sorted, race-free sidecar/video finalization.

**Fixes**

- glTF: sparse accessors and non-indexed (draw-arrays) primitives no longer fail to load (VIRO-3664).

### Notes

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
