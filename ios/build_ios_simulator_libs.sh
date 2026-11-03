#!/bin/bash
# build_ios_simulator_libs.sh: adds the arm64 iOS simulator slices ViroKit is missing
#
# ViroKit links six third-party archives. For the simulator it searches the Libraries/*/x86_64
# folders, which despite the name hold fat simulator archives, and every one of them already had
# an arm64 simulator slice except three:
#
#   bullet/x86_64/libLinearMath.a        x86_64 only   rebuilt from Bullet 2.87
#   protobuf/x86_64/libprotobuf-lite.a   x86_64 only   rebuilt from protobuf 3.2.0
#   GVRAudioSDK (CocoaPods)              no arm64 sim  closed source, relabelled by
#                                                      retag_for_simulator.py
#
# The versions are pinned to the ones the device archives were built from (version.txt in each
# Libraries folder), so device and simulator run the same code. Compile flags follow the device
# archives too: default symbol visibility, since ViroKit re-exports those symbols on the device.
#
# Apple Silicon simulators run arm64 only (the iOS 27 runtime has no x86_64 at all), so the
# arm64 slice is what makes ViroKit usable there. The existing x86_64 slices are kept.
#
# Output (arm64 simulator slice added in place):
#   ios/Libraries/bullet/x86_64/libLinearMath.a
#   ios/Libraries/protobuf/x86_64/libprotobuf-lite.a
#   ios/Libraries/GVRAudioSDK/simulator/libGVRAudioSDK.a   (arm64 + x86_64)
#
# Usage:
#   ./build_ios_simulator_libs.sh [bullet3-2.87 source] [protobuf-3.2.0 source]
# Run `pod install` first: the GVR slice is taken from Pods/GVRAudioSDK.

set -euo pipefail

BULLET_VERSION="2.87"
PROTOBUF_VERSION="3.2.0"
MIN_VERSION="15.1"
TRIPLE="arm64-apple-ios$MIN_VERSION-simulator"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LIBS="$SCRIPT_DIR/Libraries"
SYSROOT="$(xcrun --sdk iphonesimulator --show-sdk-path)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

fetch() {
  local url="$1" dir="$2"
  echo "--- Downloading $url ---" >&2
  curl -sSL -o "$WORK/src.tar.gz" "$url"
  tar xzf "$WORK/src.tar.gz" -C "$WORK"
  rm "$WORK/src.tar.gz"
  echo "$WORK/$dir"
}

BT_SRC="${1:-$(fetch "https://github.com/bulletphysics/bullet3/archive/refs/tags/$BULLET_VERSION.tar.gz" "bullet3-$BULLET_VERSION")}"
PB_SRC="${2:-$(fetch "https://github.com/protocolbuffers/protobuf/archive/refs/tags/v$PROTOBUF_VERSION.tar.gz" "protobuf-$PROTOBUF_VERSION")}"
[ -f "$BT_SRC/src/btBulletDynamicsCommon.h" ] || { echo "error: $BT_SRC is not a bullet3 tree" >&2; exit 1; }
[ -f "$PB_SRC/src/google/protobuf/message_lite.h" ] || { echo "error: $PB_SRC is not a protobuf tree" >&2; exit 1; }

