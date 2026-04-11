#!/bin/bash
# build_visionos.sh
# Compila ViroKit para visionOS (device only — xros) y genera:
#   - ViroKit.xcframework  (device slice únicamente, igual que iOS)
#   - include/             (headers planos para HEADER_SEARCH_PATHS)
#
# Output: viro/ios/dist/ViroRendererVisionOS/
#
# Uso:
#   cd virocore/ios && ./build_visionos.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
WORKSPACE="$SCRIPT_DIR/ViroRenderer.xcworkspace"
SCHEME="ViroKit"

# Destino de salida — relativo a virocore/ios/ → ../../viro/ios/dist/
VIRO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)/viro"
OUT_DIR="$VIRO_ROOT/ios/dist/ViroRendererVisionOS"
ARCHIVES="/tmp/virokit-visionos-$$"

PODS_DIR="$SCRIPT_DIR/Pods"
PODS_IOS_BAK="$SCRIPT_DIR/Pods.ios_bak"
PODS_XROS="$SCRIPT_DIR/Pods.xros"

echo "=== ViroKit visionOS build (device only) ==="
echo "  Workspace : $WORKSPACE"
echo "  Output    : $OUT_DIR"
echo ""

mkdir -p "$ARCHIVES" "$OUT_DIR"

# xcconfig que excluye archivos iOS-only y activa VRO_METAL=1
XCCONFIG="$SCRIPT_DIR/ViroKit-visionOS.xcconfig"

# ── visionOS Pods setup ────────────────────────────────────────────────────────
# Podfile.visionos has NO ARCore/GVR pods — CocoaPods generates a clean Pods/
# project with no iOS-only framework references at all. No patching needed.
#
# First run: install visionOS pods once (no network download — zero pods).
# Subsequent runs: just swap the Pods directory.

setup_xros_pods() {
  if [ ! -d "$PODS_XROS" ]; then
    echo "→ Installing visionOS pods (first time — no downloads)..."
    # CocoaPods always reads 'Podfile' — swap ours in temporarily
    [ -d "$PODS_DIR" ] && mv "$PODS_DIR" "$PODS_IOS_BAK"
    mv "$SCRIPT_DIR/Podfile" "$SCRIPT_DIR/Podfile.ios_bak"
    cp "$SCRIPT_DIR/Podfile.visionos" "$SCRIPT_DIR/Podfile"
    (cd "$SCRIPT_DIR" && pod install --silent) || {
      mv "$SCRIPT_DIR/Podfile.ios_bak" "$SCRIPT_DIR/Podfile"
      [ -d "$PODS_IOS_BAK" ] && mv "$PODS_IOS_BAK" "$PODS_DIR"
      echo "✗ pod install failed" >&2; exit 1
    }
    mv "$SCRIPT_DIR/Podfile.ios_bak" "$SCRIPT_DIR/Podfile"
    mv "$PODS_DIR" "$PODS_XROS"
    [ -d "$PODS_IOS_BAK" ] && mv "$PODS_IOS_BAK" "$PODS_DIR"
    echo "  ✓ Pods.xros/ created"
  fi
}

swap_to_xros_pods() {
  [ -d "$PODS_DIR" ] && mv "$PODS_DIR" "$PODS_IOS_BAK"
  mv "$PODS_XROS" "$PODS_DIR"
  echo "  ✓ Using visionOS Pods (no ARCore/GVR)"
}

restore_ios_pods() {
  # Called by trap on exit — restore regardless of success/failure
  if [ -d "$PODS_IOS_BAK" ]; then
    [ -d "$PODS_DIR" ] && mv "$PODS_DIR" "$PODS_XROS" 2>/dev/null || true
    mv "$PODS_IOS_BAK" "$PODS_DIR"
  fi
}

# ── visionOS static library repathing ─────────────────────────────────────────
# iOS arm64 static archives (.a) have their object files tagged as built for iOS.
# The Apple linker rejects them when linking for visionOS (same ISA, different platform).
# We repath once per library by: (1) extracting arm64 objects, (2) retagging each
# object file from iphoneos to xros using vtool, (3) repacking.
# Repathed libs are cached in Libraries.xros/ and swapped in around the archive step.

LIBS_XROS="$SCRIPT_DIR/Libraries.xros"

# Map: iOS library paths relative to SCRIPT_DIR
IOS_LIBS=(
  "Libraries/freetype/armv7_arm64/libfreetype.a"
  "Libraries/protobuf/armv7_arm64/libprotobuf-lite.a"
  "Libraries/harfbuzz/armv7_arm64/harfbuzz.a"
  "Libraries/bullet/armv7_arm64/libBulletCollision.a"
  "Libraries/bullet/armv7_arm64/libBulletDynamics.a"
  "Libraries/bullet/armv7_arm64/libBulletSoftBody.a"
  "Libraries/bullet/armv7_arm64/libLinearMath.a"
  "Libraries/reactvisioncca/arm64/libreactvisioncca.a"
)

