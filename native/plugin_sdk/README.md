# Plugin SDK

What a mod's code builds against. The guide is [LumaLink's docs/plugins.md](https://github.com/keepvaibin/lumalink/blob/main/docs/plugins.md).

| Path | What it is |
|---|---|
| `../include/nebula_mod_api.h` | The stable interface, plain C. Fields are only added, at the end. |
| `include/nebula_game_sdk.h` | For a plugin built with the game's own headers, tied to those exact headers. |
| `examples/one_health` | Reads and sets the player's state each frame. |
| `examples/coin_kills` | An event changes what the game does. |
| `examples/boss_music` | Starts and stops a track of the mod's. |
| `examples/movie_start` | Plays a movie in a level and stops it. |
| `examples/cutscene_log` | Says each cutscene's name as it starts and ends. |
| `examples/cube` | A new object with the stable interface and a model. |
| `examples/pulse` | A new object with the game's headers, and a function hook. |
| `examples/selftest` | Calls everything in the interface and says what came back. |
| `examples/gecko/<code>` | Popular cheat codes done through the interface: moon jump, infinite health and lives, star bits, low gravity, power-ups anywhere or lasting, rainbow, freeze timer, free camera, wide fov, hide HUD, no fall death, slow motion and frame advance, position display, freeze enemies, warp anywhere. LumaLink names the one that does a code it reads. |

A plugin with only the stable interface builds with clang and nothing else:

```console
clang --target=x86_64-pc-windows-msvc -O2 -shared -fuse-ld=lld -nostdlib -Wl,/NOENTRY -I native/include -o my.dll my.c
```

A plugin with the game's headers links against the `smg_game.lib` of the build it's for. The game leaves out one built against other headers and says why.
