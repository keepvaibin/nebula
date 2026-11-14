# Table patches: how a row is named

## More rules

### Table-keys

What identifies a row of each game table, so a patch (luma-format 5.1) names the row it changes, the validator checks
it, and two mods' patches combine.

### 1. Evidence

The keys and columns below were read from every table of the US, PAL and Japanese games (RMGE01 13,552 tables,
RMGP01 13,558, RMGJ01 13,548). They hold for all three unchanged: the same two files repeat a key (section 4) and
every kind's columns are the same.

### 2. The keys

One file, `crates/nebula-import/data/table-keys.tsv`: `pattern`, `key`, `why`. A game path (lowercase) takes the first
pattern that matches (`*` within one folder name, `**` any number of folders). `key` is column names joined by `,`,
or `#`. The last pattern is `**` with `#`: no table is without a rule.

| Pattern | Key | Files | Rows | Repeats in the game |
|---|---|---|---|---|
| `stagedata/*/jmp/placement/*/*` (objects, areas, cameras, planets, stage objects, demos, sounds) | `l_id` | 4,542 | 22,221 | none |
| `stagedata/*/jmp/mapparts/*/*`, `.../childobj/*/*`, `.../path/commonpathinfo` | `l_id` | 1,506 | 4,145 | none |
| `stagedata/*/jmp/start/*/*` | `MarioNo` (315 files have no `l_id`) | 649 | 480 | none |
| `stagedata/*/jmp/generalpos/*/*` | `PosName`, `Obj_ID` (how the game finds one) | 648 | 454 | 2 files (section 4) |
| `stagedata/*/camera/cameraparam.bcam` | `id` | 230 | 9,530 | none, read as bytes (section 5) |
| `stagedata/*/zonelist.bcsv`, `scenariodata.bcsv`, `objnametable.tbl` | `ZoneName`, `ScenarioNo`, `en_name` | 97 | 2,061 | none |
| `**/messageid.tbl` | `MessageId` | 10 | 70 | none |
| `objectdata/**/*.banmt`, `actoranimctrl.bcsv`, `shadow.bcsv`, `groupinfo` | `name`, `ActorAnimName`, `Name`, `name` | 372 | 2,511 | none |
| `objectdata/*/padrumbledata.bcsv` | `PadRumbleName`, `StartFrame` | 6 | 391 | none |
| `objectdata/lightdata/lightdata.bcsv` / `objectdata/*/light*.bcsv` | `AreaLightName` / `AreaLightName`, `LightID` | 265 | 948 | none |
| `nebuladata/rt*` (Nebula's own) | stage / effect / model, material / model[, material] | | | none; case-insensitive (6) |
| keyframes `objectdata/**/*.pa`, cutscene steps `objectdata/**/demo*/*.bcsv`, rail points, `jmp/list/*`, `struct.tbl`, and the 313 other one-off tables | `#` | 5,220 | 689,894 | (by place) |

### 3. `#`: rows by place

Tables the game reads in order (keyframes, a cutscene's steps, a rail's points) or with no column that tells rows
apart. A patch names a row by its place: `"where": { "#": 3 }` (0 is the first row of the game's table).
- `set` by `#` and `select` work as on any table. `add` appends after the game's rows, the mods' rows from the bottom
  of the mod list up, so the higher mod's rows come last.
- `remove` is refused on a `#` table (L402 says why): it would move every later row under the other mods' `#`.
- `rows` (`#` tables only) gives the whole table: the mod's rows replace the game's. Two mods that give `rows` for one
  table conflict: the one higher in the mod list wins, and the Mods screen names both (`Mods.Conflict.Row`, row `*`).
  A mod that gives `rows` and another that `set`s the same table conflict the same way.
- Every `#` a patch names is checked against the game's row count (L402).

### 4. Keys the game itself repeats

Two `generalposinfo` files hold two rows with one key (`snowcapsulezone/common`: "隠れ位置", -1;
`stardustassemblyblockzone/layerb`). The game finds the first. A patch whose `where` matches more than one row is
L402 naming the key and how many rows; `select` changes all of them. Nothing else in section 2 repeats.

### 5. Words that are not UTF-8

Two cameraparam files (AstroGalaxy, SpringWaterPlanetZone) have ids ending in a counter byte that no encoding reads
(`e:ワープカメラ 0-` followed by 0x91, then 0x92, ...). The cook keeps the bytes, so the game tells them apart; JSON
text cannot. Such a cell is `{ "bytes": "<hex>" }` wherever a table is JSON (LumaLink's extract and list, a patch's
`where`, `values` and `add`). LumaLink writes it; the validator and the applier read it.

### 6. Matching

Keys compare exactly (bytes), but for the RT tables, which compare without regard to case; a trailing `*`
is part of the key's text there.

### 7. Columns

A patch names columns by name; tables keep the hash of the name (the game's x31). The validator and the applier
compare hashes, so no list of names is needed to check a patch (a name that is not a column is still L403 naming it).
Column types come from the cooked table.

### 8. Who does what

- `table-keys.tsv` and `cook::table_keys` (compiled into nebula-import) give each path its rule.
- The applier (nebula-import, `luma_patch_table` in the C interface beside `luma_validate`) takes a game table and
  the patches of every mod that is on, from the bottom of the mod list up, and gives the table the game reads. Each
  mod's patch applies in this order: `rows`, `remove`, `set`, `add`, `select`. Where two mods meet, the one higher in
  the list wins and the Mods screen names both (`Mods.Conflict.Cell`, `Mods.Conflict.Row`, `Mods.Conflict.Removed`).
- The validator reads the same rules and the same table views, in the game's start check and in LumaLink's `check`
  and `pack`.
- The game patches its tables once at the start, in the mods' cook: a stage load reads the patched tables and costs
  the same with or without mods.

### 9. Link keys

1. A link group is one zone's files of one kind across its layers: `stagedata/<zone>/jmp/<folder>/<layer>/<file>`
   with `<folder>` one of `placement`, `mapparts`, `childobj`, `start`, `generalpos` and `<layer>` one of `common`,
   `layera` to `layerp` (the 17 the game has), so `.../placement/*/objinfo` is one group. The game links by these
   keys across a zone's layers: a linked object is found by (`l_id`, zone, file kind), a demo group likewise.
2. `l_id` (placement, mapparts, childobj) is unique across its group. `MarioNo` (start) and `PosName` with `Obj_ID`
   (generalpos) are unique across Common and the layers one scenario turns on together (item 4): the game itself has
   one `MarioNo` in two layers that no scenario turns on together.
3. An `add` to a group's table gives its key (L402 `Patch.AddNoKey`). A key already in another table of its group,
   in the game or in another file of the same mod, is L402 `Patch.KeyInGroup`, naming the key and that table:

   ```text
   gold-kuribo: layers/base/patches/StageData/EggStarGalaxy.arc/jmp/placement/layera/objinfo.json line 4: the key 120
   is in StageData/EggStarGalaxy.arc/jmp/Placement/Common/ObjInfo already: the game links by it across the zone's
   layers; give another.
   ```

   The applier leaves such a patch out whole. Two mods adding one key to one group (one table or two): the higher
   mod's row stays, the lower mod's row is left out with a conflict on the Mods screen, and the rest of the lower
   mod's patch applies. Two installed mods adding one key to one group are L404 for each other, as for one table.
4. The layers on together. A scenario's layers in a zone are the zone's column of the galaxy's scenario table
   (`stagedata/<galaxy>scenario/scenariodata.bcsv`, the column named by the zone) times 2, with bit 0 (Common) always
   on. Two layers are on together when some row of some galaxy naming the zone has both bits. Counted with the game's
   own tables: the scenario patches of this mod and of the other mods that are on (the lowest first, a later cell
   winning), and a new galaxy's own scenario table a mod brings whole. A new galaxy's table that does not read as a
   table leaves L402 not checked (`Missing.NewScenarios`); a store that cannot list its galaxies checks Common and the
   same layer only and says 402 not checked (`Missing.Scenarios`).
5. The cost is at the start only: a group's tables are read when a table of the group has a patch.

### 10. Columns a table lacks, and a new row's values

Files of one kind do not all have every column. An empty layer's table often has a few: 21 empty `generalposinfo`
files have only positions and directions, not even `PosName` and `Obj_ID`; 622 `planetobjinfo` files lack 14 columns.
And the game's rows have -1 in many columns a mod might leave out (`Obj_arg0`-`7`, `SW_*`, `CommonPath_ID`,
`CameraSetId`, `ShapeModelNo`, ...) and 1 in others (`scale_*`).

1. Kinds. A kind is a rule of section 2, and for a rule whose last part is `*` (placement, mapparts, childobj,
   generalpos, start, list) also the file's name: `stagedata/*/jmp/placement/*/*|objinfo`. Rules that take unlike
   files by place (`**`, `objectdata/**/*.pa`, `objectdata/**/demo*/*.bcsv`) have no kind. 27 kinds, 759 columns.
2. `data/table-columns.tsv` (`kind column type width mask usual`), compiled into nebula-import: each kind's columns,
   each in the form its files have, and `usual`, the value most of the game's rows have (a word: empty).
3. An empty table takes its kind's columns before any patch applies, in the applier and as the validator sees it. A
   table with rows keeps its columns: a column it lacks is L403 (existing rows would take a value where the game reads
   none).
4. A new row (`add`, `rows`) starts from its kind's usual values; a column of no kind is 0 or empty.

### 11. The game's store

The validator's view of a table (key, `#`, repeats, cells, kind columns) is built in Rust from the table's cooked
bytes, in LumaLink and in the game alike: the game's store gives each table's bytes and lists a folder's names, so
the game's start check and LumaLink's `check` give the same findings.
