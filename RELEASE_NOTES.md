# Release Notes

## v3.3.0

**Android materials gain `emissionColor`, `emissionTexture` and `reflectiveTexture`, as on iOS. On the web, AR gets the tracker's point cloud and draws it as native does, cameras take a field of view, materials take emission and reflection, and the gaze reticle is gone.** The web build ships in `@reactvision/viro-web-renderer` 1.4.0, and the native build in `@reactvision/react-viro` 3.3.0. iOS behaves as in 3.2.3.

### Android

- **`Material.setImmutableColor` and `setImmutableTexture` (ViroReact only)** set a named channel at once on the calling thread, as the immutable constructor does. ViroReact's material manager uses them for `emissionColor`, `emissionTexture` and `reflectiveTexture`, which the queued setters never delivered to the copies it puts on nodes.

### Web (`wasm/`)

- **AR point cloud:** `viroARSetPointCloud` puts the tracker's feature points on every AR frame, so `VROARFrame::getPointCloud()` returns them as on iOS and Android, and `viroARDisplayPointCloud` draws them with a fixed-particle emitter, as `VROARScene::displayPointCloud` does. The web build draws its own default point texture.
- **Cameras:** `viroSetCameraFieldOfView`.
- **Materials:** `viroSetMaterialEmissionColor`, and emission and reflective texture channels.
- **No reticle:** the web points with a mouse or a finger.

Every new export sits beside the existing ones; none changes a signature.