repath_lib_for_xros() {
  local src="$SCRIPT_DIR/$1"
  local dst="$LIBS_XROS/$1"
  [ -f "$dst" ] && return  # already done
  mkdir -p "$(dirname "$dst")"
  local tmp; tmp=$(mktemp -d)
  # Extract arm64-only slice (strip armv7 if fat binary)
  if xcrun lipo -thin arm64 "$src" -output "$tmp/thin" 2>/dev/null; then
    :
  else
    cp "$src" "$tmp/thin"  # already thin or single-arch
  fi
  # Check if it's an AR archive or a Mach-O binary (e.g. dylib with .a extension)
  local filetype; filetype=$(file "$tmp/thin" 2>/dev/null || echo "unknown")
  if echo "$filetype" | grep -q "ar archive"; then
    # Static archive: extract .o files and repath each one
    (cd "$tmp" && xcrun ar -x thin 2>/dev/null)
    rm -f "$tmp/thin" "$tmp/__.SYMDEF"* 2>/dev/null || true
    shopt -s nullglob
    objs=("$tmp"/*.o)
    shopt -u nullglob
    for obj in "${objs[@]}"; do
      xcrun vtool -arch arm64 -set-build-version visionos 1.0 1.0 -replace \
                  -output "$obj" "$obj" 2>/dev/null || true
    done
    if [ ${#objs[@]} -gt 0 ]; then
      xcrun ar rcs "$dst" "${objs[@]}"
    else
      cp "$src" "$dst"
    fi
  else
    # Mach-O binary (dylib / .a-disguised-dylib): repath directly
    xcrun vtool -set-build-version visionos 1.0 1.0 -replace \
                -output "$dst" "$tmp/thin" 2>/dev/null || cp "$tmp/thin" "$dst"
  fi
  rm -rf "$tmp"
  echo "  ✓ $(basename "$dst") → xros"
}

setup_xros_libs() {
  local needs_repath=0
  for lib in "${IOS_LIBS[@]}"; do
    [ -f "$LIBS_XROS/$lib" ] || { needs_repath=1; break; }
  done
  if [ "$needs_repath" -eq 1 ]; then
    echo "→ Repathing static libs from iOS → visionOS (one time)..."
    for lib in "${IOS_LIBS[@]}"; do
      repath_lib_for_xros "$lib"
    done
  fi
}

swap_to_xros_libs() {
  for lib in "${IOS_LIBS[@]}"; do
    local ios="$SCRIPT_DIR/$lib"
    local xros="$LIBS_XROS/$lib"
    [ -f "$ios" ] && mv "$ios" "${ios}.ios_bak"
    cp "$xros" "$ios"
  done
  echo "  ✓ visionOS libs swapped in"
}

restore_ios_libs() {
  for lib in "${IOS_LIBS[@]}"; do
    local ios="$SCRIPT_DIR/$lib"
    local bak="${ios}.ios_bak"
    [ -f "$bak" ] && mv "$bak" "$ios"
  done
}

setup_xros_libs
swap_to_xros_libs

# Always restore iOS Pods and libs on exit (success or failure)
trap "restore_ios_libs; restore_ios_pods" EXIT

setup_xros_pods

echo "→ Swapping to visionOS Pods..."
swap_to_xros_pods

# Flags comunes para xcodebuild
# SDKROOT + SUPPORTED_PLATFORMS must be on the command line (not just xcconfig)
# so xcodebuild resolves the visionOS destination before applying xcconfig.
XCFLAGS=(
  -workspace "$WORKSPACE"
  -scheme    "$SCHEME"
  -xcconfig  "$XCCONFIG"
  SKIP_INSTALL=NO
  BUILD_LIBRARY_FOR_DISTRIBUTION=YES
  SDKROOT=xros
  SUPPORTED_PLATFORMS="xros xrsimulator"
  XROS_DEPLOYMENT_TARGET=1.0
)

# ── Device ────────────────────────────────────────────────────────────────────
echo "→ Archivando para visionOS Device (xros)..."
xcodebuild archive "${XCFLAGS[@]}" \
  -destination "generic/platform=visionOS" \
  -archivePath "$ARCHIVES/device.xcarchive" \
  -quiet
echo "  ✓ Device"

# ── XCFramework (device only) ─────────────────────────────────────────────────
echo "→ Creando ViroKit.xcframework..."
XCFW="$OUT_DIR/ViroKit.xcframework"
rm -rf "$XCFW"

DEV_FW="$ARCHIVES/device.xcarchive/Products/Library/Frameworks/ViroKit.framework"

xcodebuild -create-xcframework \
  -framework "$DEV_FW" \
  -output "$XCFW"
echo "  ✓ $XCFW"

# ── Headers planos para HEADER_SEARCH_PATHS ───────────────────────────────────
# VRORendererBridge.mm usa includes estilo "VRORenderer.h" (sin prefijo de framework),
# así que necesitamos los headers en un directorio plano en HEADER_SEARCH_PATHS.
echo "→ Extrayendo headers..."
INCLUDE_DIR="$OUT_DIR/include"
rm -rf "$INCLUDE_DIR"
cp -r "$DEV_FW/Headers" "$INCLUDE_DIR"
echo "  ✓ $INCLUDE_DIR ($(ls "$INCLUDE_DIR" | wc -l | tr -d ' ') headers)"

# ── Limpieza ──────────────────────────────────────────────────────────────────
rm -rf "$ARCHIVES"

echo ""
echo "=== Listo ==="
echo "  XCFramework : $XCFW"
echo "  Headers     : $INCLUDE_DIR"
echo ""
echo "Siguiente paso: rebuild viro (npm run build) y luego expo prebuild en showcase."
