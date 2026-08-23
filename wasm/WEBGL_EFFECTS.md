# WebGL2 effects & capability degradation

How the WASM renderer's post-processing effects (HDR, bloom, PBR/IBL, shadows)
map onto WebGL2, and how they degrade when the GPU/browser lacks the required
extension.

## The key extension: `EXT_color_buffer_float`

HDR, bloom, and PBR image-based lighting render into **floating-point color
targets** (`GL_RGBA16F` / `GL_RG16F`, see `VRORenderTargetOpenGL.cpp`). In
WebGL2, rendering *to* a float color attachment is not core — it requires
`EXT_color_buffer_float`, and WebGL2 does **not** enable it implicitly.

`VROSceneWeb` enables it right after context creation:

```cpp
bool colorBufferFloat = emscripten_webgl_enable_extension(_context, "EXT_color_buffer_float");
_driver->setColorBufferFloatSupported(colorBufferFloat);
```

## How support flows into the pipeline

`VRODriverOpenGLWasm` reports capability through two existing seams that
`VROChoreographer` already consults when deciding what to enable:

| Driver method | float supported | float NOT supported |
|---|---|---|
| `getColorRenderingMode()` | `LinearSoftware` | `NonLinear` |
| `isBloomSupported()` | `true` | `false` |

`VROChoreographer` derives, at construction:

```
_hdrSupported   = _mrtSupported && colorMode != NonLinear
_pbrSupported   = _hdrSupported
_bloomSupported = _mrtSupported && _hdrSupported && driver->isBloomSupported()
_shadowsEnabled = _mrtSupported && config.enableShadows
```

So when `EXT_color_buffer_float` is **absent**, `getColorRenderingMode()` returns
`NonLinear`, which cascades to `_hdrSupported = _pbrSupported = _bloomSupported =
false`. The renderer falls back to a plain LDR pipeline and skips the float
targets entirely — no GL errors, just no HDR/bloom/PBR. This is the intended
graceful degradation.

> Note: `NonLinear` also disables the software gamma pass. Colors will look
> slightly different (no linear-space correction) in the degraded path. This is
> the accepted tradeoff for the no-float fallback; a future improvement could
> keep software gamma while disabling only the float-dependent effects.

## Paridad MRT: outputs de bloom (WebGL2 estricto)

Con bloom activo, el target HDR tiene **3 draw buffers**: `location 0` (color),
`location 1` (máscara de tone-mapping), `location 2` (bloom / `_bright_color`).

- Los outputs 0 y 1 los escribe todo shader (el modifier de tone-mapping se
  agrega siempre que HDR está activo).
- El output 2 (bloom) originalmente solo se agregaba a materiales con
  `bloomThreshold >= 0`. OpenGL ES nativo tolera que un shader no escriba todos
  los draw buffers activos; **WebGL2 no** → `INVALID_OPERATION: glDrawElements:
  Active draw buffers with missing fragment shader outputs`.

**Fix (core, general):** se agregó un flag de bloom a nivel de pipeline
(`VROLightingShaderCapabilities::bloom`, derivado de `VRORenderContext::isBloomEnabled()`,
sincronizado desde `VROChoreographer::isBloomEnabled()`). Ahora el bloom modifier
se agrega a **todos** los shaders cuando el pipeline tiene bloom, escribiendo
`_bright_color = vec4(0)` para los materiales sin threshold. Así todos los draw
buffers activos se escriben siempre. En native es aditivo (un output extra en
cero), sin cambio de comportamiento visible.

## Shadows

Shadow maps use MRT/depth targets, gated on `_mrtSupported` (GPU is not
Adreno-330-or-older), independent of `EXT_color_buffer_float`. They are enabled
in the demo scene and validated visually via a floor plane under the cube.

## Status (Phase 1, task 4)

- Demo scene (`VROSceneWeb`) now requests **shadows + HDR + bloom + PBR**.
- On desktop Safari/Chrome, `EXT_color_buffer_float` is available → the full
  HDR pipeline runs; the cube casts a shadow on the floor.
- Degradation path is wired but should be validated on a device/browser that
  lacks the extension (rare on modern mobile — most support it).
