#!/usr/bin/env ruby
# add_visionos_target.rb
#
# Adds ViroKitVisionOS static-library target to ViroRenderer.xcodeproj.
# Run from any directory:  ruby ios/add_visionos_target.rb
#
# Prerequisites: gem install xcodeproj  (already present in this workspace)

require 'xcodeproj'
require 'pathname'

PROJ_PATH   = File.expand_path('../ios/ViroRenderer.xcodeproj', __dir__)
RENDERER_DIR = File.expand_path('../ViroRenderer', __dir__)
IOS_DIR     = File.expand_path('../ios/ViroKit', __dir__)

proj = Xcodeproj::Project.open(PROJ_PATH)

# ── Remove existing target so the script is idempotent ───────────────────────
existing = proj.targets.find { |t| t.name == 'ViroKitVisionOS' }
if existing
  puts "Removing existing ViroKitVisionOS target…"
  existing.remove_from_project
  # Also remove the group
  old_group = proj.main_group.find_subpath('ViroKitVisionOS')
  old_group&.remove_from_project
end

# ── Create static-library target ─────────────────────────────────────────────
target = proj.new_target(:static_library, 'ViroKitVisionOS', :visionos, '1.0')
target.build_configuration_list.set_setting('SDKROOT', 'xros')
target.build_configuration_list.set_setting('SUPPORTED_PLATFORMS', 'xros xrsimulator')
target.build_configuration_list.set_setting('TARGETED_DEVICE_FAMILY', '7')
target.build_configuration_list.set_setting('XROS_DEPLOYMENT_TARGET', '1.0')
target.build_configuration_list.set_setting('IPHONEOS_DEPLOYMENT_TARGET', '')

# Force Metal (VRO_METAL=1) and suppress OpenGL path (no VRO_OPENGL)
target.build_configuration_list.set_setting('GCC_PREPROCESSOR_DEFINITIONS', [
  '$(inherited)',
  'VRO_METAL=1',
])

# Header search paths: ViroRenderer + ios/ViroKit (VisionOS subdir)
header_paths = [
  '$(inherited)',
  '"$(SRCROOT)/../ViroRenderer"',
  '"$(SRCROOT)/ViroKit"',
  '"$(SRCROOT)/ViroKit/VisionOS"',
  # Note: glm/ intentionally NOT listed as a root — GLM headers are accessed
  # as "glm/..." so $(SRCROOT)/../ViroRenderer is sufficient.
  # Adding glm/ as a root would make glm/simd/ visible as simd/, shadowing
  # Apple's <simd/simd.h> includes (ModelIO → simd/geometric.h etc.).
  '"$(SRCROOT)/../ViroRenderer/giflib"',
  '"$(SRCROOT)/../ViroRenderer/poly2tri"',
  '"$(SRCROOT)/../ViroRenderer/ucdn"',
]
target.build_configuration_list.set_setting('HEADER_SEARCH_PATHS', header_paths)

# C++ standard
target.build_configuration_list.set_setting('CLANG_CXX_LANGUAGE_STANDARD', 'c++14')
target.build_configuration_list.set_setting('CLANG_CXX_LIBRARY', 'libc++')
target.build_configuration_list.set_setting('GCC_C_LANGUAGE_STANDARD', 'gnu11')

# Compiler flags
target.build_configuration_list.set_setting('OTHER_CPLUSPLUSFLAGS', [
  '$(inherited)',
  '-fno-objc-arc',
])
target.build_configuration_list.set_setting('CLANG_ENABLE_OBJC_ARC', 'NO')

# Product
target.build_configuration_list.set_setting('PRODUCT_NAME', '$(TARGET_NAME)')
target.build_configuration_list.set_setting('SKIP_INSTALL', 'YES')
target.build_configuration_list.set_setting('ENABLE_BITCODE', 'NO')
target.build_configuration_list.set_setting('ARCHS', '$(ARCHS_STANDARD)')

