# Plan: prototype app to pin down the iOS orthographic zoom bug

## Why we need it

A customer (Fábio, store-map app, 3.0.1, physical iPhone, Fabric) reports that on iOS, changing `orthographicScale` on a mounted `ViroCamera` does nothing. It fails both through `setNativeProps` every frame and as a declarative prop set when the gesture ends. The orthographic projection itself renders correctly.

Reading the code (see the session report) found **no defect** on any layer:
- the prop is declared and present in the shipped `libViroReact.a`;
- React Native 0.86's Fabric interop passes numeric changes through;
- `VRTCamera setOrthographicScale:` writes straight to the native camera;
- the renderer rebuilds the projection from that camera every frame.

So the bug only shows at runtime. This app exists to find **which layer drops the value**, on a real device, with the customer's setup.

## Questions the app must answer

1. **Q1:** Does the *initial* `orthographicScale` apply at all? Does a fixed 2 look different from a fixed 50?
2. **Q2:** Does a *declarative* update (a state change) reach the `VRTCamera` setter on iOS?
3. **Q3:** Does a *`setNativeProps`* update reach the setter?
4. **Q4:** When it reaches the setter, is the value still on the camera the renderer uses at frame time?
5. **Q5:** Does the customer's two-node rig (yaw node → boom node → camera) change any of Q1–Q4?
6. **Q6:** Is Android the same or different?
7. Also confirm the `ViroGameLoopUtils` fix (`01-gameloop-utils-ios-fix.md`) and log the `onCameraTransformUpdate` rate.

## Setup

| Item | Choice | Why |
|---|---|---|
| App | Bare RN 0.86 app or Expo dev client (**not Expo Go**), New Architecture on | Matches the customer: RN 0.86 has no legacy architecture, so Fabric is the only path |
| Viro | **Built from source**: `ReactVision/viro` and `virocore` checked out locally and linked | The npm package ships prebuilt binaries, which we can't instrument. We need logging in native code (below). |
| Also test | The published `@reactvision/react-viro@3.0.1` from npm, same app, second build | Rules out a difference between the source and the prebuilt dist (the customer uses dist) |
| Devices | One physical iPhone (iOS 18+) and one Android phone | The simulator isn't enough: the customer's report is from a device |
| Scene | `Viro3DSceneNavigator` + `ViroScene`, non-AR | Same as the customer: `VROViewScene` path |

## Native instrumentation (debug builds only)

Put each log behind a `VIRO_DEBUG_ORTHO` compile flag so it can stay in the tree.

1. **Bridge setter:** `viro/ios/ViroReact/Views/VRTCamera.mm`, in `setOrthographicScale:` (~line 86)
   `NSLog(@"[ORTHO] setter self=%p camera=%p scale=%f", self, self.nodeCamera.get(), scale);`
   This answers Q2 and Q3: did the value arrive, and on which instance?
2. **Scene camera binding:** `viro/ios/ViroReact/Views/VRTScene.mm`, in `setCameraIfAvailable` (~line 187)
   `NSLog(@"[ORTHO] pointOfView VRTCamera=%p camera=%p", _camera, _camera.nodeRootTransformCamera->getCamera().get());`
   This tells us which camera the renderer draws from.
3. **Renderer:** `virocore/ViroRenderer/VRORenderer.cpp`, in `computeProjection` (~line 237), throttled to once a second:
   log the camera pointer, its projection type and `getOrthographicHeight()`.
   This answers Q4: is the value still there when the frame is drawn?
4. **Android equivalents:** `VRTCamera.java#setOrthographicScale` (log tag `ORTHO`), plus the same `computeProjection` log, which is shared C++.

How to read the logs:
- **Setter pointer = scene pointer = render pointer, and the height changes:** the value reaches the renderer. The bug is visual or downstream (look at the view matrix, clip planes, viewport).
- **Setter logs, but the render height doesn't change:** the renderer is drawing from a different camera (Q4).
- **The setter never logs on update:** the value is dropped in the JS, Fabric or interop layer (Q2 and Q3).
- **The setter logs different pointers over time:** the camera view is being recreated.

## Test screens

### Screen 1: Orthographic matrix (the core test)

