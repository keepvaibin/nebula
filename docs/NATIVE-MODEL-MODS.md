# Modern files in mods

A mod's files go in its `files/` folder (or a .luma layer's), at the game's paths. When the mod is enabled, Nebula cooks them with the same cooker as the game's own files (`nebula-import overlay`, into `mods.pak`). The game reads only cooked files, so a mod's files and the game's are the same to it. Besides the game's own formats, a mod can bring these modern files. A file that cannot be cooked is left out and named in the log; the rest of the mod still loads.

| File | Becomes | Where |
|---|---|---|
| `name.gltf`, `name.glb` | the model `name.bmd`, and each glTF animation as `<animation name>.bck` beside it | `ObjectData/Name.arc/` |
| `name.bdl.gltf`, `name.bdl.glb`, `name.bmd.gltf`, `name.bmd.glb` | the model with that ending | the same |
| `name.kcl.gltf`, `name.kcl.glb` | collision `name.kcl` and its attributes `name.pa` | in an archive folder, beside it; elsewhere (a mod's `assets/`), in `Name.arc`, the archive the model of the same name goes to, where an actor of that model reads its collision |
| `name.png` | a texture `name.bti`; under a `timg/` folder, a layout picture `name.tpl` | anywhere |
| `name.wav`, `name.ogg` | a music stream `name.ast` (its loop points kept) | `AudioRes/Stream/` |
| `Table.bcsv.json` | the table `Table.bcsv`, whole | anywhere a table is |
| `merge/.../Table.bcsv.json` | rows added to the game's table (or an earlier mod's) | a .luma layer's `merge/`, at the table's path |
| `merge/ParticleData/Effect.arc/...jpc` with `ParticleNames.bcsv.json` | new particles added to the game's, numbered after its names | a .luma layer's `merge/` |

A mod's file can also be one member of an archive (`StageData/MyGalaxy/MyGalaxyMap.arc/jmp/placement/common/objinfo`). The members it does not bring come from the game's archive. Files only a console reads (the channel banner, the HOME menu, the module list) are refused.

## Models

glTF 2.0: triangles, skins and node animation. Lengths are metres: one metre is 100 of the game's units. Joint and material names should match any game code or animations that address them.

Model counts, indices, texture sizes and animation frame ends use 32 bits. Texture-pattern samples stay integers through cook and playback. The cook picks 16-bit or 32-bit vertex indices per mesh. Rotations keep float radians, including animation values and tangents.

A material has sixteen texture and coordinate slots. Its `pbrMetallicRoughness.baseColorTexture` keeps its `texCoord`. More multiplying layers go in the material's extras:

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

Each `index` names a glTF texture; each `texCoord` picks `TEXCOORD_0` to `TEXCOORD_15`. Layers multiply in order, after the base colour and its texture, up to sixteen with the base texture. A coordinate out of range or a seventeenth layer stops the cook of that file with an error.

## Collision

A `.kcl.glb` is solid ground, not something to see. Each material is one row of the attributes table; its `extras` are the row's columns: `Floor_code`, `Wall_code`, `Sound_code`, `Camera_id`, `Camera_through`.

## Versions

Cooked files carry their format's version (model, clip, texture record, store: `runtime/include/galaxy/store_version.inc`). A Nebula with another store version imports the game again and cooks the mods again.
