# Plugin SDK

What a mod's code builds against. The guide is [LumaLink's docs/plugins.md](https://github.com/keepvaibin/lumalink/blob/main/docs/plugins.md).

| Path | What it is |
|---|---|
| `../include/nebula_mod_api.h` | The stable interface, plain C. Fields are only added, at the end. |
| LumaLink's `examples/one-health`, `coin-kills`, `boss-music`, `double-jump`, `achievements`, `little-boss`, `gold-flower` | Whole mods with code, each built for every device by `lumalink build`: the player's state, events, music, a new move, achievements, a new enemy, a new power-up. |
| `examples/movie_start` | Plays a movie in a level and stops it. |
| `examples/cutscene_log` | Says each cutscene's name as it starts and ends. |
| `examples/cube` | A new object with the stable interface and a model. |
| `examples/selftest` | Calls everything in the interface and says what came back. |
| LumaLink's `examples/gecko-<code>` | Popular cheat codes done through the interface, one mod each, built for every device: moon jump, infinite health and lives, star bits, low gravity, power-ups anywhere or lasting, rainbow, freeze timer, free camera, wide fov, hide HUD, no fall death, slow motion and frame advance, position display, freeze enemies, warp anywhere, Mario's size, no spin cooldown, spin and pointer from the start. LumaLink names the one that does a code it reads. |

A plugin with only the stable interface links no library, not even the C runtime: clang and lld build it for every
device the game runs on, from one computer. `lumalink build <project>` does all eight (Windows, Linux, Android and
macOS, each x64 and arm64) and names them in the project's luma.json; by hand, one device:

```console
clang --target=aarch64-linux-android24 -O2 -shared -nostdlib -ffreestanding -fno-stack-protector -fvisibility=hidden -fuse-ld=lld -fPIC -Wl,-z,defs -I native/include -o my.so my.c
```

The targets: `x86_64-pc-windows-msvc` and `aarch64-pc-windows-msvc` (add `-Wl,/NOENTRY`), `x86_64-linux-gnu`,
`aarch64-linux-gnu`, `x86_64-linux-android24` and `aarch64-linux-android24` (add `-fPIC -Wl,-z,defs`),
`x86_64-apple-macos10.15` and `arm64-apple-macos11`. A call the C library would answer (malloc, printf) leaves a
symbol undefined and the build stops; the interface answers them instead: `alloc` and `release` for memory, `format`
for printf's formatting, `log` for text. clang can still turn a large copy or clear into a call to `memcpy` or
`memset`; a plugin the linker stops on one defines it in its own source.

Every capability is also reachable without code (luma.json rows and LumaScript), which every device runs, iOS
included. `hook` and `find_function` rewrite a function's first instructions: they work only where a device lets a
plugin load and patch code (Windows, Linux, macOS, Android), and nothing in Nebula depends on them.
