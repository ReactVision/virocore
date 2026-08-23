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

# emcmake injects the Emscripten CMake toolchain automatically (replaces the
# old hand-passed -DCMAKE_TOOLCHAIN_FILE=$EMSCRIPTEN/cmake/... path).
emcmake cmake -H. -Bproducts -DCMAKE_BUILD_TYPE=Release
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
