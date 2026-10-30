# Release Notes

## v3.1.0

**Advanced VPS, a world mesh that persists, device depth in AR recordings, and a Meta Quest pass with hand occlusion.** The native build ships inside `@reactvision/react-viro` 3.1.0. It links Spatial 1.4.0, which adds continuous localisation against a scanned place and the VPS fuser.

### Advanced VPS

- **VPS map localisation in the AR session.** `VROARSession` gains map load, unload and localisation, implemented on ARKit and ARCore, and exposed on `ARScene` and through JNI. A loaded `.rvmap` is matched against camera frames while the session runs.
- **Server mode.** The engine hands frames out for the server to localise and feeds the answers into the same fuser, without loading a map (`rvStartVPSServerSession`, `rvReportVPSServerFix`, `rvStopVPSServerSession`, `VROARCamera::getImageTransform()`).
- **Every fix goes through Spatial's `VROVPSLocalizer`,** which keeps one smoothed map-to-world transform. `VROVPSAnchorNode` keeps content placed in map coordinates following it, and `VROARScene::setVPSMapMesh()` loads the place's mesh as occlusion and a kinematic physics body tagged `"world"`.
- **Recordings carry GPS and a camera-forward heading** on each pose, which ViroScan turns into the map's geo-registration.

### World mesh

- **Depth frames fuse into a persistent room (`VROTSDFVolume`, `VROARWorldMesh`)** on the depth-image and monocular paths, re-meshing only the blocks that changed, within a memory ceiling. Fusion pauses while tracking is limited, a depth drop-out keeps the room instead of falling back to plane polygons, and `resetAccumulation()` starts a new one.
- **The mesh reports its source** (`lidar`, `depth`, `monocular`, `plane`) and whether it is accumulated.
- **Android: the depth mesh is geometrically correct** and no longer depends on acquiring a CPU camera image.
- **Thread safety:** the Android snapshot no longer reads a mesh the render thread is replacing, a reset is not undone by a collision shape still being built, `voxelSize` and `maxMemoryMB` take effect mid-session, and iOS monocular depth is published whole with the pose it was inferred at.

### Meta Quest

- **Input:** the left Menu button and the palm pinch report `MenuButton`, Y goes back as B does, a menu or system pinch never ends as a select, released hands release, and a hand and a controller on one side press once. `getSourceRay` / `getControllerRayAsync` report where a ray points, and `setInputFocusListener` / `setRoomMoveListener` report focus changes and recentres.
- **Rendering:** the right eye clears its depth buffer, each eye's framebuffer is made once and its depth discarded after drawing, the aim laser stays on top of panels, and switching AR scenes or closing one logs no errors.
- **Room planes** stay on the real surface across a recentre, are asked for at once, face the right way, and a desk is a table.
- **Hand occlusion in passthrough (optional, off by default):** the tracked hands are drawn into each eye's depth buffer before the scene, so the real hands show through virtual content in front of them. It uses the runtime's hand mesh, or capsules between the joints, on Quest 2, 3, 3S and Pro.
- **Stability:** videos no longer abort the app or leak JNI references, hit tests deliver their results without leaking, and `recenterTracking()` no longer races the render thread.

### Everywhere

- **AR recordings can carry the device's depth:** LiDAR scene depth on iOS and the ARCore Depth API on Android, as 16-bit millimetre PNGs with confidence, about 10 a second, when the session is set to capture depth. ViroScan fuses them into the scanned place's mesh, which is what makes blank walls reconstruct.
- **Physics runs at real time on every display,** in fixed 1/60 s substeps, instead of 1.5× fast at 90 Hz and 2× at 120 Hz.
- **ARCore reports the ambient light again,** and a paused AR session stays paused when an option or an image or object target changes.
- **visionOS:** spot light cones match the GL backends, and morph targets blend on the CPU.
- **Web:** vertex colours show, as on the native backends.
- **Co-location:** `VROColocationSession::deviceId()`, sent as `x-rv-device`, so the relay bills a device once.
- **A click goes to the node drawn on top,** and a click in open space reaches the controller's own delegates again.
- **Text sits where its vertical alignment puts it,** and a back-culled draw after a front-culled one culls back faces again.
- **Video:** a paused iOS video stays paused and its late callbacks no longer reach a freed texture; Android play and pause keep their order.
- **iOS:** `ViroKit.framework` targets iOS 15.1.
- **Removed:** the macOS target, which no longer linked and nothing built, and dead code (an unused metrics client, the API key validator, the OpenCV headers, an old OpenXR loader).

See `CHANGELOG.md` for every change.
