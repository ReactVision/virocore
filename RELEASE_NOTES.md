# Release Notes

## v3.2.2

**The web build gains styled text, material effects, pinch, rotate and fuse, and node transform and bounding box reads, and its canvas is opaque.** It ships in `@reactvision/viro-web-renderer` 1.2.0, and the native build in `@reactvision/react-viro` 3.2.2. iOS and Android behave as in 3.2.1.

### Web (`wasm/`)

- **Styled text:** `viroCreateStyledText` with a family list, weight, italic, extrusion and an outline or drop shadow; weight and italic pick one of 11 Roboto faces in `wasm/fonts`.
- **Material alpha, bloom threshold, colour write mask and chroma key,** copied onto a model only when the material set them, as the native bridges do.
- **Pinch, rotate and fuse:** two fingers act on the node under their midpoint, and fuse is reported once it completes.
- **Node reads:** a node's world transform and bounding box, as native's `getTransformAsync` and `getBoundingBoxAsync` read them; `viroSetNodeSelectable` takes a subtree out of hit tests.
- **The canvas is opaque,** and a disposed renderer, or one whose context was lost, stops its loop.

### Everywhere

- **`VROInputControllerBase::cancelButtonEvent(source)`** ends a press without a click. Only the web build calls it.
