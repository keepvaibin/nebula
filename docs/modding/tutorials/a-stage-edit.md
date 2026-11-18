---
title: Changing a stage
shortTitle: Change a stage
intro: Add, change and remove objects in one of the game's stages with table patches alone, no code.
contentType: tutorials
example: a-stage-edit
---

## Introduction

In this tutorial, you change the first planet of the Good Egg Galaxy. A line of five coins lies on the way Mario first runs, the planet's three Octoombas are one and a half times bigger, a star piece moves, and the timer block under the start is gone. The mod has no code: everything is a patch of the stage's placement tables.

You learn how to:

* Find a stage's tables, its zones, and the rows of the objects you want to change.
* Add objects to a galaxy's own table.
* Change every row that matches (`select`), one row by its `l_id` (`set`), and take rows out (`remove`) in a zone's table.
* Work out where a zone's object stands in the world.
* Test the changes in the game.

## Prerequisites

* Nebula, installed with Nebula Setup.
* LumaLink. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).

## Mod folder layout

When you finish, the mod folder contains these files:

```text
a-stage-edit/
  luma.json
  layers/base/patches/StageData/EggStarGalaxy.arc/jmp/placement/common/objinfo.json     the coins
  layers/base/patches/StageData/DiskGardenZone.arc/jmp/placement/common/objinfo.json    the first planet's changes
  tests/remix/test.json
```

## Step 1: Find the tables

A galaxy is a stage archive, and most of its planets are zones: stages of their own, placed in the galaxy. Each has its own placement tables.

1. List the Good Egg Galaxy's zones. They are the rows of its `stageobjinfo` table:

   ```shell
   lumalink extract StageData/EggStarGalaxy.arc/jmp/placement/common/stageobjinfo --output game-files
   ```

   The `.json` copy names each zone and where it stands. The first planet is `DiskGardenZone`, at -3850, -13550, -17600.

1. Extract the zone's objects:

   ```shell
   lumalink extract StageData/DiskGardenZone/jmp/placement/common/objinfo --output game-files
   ```

   Each row has an object's `name`, its `l_id` and its place. The Octoombas are the rows named `Takobo`, the timer block is `TimerPieceBlock` (`l_id` 81), and the star pieces are `StarPiece`.

A zone's places are the zone's own: they are measured from the zone's place, and turned with it. The first planet is turned 45 degrees about its up, so a row at `x`, `z` in the zone stands in the world at the zone's place plus `x`, `z` turned by -45 degrees. A galaxy's own table uses the world's places.

## Step 2: Write the manifest

```json
{
  "format": "1.0",
  "id": "good-egg-remix",
  "version": "1.0.0",
  "name": "Good Egg Remix",
  "summary": "Changes the Good Egg Galaxy's first planet: a line of coins on the way, bigger Octoombas, no timer block.",
  "authors": ["you"],
  "game": { "id": "RMG", "regions": ["RMGE01", "RMGP01", "RMGJ01"] },
  "layers": [{ "name": "base" }]
}
```

The finished mod also has the name and summary in every language the game supports, in `text`.

## Step 3: Add the coins

Mario starts star 1 at -3305, -13082, -15354 and, with the stick forward, runs toward -3736, -13007, -15797. The coins stand on that line, 60 units above the ground, from about 230 units out: a coin nearer the start is taken as Mario lands there, before you can see it. They go in the galaxy's own table, with world places:

```json
{
  "patch": 1,
  "key": ["l_id"],
  "add": [
    { "name": "Coin", "l_id": 9701, "pos_x": -3464.3, "pos_y": -12996.4, "pos_z": -15517.9, "dir_x": 0.0, "dir_y": 0.0, "dir_z": 0.0 },
    { "name": "Coin", "l_id": 9702, "pos_x": -3522.5, "pos_y": -12985.8, "pos_z": -15577.7, "dir_x": 0.0, "dir_y": 0.0, "dir_z": 0.0 }
  ]
}
```

The finished mod has five coins. Each `l_id` is one the table doesn't have yet. For more information on rows and their columns, see [Placing objects in stages](../how-tos/placing-objects-in-stages.md).

## Step 4: Change the first planet

The zone's patch changes the game's rows instead of adding new ones:

```json
{
  "patch": 1,
  "key": ["l_id"],
  "select": [{ "match": { "name": { "equals": "Takobo" } }, "values": { "scale_x": 1.5, "scale_y": 1.5, "scale_z": 1.5 } }],
  "set": [{ "where": { "l_id": 60 }, "values": { "pos_z": -319.0 } }],
  "remove": [{ "l_id": 81 }]
}
```

* `select` changes every row that matches: here each Octoomba of the zone. Octoombas on other planets are in other tables, and stay as they are.
* `set` changes the cells you name of one row: the star piece with `l_id` 60 moves 400 units along the zone's `z`.
* `remove` takes the timer block out. The game never makes it.

## Step 5: Check the mod

```shell
lumalink check a-stage-edit
```

LumaLink applies both patches to the game's tables, as the game does, and reports a row that `set` or `remove` names but the table doesn't have, or an `add` whose `l_id` is already there.

## Step 6: Test it in the game

The test starts star 1 and checks each change before Mario moves, then runs him forward along the coins:

```json
"steps": [
  { "at": 2, "count": "class:Coin", "near": [-3541.9, -13042.2, -15597.6], "within": 600, "fact": "coins_on_the_walk" },
  { "at": 2, "count": "object:タイマーピースブロック", "near": [-3827.0, -14353.0, -15625.0], "within": 300, "fact": "timer_blocks" },
  { "at": 2, "read": "LiveActor.mScale", "of": "object:タコボー#3", "type": "Vec3f", "fact": "octoomba_3" },
  { "at": 10, "stick": [0, 1], "frames": 22 },
  { "at": 40, "stick": [0, 1], "frames": 60 },
  { "at": 105, "call": "MR::getCoinNum", "signature": "s32()", "args": [], "fact": "coins_taken" },
  { "at": 108, "end": true }
]
```

A `count` step counts the live objects of a class, or of the name the game gives them (the timer block's is `タイマーピースブロック`), near a place. The expected facts are five coins on the walk, no timer block, the scale 1.5 and five coins taken. For more information, see [Test files](../reference/test-files.md).

```shell
lumalink test a-stage-edit
```

## Further reading

* [Placing objects in stages](../how-tos/placing-objects-in-stages.md)
* [Patching game tables](../how-tos/patching-game-tables.md)
* [Adding a new galaxy](a-new-galaxy.md)
