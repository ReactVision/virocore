# Release Notes

## v3.0.2

Fixes to the web renderer. Native platforms get one shared change, a glTF load that no longer copies the model, and otherwise behave as in 3.0.1. On the web the build ships inside `@reactvision/viro-web-renderer` 1.0.1, and on native inside `@reactvision/react-viro` 3.0.2.

### Fixed

- **The web heap grows.** It was fixed at 256 MB, and a single generated or photogrammetry GLB of 30 to 40 MB crossed it once decoded. `abort("OOM")` is permanent in Emscripten, so the whole renderer died rather than the one model. The heap now grows to 2 GB; a failure past that still aborts, and `viro-web-renderer` reports it through `onAbort`.
- **Loading a glTF no longer copies the model.** The parsed model was copied into the renderer dispatch and its images twice more before decoding. It now moves into a shared pointer and images decode in place. This is shared code: every platform gets the lower peak, and `VROPlatformLoadImageWithBufferedData` takes its buffer by `const &`.
- **Web: taps are no longer mirrored vertically.** Y was flipped twice on the way to `unproject`.
- **Web AR: a tracking dropout holds the last pose** instead of snapping the scene to identity rotation. Limited applies the new rotation and holds the position.

### Added

- **Web: source textures** (`viroCreateSourceTexture`, `viroUpdateTextureFromSource`), filled by the GPU straight from a `<video>`, `<canvas>`, `ImageBitmap` or `VideoFrame`. The AR camera feed uses them instead of a per-frame readback and upload.
