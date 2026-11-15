---
title: kit.progress
shortTitle: progress
intro: "Stars beyond 120 and new star kinds counted, comets added to any galaxy, per-file flags and counters, what a star gives, new-file rules."
contentType: reference
---

## About kit.progress

The `kit.progress` module of LumaKit gives you stars beyond 120 and new star kinds counted, comets added to any galaxy, per-file flags and counters, what a star gives, new-file rules.

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.progress
```

### How the module works

Stars beyond the game's own counted, what getting a star does, comets added to any galaxy, flags and numbers kept per save file, new-file rules. Stars. A star of a galaxy the game knows is the game's (its own save data, GameDataFunction). A star of a galaxy a mod adds (kit.stage) is kept by the kit on the save file (svc.save: a file's copy takes it along, an erased file forgets it) and counted into the game's own total wherever the game asks for it (GameDataFunction::calcCurrentPowerStarNum, MR::getPowerStarNum), so star gates open past 120. on_star: when the game records a star as got (GameDataFunction::setGameFlagPowerStarSuccess), each mod's function is asked, newest first; the first that does not answer Star decides: Nothing (the star is not recorded) or Instead (kit.item gives that, and the star is not recorded). Comets: a comet added to a galaxy's scenario is what the game's comet schedule answers for that galaxy (GalaxyCometScheduler isCometLand, getEncounterCometName, getEncounterCometPowerStarId) while the mod is on. Flags and numbers: per file, per mod, by id; their handles' functions are also module functions (`flag_get`). Everything belongs to the mod that made it: its on_star and on_new_file functions and its comets go when it stops; its flags, numbers and stars stay on the file for when it is on again.

The module's source is `lumakit/source/progress.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`StarGet`](#starget) | Enum |  |
| [`Flag`](#flag) | Struct |  |
| [`Number`](#number) | Struct |  |
| [`stars`](#stars) | Function | The game's stars and every mod's, on this file. |
| [`has_star`](#has_star) | Function |  |
| [`give_star`](#give_star) | Function | The star recorded as got on this file, as the game records its own (so on_star functions are asked for it too). |
| [`on_star`](#on_star) | Function | What getting a star does, for the calling mod: asked with the galaxy and the scenario. |
| [`add_comet`](#add_comet) | Function | A comet over the galaxy's scenario for as long as the calling mod is on; a second for the same galaxy by the same mod replaces the first, another mod's newer one holds it. |
| [`flag`](#flag) | Function | A per-file flag of the calling mod (following the file's copy and erase). |
| [`flag_get`](#flag_get) | Function |  |
| [`flag_set`](#flag_set) | Function | Only the calling mod's own flag is set: a Flag is a plain key, so one naming another mod's is refused. |
| [`set_flag`](#set_flag) | Function | The calling mod's flag `id` set in one call. |
| [`number`](#number) | Function | A per-file number of the calling mod: experience, achievements. |
| [`number_get`](#number_get) | Function |  |
| [`number_set`](#number_set) | Function | Only the calling mod's own number is set, as flag_set. |
| [`number_add`](#number_add) | Function | The number moved by `d`: its new value. |
| [`on_new_file`](#on_new_file) | Function | The calling mod's rule for a new file (New Game+): run when the player makes a file, after the game has made it, so the function sets its flags, numbers and stars on the new file. |

### `StarGet`

```lumascript
pub enum StarGet { Star, Nothing, Instead(g: item.Gives) }
```

### `Flag`

```lumascript
pub struct Flag {
    key: text
    fn get(self) -> bool { return flag_get(self) }
    fn set(self, value: bool) { flag_set(self, value) }
}
```

### `Number`

```lumascript
pub struct Number {
    key: text
    fn get(self) -> i64 { return number_get(self) }
    fn set(self, v: i64) { number_set(self, v) }
    fn add(self, d: i64) -> i64 { return number_add(self, d) }
}
```

### `stars`

```lumascript
pub fn stars() -> u32
```

The game's stars and every mod's, on this file.

### `has_star`

```lumascript
pub fn has_star(galaxy: text, scenario: u32) -> bool
```

### `give_star`

```lumascript
pub fn give_star(galaxy: text, scenario: u32)
```

The star recorded as got on this file, as the game records its own (so on_star functions are asked for it too).

### `on_star`

```lumascript
pub fn on_star(f: fn(text, u32) -> StarGet)
```

What getting a star does, for the calling mod: asked with the galaxy and the scenario.

### `add_comet`

```lumascript
pub fn add_comet(galaxy: text, scenario: u32, comet: stage.Comet)
```

A comet over the galaxy's scenario for as long as the calling mod is on; a second for the same galaxy by the same mod replaces the first, another mod's newer one holds it.

### `flag`

```lumascript
pub fn flag(id: text) -> Flag
```

A per-file flag of the calling mod (following the file's copy and erase).

### `flag_get`

```lumascript
pub fn flag_get(f: Flag) -> bool
```

### `flag_set`

```lumascript
pub fn flag_set(f: Flag, value: bool)
```

Only the calling mod's own flag is set: a Flag is a plain key, so one naming another mod's is refused.

### `set_flag`

```lumascript
pub fn set_flag(id: text, value: bool)
```

The calling mod's flag `id` set in one call.

### `number`

```lumascript
pub fn number(id: text) -> Number
```

A per-file number of the calling mod: experience, achievements.

### `number_get`

```lumascript
pub fn number_get(n: Number) -> i64
```

### `number_set`

```lumascript
pub fn number_set(n: Number, v: i64)
```

Only the calling mod's own number is set, as flag_set.

### `number_add`

```lumascript
pub fn number_add(n: Number, d: i64) -> i64
```

The number moved by `d`: its new value.

### `on_new_file`

```lumascript
pub fn on_new_file(f: fn())
```

The calling mod's rule for a new file (New Game+): run when the player makes a file, after the game has made it, so the function sets its flags, numbers and stars on the new file.

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
