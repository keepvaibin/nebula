---
title: Adding models
shortTitle: Add models
intro: Replace one of the game's models, or add a model of your own, from a glTF file exported by Blender or any other 3D tool.
contentType: how-tos
---

## About models in a mod

A mod's models are glTF 2.0 files (`.glb`, or `.gltf` with its files beside it). When the game loads the mod, Nebula cooks each one into the game's own model format, so a mod's model is drawn exactly like the game's models, on every renderer, at no extra cost.

There are two ways to use a model:

* Replace a game model. Put the file at the game model's path. Everywhere the game draws that model, it draws yours.
* Add a new model. Put the file in the mod's `assets/` folder. Your code draws it, or your objects use it.

LumaLink cooks every model when you run `lumalink check`, so a model the game can't use is found on your computer, before you share the mod.

## Prerequisites

* LumaLink and an installed Nebula. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* A mod folder. For more information, see [Creating a mod](creating-a-mod.md).
* A 3D tool that exports glTF 2.0, such as Blender.

## Exporting from your 3D tool

Export with these settings:

| Setting | Value |
|---|---|
| Format | glTF Binary (`.glb`), or glTF Separate (`.gltf` with its files) |
| Up axis | +Y up. The model faces +Z. These are glTF's defaults. |
| Unit | 1 metre is 100 of the game's units. A 1 m cube is 100 units on each side. |
| Geometry | Triangles. Strips and fans are turned into triangles. Points and lines are refused. |
| Skin weights | At most 4 joints for each vertex. |
| Textures | PNG or JPEG. |

Don't change the axes or the winding of triangles: Nebula converts them to the game's own.

## Replacing a game model

1. Find the model's archive and name. To list the game's models whose path contains `Kuribo`:

   ```shell
   lumalink list models Kuribo
   ```

   Each line names a model and how many animations it has, for example `ObjectData/Kuribo.arc/Kuribo.bdl: animations: 12`.
1. Add your file at the same path, with `.glb` in place of the game's ending:

   ```shell
   lumalink add file Kuribo.glb my-mod --at ObjectData/Kuribo.arc/Kuribo.glb
   ```

   LumaLink copies it to `layers/base/files/ObjectData/Kuribo.arc/Kuribo.glb`.
1. Check the mod:

   ```shell
   lumalink check my-mod
   ```

Your model replaces only the model file. The archive's other files stay as the game has them: its animations, collision and other models.

If an archive has both a `NAME.bdl` and a `NAME.bmd`, name your file `NAME.bdl.glb` or `NAME.bmd.glb` to say which one it replaces.

### Keeping the game's joints

The game's animations and code move a model's joints by number and by name. When your model replaces a game model, Nebula matches your joints to the game model's joints by name, so each keeps the game's number.

* Every joint of the game model must be in your model, with the same name. A missing joint is an error that names it.
* Your model can have extra joints. They follow their parents and play none of the game's animations.
* If the game model has only one joint, as most objects and map parts do, your model's root is that joint whatever its name.

The error for a missing joint names the joint the game uses.

To replace Mario or Luigi, use `lumalink add character`, which matches joints for you. For more information, see [lumalink add](../reference/lumalink-commands/add.md).

### Hiding a game model

To hide a game model without replacing it, add a file named `NAME.model.json` beside where the model would be, holding only:

```json
{ "hide": true }
```

The game keeps the model's joints, so its animations and code still work, and draws nothing.

## Adding a new model

1. Add the file to the mod's assets:

   ```shell
   lumalink add asset Keeper.glb my-mod
   ```

   LumaLink copies it to `layers/base/assets/Keeper.glb`. To put it in a folder, add `--at characters/Keeper.glb`.
1. Name the model in your code by its path in `assets/`, without the ending. In LumaScript, `model "Keeper"` names it, and the build checks that the file exists. This example uses LumaKit, so add it to the mod's needs with `lumalink add need lumakit my-mod`:

   ```lumascript
   use kit.actor

   on start {
       actor.define(actor.Kind {
           name: "LumaKeeper"
           look: model "Keeper"
           idle_action: "Wait"
       })
   }
   ```

   The kind's `name` is the class name a stage places it by. For more information, see [Placing objects in stages](placing-objects-in-stages.md).

The game knows a mod's model as `MOD-ID:PATH`, so it never collides with the game's models or another mod's.

