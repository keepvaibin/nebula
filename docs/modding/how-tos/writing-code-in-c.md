---
title: Writing code in C
shortTitle: Write code in C
intro: Write part of a mod in C or C++, call it from LumaScript, and hook the game's functions from C.
contentType: how-tos
---

## About C in a mod

A mod's code is LumaScript, and it can also include C or C++ files. Use C when you already have C code, such as a library or a port of an existing mod, or when you want the game's function hooks at their lowest level.

LumaLink compiles your C into the same library as the mod's LumaScript, for every platform the mod lists. Nothing is interpreted while the game runs.

A mod can also be C alone, with no `.lum` file. Its C then declares the mod's entry with `LUMA_MOD` from `luma.h`: what it asks the game for, and its start and stop functions. For more information, see [Writing a mod in C alone](#writing-a-mod-in-c-alone). For more information on LumaScript, see [About LumaScript](../concepts/lumascript.md).

## Prerequisites

* LumaLink and an installed Nebula. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* A mod folder with a `code` entry in its `luma.json`. For more information, see [Creating a mod](creating-a-mod.md).

You don't need a compiler of your own. LumaLink brings the compilers for every platform.

## Adding C files

Put `.c`, `.cpp` and `.h` files in the mod's `source` folder, beside its `.lum` files:

```text
double-bits/
  luma.json
  source/
    main.lum
    bits.c
    bits.h
```

`lumalink build` compiles every C and C++ file in `source` and its folders. Include a header by its path from `source`.

## Calling C from LumaScript

1. Declare the C function in a header:

   ```c
   #include <stdbool.h>

   bool bits_on(void);
   ```

1. In LumaScript, read the header with `use c`, and declare the function with `extern c fn`:

   ```lumascript
   use c "bits.h"

   extern c fn bits_on() -> bool

   on start {
       if not bits_on() {
           log "double-bits: the game has no addStockedStarPiece to hook"
       }
   }
   ```

The LumaScript declaration must match the C one. These types cross between the two:

| LumaScript | C |
|---|---|
| `i8` to `i64`, `u8` to `u64` | `int8_t` to `int64_t`, `uint8_t` to `uint64_t` |
| `f32`, `f64` | `float`, `double` |
| `bool` | `bool` |
| `vec2`, `vec3`, `vec4`, `mat34`, `color` | Structs of floats, in that order |
| A struct marked `c struct` | The same struct, in C layout |
| `text`, `bytes` | A pointer and a length, valid until the call returns |
| `ref<C>` | A handle. Get the object with `lumart_ref_get`, on the game thread. |

Lists, maps and other containers don't cross. Pass them one item at a time, or as `bytes`.

## Calling LumaScript from C

Mark a LumaScript function `export c fn`:

```lumascript
var times: i32 = 2

export c fn bits_times() -> i32 {
    return times
}
```

LumaLink writes a header with every exported function, `lm_exports.h`. Include it in your C:

```c
#include "lm_exports.h"

int n = bits_times();
```

Only your mod's own C sees an exported function. The game and other mods can't call it.

## Using the game from C

Two headers give your C the game:

* `luma.h`, which comes with Nebula: finding the game's functions, hooking them, reading fields, services, memory and the mod log.
* `lumart.h`, which comes with LumaLink: `luma_rt_core` is the game's table of functions, and `luma_rt_self` is your mod.

```c
#include "luma.h"
#include "lumart.h"

luma_rt_core->log(luma_rt_self, "double-bits: on");
```

Memory from `luma_rt_core->alloc(luma_rt_self, size, align)` is counted on the **Mods** page and freed when the mod stops. Give it back sooner with `luma_rt_core->free`.

### Services

Each service has a header of its own, such as `luma_audio.h` for `audio`. Ask for the service's table by its name and version:

```c
#include "luma_audio.h"

void bits_ding(void) {
    const LumaAudioService* audio = (const LumaAudioService*)luma_rt_core->service(
        luma_rt_self, LUMA_AUDIO_NAME, LUMA_AUDIO_MAJOR, LUMA_AUDIO_MINOR);
    LumaHandle voice;
    if (audio != 0) {
        audio->play(luma_rt_self, "Ding", 0, 0, 0, &voice);
    }
}
```

`service` returns null when the game has no such service, or the player didn't grant the permission it needs. The service works as it does from LumaScript. For more information, see [Services](../concepts/services.md).

## Hooking a game function

A hook is your function, run when the game calls one of its own. This example doubles every star bit Mario collects:

