# Content: stages

## 1. Placing the game's objects in a stage (layer 1)

A stage's placement table is a file of its archive: `StageData/<Stage>.arc/jmp/placement/<layer>/objinfo` (also
`mapparts`, `areaobjinfo`, `cameracubeinfo`, ... any table of `jmp/`), as `lumalink extract` writes it. A mod changes a
game's table by a patch by key, `layers/<l>/patches/StageData/<Stage>.arc/jmp/placement/<layer>/objinfo.json`
(`{"patch": 1, "add": [...], "set": [...], "remove": [...]}`);
a whole file under `files/` is only for a table the game does not have (a new stage's), the validator refuses one
that replaces a table with rows (L303).

Each row is the game's columns by name: `name` (the object, as the game's factory knows it), `l_id`, `pos_x`..`z`,
`dir_x`..`z`, `scale_x`..`z`, `SW_APPEAR`, `SW_A`, ..., `Obj_arg0`..`Obj_arg7`, `CommonPath_ID`, ...

### Arguments by name

A row may give its object's arguments by name instead of `Obj_arg<n>`:

```json
{ "name": "IronCannonLauncher", "l_id": 40, "pos_x": 0, "pos_y": 500, "pos_z": -2000,
  "args": { "ShotInterval": 120, "Speed": 30, "IsRotate": true } }
```

- The names are the game's own: for the row's object (`name`), the class the factory makes for it, and every
  placement read of that class and its bases, named by the field it fills without its `m` (`mShotInterval` ->
  `ShotInterval`). One name per argument: a read with a field in the class itself first, then one in a base, else
  `Arg<n>` (a local, a helper's read, a field two arguments share). A column the game reads by its own name
  (`getPlacementArgNoInit(iter, "RotateSpeed", &v)`) is named as it is and takes the value as given (a fraction
  too, as its column holds it). From `smg_game.luma-placement.json`, which the build writes beside the game module
  from its source; nothing is matched by hand. `Arg0`..`Arg7` name any object's arguments.
- Values: what the game reads is a whole number, -1 meaning "not given" (PlacementUtil `getPlacementArgNoInit`).
  A whole number goes in as it is (a float field takes it converted, as the game does); `true` is 1, `false` and
  `null` are -1 (a bool field is true for any value but -1). A fraction, text or a number outside 32 bits: refused
  (Cook.Effect.Kind / Cook.Effect.Range {where}).
- A row with `args` (`add`, `rows`, a whole file) has -1 in each `Obj_arg<n>` it gives neither way, as the game's rows
  have. In a patch's `set` (`{"where": {"l_id": 40}, "values": {"args": {"Speed": 45}}}`) the object is the found
  row's `name`, and only the arguments named change.
- The validator takes `args` as an object in a `jmp/` table's patch (Patch.ArgsShape when it is not one); the names
  are the cook's to check: at the game's start the patch fails whole (a Mods-screen fact, the other mods' patches
  apply), and `lumalink check --game <game.pak> --names <folder>` applies each such patch to the game's table as the
  start does and says the same refusal (L804). `--names` is the folder holding smg_game.luma-placement.json.
- Refusals (L804 on the file, at the game's start and in `lumalink check` with the game):
  - Cook.Stage.NoObject {where, name}: the factory has no such object (or the row has no `name`).
  - Cook.Stage.ArgName {where, name, other: the object, expected: its names and Arg0-Arg7}.
  - Cook.Stage.ArgTwice {where, name, column}: one argument given twice (by name and as Obj_arg, or two names).

### smg_game.luma-placement.json

```json
{ "placement": 1,
  "factory": [ { "name": "Kuribo", "class": "Kuribo", "creator": "createNameObj<Kuribo>" } ],
  "placement_args": { "Kuribo": [ { "arg": 0, "field": "Obj_arg0", "type": "f32", "with_init": false,
                                    "target": "mSpeed", "local": null, "function": "Kuribo::init", "file": "...",
                                    "line": 1, "from": "Kuribo", "via": [] } ] } }
```

Beside the game module (smg_game.luma-self.json's folder). The game's cook reads it from beside the running
program, LumaLink from its `--names` folder, once, at the first row with `args`; a stage without named arguments
never opens it. Missing, it is Cook.Stage.NoFacts naming the path. A later `placement` version is not read.

## 2. Proven, and not yet

Proven by unit tests (nebula-import `cook::patch`): `add` and `set` by name in a placement patch, a wrong name failing
that patch alone with the table unchanged. At the command line (lumalink check, the HeavenlyBeachGalaxy objinfo, a
fixture facts file): a wrong name and an argument given twice refused by name.

Next layers: (2) collision cook and a flat zone; (3) gravity and areas; (4) cameras and paths; (5) a whole
new stage with a star; (6) lights, sky, fog.
