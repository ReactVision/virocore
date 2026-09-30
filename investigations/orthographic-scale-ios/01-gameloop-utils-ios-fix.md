# Fix: `ViroGameLoopUtils` fails on iOS (Fabric)

**Repo:** `ReactVision/viro` · **Target:** 3.0.2 · **Size:** small (one native file, a prebuilt rebuild, changelog)

## Problem

On iOS, `ViroGameLoopUtils.setPosition`, `setRotation` and `setScale` fail on every call. They work on Android.

- **Reported by a customer (Fábio, store-map app, 3.0.1, physical iPhone, Fabric):** it redboxes with `No command found with name "setPosition"`.
- **Also affected:** our own `useViroMapCamera` hook, which calls these on every gesture step (`components/useViroMapCamera.ts:127–128`). So its pan, orbit and fly-to are broken on iOS, and so is the `map-camera` showcase.

## Root cause

JS and iOS disagree on the shape of the command arguments.

| Layer | What it does |
|---|---|
| JS, `components/ViroGameLoopUtils.ts:32/44/56` | `dispatchViewManagerCommand(handle, "setPosition", position)` sends the vector **flattened** as the args: `[x, y, z]` |
| RN 0.86 Fabric interop (`RCTLegacyViewManagerInteropCoordinator.mm`) | Adds the view tag in front, then invokes the method with `[tag, x, y, z]`. That's **4 arguments**. |
| Android, `VRTNodeManager.java:459–471` | Reads `args` as a flat float array. **Works.** |
| iOS, `ios/ViroReact/ViroViewManager.mm:292/300/307` | Declares `setPosition:(NSNumber *)reactTag position:(NSArray *)position`, which takes **2 arguments**. **Fails.** |

What happens in `RCTModuleMethod invokeWithBridge:` (RN 0.86):
- **Debug:** it logs `ViroViewManager.setPosition was called with 4 arguments but expects 2` (an `RCTLogError`, which shows as a redbox) and drops the call.
- **Release:** that check is compiled out. The argument loop then reads past the end of its argument handlers, which is most likely an `NSRangeException` crash. Not verified on a device yet.

Things I checked and ruled out:
- **The commands are declared.** They're on `ViroViewManager`, which every node manager (including `VRTViewContainerManager`, which backs `ViroNode`) inherits from. The shipped `ios/dist/lib/libViroReact.a` contains `-[ViroViewManager setRotationEuler:rotation:]` and the other two.
- **The lookup should find them.** In bridgeless mode the interop's `_lookupModuleMethodsIfNecessary` walks the class hierarchy, and in bridge mode `RCTModuleData` does the same. So the customer's exact `No command found` message isn't explained by the 3.0.1 source. It may come from a stale pod or a ref that isn't a Viro node. Either way, the call can't succeed until the argument shapes match.

## Fix

Make iOS take the flat arguments that JS and Android already use. JS and Android stay as they are.

`ios/ViroReact/ViroViewManager.mm`, replacing the three methods at lines 292–312:

```objc
// The command args arrive flattened ([x, y, z]), matching ViroGameLoopUtils.ts and
// VRTNodeManager on Android. The interop layer prepends the react tag, so each method
// takes the tag plus three numbers. Keep these three in step with both of those.
RCT_EXPORT_METHOD(setPosition:(nonnull NSNumber *)reactTag
                            x:(nonnull NSNumber *)x
                            y:(nonnull NSNumber *)y
                            z:(nonnull NSNumber *)z)
{
    NSArray<NSNumber *> *position = @[x, y, z];
    VRTDispatchNodeCommand(self.bridge, reactTag, @"setPosition", ^(VRTNode *node) {
        node.position = position;
    });
}

// Euler degrees, matching the rotation prop.
RCT_EXPORT_METHOD(setRotationEuler:(nonnull NSNumber *)reactTag
                                 x:(nonnull NSNumber *)x
                                 y:(nonnull NSNumber *)y
                                 z:(nonnull NSNumber *)z)
{
    NSArray<NSNumber *> *rotation = @[x, y, z];
    VRTDispatchNodeCommand(self.bridge, reactTag, @"setRotationEuler", ^(VRTNode *node) {
        node.rotation = rotation;
    });
}

RCT_EXPORT_METHOD(setScale:(nonnull NSNumber *)reactTag
                         x:(nonnull NSNumber *)x
                         y:(nonnull NSNumber *)y
                         z:(nonnull NSNumber *)z)
{
    NSArray<NSNumber *> *scale = @[x, y, z];
    VRTDispatchNodeCommand(self.bridge, reactTag, @"setScale", ^(VRTNode *node) {
        node.scale = scale;
    });
}
```

The JS method name is the selector up to its first colon, so these still register as `setPosition`, `setRotationEuler` and `setScale`, and no JS changes are needed.

### Also in this change

1. **Update the header comment in `components/ViroGameLoopUtils.ts`.** Say that the args are sent flattened, and that the iOS method signatures (tag plus x, y, z) and Android's `applyTransformCommand` must match that shape.
2. **Rebuild the prebuilt iOS binaries in `ios/dist`.** The npm package ships `ios/dist/lib/libViroReact.a`, so a source-only fix never reaches customers. Rebuild it and check that the new selectors are in it:
   `strings -a ios/dist/lib/libViroReact.a | grep "setPosition:x:y:z:"`
3. **Add a changelog entry (3.0.2):**
   > **`ViroGameLoopUtils` works on iOS.** `setPosition`, `setRotation` and `setScale` sent their vector flattened, as Android expects, but the iOS commands took a single array, so every call failed on iOS: a redbox in debug builds and a likely crash in release. The iOS commands now take the same flattened arguments. This also fixes pan, orbit and fly-to in `useViroMapCamera` on iOS.

### Optional follow-up (not needed for the fix)

- **A redundant UI-block hop under bridgeless.** The interop already runs the method inside `addUIBlock` (`_handleCommandsOnBridgeless`), and `VRTDispatchNodeCommand` then queues a second `addUIBlock`. Each update may land one frame late. Worth measuring before changing anything.

## Verification

Run these on a physical iPhone under Fabric, in both a Debug and a Release build:

1. `ViroGameLoopUtils.setPosition(ref, [0, 1, 0])` on a `ViroNode` moves the node, with no redbox or log error.
2. Same check for `setRotation` and `setScale`.
3. Calling all three every frame for 60 s from a `requestAnimationFrame` loop causes no crash or leak.
4. The `map-camera` showcase: drag pans, two-finger drag orbits, and the fly-to buttons animate the camera.
5. Android regression check: repeat tests 1–4 on an Android device.

**Done when:** all five pass, the rebuilt `libViroReact.a` is committed, and the changelog entry has landed.
