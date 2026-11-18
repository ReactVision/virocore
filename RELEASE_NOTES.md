# Release Notes

## v3.2.1

**iOS anchors nodes at world positions, Android tracking no longer flickers on phones that could not sustain the camera config, taps on moving nodes click, and an Android `ARScene` used after dispose answers instead of crashing.** The native build ships inside `@reactvision/react-viro` 3.2.1.

### Everywhere

- **A tap on a moving node clicks it:** a short touch that barely moved completes the click on the pressed node, with the press's hit position (`VROInputControllerAR`, `VROInputControllerBase`).
- **A node faded by its parent's opacity is sorted as transparent,** so translucent content behind it is no longer hidden (`VROGeometry`).

### iOS

- **`VROARSessioniOS::createAnchoredNode(position)` and `removeAnchoredNode(node)`:** an ARKit anchor at a world position whose updates move the returned node; removal works while the session is paused.
- **Object anchors reach their object targets,** and an object target whose `.arobject` fails to load is skipped with a warning instead of raising `NSInvalidArgumentException`.

### Android

- **Tracking no longer flickers on phones that could not sustain the camera config (viro#568):** the camera config is capped at 1920×1080 at 30 fps, a paused plane is kept, and Unavailable reaches the scene after about 200 ms (`ARCore_Native`, `VROARSessionARCore`, `VROSceneRendererARCore`).
- **A ReactVision cloud anchor resolve is held by an ARCore anchor,** so it moves with ARCore's corrections and reaches the anchor callbacks (`VROARSessionARCore`).
- **`ARScene` answers after `dispose()`:** callbacks get `AR scene is gone` and return values a "nothing there" value, instead of a SIGSEGV.

### Check when upgrading

- **A ReactVision resolve result's anchor id is now the ARCore anchor's id;** its cloud anchor id is unchanged.
- **The back camera config is no larger than 1920×1080.** A phone that offered a larger CPU image no longer gets it.
