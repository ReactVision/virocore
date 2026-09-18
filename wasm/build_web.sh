#!/bin/bash
#
# Phase 0 — build the web library/module target (viro_web).
#
# Prereqs:
#   1. Install emsdk 3.x/4.x and activate it:
#        git clone https://github.com/emscripten-core/emsdk.git
#        cd emsdk && ./emsdk install latest && ./emsdk activate latest
#   2. Source the environment so $EMSDK / emcmake are on PATH:
#        source /path/to/emsdk/emsdk_env.sh
#
# Output (in products/build/): viro-web.js, viro-web.wasm, viro-web.data
#
set -e

echo "Building viro_web [emcc: $(command -v emcc || echo 'NOT FOUND')]"

# What this binary is, so the artifact can still say where it came from after it
# has been copied into viro-web-renderer and from there into an app's
# node_modules. "-dirty" is not cosmetic: it is the difference between a build
# someone else can reproduce and one only this working tree ever had.
BUILD_ID="$(git rev-parse --short HEAD 2>/dev/null || echo nogit)"
if ! git diff --quiet HEAD -- .. 2>/dev/null; then
  BUILD_ID="$BUILD_ID-dirty"
fi
BUILD_ID="$BUILD_ID $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "Build id: $BUILD_ID"

# emcmake injects the Emscripten CMake toolchain automatically (replaces the
# old hand-passed -DCMAKE_TOOLCHAIN_FILE=$EMSCRIPTEN/cmake/... path).
emcmake cmake -H. -Bproducts -DCMAKE_BUILD_TYPE=Release \
    -DVIRO_WEB_BUILD_ID="$BUILD_ID"
cmake --build products --target viro_web -- -j 4

# Co-locate the HTML harness with the artifacts so everything is servable from
# one directory (the module imports ./viro-web.js relative to the page).
cp test/web/index.html products/build/index.html

echo
echo "Done. Artifacts (in products/build/):"
ls -lh products/build/viro-web.* products/build/index.html 2>/dev/null \
    || echo "  (no artifacts — check the build log above)"
echo
echo "To run:"
echo "  cd products/build && python3 -m http.server 8080"
echo "  open http://localhost:8080/  (Chrome + Safari)"
