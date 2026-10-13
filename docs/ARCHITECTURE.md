# Architecture

Nebula is an install-time static recompiler for the North American release of Super Mario Galaxy
(`RMGE01`, revision 0). The game's PowerPC code is translated to C++ once, during installation,
and compiled into a native Windows module. The runtime never decodes or interprets PowerPC
instructions.

## Repository layout

| Path | Contents |
|---|---|
| `crates/nebula-recomp-core` | Disc and DOL parsing, RMGE01 validation, function metadata, PowerPC-to-C++ and DSP-to-C++ translation |
| `crates/nebula-recomp-cli` | `nebula-recomp`: `identify`, `extract`, `package-content`, `generate` and related commands |
| `runtime/include/galaxy`, `runtime/src` | The runtime: guest memory, hardware devices, audio, input, settings, scheduling |
| `runtime/include/galaxy/gx`, `runtime/src/gx` | The GX graphics backend on Direct3D 12 |
| `installer/src` | Setup (`Setup/`), the launcher and settings app (`Launcher/`), shared code (`Common/`) |
| `metadata/rmge01` | Function boundaries of the supported executable (addresses and sizes) |
| `third_party` | Vendored code; see [PROVENANCE.md](PROVENANCE.md) |
| `tools` | Repository audit, build environment and diagnostics scripts |

## Recompiler

`nebula-recomp generate` reads the game executable (`main.dol`), the HOME Menu module and the audio
DSP program from the user's game and emits:

- the game module source: every function of the executable (41,941 functions), sharded into C++
  files, plus a manifest of entry points;
- the HOME Menu module source;
- the DSP program translated to C++;
- `RMGE01_boot_image.bin`, the initialized data sections of the executable.

Unknown instructions, branch targets, relocations and memory ranges are errors. The recompiler
never skips, stubs or approximates code it does not understand.

Generated functions keep guest registers in host locals between calls. A call or return
synchronizes only the registers the PowerPC EABI lets the other side observe; functions that do
not follow the ABI keep full synchronization and are listed in `abi_contract.json`. Guest RAM is
accessed through one flat mapping, single-precision values convert with host instructions, and
the module is linked with the functions the game runs most placed first
(`crates/nebula-recomp-core/src/hot_functions.rs`).

## Runtime

`NebulaRuntime.exe` loads the compiled game module and supplies the hardware the game expects:

- **Guest memory**: MEM1 and MEM2 mapped at their Wii addresses, with memory-mapped hardware
  registers routed to device handlers.
- **Scheduling**: the game's threads run as virtual threads on the main simulation thread,
  switching at checkpoints the recompiler emits; interrupts (video, audio, GX, timers) are
  delivered at those checkpoints.
- **Video interface**: a 60 Hz clock drives the game's frame timing and selects the frame
  buffer to show.
- **Graphics** (`runtime/src/gx`): the GX command stream is parsed into register state and draws,
  translated to Direct3D 12 pipelines and shaders, and rendered at a selectable internal
  resolution. See [GRAPHICS_ARCHITECTURE.md](GRAPHICS_ARCHITECTURE.md).
- **Audio**: the translated DSP program and the game's audio thread run natively; mixed audio
  goes to Windows through XAudio2.
- **Input**: keyboard, mouse, XInput and Switch controllers are mapped to a virtual Wii Remote
  and Nunchuk, including the pointer, motion and vibration.
- **Storage**: disc reads come from `game.pak`; saves are written to a NAND folder.

The game module and the runtime communicate through the C ABI in
`runtime/include/galaxy/native_api.h`. `kNativeAbiVersion` identifies the ABI; it changes whenever
`PpcContext`, `NativeServicesV1`, `GuestMemoryV1`, `GameModuleManifestV1` or the module exports
change, and Setup rebuilds the game modules when it does.

Only one copy of the game runs at a time: the runtime holds the named mutex
`Local\Nebula.RMGE01.NativeRuntime` for its lifetime. Settings files are written to a temporary
file and renamed over the original.

## Installation

`Nebula-Setup.exe` runs the same steps a developer can run by hand
([BUILDING.md](BUILDING.md)):

1. `nebula-recomp identify` checks the game ID, disc revision and the SHA-1 of `main.dol`.
2. `nebula-recomp extract` reads the game partition from an ISO, RVZ, WIA or WBFS image;
   extracted folders are used as they are.
3. `nebula-recomp package-content` writes `game.pak` and the few loose files the runtime opens.
4. `nebula-recomp generate` emits the module sources and the boot image and records
   `generation.json`.
5. CMake and Ninja compile `RMGE01_game.dll`, `RMGE01_home_button.dll` and `RMGE01_dsp.dll` with
   clang-cl. `NebulaRuntime.exe` ships prebuilt in Setup.
6. Setup checks the module exports, records every installed file's hash in `install.json` and
   switches `current.json` to the new installation.

The source code Setup compiles is downloaded from this repository at a pinned revision, and every
file is checked against a SHA-256 manifest built into Setup. Toolchain downloads are checked
against pinned hashes in `installer/toolchain.json`. Updates are checked against an RSA
signature before they run.

Files derived from the user's game (the generated sources, the boot image, `game.pak` and the
compiled modules) stay on the user's PC; see [CONTENT_POLICY.md](CONTENT_POLICY.md).
