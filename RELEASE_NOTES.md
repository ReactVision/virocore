# Release Notes

## v3.1.1

**glTF models render as glTF defines them on every platform: metals look metallic without an environment map, visionOS draws a model's normal, metallic-roughness, occlusion and emissive maps, an occlusion map can sit on its own UV set, and mirrored nodes draw their faces.** The native build ships inside `@reactvision/react-viro` 3.1.1.

### Everywhere

- **Metals under flat ambient light:** without an environment map the ambient term now leaves out the share a metal reflects, so a metal visor is dark and reflective instead of light grey.
- **glTF factors:** `normalTexture.scale` and `occlusionTexture.strength` apply.
- **Occlusion on `TEXCOORD_1`** (a lightmap) samples its own UV set (GL; left out on visionOS).
- **Mirrored nodes** (a negative scale) draw their faces instead of losing them to back-face culling.

### visionOS

- **PBR maps:** normal, metallic-roughness, occlusion and emissive maps, which the Metal path skipped; a textured glTF model no longer draws flat.

### Check when upgrading

- **A metallic model without a `lightingEnvironment` looks darker than in 3.1.0.** This is how glTF and three.js light it; give metallic models an environment map.

See `CHANGELOG.md` for every change.
