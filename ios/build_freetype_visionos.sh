#!/bin/bash
# build_freetype_visionos.sh — builds libfreetype.a for visionOS (M5)
#
# Why this exists: the prebuilt libraries in ios/Libraries/freetype are tagged
# LC_VERSION_MIN_IPHONEOS and cannot be retagged for xros — `vtool` has no space in the
# load commands to rewrite. The only way to get freetype onto visionOS is to build it from
# source, which is what this does.
#
# It deliberately avoids freetype's autotools and CMake builds. Cross-compiling either for
# xros means fighting the build system over the target triple and the SDK; compiling the
# documented source list directly with clang is both shorter and fully explicit about what
# ends up in the archive.
#
# Output:
#   ios/Libraries/freetype/xros/libfreetype.a          (arm64, device)
#   ios/Libraries/freetype/xrsimulator/libfreetype.a   (arm64 + x86_64, fat)
#   ios/Libraries/freetype/include/                    (headers, shared with the iOS build)
#
# Usage:
#   ./build_freetype_visionos.sh [path-to-freetype-source]
#
# With no argument the source tarball is downloaded into a temporary directory.

set -euo pipefail

FREETYPE_VERSION="2.13.3"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
LIB_OUT="$SCRIPT_DIR/Libraries/freetype"
MIN_VERSION="1.0"

