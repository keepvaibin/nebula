# Table patches: how a row is named

## More rules

### Table-keys

What identifies a row of each game table, so a patch (luma-format 5.1) names the row it changes, the validator checks
it, and two mods' patches combine.

### 1. Evidence

Results: `g/tests/f4-key-survey.tsv` (1,284 kinds) and `g/tests/table-keys-check.tsv` (the rules of section 2 against
every table).

### 2. The keys

One file, `crates/nebula-import/data/table-keys.tsv`: `pattern`, `key`, `why`. A game path (lowercase) takes the first
pattern that matches (`*` within one folder name, `` any number of folders). `key` is column names joined by `,`,
or `#`. The last pattern is `` with `#`: no table is without a rule.

| Pattern | Key | Files | Rows | Repeats in the game |
|---|---|---|---|---|
| `stagedata/*/jmp/placement/*/*` (objects, areas, cameras, planets, stage objects, demos, sounds) | `l_id` | 4,542 | 22,221 | none |
| `stagedata/*/jmp/mapparts/*/*`, `.../childobj/*/*`, `.../path/commonpathinfo` | `l_id` | 1,506 | 4,145 | none |
| `stagedata/*/jmp/start/*/*` | `MarioNo` (315 files have no `l_id`) | 649 | 480 | none |
| `stagedata/*/jmp/generalpos/*/*` | `PosName`, `Obj_ID` (how the game finds one) | 648 | 454 | 2 files (section 4) |
| `stagedata/*/camera/cameraparam.bcam` | `id` | 230 | 9,530 | none, read as bytes (section 5) |
| `stagedata/*/zonelist.bcsv`, `scenariodata.bcsv`, `objnametable.tbl` | `ZoneName`, `ScenarioNo`, `en_name` | 97 | 2,061 | none |
| `/messageid.tbl` | `MessageId` | 10 | 70 | none |
| `objectdata//*.banmt`, `actoranimctrl.bcsv`, `shadow.bcsv`, `groupinfo` | `name`, `ActorAnimName`, `Name`, `name` | 372 | 2,511 | none |
| `objectdata/*/padrumbledata.bcsv` | `PadRumbleName`, `StartFrame` | 6 | 391 | none |
| `objectdata/lightdata/lightdata.bcsv` / `objectdata/*/light*.bcsv` | `AreaLightName` / `AreaLightName`, `LightID` | 265 | 948 | none |
| `nebuladata/rt*` (Nebula's own) | stage / effect / model, material / model[, material] | | | none; case-insensitive (6) |
| keyframes `objectdata//*.pa`, cutscene steps `objectdata//demo*/*.bcsv`, rail points, `jmp/list/*`, `struct.tbl`, and the 313 other one-off tables | `#` | 5,220 | 689,894 | (by place) |

### 3. `#`: rows by place

Tables the game reads in order (keyframes, a cutscene's steps, a rail's points) or with no column that tells rows
apart. A patch names a row by its place: `"where": { "#": 3 }` (0 is the first row of the game's table).
- `set` by `#` and `select` work as on any table. `add` appends at the end, in mod order.
- `remove` is refused on a `#` table (L402 says why): it would move every later row under the other mods' `#`.
- `rows` (new in a patch, `#` tables only) gives the whole table: the mod's rows replace the game's. Two mods that give
  `rows` for one table conflict: the later in load order wins, and the Mods screen names both (luma-format 5.1).
  A mod that gives `rows` and another that `set`s the same table also conflict, named the same way.
- Every `#` a patch names is checked against the game's row count (L402).

### 4. Keys the game itself repeats

Two `generalposinfo` files hold two rows with one key (`snowcapsulezone/common`: "隠れ位置", -1;
`stardustassemblyblockzone/layerb`). The game finds the first. A patch whose `where` matches more than one row is
L402 naming the key and how many rows; `select` changes all of them. Nothing else in section 2 repeats.

### 5. Words that are not UTF-8

Two cameraparam files (AstroGalaxy, SpringWaterPlanetZone) have ids ending in a counter byte that no encoding reads
(`e:ワープカメラ 0-` followed by 0x91, then 0x92, ...). The cook keeps the bytes, so the game tells them apart; JSON
text cannot. Such a cell is `{ "bytes": "<hex>" }` wherever a table is JSON (LumaLink's extract and list, a patch's
`where`, `values` and `add`). LumaLink writes it; the validator and the applier read it (3.11).

### 6. Matching

Keys compare exactly (bytes), but for the RT tables, which compare without regard to case; a trailing `*`
is part of the key's text there.

### 7. Columns

A patch names columns by name; tables keep the hash of the name (the game's x31). The validator and the applier
compare hashes, so no list of names is needed to check a patch (a name that is not a column is still L403 naming it).
Column types come from the cooked table.

### 8. Who does what

- G (3.11): `table-keys.tsv` and `cook::table_keys` (reads it, matches a path, compiled into the crate); the applier
  in `nebula-import` (game table and patches in load order -> patched table, every conflict as a fact for the Mods
  screen), exposed in the C interface beside `luma_validate`; the validator's `Table` from it (hashes, `#`, `rows`,
  bytes); LumaLink's `Store` over game.pak, so `pack` checks layer files and patches (today it refuses, L302 and L303
  unchecked); the conflict tests; PAL and JP checks.
- F (1.1/3.12 side): where the game's table loading calls the applier at start, and the Mods screen lines for its
  conflict facts.

### 10. Columns a table lacks, and a new row's values

Evidence (`crates/nebula-import/tests/store_view_columns.rs`, `store_view_game.rs`, over the US store): files of one
kind do not all have every column. An empty layer's table often has a few: 21 empty `generalposinfo` files have only
positions and directions, not even `PosName` and `Obj_ID`; 622 `planetobjinfo` files lack 14 columns. So the first
object or named position a mod adds to an empty layer had nowhere to go (L403, or the applier's error on a missing key).
And a row a mod added took 0 for each column it left out, where the game's rows have -1 (`Obj_arg0`-`7`, `SW_*`,
`CommonPath_ID`, `CameraSetId`, `ShapeModelNo`, ...) and 1 (`scale_*`).

1. Kinds. A kind is a rule of section 2, and for a rule whose last part is `*` (placement, mapparts, childobj,
   generalpos, start, list) also the file's name: `stagedata/*/jmp/placement/*/*|objinfo`. Rules that take unlike
   files by place (``, `objectdata//*.pa`, `objectdata//demo*/*.bcsv`) have no kind. 27 kinds, 759 columns.
2. `data/table-columns.tsv` (`kind column type width mask usual`), compiled into nebula-import and made from the
   game by the test above, which also checks it against a game: each kind's columns, each column in the form its files
   have (no column has two forms within a kind), and `usual`, the value most of the game's rows have (a word: empty).
3. An empty table takes its kind's columns before any patch applies (applier) and as the validator sees it
   (`store_view::table_view`; the C store's `Table` too, `widen_view`). A table with rows keeps its columns: a column
   it lacks is L403 (existing rows would take a value where the game reads none).
4. A new row (`add`, `rows`) starts from its kind's usual values; a column of no kind is 0 or empty.
5. Open: PAL and JP stores are checked against the file when imported (the test runs on any store); a kind file per
   region if they differ.

### 11. The C store's tables

The validator's view of a table (key, `#`, repeats, cells for L407, kind columns) is built in Rust from the cooked
table (`store_view::table_view`). The game's C store today builds `LumaTable` itself (key, columns, rows) and gives no
repeats or cells, so the game's check and LumaLink's could differ. Proposal: the C store's `table` callback returns
the table's NTBL bytes and the game path; Rust builds the view (one code for both).