One scene, a 10 × 10 grid floor (the grid makes the scale easy to read by eye), and a top-down camera: `position [0,10,0]`, `rotation [-90,0,0]`.

On-screen controls:
- **Projection:** perspective / orthographic toggle
- **Scale:** preset buttons 2 · 5 · 10 · 50, and a slider
- **Update path:** `state` / `setNativeProps` / `both`
- **Camera type:** `ViroCamera` / `ViroOrbitCamera`
- **Remount:** a "Remount camera" button that bumps the camera's `key`
- **Mount scale:** a "Mount scale" field, so the camera can be mounted with a fixed scale and no updates (Q1)
- **Status:** a text line with the last value sent and the send method

### Screen 2: The customer's rig

Rebuild Fábio's rig faithfully:
- a yaw `ViroNode` at `[cx, 0, cz]` with rotation `[0, yaw, 0]`;
- a boom `ViroNode` with rotation `[-62, 0, 0]` and position `[0, d·sin62°, d·cos62°]`;
- the `ViroCamera` at the boom tip, with `fieldOfView = 60`.

Drive it the way they do:
- The navigator has `pointerEvents="none"`, with a `PanResponder` above it for one-finger pan, pinch and twist.
- Every frame, `setNativeProps` goes to both nodes, plus `setNativeProps({ orthographicScale })` to the camera.
- A "commit declaratively on release" toggle.
- An ORTHO / PERSP live toggle, matching their A/B.

### Screen 3: Game loop and events

- A `ViroNode` moved every frame through `ViroGameLoopUtils.setPosition`, `setRotation` and `setScale`. Run it in Debug and Release to verify the fix in doc 01.
- A `useViroMapCamera` demo: a copy of `showcase/.../map-camera.tsx`.
- An `onCameraTransformUpdate` counter (events per second), shown next to a "camera moved" counter that only counts when the transform actually changed. This demonstrates the per-frame behaviour for the docs.

## Test run

Fill in a table like this per platform (iOS on the source build, iOS on npm 3.0.1, Android):

| # | Case | Expected | Setter log? | Render height changes? | Visible zoom? |
|---|---|---|---|---|---|
| 1 | Mount at 2, then separately mount at 50 (no updates) | Different framing | | | |
| 2 | Change scale via state | Zooms | | | |
| 3 | Change scale via `setNativeProps` | Zooms | | | |
| 4 | Change scale, then toggle projection off and on | Zooms | | | |
| 5 | Change scale, then remount with a new `key` | Zooms | | | |
| 6 | Cases 2–5 with `ViroOrbitCamera` | Zooms | | | |
| 7 | Screen 2, pinch in orthographic (`setNativeProps` each frame) | Zooms | | | |
| 8 | Screen 2, commit on release | Zooms | | | |
| 9 | Screen 3, game loop commands, Debug and Release | Node moves, no redbox or crash | n/a | n/a | |
| 10 | Screen 3, `onCameraTransformUpdate` rate while idle | ~60/s (documents the behaviour) | n/a | n/a | n/a |

## Outcomes, and what we do next

- **Reproduced, and the logs show the dropping layer:** write the fix (in `VRTCamera.mm`, the interop or `VRTScene`, depending on the logs), add the changelog entry, then land doc change A2.
- **Doesn't reproduce on the source build, but does on npm 3.0.1:** the prebuilt `ios/dist` is stale or different. Rebuild and republish.
- **Doesn't reproduce at all:** send Fábio the Screen 2 build (or its source) and ask them to diff it against their app. The difference is in their integration.
- **Android also fails:** the cause is shared, so look at JS or the renderer (C++) first.

## Effort and owner

- **Scaffold and Screens 1–3:** about 1 day
- **Native logging and building from source:** about half a day (mostly setting up pods to point at the local viro and virocore)
- **Test runs and write-up:** about half a day
- **Owner:** one RN and native engineer with an iPhone. Nothing else is blocked on this apart from doc change A2 and the customer's orthographic zoom.

## Deliverables

1. The prototype app, in `ReactVision/viro` under a `testapps/ortho-debug/` folder or a similar place, **not** in the shipped package.
2. The completed test table, per platform.
3. Logs from the failing case.
4. A root-cause note, with a fix PR if the bug reproduces.
