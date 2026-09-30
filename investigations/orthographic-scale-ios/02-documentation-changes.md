# Documentation changes: orthographic camera, map camera and game loop

## Is this on the MCP side?

Partly. The docs live in two places, and the ReactViro MCP serves both:

| Where | What | How it reaches developers |
|---|---|---|
| **JSDoc and comments in `ReactVision/viro`** (`components/*.ts(x)`) | Prop and hook docs, shown in editors and in the shipped `.d.ts` files | Ships with the npm package, and the MCP indexes it (`pkg: components`) |
| **`ViroWorkspace/docs/`** | The public docs site content | The MCP indexes it too (`pkg: docs`). It sits outside any repo, so it must be edited where it lives, not through a viro PR. |

The MCP itself needs no code change, only a **re-index** after the edits below land, so tools such as `reactviro_get_docs` and `reactviro_search` stop serving the old text. Today a docs search finds nothing for `orthographicScale`, `onCameraTransformUpdate` or `useViroMapCamera`.

---

## A. Fixes to existing text (wrong today)

### A1. `ViroWorkspace/docs/PLATFORM_EXTENSIONS.md`, section "Utility: bypass reconciler" (~line 72)

The documented signature doesn't match the real one:

```typescript
// Documented (wrong)
ViroGameLoopUtils.setPosition(nodeRef, x, y, z);

// Actual (components/ViroGameLoopUtils.ts)
ViroGameLoopUtils.setPosition(nodeRef, [x, y, z]);
ViroGameLoopUtils.setRotation(nodeRef, [x, y, z]);   // Euler degrees
ViroGameLoopUtils.setScale(nodeRef, [x, y, z]);
```

Also add a line to that section: *"iOS requires 3.0.2 or later. In 3.0.0–3.0.1 these calls fail on iOS."*

### A2. `viro/components/useViroMapCamera.ts`, header comment (lines 15–17)

The current text says:
> Only `orthographicScale` is a prop, because it has no reconciler-free path.

That's wrong: `setNativeProps({ orthographicScale })` on the `ViroCamera` is a reconciler-free path. Replace it with:

> Position and rotation reach the scene through ViroGameLoopUtils. `orthographicScale` is held as React state and passed as a prop, which is simpler for a value that only changes on zoom. If you're driving zoom every frame, `cameraRef.setNativeProps({ orthographicScale })` updates it without a render.

⚠️ **Wait on this one:** the orthographic zoom bug on iOS is still open (see `03-prototype-test-app-plan.md`). Land A2 once the prototype confirms `setNativeProps` actually applies on iOS. Otherwise we'd be documenting a path that doesn't work yet.

### A3. `showcase/components/ar-examples/map-camera.tsx`, comment above the camera node

It says every gesture writes position and rotation straight to the node. That's true on Android but not on iOS until the 3.0.2 fix. There's no text change needed once 3.0.2 ships. Until then, **don't point customers at this example as an iOS reference.**

---

## B. New content (missing today)

### B1. Orthographic camera: add to the camera docs page in `ViroWorkspace/docs`

There's currently no public documentation for `projection` or `orthographicScale`. Add:

- **`projection`**: `"perspective"` (default) or `"orthographic"`, on `ViroCamera` and `ViroOrbitCamera`. Ignored in AR and VR scenes, where the device supplies the projection.
- **`orthographicScale`**: the full vertical height of the view in world units (not the half-height). The width follows from the viewport's aspect ratio. Only used when `projection="orthographic"`. Default note: if it's unset or 0, the renderer uses a height of 1 world unit.
- **Zoom under orthographic:** moving the camera does not change how big things look. Only `orthographicScale` does. So for a map viewer, park the camera at a fixed distance and zoom by changing the scale.
- **Updating it:** declaratively as a prop, or reconciler-free with `setNativeProps({ orthographicScale })`. *(Keep the second part back until the prototype confirms it on iOS; see A2.)*
- **Platform support:** iOS, Android and web (3.0.1+).
- **Snippet:**
  ```tsx
  <ViroCamera active position={[0, 10, 0]} rotation={[-90, 0, 0]}
              projection="orthographic" orthographicScale={12} />
  ```

### B2. `onCameraTransformUpdate` on `ViroScene` / `ViroARScene`: document how often it fires

Confirmed in `virocore/ViroRenderer/VROInputControllerAR.cpp:53`: it fires **once per rendered frame**, whether or not the camera moved. The customer hit exactly this: their counter read 60/s during a pinch that visibly did nothing.

Add to both the JSDoc on `onCameraTransformUpdate` in `viro/components/ViroScene.tsx` (and `ViroARScene.tsx`) and the scene docs page:

> Called once per rendered frame (about 60 times a second) while a handler is attached, whether or not the camera moved. Treat it as a per-frame sample of the camera, not a "camera moved" event. If you only care about changes, compare against the last value you received. Attaching a handler has a per-frame cost across the bridge, so remove it when you don't need it.

### B3. `useViroMapCamera`: add a docs page

The hook has no public page. Cover:
- what it's for (pan, zoom and orbit for non-AR map or floor-plan views) and why not to use `dragType="FixedDistance"` (that text is already in the hook header);
- the state model: `target`, `distance`, `yaw`, `pitch`, `orthographicScale`;
- the return value: `nodeRef` and `nodeProps` go on the wrapping `ViroNode`, `cameraProps` go on the `ViroCamera`, and `panHandlers` and `onLayout` go on an overlay `View`;
- `controls`: `panByPixels`, `zoomBy`, `orbitBy`, `setState`, `flyTo`, `get`;
- the input note: if the app owns touch itself (for example with `pointerEvents="none"` on the navigator), drive `controls` from your own gesture code;
- **Minimum version: 3.0.2 for iOS** (it depends on the `ViroGameLoopUtils` fix).

---

## C. Changelog (3.0.2)

The `ViroGameLoopUtils` entry is in `01-gameloop-utils-ios-fix.md`. Add a docs line:

> **Docs:** documented `projection` and `orthographicScale`, fixed the `ViroGameLoopUtils` signatures in the platform extensions guide, and noted that `onCameraTransformUpdate` fires every frame.

## Checklist

- [ ] A1 `PLATFORM_EXTENSIONS.md` signatures and iOS version note
- [ ] A2 `useViroMapCamera.ts` header *(after the prototype confirms the `setNativeProps` path)*
- [ ] B1 orthographic section on the camera docs page
- [ ] B2 `onCameraTransformUpdate` JSDoc and docs page
- [ ] B3 `useViroMapCamera` docs page
- [ ] C changelog line
- [ ] Re-index the ReactViro MCP, then check that `reactviro_search "orthographicScale"` with `pkg: docs` returns hits
