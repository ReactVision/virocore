#!/bin/bash
# build_bullet_visionos.sh — builds the Bullet static libraries for visionOS (M5)
#
# Same situation and same approach as build_freetype_visionos.sh: the prebuilt archives in
# ios/Libraries/bullet are tagged LC_VERSION_MIN_IPHONEOS and cannot be retagged for xros,
# so they are rebuilt from source. Bullet's CMake build is avoided for the same reason —
# compiling the source globs directly with clang is shorter and fully explicit.
#
# The version is pinned to 2.87, matching ios/Libraries/bullet/version.txt and the Android
# build. Physics is simulation: iOS, Android and visionOS running different Bullet versions
# would mean the same scene settling differently per platform.
#
# BulletSoftBody is deliberately not built. No btSoftBody symbol appears anywhere in the
# renderer, so it would be dead weight in the archive.
#
# Output:
#   ios/Libraries/bullet/xros/{libLinearMath,libBulletCollision,libBulletDynamics}.a
#   ios/Libraries/bullet/xrsimulator/...   (arm64 + x86_64 fat)
#   ios/Libraries/bullet/include-visionos/ (headers)
#
# Usage:
#   ./build_bullet_visionos.sh [path-to-bullet3-source]

set -euo pipefail

BULLET_VERSION="2.87"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LIB_OUT="$SCRIPT_DIR/Libraries/bullet"
MIN_VERSION="1.0"

# The three Bullet libraries the renderer actually links, in dependency order.
LIBRARIES=( LinearMath BulletCollision BulletDynamics )

