#!/bin/bash
# build_visionos.sh — builds ViroKit.xcframework for visionOS (M1)
#
# Output: viro/ios/dist/ViroRendererVisionOS/ViroKit.xcframework
#
# Usage:
#   cd virocore/ios
#   ./build_visionos.sh [Debug|Release]   (default: Release)

set -euo pipefail

CONFIGURATION="${1:-Release}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
VIRO_ROOT="$(cd "$REPO_ROOT/../viro" && pwd)"

PROJ="$SCRIPT_DIR/ViroRenderer.xcodeproj"
BUILD_DIR="$SCRIPT_DIR/build"
HEADERS_STAGING="$BUILD_DIR/visionos_headers"
XCFW_OUT="$VIRO_ROOT/ios/dist/ViroRendererVisionOS/ViroKit.xcframework"

echo "=== ViroKitVisionOS xcframework build ==="
echo "Configuration : $CONFIGURATION"
echo "Output        : $XCFW_OUT"
echo ""

# ── 1. Build device slice (arm64, xros) ──────────────────────────────────────
echo "--- Building xros (device) ---"
xcodebuild \
  -project "$PROJ" \
  -target ViroKitVisionOS \
  -sdk xros \
  -configuration "$CONFIGURATION" \
  CONFIGURATION_BUILD_DIR="$BUILD_DIR/$CONFIGURATION-xros" \
  build

# ── 2. Build simulator slice (arm64 + x86_64, xrsimulator) ──────────────────
echo "--- Building xrsimulator ---"
xcodebuild \
  -project "$PROJ" \
  -target ViroKitVisionOS \
  -sdk xrsimulator \
  -configuration "$CONFIGURATION" \
  CONFIGURATION_BUILD_DIR="$BUILD_DIR/$CONFIGURATION-xrsimulator" \
  build

# ── 3. Stage public headers ───────────────────────────────────────────────────
echo "--- Staging headers ---"
rm -rf "$HEADERS_STAGING"
mkdir -p "$HEADERS_STAGING"

# Shared C++ renderer headers
cp "$REPO_ROOT/ViroRenderer"/*.h "$HEADERS_STAGING/"

# Third-party headers the renderer exposes
[ -f "$REPO_ROOT/ViroRenderer/optional.hpp" ] && cp "$REPO_ROOT/ViroRenderer/optional.hpp" "$HEADERS_STAGING/"
[ -d "$REPO_ROOT/ViroRenderer/glm" ]          && cp -r "$REPO_ROOT/ViroRenderer/glm" "$HEADERS_STAGING/"

# iOS/Metal-specific headers required by VRODriverMetal.h
cp "$SCRIPT_DIR/ViroKit/VROVideoTextureCache.h"       "$HEADERS_STAGING/"
cp "$SCRIPT_DIR/ViroKit/VROVideoTextureCacheMetal.h"  "$HEADERS_STAGING/"

# visionOS-specific driver headers
cp "$SCRIPT_DIR/ViroKit/VisionOS/VRODriverVisionOS.h"    "$HEADERS_STAGING/"
cp "$SCRIPT_DIR/ViroKit/VisionOS/VRORenderTargetMetal.h" "$HEADERS_STAGING/"

# Umbrella header (must match target name: ViroKitVisionOS.h)
cp "$SCRIPT_DIR/ViroKit/ViroKitVisionOS.h" "$HEADERS_STAGING/"

echo "Staged $(ls "$HEADERS_STAGING" | wc -l | tr -d ' ') header entries."

# ── 4. Create xcframework ─────────────────────────────────────────────────────
echo "--- Creating xcframework ---"
rm -rf "$XCFW_OUT"
mkdir -p "$(dirname "$XCFW_OUT")"

xcodebuild -create-xcframework \
  -library "$BUILD_DIR/$CONFIGURATION-xros/libViroKitVisionOS.a" \
  -headers "$HEADERS_STAGING" \
  -library "$BUILD_DIR/$CONFIGURATION-xrsimulator/libViroKitVisionOS.a" \
  -headers "$HEADERS_STAGING" \
  -output "$XCFW_OUT"

# ── 5. Verify ─────────────────────────────────────────────────────────────────
echo ""
echo "=== Verification ==="
echo ""

echo "--- xcframework slices ---"
ls "$XCFW_OUT"

echo ""
echo "--- lipo: device slice ---"
DEVICE_LIB=$(find "$XCFW_OUT" -name "libViroKitVisionOS.a" | grep -v simulator | head -1)
lipo -info "$DEVICE_LIB"

echo ""
echo "--- lipo: simulator slice ---"
SIM_LIB=$(find "$XCFW_OUT" -name "libViroKitVisionOS.a" | grep simulator | head -1)
lipo -info "$SIM_LIB"

echo ""
echo "--- otool LC_BUILD_VERSION (device) ---"
otool -l "$DEVICE_LIB" | grep -A4 "LC_BUILD_VERSION" | head -20

echo ""
echo "=== DONE ==="
echo "xcframework: $XCFW_OUT"
