---
title: Placing objects in stages
shortTitle: Place objects in stages
intro: Add the game's objects or your own to a stage, move or remove the game's, and set an object's arguments by name.
contentType: how-tos
---

## About stage placements

Every object in a stage, every coin, enemy, planet and fan, is a row in one of the stage's placement tables. A row names the object, its place, its turn, its scale and its arguments.

A mod changes these tables with patches. It names the rows it adds, changes or removes, and the rest of the stage stays as the game has it. Several mods can change the same stage. For more information on patches, see [Patching game tables](patching-game-tables.md).

## Prerequisites

* LumaLink and an installed Nebula. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* A mod folder. For more information, see [Creating a mod](creating-a-mod.md).

## Finding the stage's table

A stage is an archive in `StageData`, such as `StageData/EggStarGalaxy.arc` for the Good Egg Galaxy. Its objects are in `jmp/placement/LAYER/objinfo`:

* `common` holds what is always there.
* `layera`, `layerb` and on hold what appears only in some of the galaxy's missions.

Other tables beside `objinfo` hold other kinds of placements, such as `mapparts` (moving parts of the map), `planetobjinfo` (planets) and `areaobjinfo` (areas such as gravity and water).

1. List the stage's tables:

   ```shell
   lumalink list tables EggStarGalaxy
   ```

1. Extract the table you want:

   ```shell
   lumalink extract StageData/EggStarGalaxy.arc/jmp/placement/common/objinfo --output game-files
   ```

   LumaLink writes the table and a `.json` copy you can read. Each row shows an object's `name`, `l_id`, position and arguments.

## Adding an object

Add a patch at the table's path, with `.json` added:

```text
layers/base/patches/StageData/EggStarGalaxy.arc/jmp/placement/common/objinfo.json
```

```json
{
  "patch": 1,
  "key": ["l_id"],
  "add": [
    { "name": "Coin", "l_id": 9501,
      "pos_x": -3439.8, "pos_y": -12981.3, "pos_z": -15575.5,
      "dir_x": 0.0, "dir_y": 0.0, "dir_z": 0.0 }
  ]
}
```

Each row needs:

| Column | What it is |
|---|---|
| `name` | The object, by the name the game knows it, such as `Coin` or `Kuribo`. For a mod's own object, `MOD-ID/NAME`. |
| `l_id` | A number that names the row. Use one the table doesn't have yet. |
| `pos_x`, `pos_y`, `pos_z` | Its place, in game units. |
| `dir_x`, `dir_y`, `dir_z` | Its turn, in degrees. |

A column you leave out takes the value most of the game's rows have: 1 for `scale_x`, `scale_y` and `scale_z`, and -1, "not given", for each argument.

To find a good place, copy the position of an object near it from the extracted table and move it from there. 100 game units are 1 metre.

### Your own objects

A mod's code defines its own kinds of object, and a stage places them by `MOD-ID/NAME`. For example, a kind named `LumaKeeper` in the mod `luma-keeper` is placed as:

```json
{ "name": "luma-keeper/LumaKeeper", "l_id": 9412, "pos_x": -3439.8, "pos_y": -13081.3, "pos_z": -15575.5 }
```

For more information on defining objects, see [Adding models](adding-models.md#adding-a-new-model).

## Setting an object's arguments by name

Many objects take arguments: how far a fan blows, how fast a platform moves. In the game's tables they are numbers in `Obj_arg0` to `Obj_arg7`. A mod can give them by name instead, in `args`:

```json
{ "name": "BigFan", "l_id": 9502,
  "pos_x": -3000.0, "pos_y": -13081.3, "pos_z": -15575.5,
  "args": { "WindLength": 3000, "WindSpeed": 30 } }
```

The names come from the game's own code: each argument is named after what the object stores it in. To learn an object's names, give a name it doesn't have and run `lumalink check`. The error lists every name the object takes:

```text
BigFan has no argument "Nonsense"; its arguments are WindLength, WindSpeed, Arg0-Arg7.
```

`Arg0` to `Arg7` name any object's arguments by number.

* An argument is a whole number. `true` is 1. `false` and `null` are -1, "not given".
* An argument you don't give is -1.
* To change one argument of a game object, use `set` with `args`. Only the arguments you name change:

  ```json
  "set": [ { "where": { "l_id": 40 }, "values": { "args": { "WindSpeed": 45 } } } ]
  ```

## Moving or removing the game's objects

Use `set` to change cells of a row, by its `l_id`:

```json
"set": [ { "where": { "l_id": 12 }, "values": { "pos_y": -12900.0, "scale_x": 2.0, "scale_y": 2.0, "scale_z": 2.0 } } ]
```

Use `remove` to take objects out:

```json
"remove": [ { "l_id": 12 }, { "l_id": 13 } ]
```

Use `select` to change every row that matches, such as every Goomba:

```json
"select": [ { "match": { "name": { "equals": "Kuribo" } }, "values": { "scale_x": 1.5, "scale_y": 1.5, "scale_z": 1.5 } } ]
```

## Checking the patch

```shell
lumalink check my-mod
```

LumaLink applies your patch to the game's table, as the game does when it starts, and reports:

* An object the game can't make.
* An argument name the object doesn't have, or one given twice.
* A row that `set` or `remove` names but the table doesn't have.
* An `add` whose `l_id` already exists.

When a patch fails in the game, that patch is left out whole, the stage plays without it, and the **Mods** page says why. Other mods' patches still apply.

## Making a new galaxy

A new galaxy is a new stage archive, with whole tables of its own under `files/` rather than patches, and LumaKit's `stage` module to add it to the game's dome. For a complete example, see the guide "A new galaxy" in the guides that come with LumaLink.

## Further reading

* [Patching game tables](patching-game-tables.md)
* [Table keys](../reference/table-keys.md)
* [Adding models](adding-models.md)
