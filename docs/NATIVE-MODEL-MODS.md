# Native model mods

Place a glTF 2 model inside the mod's `files/ObjectData/Name.arc/` folder. `name.gltf` or `name.glb` replaces `name.bmd`. To replace a model with an explicit existing ending, use `name.bdl.gltf`, `name.bdl.glb`, `name.bmd.gltf` or `name.bmd.glb`. Joint and material names should match any game code or animations that address them.

Native model counts, indices, texture dimensions and animation frame endpoints use 32 bits. Texture-pattern samples remain integers through cook and playback, preserving every index bit. The cook selects 16-bit or 32-bit vertex indices per mesh. Rotations retain float radians, including animation values and tangents.

Materials have sixteen texture and coordinate slots. A material's standard `pbrMetallicRoughness.baseColorTexture` retains its `texCoord`. Additional multiplicative layers can be supplied in the material's extras:

```json
{
  "extras": {
    "nebula": {
      "textures": [
        { "index": 0, "texCoord": 0 },
        { "index": 1, "texCoord": 1 },
        { "index": 2, "texCoord": 11 }
      ]
    }
  }
}
```

Each `index` names a glTF texture; each `texCoord` selects `TEXCOORD_0` through `TEXCOORD_15`. Layers multiply in order, after the base colour and its texture. A material may use up to sixteen layers including the base texture. Unsupported coordinates or excess layers produce an import error.

Model v3, clip v2, texture record v3 and store v4 are the native formats. Reimport older cooked stores and recook mod overlays before using this build.
