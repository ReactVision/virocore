#!/usr/bin/env bash
#
# build-pico-aar.sh — build the patched virocore renderer AAR and stage it for
# the @mikevocalz/react-viro JS package.
#
# This is the bridge between the two forks:
#   mikevocalz/virocore  (this repo, native)  -- produces -->  viro_renderer-release.aar
#   mikevocalz/viro      (JS package fork)      -- consumes --> bundles the AAR, renames pkg
#
# Prereqs (NOT available in CI-less envs — this is the real toolchain boundary):
#   - Android SDK + NDK (matching virocore's build.gradle ndkVersion)
#   - JDK 17, Gradle wrapper present in android/
#   - ANDROID_HOME / ANDROID_NDK_HOME exported
#
# Usage:
#   ./scripts/build-pico-aar.sh [--out <dir>]
#
set -euo pipefail

OUT_DIR="${PWD}/dist-aar"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUT_DIR="$2"; shift 2;;
    *) echo "unknown arg: $1"; exit 2;;
  esac
done

echo "==> Building patched virocore renderer AAR (PICO support)"
echo "    branch: $(git rev-parse --abbrev-ref HEAD)  commit: $(git rev-parse --short HEAD)"

# Sanity: confirm the PICO patches are actually present in this checkout, so we
# never publish an AAR that silently lacks the work.
grep -q "bytedance/pico4s_controller" \
  android/sharedCode/src/main/cpp/VROInputControllerOpenXR.cpp \
  || { echo "ERROR: PICO interaction profiles missing — wrong branch?"; exit 1; }
grep -q "VROOpenXRRuntimeInfo" \
  android/sharedCode/src/main/cpp/VROSceneRendererOpenXR.h \
  || { echo "ERROR: runtime-info struct missing — wrong branch?"; exit 1; }

cd android
echo "==> ./gradlew :sharedCode:assembleRelease"
./gradlew :sharedCode:assembleRelease

AAR_PATH="sharedCode/build/outputs/aar/sharedCode-release.aar"
[[ -f "$AAR_PATH" ]] || { echo "ERROR: AAR not produced at $AAR_PATH"; exit 1; }

mkdir -p "$OUT_DIR"
# The JS package expects the file named viro_renderer-release.aar.
cp "$AAR_PATH" "$OUT_DIR/viro_renderer-release.aar"

echo "==> AAR staged: $OUT_DIR/viro_renderer-release.aar"
echo "    sha256: $(sha256sum "$OUT_DIR/viro_renderer-release.aar" | cut -d' ' -f1)"
echo ""
echo "Next: in the mikevocalz/viro JS fork,"
echo "  cp $OUT_DIR/viro_renderer-release.aar android/viro_renderer/viro_renderer-release.aar"
echo "  npm version <x.y.z-pico.N> && npm publish --access public"
