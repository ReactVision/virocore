# Release Notes

## v3.2.0

**Controllers hear pinch and rotate, and iOS plays sounds built from preloaded data.** The native build ships inside `@reactvision/react-viro` 3.2.0, which moves to React Native 0.88 and Expo SDK 58.

### Everywhere

- **Controllers receive `onPinch` and `onRotate`:** they reach a controller's delegates as drag does, gated on the controller enabling them, with a null node when the gesture started on nothing that takes it.

### iOS

- **Sounds built from preloaded sound data play:** a bare temp-file path opens as a file URL, a failure reaches `soundDidFail`, and the player reports ready and finished as one built from a URL does.

### Check when upgrading

- **A controller that enabled pinch or rotate now gets them,** including with a null node.
