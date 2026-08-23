#!/usr/bin/env ruby
# generate_shader_source.rb
#
# Generates the ViroShadersSource.txt resource from ViroRenderer/Shaders.metal.
#
# VRODriverMetal compiles shader modifiers at runtime with
# [MTLDevice newLibraryWithSource:], which has no include paths: an
# #include "VROSharedStructures.h" in the source is a fatal error at runtime even
# though the same line compiles fine ahead of time. The bundled resource is therefore
# not a verbatim copy of Shaders.metal but a preprocessed variant with the shared
# structures inlined and the build-time-only guards removed.
#
# That variant used to be maintained by hand and drifted from Shaders.metal — silently,
# because a stale copy still compiles. This script removes the possibility.
#
# Usage: ruby ios/generate_shader_source.rb <output-path> [<output-path> ...]

require 'pathname'

RENDERER_DIR = File.expand_path('../ViroRenderer', __dir__)
SHADERS      = File.join(RENDERER_DIR, 'Shaders.metal')
STRUCTURES   = File.join(RENDERER_DIR, 'VROSharedStructures.h')

abort "generate_shader_source: #{SHADERS} not found" unless File.exist?(SHADERS)
abort "generate_shader_source: #{STRUCTURES} not found" unless File.exist?(STRUCTURES)

# ── Extract the Metal section of VROSharedStructures.h ───────────────────────────
# The file is: header comment, #ifndef guard, #if VRO_METAL, <the structures>, #endif,
# #endif. Keep only the structures, and drop its own includes — Shaders.metal already
# pulls in simd.
structures = File.read(STRUCTURES)
metal_start = structures.index('#if VRO_METAL')
abort 'generate_shader_source: no "#if VRO_METAL" in VROSharedStructures.h' if metal_start.nil?

body = structures[metal_start..]
body = body.sub(/\A#if VRO_METAL\s*\n/, '')
body = body.gsub(/^\s*#include\s+[<"][^>"]+[>"]\s*\n/, '')
body = body.strip
# Drop the trailing #endif lines (the VRO_METAL guard and the header guard). Matching
# them one at a time survives whatever trailing whitespace the header happens to have.
body = body.sub(/^#endif[^\n]*\z/, '').strip while body =~ /^#endif[^\n]*\z/

# ── Inline it into Shaders.metal ────────────────────────────────────────────────
source = File.read(SHADERS)

replaced = source.sub(/^\s*#include\s+"VROSharedStructures\.h"\s*\n/, body + "\n")
abort 'generate_shader_source: Shaders.metal no longer includes VROSharedStructures.h' if replaced == source
source = replaced

# VRODefines.h is a build-time header and the VRO_METAL guard is always satisfied for a
# runtime Metal compile, so both go.
source = source.gsub(/^\s*#include\s+"VRODefines\.h"\s*\n/, '')
source = source.sub(/^#if VRO_METAL\s*\n/, '')
source = source.sub(/#endif\s*\n?\z/, '')

if source.include?('#include "')
  leftover = source.scan(/#include\s+"[^"]+"/).uniq.join(', ')
  abort "generate_shader_source: unresolved local include(s) remain: #{leftover}"
end

# The runtime compile has no build settings, so any surviving conditional would be
# evaluated with everything undefined. Shaders.metal and the structures header only use
# the VRO_METAL guard, both of which are stripped above — anything left means someone
# added a conditional that this script has to learn about.
conditionals = source.scan(/^\s*#(?:if|ifdef|ifndef|else|elif|endif)[^\n]*/)
unless conditionals.empty?
  abort "generate_shader_source: unhandled preprocessor conditional(s):\n  " +
        conditionals.uniq.map(&:strip).join("\n  ")
end

ARGV.each do |dest|
  dir = File.dirname(dest)
  unless Dir.exist?(dir)
    warn "generate_shader_source: skipping #{dest} (no such directory)"
    next
  end
  File.write(dest, source)
  puts "  generated #{dest} (#{source.bytesize} bytes)"
end

puts 'generate_shader_source: no output paths given' if ARGV.empty?
