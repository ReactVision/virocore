# Release Notes

## v3.0.2

Web renderer fixes, Quest input and capture, and cloud anchors and co-location that work with a signed-in user instead of an API key. The web build ships inside `@reactvision/viro-web-renderer` 1.0.1, and the native build inside `@reactvision/react-viro` 3.0.2. It links ReactVisionCCA 1.3.0 on every platform, visionOS included.

### Web

- **The heap grows.** It was fixed at 256 MB, and a single generated or photogrammetry GLB of 30 to 40 MB crossed it once decoded. `abort("OOM")` is permanent in Emscripten, so the whole renderer died rather than the one model. The heap now grows to 2 GB. A failure past that still aborts, and `viro-web-renderer` reports it through `onAbort`.
- **Taps are no longer mirrored vertically.** Y was flipped twice on the way to `unproject`.
- **AR holds the last pose through a tracking dropout** instead of snapping the scene to identity rotation. Limited applies the new rotation and holds the position.
- **Source textures** (`viroCreateSourceTexture`, `viroUpdateTextureFromSource`) are filled by the GPU straight from a `<video>`, `<canvas>`, `ImageBitmap` or `VideoFrame`. The AR camera feed uses them instead of a per-frame readback and upload.

### Meta Quest

- **Gaze works on every Quest.** Where there is no eye tracker it falls back to the head pose. The head-pose hit drives hover and the reticle only, so it never takes fuse, pinch, rotate or drag away from the controller in use.
- **Screen capture.** `ViroViewOpenXR` now creates a `ViroMediaRecorder`, sized to one eye's swapchain. A passthrough capture holds the virtual content on transparency, since the OS composites the room underneath.
- **AR hit test** against the planes the session tracks, reachable through `ViroViewOpenXR.performARHitTestWithRay` from the camera along a ray or from an origin to a destination.
- **Two drags at once, one per hand.** Each hand drags its own node and releases it independently. A hand can't grab a node the other one holds, and its own `onMove` and fuse stay on what it points at. Single-pointer platforms behave as before.
- **Controllers vibrate on click**, on the hand that pressed.
- **The left-palm menu pinch** with hand tracking reaches the app as the Menu button does. A pinch made during a system gesture no longer also counts as a select.

### Cloud anchors and co-location

- **A signed-in session authenticates cloud anchors and the co-location channel** (`VROReactVisionAuth`, `com.viro.core.ReactVisionAuth`). Each request reads its credential when it is sent, and a session wins over a key. On iOS a session that arrives after the AR scene mounted is picked up. With neither a session nor a key, a request fails right away with "Not signed in".
- **Cancelling works.** `rvCancelOperations()` (`ARScene.rvCancelOperations()` on Android) cancels pending ReactVision hosts and resolves, which report `ErrorCancelled` once, and closes an open scan. An upload already in flight can still leave an anchor, which expires with its TTL.
- **Scan status reports its points.** `triangulatedPoints` and `minTriangulatedPoints` show how far a scan is from the host floor.
- **A scan now needs 300 triangulated points to host, up from 40.** This applies to key-only apps too. Maps of 96 and 211 points used to host, and then no other device could resolve against them.

### All platforms

- **Loading a glTF no longer copies the model.** It moves into a shared pointer and images decode in place, which lowers peak memory on large files. `VROPlatformLoadImageWithBufferedData` takes its buffer by `const &`.
- **Remounting a navigator with physics no longer crashes** (`btDbvtBroadphase::destroyProxy` on teardown).
- **iOS: text no longer comes back garbled after a navigator remounts.** The old view's GL cleanup ran in the new view's context.
- **Android: remounting a navigator no longer crashes** (`AAssetManager_open` on a null manager in the new renderer's first frame).
- **Quest: hit tests, screenshots and recordings work.** A hit test from JS crashed on the OpenXR thread, a screenshot aborted on an assertion, and every recording after the first came out black.
- **Quest: the laser follows a dragged object,** and a drag whose node leaves the scene (a scene change mid-drag) ends instead of crashing.
- **iOS: a gesture in flight during AR teardown no longer crashes** (`EXC_BAD_ACCESS` in `handleLongPress:`).