```c
#include "luma.h"
#include "lumart.h"
#include "lm_exports.h"
#include "bits.h"

/* Runs before the game adds star bits, and multiplies how many. */
static int32_t more_bits(LumaCall* call, void* user) {
    (void)user;
    int* count = (int*)call->args[1];
    *count *= bits_times();
    return 0; /* 0: the game's own function runs next, with the new count */
}

bool bits_on(void) {
    const LumaFunction* add = luma_rt_core->function("GameDataFunction::addStockedStarPiece", "void(int)");
    return add != 0 && luma_rt_core->hook(luma_rt_self, add, LUMA_BEFORE, more_bits, 0) == 0;
}
```

1. `function(name, signature)` finds the game's function by its name, and its signature as the game's source writes it. It returns null when the game has none.
1. `hook(mod, function, kind, hook, user)` adds your hook. `user` is passed back to your hook each time.
1. The hook gets the call:
   * `call->args[0]` is the object (`this`), or null for a function without one.
   * `call->args[1]` and on point at the arguments, so a hook can change them. For a reference parameter, it points at the object itself.
   * `call->result` points at the result.

A hook runs at one of four times:

| Kind | When it runs |
|---|---|
| `LUMA_BEFORE` | Before the function. Return 0 to let it run, or non-zero to skip it. Write `call->result` before skipping. |
| `LUMA_AFTER` | After the function. It can change the result. |
| `LUMA_WRAP` | Around the function. Call `luma_rt_core->proceed(call)` where the function should run. |
| `LUMA_REPLACE` | Instead of the function. Only one mod can replace a function. |

`hook` returns:

| Value | Meaning |
|---|---|
| 0 | Added. |
| -1 | Refused. A constructor or destructor takes `LUMA_BEFORE` and `LUMA_AFTER` only, and a function whose result has no default can't be skipped. |
| -2 | Another mod replaces this function already. |
| -3 | Not called on the game thread. Add hooks from `on start` or another event. |

`luma_rt_core->unhook`, with the same values, takes a hook out. All of a mod's hooks go when the mod stops.

A hook runs on whichever thread calls the function. `luma_rt_core->game_thread()` returns 1 on the game thread.

To find a function's name and signature, look in the game description. For more information, see [The game description](../concepts/the-game-description.md).

In LumaScript, `before`, `after` and `wrap` hook the game's functions with less code. Use C hooks when the rest of the work is in C.

## Writing a mod in C alone

A mod needs no LumaScript. Its C declares the mod's entry with `LUMA_MOD(imports, start, stop)` from `luma.h`:

* `imports` lists what the mod asks the game for by name: functions, fields, hooks and services. The game fills each before `start` runs. A name the game doesn't have stops the mod from loading, and the **Mods** page names it.
* `start` runs when the mod starts. Return 0, or non-zero to stop the mod.
* `stop` runs when the mod stops, or is 0.

This mod doubles every star bit Mario collects, with no `.lum` file:

```c
#include <luma.h>

/* Runs before the game adds star bits, and doubles how many. */
static int32_t more_bits(LumaCall* call, void* user) {
    (void)user;
    int* count = (int*)call->args[1];
    *count *= 2;
    return 0;
}

static int32_t start(const LumaCore* core, const LumaMod* mod) {
    core->log(mod, "double-bits: on");
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("GameDataFunction::addStockedStarPiece", "void(int)", LUMA_BEFORE, more_bits),
};

LUMA_MOD(imports, start, 0);
```

A mod that asks for nothing by name uses `LUMA_MOD_NO_IMPORTS(start, stop)`. For every kind of import, see [LumaCore C API](../reference/lumacore-c-api.md).

## Building and checking

```shell
lumalink build double-bits
lumalink check double-bits
```

`build` compiles the C and LumaScript for every platform in `luma.json`, and prints each library it makes. A C error is printed as the compiler gives it, with the file and line.

`check` also checks the game names your LumaScript uses. It can't see inside your C, so a name your C looks up with `function` is found only when the game runs. Always handle the null case, as the example does.

## Troubleshooting

| Message | What to do |
|---|---|
| ... has no code: no .lum, .c or .cpp file in source/ | Put the mod's `.lum`, `.c` or `.cpp` files in `source`. |
| The compiler's own error, with a file and line | Fix the C. |
| A type that can't cross | Use a type from the table in [Calling C from LumaScript](#calling-c-from-lumascript). |

While the game runs, a fault in your C stops the mod, and the **Mods** page and the mod log say where. For more information, see [When a mod fails](../concepts/when-a-mod-fails.md).

## Further reading

* [About LumaScript](../concepts/lumascript.md)
* [The game description](../concepts/the-game-description.md)
* [Services](../concepts/services.md)
