---
title: Table keys
shortTitle: Table keys
intro: How a patch names the row it changes in each of the game's tables, and the rules for adding, removing and linking rows.
contentType: reference
---

## About table keys

A patch changes rows of a game table by naming them with the table's key. The validator checks the key, and patches from several mods combine by it. For the patch file itself, see [The .luma file format](luma-file-format.md#patches).

The keys and columns on this page hold for every table of the US, PAL and Japanese games (RMGE01, RMGP01 and RMGJ01): the same rules work for all three.

## Keys

A game path (in lowercase) takes the key of the first pattern it matches. In a pattern, `*` matches within one folder name and `**` matches any number of folders. The key is one or more column names, or `#`, which names rows by their place. Every table has a rule: the last pattern, `**`, uses `#`.

| Pattern | Key | Notes |
|---|---|---|
| `stagedata/*/jmp/placement/*/*` (objects, areas, cameras, planets, stage objects, cutscene objects and sounds) | `l_id` | None |
| `stagedata/*/jmp/mapparts/*/*`, `stagedata/*/jmp/childobj/*/*`, `stagedata/*/jmp/path/commonpathinfo` | `l_id` | None |
| `stagedata/*/jmp/start/*/*` | `MarioNo` | Many start files have no `l_id`. |
| `stagedata/*/jmp/generalpos/*/*` | `PosName`, `Obj_ID` | The game itself repeats a key in two files. For more information, see [Keys the game repeats](#keys-the-game-repeats). |
| `stagedata/*/camera/cameraparam.bcam` | `id` | Some IDs are not UTF-8. For more information, see [Cells that are not UTF-8](#cells-that-are-not-utf-8). |
| `stagedata/*/zonelist.bcsv` | `ZoneName` | None |
| `stagedata/*/scenariodata.bcsv` | `ScenarioNo` | None |
| `stagedata/*/objnametable.tbl` | `en_name` | None |
| `**/messageid.tbl` | `MessageId` | None |
| `objectdata/**/*.banmt` | `name` | None |
| `objectdata/**/actoranimctrl.bcsv` | `ActorAnimName` | None |
| `objectdata/**/shadow.bcsv` | `Name` | None |
| `objectdata/**/groupinfo` | `name` | None |
| `objectdata/*/padrumbledata.bcsv` | `PadRumbleName`, `StartFrame` | None |
| `objectdata/lightdata/lightdata.bcsv` | `AreaLightName` | None |
| `objectdata/*/light*.bcsv` | `AreaLightName`, `LightID` | None |
| Keyframes (`objectdata/**/*.pa`), cutscene steps (`objectdata/**/demo*/*.bcsv`), rail points, `jmp/list/*`, `struct.tbl` and every other table | `#` | Rows by place. For more information, see [Rows by place](#rows-by-place). |

The rules are in `crates/nebula-import/data/table-keys.tsv`, which nebula-import compiles in.

Keys compare exactly, byte by byte.

## Rows by place

Tables the game reads in order, such as keyframes, a cutscene's steps and a rail's points, or tables with no column that tells rows apart, use `#`. A patch names a row by its place: `"where": { "#": 3 }`, where `0` is the first row of the game's table.

* `set` and `select` work as on any table.
* `add` appends rows after the game's rows, with mods' rows from the bottom of the mod list up, so the higher mod's rows come last.
* `remove` is refused (L402), because it would move every later row under other mods' `#` numbers.
* `rows` gives the whole table: the mod's rows replace the game's. When two mods give `rows` for one table, or one mod gives `rows` and another sets cells in it, the mod higher in the list wins, and the **MODS** page names both.
* Every `#` that a patch names is checked against the game's row count (L402).

## Keys the game repeats

Two `generalposinfo` files hold two rows with the same key: `snowcapsulezone/common` and `stardustassemblyblockzone/layerb`. The game finds the first one.

A patch whose `where` matches more than one row is an error (L402) that names the key and how many rows match. `select` changes all of them.

## Cells that are not UTF-8

Two camera files, in AstroGalaxy and SpringWaterPlanetZone, have IDs that end in a counter byte that no text encoding can read. The game keeps the bytes, so it can tell them apart, but JSON text cannot. Wherever a table is JSON, such a cell is written as `{ "bytes": "HEX" }`: in LumaLink's `extract` and `list`, and in a patch's `where`, `values` and `add`. LumaLink writes this form, and the validator and the game read it.

## Columns

A patch names columns by name. Tables keep a hash of each name, and the validator and the game compare hashes, so a patch needs no list of names to be checked. A name that is not a column is still an error (L403) that names it.

Column types come from the game's table.

### Columns a table lacks

Files of one kind do not all have every column. An empty layer's table often has only a few columns. For example, some empty `generalposinfo` files have only positions and directions, and many `planetobjinfo` files lack 14 columns.

* A kind is one of the rules in [Keys](#keys). For a rule whose last part is `*` (placement, mapparts, childobj, generalpos, start and list), the file's name is also part of the kind, for example `stagedata/*/jmp/placement/*/*|objinfo`. Rules that take unlike files by place have no kind.
* An empty table takes its kind's columns before any patch applies.
* A table with rows keeps its own columns, and a column it lacks is an error (L403), because the existing rows would take a value where the game reads none.

### Values of a new row

A new row, from `add` or `rows`, starts from its kind's usual values: the value most of the game's rows have. For example, most rows have `-1` in `Obj_arg0` to `Obj_arg7`, the `SW_` columns, `CommonPath_ID`, `CameraSetId` and `ShapeModelNo`, and `1` in the `scale_` columns. A column of no kind starts as `0` or empty.

## Linked rows

The game links objects across a zone's layers by key, so keys must be unique across those layers.

A link group is one zone's files of one kind across its layers: `stagedata/ZONE/jmp/FOLDER/LAYER/FILE`, where FOLDER is `placement`, `mapparts`, `childobj`, `start` or `generalpos`, and LAYER is `common` or `layera` to `layerp`. For example, every `.../placement/*/objinfo` file of a zone is one group.

* `l_id` (in placement, mapparts and childobj) is unique across its group.
* `MarioNo` (in start), and `PosName` with `Obj_ID` (in generalpos), are unique across `common` and the layers that one scenario turns on together.
* An `add` to a group's table must give its key (L402 `Patch.AddNoKey`).
* A key that is already in another table of its group, in the game or in another file of the same mod, is an error (L402 `Patch.KeyInGroup`). The game leaves out the whole patch.

```text
gold-kuribo: layers/base/patches/StageData/EggStarGalaxy.arc/jmp/placement/layera/objinfo.json line 4: the key 120
is in StageData/EggStarGalaxy.arc/jmp/Placement/Common/ObjInfo already: the game links by it across the zone's
layers; give another.
```

When two mods add the same key to one group, the higher mod's row stays, the lower mod's row is left out with a conflict on the **MODS** page, and the rest of the lower mod's patch applies.

### Layers that are on together

A scenario's layers in a zone come from the zone's column in the galaxy's scenario table (`stagedata/GALAXY/scenariodata.bcsv`), times 2, with bit 0 (`common`) always on. Two layers are on together when some row of some galaxy that names the zone has both bits.

The count uses the game's own tables, the scenario patches of this mod and of the other mods that are on (lowest first, a later cell winning), and the scenario table of a new galaxy that a mod brings whole. When a new galaxy's table cannot be read as a table, L402 is not checked (`Missing.NewScenarios`).

## How patches are applied

The game patches its tables once, at start, when it prepares the mods. A stage load reads the patched tables, so it costs the same with or without mods.

The game takes each table and the patches of every mod that is on, from the bottom of the mod list up. Each mod's patch applies in this order: `rows`, `remove`, `set`, `add`, `select`. Where two mods meet, the higher mod wins, and the **MODS** page names both (`Mods.Conflict.Cell`, `Mods.Conflict.Row` or `Mods.Conflict.Removed`).

The validator reads the same rules and the same view of each table in the game's start check and in LumaLink's `check` and `pack`, so both give the same findings.

## Further reading

* [The .luma file format](luma-file-format.md)
* [Patching game tables](../how-tos/patching-game-tables.md)
* [Validator rules](validator-rules.md)