# compile_archive <output.a> <source root> <flags...> -- <sources...>
compile_archive() {
  local out="$1" root="$2"; shift 2
  local flags=()
  while [ "$1" != "--" ]; do flags+=( "$1" ); shift; done
  shift
  local objdir="$WORK/obj-$(basename "$out" .a)"
  mkdir -p "$objdir"
  local objects=()
  for src in "$@"; do
    local obj="$objdir/$(echo "${src#$root/}" | tr '/' '_' | sed 's/\.[a-z]*$/.o/')"
    xcrun clang++ -target "$TRIPLE" -isysroot "$SYSROOT" -O2 -fPIC "${flags[@]}" -c "$src" -o "$obj"
    objects+=( "$obj" )
  done
  xcrun libtool -static -no_warning_for_no_symbols -o "$out" "${objects[@]}"
  echo "    $(basename "$out"): ${#objects[@]} objects"
}

# add_slice <fat archive> <arm64 simulator archive>
add_slice() {
  local fat="$1" slice="$2"
  if xcrun lipo -archs "$fat" | tr ' ' '\n' | grep -qx arm64; then
    xcrun lipo "$fat" -remove arm64 -output "$fat"
  fi
  xcrun lipo -create "$fat" "$slice" -output "$fat"
  echo "    $fat: $(xcrun lipo -archs "$fat")"
}

echo "--- LinearMath (Bullet $BULLET_VERSION) ---"
# Same flags as build_bullet_visionos.sh, which explains each of them, minus -fvisibility=hidden.
compile_archive "$WORK/libLinearMath.a" "$BT_SRC/src" \
  -std=c++11 -I"$BT_SRC/src" \
  -Wno-deprecated-declarations -Wno-unused-parameter -Wno-unused-variable \
  -Wno-unused-but-set-variable -Wno-deprecated-copy -Wno-argument-outside-range -Wno-nontrivial-memcall \
  -- $(find "$BT_SRC/src/LinearMath" -name '*.cpp' | sort)
add_slice "$LIBS/bullet/x86_64/libLinearMath.a" "$WORK/libLinearMath.a"

echo "--- protobuf-lite $PROTOBUF_VERSION ---"
# The libprotobuf-lite source list from the release's cmake/libprotobuf-lite.cmake. The two x86
# atomicops files compile to nothing on arm64 and are kept only to match it.
# The renderer's protobuf lives in namespace google_public (Libraries/protobuf/include and
# Nodes.pb.h), so it cannot collide with the copy inside ARCore. Macros do not expand inside an
# #include <...> path, so the rename touches only the code.
PB="$PB_SRC/src/google/protobuf"
compile_archive "$WORK/libprotobuf-lite.a" "$PB_SRC/src" \
  -std=c++11 -DHAVE_PTHREAD -Dgoogle=google_public -I"$PB_SRC/src" \
  -Wno-deprecated-declarations -Wno-unused-parameter -Wno-deprecated-copy \
  -Wno-invalid-noreturn -Wno-unused-const-variable \
  -- "$PB/arena.cc" "$PB/arenastring.cc" "$PB/extension_set.cc" \
     "$PB/generated_message_util.cc" "$PB/io/coded_stream.cc" "$PB/io/zero_copy_stream.cc" \
     "$PB/io/zero_copy_stream_impl_lite.cc" "$PB/message_lite.cc" "$PB/repeated_field.cc" \
     "$PB/stubs/atomicops_internals_x86_gcc.cc" "$PB/stubs/atomicops_internals_x86_msvc.cc" \
     "$PB/stubs/bytestream.cc" "$PB/stubs/common.cc" "$PB/stubs/int128.cc" "$PB/stubs/once.cc" \
     "$PB/stubs/status.cc" "$PB/stubs/statusor.cc" "$PB/stubs/stringpiece.cc" \
     "$PB/stubs/stringprintf.cc" "$PB/stubs/structurally_valid.cc" "$PB/stubs/strutil.cc" \
     "$PB/stubs/time.cc" "$PB/wire_format_lite.cc"
add_slice "$LIBS/protobuf/x86_64/libprotobuf-lite.a" "$WORK/libprotobuf-lite.a"

echo "--- GVRAudioSDK ---"
GVR="$SCRIPT_DIR/Pods/GVRAudioSDK/Libraries/libGVRAudioSDK.a"
[ -f "$GVR" ] || { echo "error: $GVR not found; run pod install first" >&2; exit 1; }
mkdir -p "$LIBS/GVRAudioSDK/simulator"
xcrun lipo "$GVR" -thin arm64 -output "$WORK/gvr-device-arm64.a"
xcrun lipo "$GVR" -thin x86_64 -output "$WORK/gvr-x86_64.a"
python3 "$SCRIPT_DIR/retag_for_simulator.py" "$WORK/gvr-device-arm64.a" "$WORK/gvr-sim-arm64.a" "$MIN_VERSION"
rm -f "$LIBS/GVRAudioSDK/simulator/libGVRAudioSDK.a"
xcrun lipo -create "$WORK/gvr-sim-arm64.a" "$WORK/gvr-x86_64.a" \
  -output "$LIBS/GVRAudioSDK/simulator/libGVRAudioSDK.a"
echo "    libGVRAudioSDK.a: $(xcrun lipo -archs "$LIBS/GVRAudioSDK/simulator/libGVRAudioSDK.a")"
