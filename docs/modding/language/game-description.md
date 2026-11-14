# The game's description of itself

## More rules

### Self-description

One file that says everything a mod can name in one build of the game: functions, types, globals, game data in code,
and the game's data names (nerves, messages, the keys of its tables). LumaLink, the LumaScript compiler and the
editor read it; nothing else is a source of names. The build makes it from the game's source, in the same pass that
writes the function rows, never by hand.

- `smg_game.luma-self.json`, beside the game module, made by every build of the module (a cross build too), compact
  UTF-8 JSON, about 20 MB. Setup installs it beside the game; `lumalink build --names <folder>` reads another one.
- The module carries the same data (the loader binds by it), and the game writes it on request
  (`--luma-describe <file>`). The build compares the two and stops on a difference, so the file shipped with the game
  and the installed game's answer are the same.
- Stable names. A name and signature, once in a released self-description, keeps its meaning.
- Per platform: offsets and sizes may differ between ABIs, names never do. LumaLink reads one self-description for
  every platform it builds; a mod reaches a field of a game class through the offset the loader binds at load from
  the running game, not one taken from the file.

### Shape (JSON, UTF-8)

```json
{
  "self": 1,
  "module": { "id": "<sha256 of the module>", "game": "RMG", "regions": ["RMGE01", "RMGP01", "RMGJ01"],
              "platform": "windows-x64", "abi": "msvc-x64", "luma_api": 1, "hooks": true,
              "built_from": "<commit>" },
  "functions": [
    { "name": "Kuribo::exeWander", "signature": "void()", "owner": "Kuribo", "kind": "method",
      "virtual": true, "const": false, "static": false,
      "returns": "void", "params": [], "hooks": ["before", "after", "wrap", "replace"],
      "file": "Game/Enemy/Kuribo.cpp", "line": 212 }
  ],
  "left_out": [ { "name": "...", "file": "...", "why": "template | header-only | allocation | ..." } ],
  "types": [
    { "name": "Kuribo", "kind": "class", "size": 412, "align": 4, "polymorphic": true,
      "bases": [ { "name": "LiveActor", "offset": 0, "virtual": false } ],
      "fields": [ { "name": "mVelocity", "type": "TVec3f", "offset": 36, "count": 1, "bits": null,
                    "access": "protected" } ],
      "file": "Game/Enemy/Kuribo.hpp" },
    { "name": "MarioModule::State", "kind": "enum", "underlying": "s32",
      "values": [ { "name": "State_Wait", "value": 0 } ] },
    { "name": "f32", "kind": "alias", "of": "float" }
  ],
  "globals": [ { "name": "MR::sDefaultGravity", "type": "f32", "file": "..." } ],
  "data": [
    { "key": "Kuribo.cWalkSpeed", "type": "f32", "count": 1, "game": [2.0], "file": "Game/Enemy/Kuribo.cpp",
      "function": null }
  ],
  "names": {
    "nerves": [ { "name": "NrvKuribo::KuriboNrvWander", "owner": "Kuribo", "exe": "Kuribo::exeWander" } ],
    "messages": [ { "name": "ACTMES_PLAYER_ATTACK", "value": 1 } ],
    "keys": { "sGameEventFlagStatic": ["SpinAndStarPointer", "..."] }
  }
}
```

- `module`: `platform` is `<os>-<arch>` (windows, linux, macos, android, ios; x64, arm64); `abi` is `msvc-<arch>` on
  Windows, else `itanium-<arch>`; `luma_api` is luma.h's `LUMA_API`.
- `functions`: the function rows, with each pointer parameter's marks (below). `left_out` lists every function
  without a row, with its reason.
- `types`: every field of every class, struct and union the game defines, bit-fields with their bits, arrays with
  `count`. Type text is as the game's source writes it after its own typedefs (`TVec3f`, `f32`), so a script reads
  the same words as the source.
- `names.nerves`: every class with the base `Nerve`, with the one function its `execute` calls (`exe`) and that
  function's class (`owner`); null when `execute` calls none, several, or goes through a helper.
- `names.messages`: every `ACTMES_*` with its value.
- `names.keys`: for every array table whose rows each begin with a plain string, the strings in row order: the event
  flags, event values, the object factory's names and the rest.
- `functions[].name` with `signature` is exactly what `luma.h`'s `LUMA_FUNCTION` and `LUMA_HOOK_ON` bind;
  `types[].name` with `fields[].name` exactly what `LUMA_FIELD` binds.

### Pointer parameters: marks

What a game function does with a pointer parameter is written in its header, before the parameter's type:

```cpp
void getPlayerFrontVec(LUMA_OUT Vec3f*);
void f(LUMA_IN LUMA_COUNT(n) const f32* p, s32 n, LUMA_OUT LUMA_NULLABLE Vec3f* out);
```

| Mark | Means |
|---|---|
| `LUMA_OUT` | it may write one value through the pointer, never reads it first, does not keep the address |
| `LUMA_IN` | it reads one value through it, never writes it, does not keep it |
| `LUMA_INOUT` | it reads one value, may write it, does not keep it |
| `LUMA_KEPT` | the game keeps the address (a registration); the pointer may be to anything |
| `LUMA_COUNT(param)` | with a way: an array whose element count is the integer parameter `param` |
| `LUMA_COUNT_FIXED(n)` | with a way: an array of exactly `n` elements (1 to 4096; a matrix's 12 floats) |
| `LUMA_NULLABLE` | with any way: the function accepts null there (LumaScript may pass `none`) |

Except under `LUMA_KEPT`, the pointer is to a number, an enum or a vector (2 to 4 floats). Each mark expands to
nothing for every compiler. The build reads them into each row's `marks` (one entry per declared parameter, the
implicit `this` of a method not counted) and stops, naming the file and line, on a mark of the wrong form.
LumaScript calls a function whose parameter points at a number, vector or enum only when that parameter is marked
(else the checker says the row does not say one or many, kept or not). The compiler fills a `LUMA_COUNT` parameter
from the list it passes, so the script does not write it. A mark the type cannot carry (a write through a const
pointer, a count that is no integer) says so (`Script.Why.MarkNotFit`).

### Placements

`smg_game.luma-placement.json`, beside the self-description, made by the same build pass. Top level: `placement` 1,
`factory`, `placement_args`, `left`. content/stages.md uses it to give a placed object's arguments by name.

- `factory`: the game's object factory table in its order, `{name, class, creator}`. A template creator gives its
  argument as the compiler resolved it; a function creator the one class its body creates; null when it creates none
  or several.
- `placement_args`: `{class: [row]}` for every class that reads a placement argument, its own or a base's. A row:
  `arg` (0-7 for `Obj_argN`, null for a column read by its own name), `field` (the column: `"Obj_arg0"`,
  `"RotateSpeed"`), `type` (s32, f32, bool), `with_init`, `target` (the field set, as written: `"mObjArg0"`; null
  for a local, an element or a helper's read), `local`, `written`, `function`, `signature`, `file`, `line`, `from`
  (the class whose method reads it), `via` (the functions the method hands its iterator to, the reading one last).
  Sorted by `arg`, own before inherited.
- `left`: free functions with reads that no method hands an iterator to, each with its reason.