# ── Source file selection ─────────────────────────────────────────────────────
#
# Metal-only, no static-library deps:
#   Include: scene graph, math, animation, GLTF, Metal substrates, utility
#   Exclude: OpenGL, AR, text/glyph/typeface, physics (bullet), FBX (protobuf),
#            body tracking, GVR/Cardboard, test harness, capi, Nodes.pb

EXCLUDE_PATTERNS = [
  # OpenGL / GLES renderer — not applicable on xros
  /OpenGL/i,
  /VRODriverOpenGL/i,

  # Text subsystem — freetype + harfbuzz not yet compiled for xros (M5)
  /Glyph/i,
  /Typeface/i,
  /VROText[^u]/i,          # VROText.cpp, VROTextTest, VROTextFormatter, VROTextureRef is OK
  /VROFont/i,
  /Charmap/i,
  /VROContour/i,
  /VROUCDN/i,
  /Vectorizer/i,           # VROVectorizer — glyph/contour tessellation
  /KnuthPlass/i,           # VROKnuthPlassFormatter — text layout
  /VRODebugHUD/i,          # uses text rendering
  /VROReticle/i,           # uses text rendering for debug overlay

  # FBX loader + protobuf helpers — needs protobuf (M5)
  /FBX/i,
  /Nodes\.pb/,
  /gzip_stream/i,            # protobuf GzipInputStream/GzipOutputStream helper
  /cnpy/i,                   # numpy/compression utility, protobuf-adjacent

  # Body tracking — needs CoreML / Vision framework (separate milestone)
  /Body(Mesh|Track|Surface|Player|Anim|IK)/i,
  /AnimBody/i,
  /SkeletonRenderer/i,
  /VROSkeletonRenderer/i,
  /VROIKRig/i,
  /VROIKTest/i,
  /VROPose/i,              # pose filters for body tracking
  /VROOneEuro/i,           # also used only by body tracking

  # AR subsystem — ARKit iOS (M3+)
  /AR(Session|Frame|Camera|Hit|Image|Object|Declarative|Imperative|Scene|Anchor|Constrain|Depth|WorldMesh|Shadow|Plane|Draggable)/i,
  /VROInputControllerAR/i,
  /VROARImperative/i,
  /VROARNode/i,

  # GVR / Cardboard / stereoscopic distortion — not used on xros
  /GVR/i,
  /Cardboard/i,
  /Distortion/i,
  /Stereoscopic/i,

  # Physics — bullet not yet compiled for xros (M5)
  /VROPhysics/i,

  # Head tracking (Cardboard inertial)
  /VROHeadTracker/i,

  # Input controllers — Cardboard only; visionOS input comes via RealityKit/hands
  /VROInputControllerCardboard/i,
  /VROInputPresenterCardboard/i,

  # Test harness / test files — omit from production target
  /TestHarness/i,
  /RendererTest/i,
  /Test\.cpp$/,            # VROBoxTest.cpp, VROGLTFTest.cpp, etc.
  /VROTestUtil/i,          # test utility that pulls in VRODriverOpenGL + FBX

  # Post-processing passes not needed for M0 frame (shadow, bloom, tone map)
  /VROBRDFRenderPass/i,
  /VROShadowMapRenderPass/i,
  /VROShadowPreprocess/i,
  /VROToneMappingRenderPass/i,
  /VROGaussianBlurRenderPass/i,
  /VROIrradianceRenderPass/i,
  /VROPrefilterRenderPass/i,
  /VROEquirectangularToCubeRenderPass/i,
  /VROIBLPreprocess/i,

  # Portal / stencil rendering — not applicable to full-immersion (M3+)
  /VROPortal/i,

  # Object recognition — CoreML dependency
  /VROObjectRecogniz/i,

  # C API
  /capi\//,

  # OpenGL-only utility / UBO files — Metal uses VROConcurrentBuffer, not GL UBOs
  /VROShaderProgram\.cpp/,
  /VRORenderUtil\.cpp/,
  /VROShaderFactory\.cpp/,
  /VROImageShaderProgram\.cpp/,
  /VROBoneUBO\.cpp/,
  /VROLightingUBO\.cpp/,
  /VROParticleUBO\.cpp/,            # OpenGL UBO — Metal uses VROParticleUBOMetal instead
  /VROInstancedUBO\.cpp/,
]

