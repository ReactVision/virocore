# Release Notes

## v2.58.1

### Highlights

**AR session recording fixes**

- **The clip no longer plays sideways (iOS and Android).** Recordings carried no rotation at all, so they played in the camera sensor's landscape orientation however the device was held. Both recorders now tag the file for a quarter turn clockwise on playback — Android via `MediaMuxer.setOrientationHint()`, iOS via `AVAssetWriterInput.transform`.
- **The IMU is in the right units on iOS.** CoreMotion reports acceleration in G and Android reports m/s², but `session.jsonl` has one schema for both, so every `imu.accel` and `pose.gravity` iOS wrote was off by 9.81×. iOS now scales to m/s². Against tinyvio on a real recording this moved tracking from 0% of frames to 96%.
- **One pose per frame on iOS.** ARKit can hand the recorder the same frame twice; the duplicate used to add a pose line while the muxer dropped its video frame, sliding every later pose onto the wrong frame. Duplicates are now dropped whole, and a pose is only written once its frame has actually reached the encoder — so the pairing holds even if the encoder refuses one. A session that loses video entirely still keeps its full IMU/pose sidecar.
- **Colour is correct again (Android).** Recorded `video.mp4` came out with sharp, correctly-framed luma but large green/magenta blocks over it: the encoder was configured with `COLOR_FormatYUV420Flexible` — an abstract format, not a byte layout — and fed a tightly-packed planar I420 buffer, which most devices read as NV12 semi-planar. Frames now go through `MediaCodec.getInputImage()` and honour the encoder's real per-plane strides.

### Notes

- The rotation is container metadata only, on both platforms. Encoded frames stay sensor-native so they keep matching the intrinsics in `session.jsonl`. **Anything that decodes `video.mp4` for tracking must pass `-noautorotate`** — ffmpeg would otherwise rotate the pixels while `ffprobe` still reports the unrotated size the intrinsics assume, which goes wrong silently. Older recordings carry no matrix, so the flag is harmless on them.
- **iOS `imu.accel`/`pose.gravity` are now m/s², where they used to be G.** Android was always m/s². The format only shipped in 2.58.0, so there is effectively no earlier iOS data to reconcile.
- Pairs with `@reactvision/react-viro` 2.58.1.

See [`CHANGELOG.md`](./CHANGELOG.md) for full detail.

---

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
