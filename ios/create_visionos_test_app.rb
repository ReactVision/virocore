#!/usr/bin/env ruby
# create_visionos_test_app.rb
#
# Generates ViroKitVisionOSTest/ViroKitVisionOSTest.xcodeproj —
# a minimal visionOS app that links ViroKit.xcframework and wires
# ViroImmersiveRenderer into a SwiftUI ImmersiveSpace.
#
# Usage:
#   cd virocore/ios
#   ruby create_visionos_test_app.rb
#
# Then open ViroKitVisionOSTest/ViroKitVisionOSTest.xcodeproj in Xcode,
# choose "Vision Pro Simulator" as destination, and run.

require 'xcodeproj'
require 'fileutils'

# ── Paths ─────────────────────────────────────────────────────────────────────

SCRIPT_DIR     = File.expand_path(__dir__)
REPO_ROOT      = File.expand_path('..', SCRIPT_DIR)
VIRO_ROOT      = File.expand_path('../viro', REPO_ROOT)

TEST_DIR       = File.join(SCRIPT_DIR, 'ViroKitVisionOSTest')
APP_DIR        = File.join(TEST_DIR, 'App')
PROJ_PATH      = File.join(TEST_DIR, 'ViroKitVisionOSTest.xcodeproj')

VISIONOS_SRC   = File.join(VIRO_ROOT, 'ios/ViroReact/VisionOS')
XCFRAMEWORK    = File.join(VIRO_ROOT, 'ios/dist/ViroRendererVisionOS/ViroKit.xcframework')
RENDERER_DIR   = File.join(REPO_ROOT, 'ViroRenderer')

abort "ViroKit.xcframework not found at #{XCFRAMEWORK}\nRun ./build_visionos.sh first." \
  unless File.exist?(XCFRAMEWORK)

# ── Create project ────────────────────────────────────────────────────────────

FileUtils.rm_rf(PROJ_PATH)
project = Xcodeproj::Project.new(PROJ_PATH)

target = project.new_target(:application, 'ViroKitVisionOSTest', :ios, '1.0')
target.product_name = 'ViroKitVisionOSTest'

# ── Groups ────────────────────────────────────────────────────────────────────

app_group      = project.main_group.new_group('App',      APP_DIR)
visionos_group = project.main_group.new_group('VisionOS', VISIONOS_SRC)
renderer_group = project.main_group.new_group('Renderer', RENDERER_DIR)
fw_group       = project.main_group.new_group('Frameworks')

# ── App sources ───────────────────────────────────────────────────────────────

APP_SOURCES = %w[ViroKitVisionOSTestApp.swift ContentView.swift]
APP_HEADERS = %w[BridgingHeader.h]
APP_RESOURCES = %w[Info.plist]

APP_SOURCES.each do |name|
  ref = app_group.new_reference(File.join(APP_DIR, name))
  target.add_file_references([ref])
end
APP_HEADERS.each   { |n| app_group.new_reference(File.join(APP_DIR, n)) }
APP_RESOURCES.each { |n| app_group.new_reference(File.join(APP_DIR, n)) }

# ── VisionOS sources (from viro repo) ─────────────────────────────────────────

VISIONOS_COMPILE = %w[
  VRORendererBridge.mm
  ViroImmersiveRenderer.swift
  ViroImmersiveSpace.swift
  ViroImmersiveCoordinator.swift
]
VISIONOS_HEADERS = %w[VRORendererBridge.h]

VISIONOS_COMPILE.each do |name|
  ref = visionos_group.new_reference(File.join(VISIONOS_SRC, name))
  target.add_file_references([ref])
end
VISIONOS_HEADERS.each { |n| visionos_group.new_reference(File.join(VISIONOS_SRC, n)) }

# ── Metal shaders (compiled into default.metallib in the app bundle) ──────────
# newDefaultLibrary() requires the app bundle to contain a compiled metallib.

shaders_ref = renderer_group.new_reference(File.join(RENDERER_DIR, 'Shaders.metal'))
target.add_file_references([shaders_ref])

# ── XCFramework ───────────────────────────────────────────────────────────────

xcfw_ref = fw_group.new_reference(XCFRAMEWORK)
xcfw_ref.last_known_file_type = 'wrapper.xcframework'

