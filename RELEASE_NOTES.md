# Release Notes

## v3.0.3

A patch release for Meta Quest, iOS and the physics and recording paths. The native build ships inside `@reactvision/react-viro` 3.0.3. It links ReactVisionCCA 1.3.1, which pins platform requests to the database's region.

### Meta Quest

- **A scene opened after another AR scene gets the room's planes.** The session published each plane only to the scene attached when it was first found, so a plane scene opened from an AR scene never fired `onAnchorFound`. A new scene now receives every plane found so far.
- **The boundary is hidden while passthrough is on**, through `XR_META_boundary_visibility`, and comes back for a fully virtual scene. The app has to declare `com.oculus.permission.BOUNDARY_VISIBILITY`; the viro Expo plugin does.

### iOS

- **A session-only app no longer asks for location** when it opens an AR scene. Core Location starts only when `RVApiKey` and `RVProjectId` are set, as before 3.0.2. An anchor hosted on a session records no GPS fix.
- **Camera texture recordings carry audio.** The microphone is live only while recording.

### Android

- **`startRecording` without `RECORD_AUDIO` fails through its callback** instead of leaving the caller waiting forever, and camera-open and device errors reach it too.

### Everywhere

- **`onCollision` fires against the AR world mesh**, with the mesh's `collisionTag`. Ray and hit tests skip the mesh as before.
- **A session can carry a function region** (`setSession(..., functionRegion)` on Android, `+setStudioSessionBaseUrl:accessToken:clientTag:functionRegion:` on iOS), which the cloud-anchor providers send as `x-region`. The three-argument forms still work.
- **`VROGeospatial.h` is one header again**: both copies call the earth-tracking state `Enabled`. Nothing changes at runtime.
