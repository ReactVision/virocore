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
cp "$SCRIPT_DIR/ViroKit/VisionOS/VRODriverVisionOS.h"      "$HEADERS_STAGING/"
cp "$SCRIPT_DIR/ViroKit/VisionOS/VRORenderTargetMetal.h"   "$HEADERS_STAGING/"
cp "$SCRIPT_DIR/ViroKit/VisionOS/VROMetalRenderPassHost.h" "$HEADERS_STAGING/"
cp "$SCRIPT_DIR/ViroKit/VisionOS/VROMetalFrameTimer.h"     "$HEADERS_STAGING/"

# Third-party headers that leak into public ViroKit headers: VROGlyph.h and VROTypeface.h
# include <ft2build.h>, and the physics headers include btBulletDynamicsCommon.h. A consumer
# of this xcframework cannot compile without them, so they are staged here instead of making
# every consumer add search paths.
for vendor in freetype bullet; do
  if [ -d "$SCRIPT_DIR/Libraries/$vendor/include-visionos" ]; then
    cp -R "$SCRIPT_DIR/Libraries/$vendor/include-visionos/"* "$HEADERS_STAGING/"
  else
    echo "warning: $vendor headers not staged — run ./build_${vendor}_visionos.sh first" >&2
  fi
done

# Umbrella header (must match target name: ViroKitVisionOS.h)
cp "$SCRIPT_DIR/ViroKit/ViroKitVisionOS.h" "$HEADERS_STAGING/"

# VRODriverMetal compiles shader modifiers at runtime from the MSL source, which it
# loads as a bundled resource named ViroShadersSource.txt. That resource is a
# preprocessed variant of Shaders.metal — newLibraryWithSource: has no include paths, so
# the shared structures must be inlined. It used to be maintained by hand and silently
# drifted; generate it instead.
echo "--- Generating ViroShadersSource.txt ---"
ruby "$SCRIPT_DIR/generate_shader_source.rb" \
  "$SCRIPT_DIR/ViroKitVisionOSTest/App/ViroShadersSource.txt"

echo "Staged $(ls "$HEADERS_STAGING" | wc -l | tr -d ' ') header entries."

# ── 4. Create xcframework ─────────────────────────────────────────────────────
echo "--- Creating xcframework ---"
rm -rf "$XCFW_OUT"
mkdir -p "$(dirname "$XCFW_OUT")"

# Merge the third-party static libraries into the archive rather than shipping them
# separately. The iOS build links them as separate vendored libraries through the podspec;
# here the xcframework is the whole delivery, so folding them in means a consumer needs no
# extra link lines and cannot end up with ViroKit's text or physics code and nothing behind
# it — which surfaces as a wall of undefined symbols rather than a useful error.
MERGED_DIR="$BUILD_DIR/merged"
rm -rf "$MERGED_DIR" && mkdir -p "$MERGED_DIR/xros" "$MERGED_DIR/xrsimulator"

# Built by build_freetype_visionos.sh and build_bullet_visionos.sh.
VENDORED_LIBS=(
  "freetype/%s/libfreetype.a"
  "bullet/%s/libLinearMath.a"
  "bullet/%s/libBulletCollision.a"
  "bullet/%s/libBulletDynamics.a"
)

merge_vendored() {
  local slice="$1" sdkdir="$2"
  local inputs=( "$BUILD_DIR/$sdkdir/libViroKitVisionOS.a" )
  local missing=()

  for pattern in "${VENDORED_LIBS[@]}"; do
    # shellcheck disable=SC2059
    local lib
    lib="$SCRIPT_DIR/Libraries/$(printf "$pattern" "$slice")"
    if [ -f "$lib" ]; then
      inputs+=( "$lib" )
    else
      missing+=( "$(basename "$lib")" )
    fi
  done

  xcrun libtool -static -o "$MERGED_DIR/$slice/libViroKitVisionOS.a" "${inputs[@]}" 2>/dev/null
  echo "    $slice: merged $(( ${#inputs[@]} - 1 )) vendored libraries"
  if [ ${#missing[@]} -gt 0 ]; then
    echo "    $slice: MISSING ${missing[*]} — run build_freetype_visionos.sh / build_bullet_visionos.sh" >&2
  fi
}
merge_vendored xros        "$CONFIGURATION-xros"
merge_vendored xrsimulator "$CONFIGURATION-xrsimulator"

xcodebuild -create-xcframework \
  -library "$MERGED_DIR/xros/libViroKitVisionOS.a" \
  -headers "$HEADERS_STAGING" \
  -library "$MERGED_DIR/xrsimulator/libViroKitVisionOS.a" \
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
# `head` closes the pipe early, which makes otool exit non-zero; with pipefail that fails
# the whole script over a diagnostic line. Run the check without it.
( set +o pipefail; otool -l "$DEVICE_LIB" | grep -A4 "LC_BUILD_VERSION" | head -20 )

echo ""
echo "=== DONE ==="
echo "xcframework: $XCFW_OUT"