# ── Source ────────────────────────────────────────────────────────────────────
if [ $# -ge 1 ]; then
  BT_SRC="$1"
else
  WORK="$(mktemp -d)"
  echo "--- Downloading Bullet $BULLET_VERSION ---"
  curl -sSL -o "$WORK/bullet.tar.gz" \
    "https://github.com/bulletphysics/bullet3/archive/refs/tags/$BULLET_VERSION.tar.gz"
  tar xzf "$WORK/bullet.tar.gz" -C "$WORK"
  BT_SRC="$WORK/bullet3-$BULLET_VERSION"
fi

if [ ! -f "$BT_SRC/src/btBulletDynamicsCommon.h" ]; then
  echo "error: $BT_SRC does not look like a bullet3 source tree" >&2
  exit 1
fi
echo "Source: $BT_SRC"

# ── Build one library for one slice ───────────────────────────────────────────
build_library() {
  local lib="$1" sdk="$2" triple="$3" outdir="$4"
  local sysroot
  sysroot="$(xcrun --sdk "$sdk" --show-sdk-path)"

  mkdir -p "$outdir/$lib"

  local flags=(
    -target "$triple"
    -isysroot "$sysroot"
    -arch "${triple%%-*}"
    -O2 -fPIC -fvisibility=hidden
    # Bullet 2.87 predates C++17, which removed the `register` keyword and tightened a few
    # other things it relies on. Pin the standard rather than patching the sources.
    -std=c++11
    -I"$BT_SRC/src"
    -Wno-deprecated-declarations -Wno-unused-parameter -Wno-unused-variable
    -Wno-unused-but-set-variable -Wno-deprecated-copy
    # Bullet 2.87 has an upstream bug in btVector3::normalize(): bt_splat_ps(y, 0x80)
    # builds a shuffle immediate of 10880, where the intent was clearly 0x00 (splat lane
    # zero). Newer clang promotes that from a warning to an error. It is suppressed rather
    # than patched because clang masks the immediate to lanes (0,0,0,2), affecting only the
    # w component — which is padding in btVector3 — and because the prebuilt x86_64 Bullet
    # that iOS already ships contains exactly this instruction. Patching it would make
    # visionOS the only platform whose physics numerics differ. Only x86_64 is affected;
    # the arm64 slices do not take the SSE path.
    -Wno-argument-outside-range
  )

  local objects=()
  while IFS= read -r src; do
    local obj="$outdir/$lib/$(echo "${src#$BT_SRC/src/}" | tr '/' '_' | sed 's/\.cpp$/.o/')"
    xcrun clang++ "${flags[@]}" -c "$src" -o "$obj"
    objects+=( "$obj" )
  done < <(find "$BT_SRC/src/$lib" -name '*.cpp' | sort)

  if [ ${#objects[@]} -eq 0 ]; then
    echo "error: no sources found for $lib" >&2
    exit 1
  fi

  xcrun ar -crs "$outdir/lib$lib.a" "${objects[@]}"
  rm -rf "$outdir/$lib"
  echo "    lib$lib.a — $(du -h "$outdir/lib$lib.a" | cut -f1), ${#objects[@]} objects"
}

build_slice() {
  local sdk="$1" triple="$2" outdir="$3"
  echo "--- $triple ($sdk) ---"
  rm -rf "$outdir" && mkdir -p "$outdir"
  for lib in "${LIBRARIES[@]}"; do
    build_library "$lib" "$sdk" "$triple" "$outdir"
  done
}

BUILD_TMP="$(mktemp -d)"
build_slice xros        "arm64-apple-xros$MIN_VERSION"            "$BUILD_TMP/device-arm64"
build_slice xrsimulator "arm64-apple-xros$MIN_VERSION-simulator"  "$BUILD_TMP/sim-arm64"
build_slice xrsimulator "x86_64-apple-xros$MIN_VERSION-simulator" "$BUILD_TMP/sim-x86_64"

# ── Stage ─────────────────────────────────────────────────────────────────────
echo "--- Staging ---"
mkdir -p "$LIB_OUT/xros" "$LIB_OUT/xrsimulator"
for lib in "${LIBRARIES[@]}"; do
  cp "$BUILD_TMP/device-arm64/lib$lib.a" "$LIB_OUT/xros/lib$lib.a"
  xcrun lipo -create \
    "$BUILD_TMP/sim-arm64/lib$lib.a" \
    "$BUILD_TMP/sim-x86_64/lib$lib.a" \
    -output "$LIB_OUT/xrsimulator/lib$lib.a"
done

# Headers: only the public trees, matching what ios/Libraries/bullet/include ships.
rm -rf "$LIB_OUT/include-visionos"
mkdir -p "$LIB_OUT/include-visionos"
cp "$BT_SRC/src/btBulletCollisionCommon.h" "$BT_SRC/src/btBulletDynamicsCommon.h" \
   "$LIB_OUT/include-visionos/"
for lib in "${LIBRARIES[@]}"; do
  rsync -a --include='*/' --include='*.h' --exclude='*' \
    "$BT_SRC/src/$lib/" "$LIB_OUT/include-visionos/$lib/"
done
# Patch the one upstream bug that reaches consumers. btVector3::normalize() builds a shuffle
# immediate of 10880 from bt_splat_ps(y, 0x80) where the intent was 0x00 (splat lane zero);
# newer clang treats that as an error, so *any* consumer including a physics header and
# compiling for x86_64 fails outright. The compiled archives suppress the diagnostic to keep
# their numerics identical to the other platforms' prebuilt libraries, but the shipped headers
# have to be correct or nothing downstream builds.
#
# Note: ios/Libraries/bullet/include carries the same line, so the iOS build has the same
# latent problem with a modern toolchain.
for header in btVector3.h btMatrix3x3.h; do
  target="$LIB_OUT/include-visionos/LinearMath/$header"
  [ -f "$target" ] || continue
  if grep -q "bt_splat_ps(.*0x80)" "$target"; then
    /usr/bin/sed -i '' 's/bt_splat_ps(\(.*\), 0x80)/bt_splat_ps(\1, 0x00)/g' "$target"
    echo "    patched $header (bt_splat_ps 0x80 -> 0x00)"
  fi
done

echo "Bullet $BULLET_VERSION (built from source for xros)" > "$LIB_OUT/xros/version.txt"
cp "$LIB_OUT/xros/version.txt" "$LIB_OUT/xrsimulator/version.txt"

# ── Verify ────────────────────────────────────────────────────────────────────
echo "--- Verification ---"
for slice in xros xrsimulator; do
  for lib in "${LIBRARIES[@]}"; do
    printf "%-14s %-20s " "$slice" "lib$lib.a"
    xcrun lipo -info "$LIB_OUT/$slice/lib$lib.a" | sed 's/.*are: //;s/.*is architecture: //'
  done
done
# grep -m1 closes the pipe early, which makes otool exit non-zero; with pipefail that would
# fail the whole script over a diagnostic. Verification runs in a subshell without it.
(
  set +o pipefail
  echo "platform (11 = VISIONOS):"
  xcrun otool -l "$LIB_OUT/xros/libBulletDynamics.a" 2>/dev/null \
    | grep -A3 -m1 LC_BUILD_VERSION | sed 's/^/    /'
  echo "key symbols:"
  for sym in btDiscreteDynamicsWorld btRigidBody btBvhTriangleMeshShape btCompoundShape; do
    count=$(xcrun nm "$LIB_OUT/xros/libBulletCollision.a" "$LIB_OUT/xros/libBulletDynamics.a" 2>/dev/null \
            | grep -c "$sym" || true)
    printf "    %-28s %s\n" "$sym" "$count"
  done
)

rm -rf "$BUILD_TMP"
echo "=== DONE ==="