### Attaching a model to an object

To draw a model on an object that already exists, attach it to one of the object's joints. LumaKit does this for you. Add it to the mod's needs first:

```shell
lumalink add need lumakit my-mod
```

For example, to put a model in Mario's right hand in the Good Egg Galaxy:

```lumascript
use kit.actor

on stage "EggStarGalaxy" {
    if let mario = MarioAccess::getPlayerActor() {
        let held = actor.attach(mario, model "Lantern", "HandR")
    }
}
```

The model moves to the joint every frame. To place it away from the joint, pass an offset as the fourth argument. It stays until you call `held.remove()` or the object is gone. While the object is hidden, so is the model.

If the model isn't in the mod or the game, nothing is attached and the mod log says so.

## Levels of detail

The game draws a simpler model of some objects when the player is far away. The far model has `Low` at the end of its name, with that capital:

* To replace a game model's far model, replace `ObjectData/NAMELow.arc/NAMELow.glb` like any other model.
* To give a new model a far model, add `assets/PATHLow.glb` beside `assets/PATH.glb`. Nebula switches between them where the game switches its own: in the parts of a stage where the game draws its far models.

A far model whose base model isn't in the mod is an error.

## Sockets

A socket is a named point on a model where effects and other models are attached. To add one, add an empty node (one with no mesh) named `socket:NAME` under the joint it follows. Nebula makes it a joint named `NAME`.

Each socket needs a name of its own, different from every joint's name.

## Billboards

A part of a model that always turns to face the camera is a billboard. To make a node a billboard, set its custom properties (glTF `extras`):

```json
"extras": { "nebula": { "billboard": "y" } }
```

`"y"` turns it around its vertical axis only. `"xy"` turns it fully to face the camera.

## Materials

Each glTF material becomes a material of the game's model:

* The base color, from the factor and the texture.
* Metallic, roughness, normal, emissive and occlusion maps, lit by Nebula's lighting.
* `alphaMode`: `OPAQUE`, `MASK` with `alphaCutoff`, or `BLEND`.
* `doubleSided`.
* `KHR_texture_transform` on the base color texture, with equal scales on both axes. It can be animated with `KHR_animation_pointer`.
* `KHR_materials_unlit`, `KHR_materials_emissive_strength`, `KHR_materials_ior` and `KHR_materials_transmission`.

Nebula reads the extensions `KHR_texture_transform`, `KHR_animation_pointer`, `KHR_mesh_quantization` and the material extensions above. A file that requires any other extension is refused. Export it without.

To write a material's look in code, see [Writing material shaders](writing-material-shaders.md).

## Animations

Each animation in a model's file is cooked beside the model. An animation with the same name as one of the game's animations for that model replaces it, and the game's other animations stay. For more information, see [Adding animations](adding-animations.md).

## Collision

A model named `NAME.kcl.glb` is solid ground the player stands on, not something drawn. Put it beside the model of the same name, in the same archive or in `assets/`. Each of its materials is one kind of ground. Set the kind in the material's custom properties:

```json
"extras": { "Floor_code": 0, "Wall_code": 0, "Sound_code": 1, "Camera_id": -1, "Camera_through": 0 }
```

## Troubleshooting

`lumalink check` reports each problem with the file, and the mesh, joint, node or material by its name. The most common:

| Message | What to do |
|---|---|
| The game's model has the joint "NAME", which this model lacks | Add a joint with that name, or rename yours to match. |
| ARCHIVE has no game model of this name | The file is in `files/` but the game has no model there. Check the name with `lumalink list models`, or put a new model in `assets/`. |
| ARCHIVE has both NAME.bdl and NAME.bmd | Name the file `NAME.bdl.glb` or `NAME.bmd.glb`. |
| The mesh weighs vertices on more than four joints | Limit the weights to 4 for each vertex in your 3D tool, and export again. |
| The mesh has no normals | A note: Nebula gives each triangle its own normal. Export normals for smooth shading. |
| The file requires an extension Nebula does not read | Turn off that extension when you export. |
| A joint or material name has a letter the game's models cannot hold | Use English letters, digits and signs only. |

## Further reading

* [Adding animations](adding-animations.md)
* [Writing material shaders](writing-material-shaders.md)
* [Placing objects in stages](placing-objects-in-stages.md)
* [About mod files and layers](../concepts/mod-files-and-layers.md)