fw_build_file = target.frameworks_build_phase.add_file_reference(xcfw_ref)
# Embed the xcframework (needed for dynamic linkage; for static it's optional but harmless)
embed_phase = project.new(Xcodeproj::Project::Object::PBXCopyFilesBuildPhase)
embed_phase.name = 'Embed Frameworks'
embed_phase.symbol_dst_subfolder_spec = :frameworks
target.build_phases << embed_phase
embed_file = embed_phase.add_file_reference(xcfw_ref)
embed_file.settings = { 'ATTRIBUTES' => ['CodeSignOnCopy', 'RemoveHeadersOnCopy'] }

# ── Build settings ────────────────────────────────────────────────────────────

COMMON_SETTINGS = {
  'PRODUCT_BUNDLE_IDENTIFIER'    => 'com.reactvision.ViroKitVisionOSTest',
  'PRODUCT_NAME'                 => '$(TARGET_NAME)',
  # visionOS platform
  'SDKROOT'                      => 'xros',
  'SUPPORTED_PLATFORMS'          => 'xros xrsimulator',
  'TARGETED_DEVICE_FAMILY'       => '7',
  'XROS_DEPLOYMENT_TARGET'       => '26.0',
  # Swift
  'SWIFT_VERSION'                => '5.9',
  'SWIFT_OBJC_BRIDGING_HEADER'   => 'App/BridgingHeader.h',
  # C++
  'CLANG_CXX_LANGUAGE_STANDARD'  => 'gnu++17',
  'CLANG_CXX_LIBRARY'            => 'libc++',
  # Preprocessor — enable Metal + visionOS paths in all VRO headers
  'GCC_PREPROCESSOR_DEFINITIONS'  => ['$(inherited)', 'VRO_METAL=1', 'VRO_PLATFORM_VISION=1'],
  # Metal shaders have a separate preprocessor — GCC_PREPROCESSOR_DEFINITIONS does NOT apply.
  # Shaders.metal wraps everything in #if VRO_METAL, so this define is required.
  'MTL_PREPROCESSOR_DEFINITIONS'  => ['$(inherited)', 'VRO_METAL=1'],
  # Resources / linking
  'INFOPLIST_FILE'               => 'App/Info.plist',
  'ENABLE_BITCODE'               => 'NO',
  'ALWAYS_SEARCH_USER_PATHS'     => 'NO',
  # Remove iOS deployment target noise
  'IPHONEOS_DEPLOYMENT_TARGET'   => '',
  # Sign as "sign to run locally" for simulator
  'CODE_SIGN_STYLE'              => 'Automatic',
  'DEVELOPMENT_TEAM'             => '',
}.freeze

target.build_configurations.each do |config|
  config.build_settings.merge!(COMMON_SETTINGS)
  if config.name == 'Debug'
    config.build_settings['DEBUG_INFORMATION_FORMAT'] = 'dwarf'
    config.build_settings['SWIFT_OPTIMIZATION_LEVEL'] = '-Onone'
  else
    config.build_settings['DEBUG_INFORMATION_FORMAT'] = 'dwarf-with-dsym'
    config.build_settings['SWIFT_OPTIMIZATION_LEVEL'] = '-O'
  end
end

# ── Scheme ────────────────────────────────────────────────────────────────────

shared_data_dir = File.join(PROJ_PATH, 'xcshareddata', 'xcschemes')
FileUtils.mkdir_p(shared_data_dir)

scheme = Xcodeproj::XCScheme.new
scheme.add_build_target(target)
launch_action = scheme.launch_action
launch_action.buildable_product_runnable = Xcodeproj::XCScheme::BuildableProductRunnable.new(target, 0)
scheme.save_as(PROJ_PATH, 'ViroKitVisionOSTest', true)

# ── Save ──────────────────────────────────────────────────────────────────────

project.save

puts
puts "=== ViroKitVisionOSTest project created ==="
puts "  Location : #{PROJ_PATH}"
puts "  Sources  : #{APP_DIR}"
puts "  VisionOS : #{VISIONOS_SRC}"
puts "  xcfw     : #{XCFRAMEWORK}"
puts
puts "Next steps:"
puts "  1. open '#{PROJ_PATH}'"
puts "  2. Choose destination: Vision Pro Simulator"
puts "  3. Product → Run (⌘R)"
puts "  4. Tap 'Launch Immersive' — verify red box renders"
puts