# ── Source ────────────────────────────────────────────────────────────────────
if [ $# -ge 1 ]; then
  FT_SRC="$1"
else
  WORK="$(mktemp -d)"
  echo "--- Downloading freetype $FREETYPE_VERSION ---"
  curl -sSL -o "$WORK/freetype.tar.gz" \
    "https://download.savannah.gnu.org/releases/freetype/freetype-$FREETYPE_VERSION.tar.gz"
  tar xzf "$WORK/freetype.tar.gz" -C "$WORK"
  FT_SRC="$WORK/freetype-$FREETYPE_VERSION"
fi

if [ ! -f "$FT_SRC/include/freetype/freetype.h" ]; then
  echo "error: $FT_SRC does not look like a freetype source tree" >&2
  exit 1
fi
echo "Source: $FT_SRC"

# ── Source list ───────────────────────────────────────────────────────────────
#
# The base files, plus one amalgamation per module. The module set matches
# include/freetype/config/ftmodule.h exactly: ftinit.c references every class listed
# there, so compiling a subset would leave undefined symbols at link time. Keeping the
# default list costs some archive size and buys immunity from that whole class of error.

BASE_SOURCES=(
  base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c
  base/ftbbox.c base/ftbdf.c base/ftbitmap.c base/ftcid.c
  base/ftfstype.c base/ftgasp.c base/ftglyph.c base/ftgxval.c
  base/ftmm.c base/ftotval.c base/ftpatent.c base/ftpfr.c
  base/ftstroke.c base/ftsynth.c base/fttype1.c base/ftwinfnt.c
)

MODULE_SOURCES=(
  autofit/autofit.c
  truetype/truetype.c
  type1/type1.c
  cff/cff.c
  cid/type1cid.c
  pfr/pfr.c
  type42/type42.c
  winfonts/winfnt.c
  pcf/pcf.c
  bdf/bdf.c
  psaux/psaux.c
  psnames/psnames.c
  pshinter/pshinter.c
  sfnt/sfnt.c
  smooth/smooth.c
  raster/raster.c
  # The per-module amalgamation, not the individual files: sdf.c pulls in ftsdf.c,
  # ftbsdf.c, ftsdfcommon.c and ftsdfrend.c, and the last of those is what defines
  # ft_bitmap_sdf_renderer_class, which ftinit.c references.
  sdf/sdf.c
  svg/svg.c
)

# Compression backends: freetype's own gzip/lzw are enabled by default in ftoption.h and
# are what FT_Stream_OpenGzip needs. bzip2 is off by default and its translation unit
# compiles to nothing, so it is included for safety rather than necessity.
COMPRESS_SOURCES=(
  gzip/ftgzip.c
  lzw/ftlzw.c
  bzip2/ftbzip2.c
)

ALL_SOURCES=( "${BASE_SOURCES[@]}" "${MODULE_SOURCES[@]}" "${COMPRESS_SOURCES[@]}" )

# ── Build one slice ───────────────────────────────────────────────────────────
build_slice() {
  local sdk="$1" triple="$2" outdir="$3"
  local sysroot
  sysroot="$(xcrun --sdk "$sdk" --show-sdk-path)"

  echo "--- $triple ($sdk) ---"
  rm -rf "$outdir" && mkdir -p "$outdir"

  local flags=(
    -target "$triple"
    -isysroot "$sysroot"
    -arch "${triple%%-*}"
    -O2 -fPIC -fvisibility=hidden
    -DFT2_BUILD_LIBRARY
    -I"$FT_SRC/include"
    # freetype's own headers are clean, but the modules are noisy about unused parameters
    # and freetype ships with -Wall builds anyway; keep the log readable.
    -Wno-unused-parameter -Wno-unused-function
  )

  local objects=()
  for src in "${ALL_SOURCES[@]}"; do
    if [ ! -f "$FT_SRC/src/$src" ]; then
      echo "error: missing source $src — the freetype layout changed for this version" >&2
      exit 1
    fi
    local obj="$outdir/$(echo "$src" | tr '/' '_' | sed 's/\.c$/.o/')"
    xcrun clang "${flags[@]}" -c "$FT_SRC/src/$src" -o "$obj"
    objects+=( "$obj" )
  done

  xcrun ar -crs "$outdir/libfreetype.a" "${objects[@]}"
  rm -f "${objects[@]}"
  echo "    $(basename "$outdir")/libfreetype.a — $(du -h "$outdir/libfreetype.a" | cut -f1), ${#ALL_SOURCES[@]} objects"
}

BUILD_TMP="$(mktemp -d)"
build_slice xros        "arm64-apple-xros$MIN_VERSION"            "$BUILD_TMP/device-arm64"
build_slice xrsimulator "arm64-apple-xros$MIN_VERSION-simulator"  "$BUILD_TMP/sim-arm64"
build_slice xrsimulator "x86_64-apple-xros$MIN_VERSION-simulator" "$BUILD_TMP/sim-x86_64"

# ── Stage ─────────────────────────────────────────────────────────────────────
echo "--- Staging ---"
mkdir -p "$LIB_OUT/xros" "$LIB_OUT/xrsimulator"
cp "$BUILD_TMP/device-arm64/libfreetype.a" "$LIB_OUT/xros/libfreetype.a"
xcrun lipo -create \
  "$BUILD_TMP/sim-arm64/libfreetype.a" \
  "$BUILD_TMP/sim-x86_64/libfreetype.a" \
  -output "$LIB_OUT/xrsimulator/libfreetype.a"

# The headers are version-specific, so they are refreshed alongside the archives rather
# than left pointing at whatever the old prebuilt libraries shipped.
mkdir -p "$LIB_OUT/include-visionos"
rm -rf "$LIB_OUT/include-visionos"
mkdir -p "$LIB_OUT/include-visionos"
cp -R "$FT_SRC/include/"* "$LIB_OUT/include-visionos/"
echo "freetype $FREETYPE_VERSION (built from source for xros)" > "$LIB_OUT/xros/version.txt"
cp "$LIB_OUT/xros/version.txt" "$LIB_OUT/xrsimulator/version.txt"

# ── Verify ────────────────────────────────────────────────────────────────────
echo "--- Verification ---"
for slice in xros xrsimulator; do
  printf "%-14s " "$slice"
  xcrun lipo -info "$LIB_OUT/$slice/libfreetype.a" | sed 's/.*are: //;s/.*is architecture: //'
done
echo "platform (must be 11 = VISIONOS, and 11 with simulator for the sim slice):"
xcrun otool -l "$LIB_OUT/xros/libfreetype.a" 2>/dev/null \
  | grep -A3 -m1 LC_BUILD_VERSION | sed 's/^/    /'
echo "FT_New_Memory_Face present:"
xcrun nm -g "$LIB_OUT/xros/libfreetype.a" 2>/dev/null | grep -c "T _FT_New_Memory_Face" | sed 's/^/    /'

rm -rf "$BUILD_TMP"
echo "=== DONE ==="
