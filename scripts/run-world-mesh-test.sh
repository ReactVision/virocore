#!/usr/bin/env bash
# Builds and runs the world-mesh fusion test on the host.
#
# It drives the production VROARWorldMesh with a synthetic VROARFrame: updateFromFrame, fusion,
# surface extraction, stats and .rvwm serialization. The parts fusion does not exercise — debug
# draw, occlusion geometry, the scene graph, physics — link against minimal stand-ins, and the
# async dispatch runs inline so a step is finished by the time it returns.
#
# This is not a substitute for a device. There is no real depth sensor here, no real tracking and
# no frame budget. What it does catch is the wiring between the pieces, which is where a
# single-frame mesh once replaced a whole fused room as soon as tracking went Limited.
set -euo pipefail

cd "$(dirname "$0")/.."
RENDERER="$PWD/ViroRenderer"
BULLET="$PWD/macos/Libraries/bullet/include"
TEST="$RENDERER/test/worldmesh"
OUT="${TMPDIR:-/tmp}/viro-worldmesh-test"
mkdir -p "$OUT"

# -Wno-argument-outside-range: Bullet's own SSE headers trip it with current clang.
FLAGS=(-std=c++17 -x objective-c++ -Wno-argument-outside-range -I"$RENDERER" -I"$BULLET")

SOURCES=(
  "$RENDERER/VROARWorldMesh.cpp"
  "$RENDERER/VROTSDFVolume.cpp"
  "$RENDERER/VROARDepthMesh.cpp"
  "$RENDERER/VROVector3f.cpp"
  "$RENDERER/VROVector4f.cpp"
  "$RENDERER/VROMatrix4f.cpp"
  "$RENDERER/VROQuaternion.cpp"
  "$TEST/VROTestPlatformStubs.mm"
  "$TEST/VROTestRendererStubs.mm"
  "$TEST/VROTestBulletStubs.mm"
  "$TEST/VROWorldMeshFusionTest.mm"
)

OBJECTS=()
for src in "${SOURCES[@]}"; do
  obj="$OUT/$(basename "${src%.*}").o"
  clang++ "${FLAGS[@]}" -c "$src" -o "$obj"
  OBJECTS+=("$obj")
done

clang++ -o "$OUT/worldMeshFusionTest" "${OBJECTS[@]}" -framework Foundation
"$OUT/worldMeshFusionTest"
