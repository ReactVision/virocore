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

## v3.0.1

Four fixes to shared coordinate frames on Meta Quest, all of them in the same call path, each one hidden behind the last. The renderer ships inside `@reactvision/react-viro` as prebuilt binaries; upgrade that package to 3.0.1 to get these.

### Fixed

- **Four OpenXR extensions are now requested at instance creation.** `XR_FB_spatial_entity_storage`, `XR_FB_spatial_entity_sharing`, `XR_META_spatial_entity_sharing` and `XR_META_spatial_entity_group_sharing`. `xrGetInstanceProcAddr` refuses a function whose extension was not enabled, so `xrShareSpacesMETA` never loaded, the capability flag stayed false, and every shared-frame call reported the feature unavailable on hardware that supports it.

  The last two are different extensions and the distinction is easy to miss: `xrShareSpacesMETA` is declared by `XR_META_spatial_entity_sharing`, while `XR_META_spatial_entity_group_sharing` only adds the group-uuid recipient and filter. Enabling the group one alone loads no function and reports *"XR_META_spatial_entity_group_sharing not present"* — naming the extension that is present rather than the one that is not. `static_assert`s now fail the build if any of the four leaves the list.

- **The event loop forwards `XR_TYPE_EVENT_DATA_SPATIAL_ANCHOR_CREATE_COMPLETE_FB` and `XR_TYPE_EVENT_DATA_SHARE_SPACES_COMPLETE_META`.** `onSpatialEvent` had handlers for both; `xrPollEvent`'s switch forwarded three of the five types it handles and dropped these two into `default`. Every spatial-entity call is asynchronous and answers through an event, so an event that is not forwarded is a call that never completes — no error, no timeout, just a callback that is never invoked. On device the anchor was created successfully and nothing was ever told about it.

- **The group filter chains on `XrSpaceQueryInfoFB::next`, not `::filter`.** `filter` takes the FB filter types — uuid, component — and the runtime rejects an unrecognised one with `XR_ERROR_VALIDATION_FAILURE`. The group filter is an extension struct and chains like every other META addition to an FB call.

Verified on a Quest 3: `xrCreateSpatialAnchorFB` → STORABLE and SHARABLE → `xrShareSpacesMETA` round-trips, and a group query recovers the frame.

Requires `horizonos.permission.IMPORT_EXPORT_IOT_MAP_DATA` in the app manifest, which `@reactvision/react-viro` 3.0.1's config plugin adds. Without it the Meta runtime hides the group-sharing extension from enumeration rather than failing the call, so the headset appears not to support shared anchors.