def excluded?(path)
  EXCLUDE_PATTERNS.any? { |pat| path.to_s.match?(pat) }
end

# Metal shader
metal_files = Dir.glob("#{RENDERER_DIR}/*.metal")

# VisionOS-specific sources (driver + linker stubs)
visionos_mm = [
  "#{IOS_DIR}/VisionOS/VRODriverVisionOS.mm",
  "#{IOS_DIR}/VisionOS/VROVisionOSRenderStubs.cpp",
]

# Files that must be compiled as ObjC++ because they include Metal/ObjC headers.
# These have .cpp extension but transitively pull in Metal.h → Foundation.h.
OBJCPP_NAMES = %w[
  VROTextureSubstrateMetal.cpp
  VROGeometrySubstrateMetal.cpp
  VROMaterialSubstrateMetal.cpp
  VROVideoTextureCacheMetal.cpp
  VROConcurrentBuffer.cpp
  VROPlatformUtil.cpp
  VROBoneUBOMetal.cpp
  VROParticleUBOMetal.cpp
  VROParticleEmitter.cpp
  VROFixedParticleEmitter.cpp
  VROGLTFLoader.cpp
].freeze

# Collect .cpp files from ViroRenderer/, splitting into plain C++ vs forced ObjC++
all_renderer_cpps = Dir.glob("#{RENDERER_DIR}/*.cpp").select { |f| !excluded?(f) }
renderer_cpps  = all_renderer_cpps.reject { |f| OBJCPP_NAMES.include?(File.basename(f)) }
renderer_objcpp = all_renderer_cpps.select { |f| OBJCPP_NAMES.include?(File.basename(f)) }

# ios/ViroKit files available on visionOS (UIKit, CoreVideo, Accelerate)
ios_visionos_cpps = [
  "#{IOS_DIR}/VROVideoTextureCacheMetal.cpp",
  "#{IOS_DIR}/VROImageiOS.cpp",
].select { |f| File.exist?(f) }

all_sources    = renderer_cpps + visionos_mm
objcpp_sources = renderer_objcpp + ios_visionos_cpps

# ── Add a group for the new target ───────────────────────────────────────────
visionos_group = proj.main_group.find_subpath('ViroKitVisionOS', true)
visionos_group.set_source_tree('<group>')

def add_file(proj, group, abs_path, target, compiler_flags: nil)
  file_ref = group.new_file(abs_path)
  file_ref.set_source_tree('<absolute>')
  build_files = target.add_file_references([file_ref])
  if compiler_flags && build_files.first
    build_files.first.settings = { 'COMPILER_FLAGS' => compiler_flags }
  end
  file_ref
end

# Add regular .cpp / .mm source files
all_sources.each do |f|
  next unless File.exist?(f)
  add_file(proj, visionos_group, f, target)
  puts "  + #{File.basename(f)}"
end

# Add files that need ObjC++ compilation despite .cpp extension
objcpp_sources.each do |f|
  next unless File.exist?(f)
  add_file(proj, visionos_group, f, target, compiler_flags: '-x objective-c++')
  puts "  + #{File.basename(f)} (forced ObjC++)"
end

# Add .metal shader (compiled into default.metallib)
metal_files.each do |f|
  add_file(proj, visionos_group, f, target)
  puts "  + #{File.basename(f)} (Metal shader)"
end

# ── Save ─────────────────────────────────────────────────────────────────────
proj.save
puts "\nSaved. ViroKitVisionOS target added to #{PROJ_PATH}"
puts "Added #{all_sources.count + objcpp_sources.count + metal_files.count} source files."
puts "\nNext: open ViroRenderer.xcworkspace and build the ViroKitVisionOS scheme"
puts "      targeting 'Any visionOS Simulator Device'."
