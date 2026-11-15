---
title: Patching game tables
shortTitle: Patch game tables
intro: Change, add or remove rows in the game's tables, stage placements and other files with rows, without replacing the whole file.
contentType: how-tos
---

## About patches

Many of the game's files are tables: rows with named columns. Stage placements, scenario data, cameras, lights, animation lists and message IDs are all tables. A mod never replaces a table. It patches it: it names the rows it changes, adds or removes, and leaves the rest as the game has it.

Because each mod changes only its own rows, several mods can patch the same table. When two mods change the same cell, the mod higher in the load order wins, and the **Mods** page names both.

## Finding the table and its rows

1. Find the table's path in the game. For example, to list the tables whose path contains `EggStar`:

   ```shell
   lumalink list tables EggStar
   ```

1. Extract the table from your imported game:

   ```shell
   lumalink extract StageData/EggStarGalaxy.arc/jmp/placement/common/objinfo --output game-files
   ```

   LumaLink writes the table and a `.json` copy beside it that you can read. The JSON shows every column and row.

1. Note the table's key, the column that names each row. Each kind of table has its own. For example, stage placements use `l_id`. For every kind, see [Table keys](../reference/table-keys.md).

## Writing a patch

A patch is a JSON file at `layers/LAYER/patches/GAME-PATH.json` in your mod. For example, a patch of the table above in the `base` layer is:

```text
layers/base/patches/StageData/EggStarGalaxy.arc/jmp/placement/common/objinfo.json
```

The file says which key it uses and what to change:

```json
{
  "patch": 1,
  "key": ["l_id"],
  "set": [
    { "where": { "l_id": 12 }, "values": { "scale_x": 2.0, "scale_y": 2.0, "scale_z": 2.0 } }
  ],
  "add": [
    {
      "name": "Kuribo", "l_id": 9411,
      "pos_x": -3439.8, "pos_y": -13081.3, "pos_z": -15575.5,
      "dir_x": 0.0, "dir_y": 0.0, "dir_z": 0.0
    }
  ]
}
```

A patch can do five things. Each is a list:

| Field | What it does |
|---|---|
| `set` | Changes cells of one row. `where` names the row by its key, and `values` gives the new cells. |
| `add` | Adds a row. The key must not exist yet. A column you leave out takes the value most of the game's rows have: -1 for an object's arguments, 1 for its scale. |
| `remove` | Removes the rows named by their key. |
| `select` | Changes every row whose cells match. Each match is `equals`, `prefix` or `glob` on one column, such as `{ "name": { "glob": "Kuribo*" } }`. |
| `rows` | Replaces the whole table. Only for tables whose rows have no key. |

In one mod, Nebula applies them in this order: `rows`, `remove`, `set`, `add`, `select`.

## Tables whose rows have no key

Some tables have no column that tells rows apart, such as keyframes, the steps of a cutscene and the points of a rail. The game reads them in order. Their key is `#`, the row's place, starting at 0:

```json
{
  "patch": 1,
  "key": ["#"],
  "set": [ { "where": { "#": 3 }, "values": { "Speed": 12.0 } } ]
}
```

On these tables, `add` adds rows after the game's rows, and `remove` is not allowed, because it would move every later row under other mods' patches. To replace the whole table, use `rows`.

## Patching another mod's file

To patch a table that another mod brings, put the patch at `layers/LAYER/patches/@MOD-ID/MOD-PATH.json`, and add that mod to your mod's needs:

```shell
lumalink add need MOD-ID MOD-FOLDER
```

## Checking a patch

Run `lumalink check MOD-FOLDER`. The check reads your patch against your imported game, and reports:

* A key that is not the table's key, with the key it expected.
* A column the table does not have, or a value of the wrong type.
* A row that `set` or `remove` names but the game does not have.
* An `add` whose key already exists.

For each code, see [Validator rules](../reference/validator-rules.md).

When the game loads the mod, each `select` reports how many rows it matched. A `select` that matched none is a note in the mod log.

## Further reading

* [Table keys](../reference/table-keys.md)
* [Placing objects in stages](placing-objects-in-stages.md)
* [About mod files and layers](../concepts/mod-files-and-layers.md)
