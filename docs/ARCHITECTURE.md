# Architecture

Nebula runs the North American release of Super Mario Galaxy (`RMGE01`, revision 0) as native code. The game's code
is the Petari decompilation compiled for the PC; the user's disc supplies only the game's files. Nothing translates
or interprets PowerPC instructions at any point.

## Repository layout

| Path | Contents |
|---|---|
| `native/petari` | The game: Petari's game code, JSystem, NW4R and RVLFaceLib (a fork, see [PROVENANCE.md](PROVENANCE.md)) |
| `native/sdk`, `native/msl` | The native Wii SDK the game runs on (OS, GX, VI, DVD, NAND, WPAD, AI, DSP, THP) and the MSL C interface |
| `native/host` | `NebulaNative.exe`'s entry point: loads the game module and connects it to the services below |
| `native/include/nebula_game_api.h` | The C interface between the game module and the host |
| `runtime/src`, `runtime/include/galaxy` | Host services: audio, input and controllers, settings, saves, mods, Mii import, crash reports |
| `runtime/src/gx`, `runtime/include/galaxy/gx` | The GX graphics backend: command parsing, Direct3D 12 and Vulkan renderers, upscaling, interpolation |
| `platform/sdl`, `platform/android` | The SDL3 host (Linux, Android) and the Android app |
| `crates/nebula-import` | Disc image reading and `game.pak` packing (Setup and the Android app) |
| `installer/src` | Setup (`Setup/`), the launcher (`Launcher/`), shared code (`Common/`) |
| `third_party` | Vendored code; see [PROVENANCE.md](PROVENANCE.md) |
| `tools` | Repository audit, build environment and diagnostics scripts |

## Game module

`smg_game.dll` holds the game's code, compiled with clang and the game's own C++ library (MSL), and the native SDK
under it. It is a separate module from the host because MSL's `std::` templates and the host's would otherwise
replace one another. [NATIVE_PORT.md](NATIVE_PORT.md) records how the decompilation was made to run natively: the
compiler flags, byte order, 64-bit pointers in 32-bit data, the console's memory map and every audit.

- **Threads**: the game's threads run on one host thread with the SDK's own priority scheduler; a thread switch is
  a stack switch. Parallelism lives in the host: renderer, audio, input, disc.
- **Memory**: one arena in the console's physical layout. The GPU's addresses are the console's physical addresses.
- **Graphics**: the game's GX calls write the console's command stream, which the host's GX backend parses and
  draws ([GRAPHICS_ARCHITECTURE.md](GRAPHICS_ARCHITECTURE.md)).
- **Audio**: JAudio drives the DSP; the host runs the DSP program's mixing natively and plays the output.

## Host

`NebulaNative.exe` gives the module the console's services through `NebulaHostServices` and calls back into it
through the `nebula_game_*` entry points:

- **Video timing**: a 60 Hz retrace drives the game's frames.
- **Graphics** (`runtime/src/gx`): Direct3D 12 (or Vulkan in `NebulaNativeVk.exe`), internal resolution up to 16x,
  SMAA, FXAA, MSAA, FSR 3, DLSS, frame interpolation, texture packs and HD movie replacement.
- **Input**: keyboard, mouse and controllers mapped to one Wii Remote with a Nunchuk ([KEYBOARD_CONTROLS.md](KEYBOARD_CONTROLS.md)).
- **Storage**: disc reads from `game.pak`, saves in a folder laid out like the Wii's internal memory.
- **Mods**: Riivolution patches, replaced disc files, texture packs and movies from the `mods` folder.

## Installation

`Nebula-Setup.exe` checks the disc image (game ID, `main.dol` SHA-1), packs its files into `game.pak` with
`nebula-import`, installs the prebuilt host and game module, and records every installed file's hash in
`install.json`. Updates are checked against an RSA signature before they run. Files derived from the user's game
stay on the user's PC; see [CONTENT_POLICY.md](CONTENT_POLICY.md).
