# Architecture

Nebula runs Super Mario Galaxy's releases for North America (`RMGE01`), Europe (`RMGP01`) and Japan (`RMGJ01`),
revision 0, as native code. The game's code is the Petari decompilation compiled for the PC, once for all three: where
their code differs (Petari's `VERSION` switches between them, five places), the module follows the disc's game id
(`NebulaRegion.hpp`). The user's disc supplies only the game's files.

## Repository layout

| Path | Contents |
|---|---|
| `native/petari` | The game: Petari's game code and the engine code under it (layouts, sound, models), see [PROVENANCE.md](PROVENANCE.md); its Miis are native (`Game/Mii`) |
| `native/math` | The game's math library |
| `native/host` | `NebulaNative.exe`'s entry point: loads the game module and connects it to the services below |
| `native/include/nebula_game_api.h` | The C interface between the game module and the host |
| `runtime/src`, `runtime/include/galaxy` | Host services: audio, input and controllers, settings, saves, mods, Mii import, crash reports |
| `runtime/src/render`, `runtime/include/galaxy/render` | The renderer: Direct3D 12 and Vulkan on the game's records ([GRAPHICS_ARCHITECTURE.md](GRAPHICS_ARCHITECTURE.md)) |
| `platform/sdl`, `platform/android` | The SDL3 host (Linux, Android) and the Android app |
| `platform/apple` | The Apple build: macOS, iOS and iPadOS ([APPLE.md](APPLE.md)) |
| `crates/nebula-import` | Disc image reading and `game.pak` packing (Setup and the Android app) |
| `installer/src` | Setup (`Setup/`), the launcher (`Launcher/`), shared code (`Common/`) |
| `third_party` | Vendored code; see [PROVENANCE.md](PROVENANCE.md) |
| `tools` | Repository audit, build environment and diagnostics scripts |

## Game module

`smg_game.dll` holds the game's code, compiled with clang. [NATIVE_PORT.md](NATIVE_PORT.md) records how the decompilation was made to run
natively.

- **Threads**: the game world runs on one thread, with no locks. The sound engine runs on the host's audio clock and hears
  from the game only through its command ring (`Game/Sound/SoundCommand.hpp`). The host's renderer and input threads run
  free of both.
- **Memory**: the game's heaps sit on host memory; only the machine's memory limits them.
- **Graphics**: typed records ([GRAPHICS_ARCHITECTURE.md](GRAPHICS_ARCHITECTURE.md)).
- **Audio**: the game writes each voice's settings in floats and hertz; the host's mixer plays them.

## Host

`NebulaNative.exe` gives the module its services through `NebulaHostServices` (`nebula_game_api.h`):

- **Frames**: the main loop asks for its next frame (`frame_wait`); 60 Hz logic, presentation apart.
- **Graphics**: Direct3D 12, or Vulkan in `NebulaNativeVk.exe`.
- **Input**: keyboard, mouse and controllers as the game's motion and pointer pad ([KEYBOARD_CONTROLS.md](KEYBOARD_CONTROLS.md)).
- **Storage**: assets from the store `game.pak` by name; saves through an atomic service, by name.
- **Mods**: `.luma` mods and raw mod folders in the `mods` folder. Their files, table patches and sources are cooked
  into `mods.pak`; texture packs and movies are read as they are; a mod's code is native code the LumaCore kernel
  loads into the game module ([modding/README.md](modding/README.md)).

## Installation

`Nebula-Setup.exe` checks the disc image (game ID, `main.dol` SHA-1), packs its files into `game.pak` with
`nebula-import`, installs the prebuilt host and game module, and records every installed file's hash in
`install.json`. Updates are checked against an RSA signature before they run. Files derived from the user's game
stay on the user's PC; see [CONTENT_POLICY.md](CONTENT_POLICY.md).
