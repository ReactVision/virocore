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

# Header-only delegate declarations: ObjC protocols plus inline C++ adapters, with no
# implementation in this target. Nothing here needs a .cpp — but every VRT view in
# ios/ViroReact declares conformance to VROEventDelegateProtocol and
# VROTransformDelegateProtocol, so omitting them fails the entire React view layer on a
# missing type. The set is the five protocols the VRT layer references (measured, not
# guessed) plus the transitive closure of their own includes.
#
# The two AR delegate protocols are deliberately NOT staged: the AR subsystem is excluded
# from this target, so the files needing them cannot link on visionOS regardless, and
# staging them would only turn a clear compile error into an obscure link error.
for delegate_header in \
  VROEventDelegateiOS.h \
  VROTransformDelegateiOS.h \
  VROSceneDelegateiOS.h \
  VROPortalDelegateiOS.h \
  VROPhysicsBodyDelegateiOS.h \
  VRORenderDelegate.h \
  VRORenderDelegateiOS.h \
  VROVideoDelegate.h \
  VROVideoDelegateiOS.h
do
  cp "$SCRIPT_DIR/ViroKit/$delegate_header" "$HEADERS_STAGING/"
done

# Umbrella header (must match target name: ViroKitVisionOS.h)
cp "$SCRIPT_DIR/ViroKit/ViroKitVisionOS.h" "$HEADERS_STAGING/"

# ── ViroKit/ forwarding headers ───────────────────────────────────────────────
#
# Consumers include headers two ways and both have to work:
#
#   #include "VRODefines.h"            flat — used 38 times in ios/ViroReact
#   #import <ViroKit/VRODefines.h>     framework style — 17 distinct forms, and
#                                      <ViroKit/ViroKit.h> alone appears in 32 files
#
# The flat form works because CocoaPods puts this Headers/ directory on the search path.
# The framework form needs a directory literally named ViroKit, which a static-library
# xcframework has no reason to have — so before 2026-08-23 none of those 49 includes
# resolved and the ViroReact pod could not compile for xros at all. It went unnoticed
# because the standalone test app includes "VRORendererBridge.h" directly and never goes
# through the pod.
#
# Forwarding stubs rather than copies or symlinks: copying would double 8.2 MB of headers
# per slice, and symlinks can be flattened by whatever unpacks the npm tarball. Each stub
# is one line and costs nothing.
#
# ViroKit.h maps to ViroKitVisionOS.h: the iOS framework's umbrella is named ViroKit.h and
# that is the name the 32 consumers use.
mkdir -p "$HEADERS_STAGING/ViroKit"
fwd_count=0
for h in "$HEADERS_STAGING"/*.h; do
  base="$(basename "$h")"
  printf '#include "../%s"\n' "$base" > "$HEADERS_STAGING/ViroKit/$base"
  fwd_count=$((fwd_count + 1))
done
printf '#include "../ViroKitVisionOS.h"\n' > "$HEADERS_STAGING/ViroKit/ViroKit.h"
echo "    ViroKit/ forwarding headers: $fwd_count (+ ViroKit.h -> ViroKitVisionOS.h)"

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
