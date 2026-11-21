# Release Notes

## v3.2.3

**The web build catches up on most of the web-vs-native parity review: portal backgrounds and events, per-face materials, full particle settings, physics queries and held forces, post-process effects, node pivots, mouse hover, scene-wide controller events, and pause, resume and stop for declarative animations. Web AR crops the camera feed instead of stretching it, a .gltf with external files no longer aborts the wasm, and portals keep drawing after you walk through one.** It ships in `@reactvision/viro-web-renderer` 1.3.0, and the native build in `@reactvision/react-viro` 3.2.3. iOS and Android behave as in 3.2.2.

### Web (`wasm/`)

- **New exports** for portals, materials and geometry, particles, physics, post-process effects, node pivots, mouse hover and scene-wide controller events; every one sits beside the existing exports, and none changes a signature.
- **Animations:** pause, resume and stop reach declarative passes.
- **Models:** a merged material copies only what it sets, as native does, and shader uniform updates reach the model.
- **AR:** the camera feed is cropped to fill the screen, and the projection uses the same crop, so content stays where it belongs near the edges.
- **Fixes:** a .gltf with external files fails that model instead of the whole wasm; portals keep drawing after the camera walks through; an empty AutoCompound shape builds.

### Everywhere

- **`VROTexture::setStereoMode`** sets a texture's stereo mode after it is made. Only the web build calls it.
