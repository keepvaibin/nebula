# Architecture

Nebula is an install-time static recompiler for the North American
release of Super Mario Galaxy (`RMGE01`). It is not an emulator, a JIT, or a
Wii loader. Runtime code must never decode or compile PowerPC instructions.

## Install-Time Half

The Rust workspace validates a user's extracted game dump and emits native
C++20 source for a Windows x64 game module. Unsupported input is a fatal
installation error.

Important crates:

- `nebula-recomp-cli`: command-line entry point and installer-facing commands.
- `nebula-recomp-core`: DOL/FST parsing, RMGE01 validation, function metadata,
  audits, and PowerPC-to-C++ lowering.

## Runtime Half

The C++ runtime loads the generated module and supplies native services for
guest memory, callbacks, graphics, audio, input, timing, traps, and diagnostics.
The generated module talks to the host through `runtime/include/galaxy/native_api.h`.

Normal RMGE01 execution holds the product-owned
`Local\Nebula.RMGE01.NativeRuntime` Windows mutex for process lifetime.
A second runtime fails before loading the game module; launchers never discover,
focus, close, or terminate processes by executable name.

Host render/runtime settings are written to flushed sibling temporaries and
atomically renamed over their destinations. A failed in-game save retains its
dirty/error state for retry rather than truncating the prior configuration.

`kNativeAbiVersion` gates the ABI. Bump it whenever `PpcContext`,
`NativeServicesV1`, `GuestMemoryV1`, `GameModuleManifestV1`, or exported module
contracts change.

## Non-Negotiable Boundary

The opt-in RMGE01 layout geometry candidate is emitted at exact, validated
LayoutManager instruction boundaries. Its hand-written native policy extends
identified backings before matrix calculation and offsets identified HUD branch
matrices afterward, before hit/reference consumers. Local animations, EFB/copy
coordinates and presentation/input rectangles retain their existing ownership.
NW4HBM Home geometry uses its separately verified Pane name/alpha ABI and
relocated sidecar fault addresses. A primary-scene draw marker carries the safety
surround through the FIFO chunk and selected XFB; presentation never consults a
later producer state to color an earlier frame. The marker executes on cached
native dispatch as well as the first lookup. Its color follows the identified
PicBG alpha and LogoFader's original byte-quantized fade, without launch timers.
See `ASPECT_LAYOUT_MILESTONE.md` for acceptance limits and experimental status.

Unknown instructions, branch targets, relocations, FIFO producers, guest memory
ranges, and required services must hard-fail. A fallback that silently skips,
stubs, interprets, or approximates unsupported behavior is a correctness bug.

Generated and dump-derived outputs belong under ignored local directories, not
in source control.

## Installation pipeline

`Nebula-Setup.exe` runs the same steps a developer can run by hand:

1. `nebula-recomp identify` checks the game ID, disc revision and exact
   `main.dol` SHA-1 before anything else happens.
2. `nebula-recomp extract` (ISO/RVZ/WIA/WBFS) writes the game partition in the
   Dolphin "Extract Entire Disc" layout; extracted folders are used as is.
3. `nebula-recomp package-content` writes the sparse `game.pak` and the few
   loose partition files the runtime opens.
4. `nebula-recomp generate` emits the sharded game module (41,941 of 41,941
   functions, 329,871 callable entries, 1,350,000 instructions, none
   excluded), the Home Button sidecar, the lowered AX DSP program and the
   deterministic `RMGE01_boot_image.bin`. It records `generation.json`.
5. CMake and Ninja build `NebulaRuntime.exe` (which links the generated DSP
   program), `RMGE01_game.dll` and `RMGE01_home_button.dll` with the pinned
   MSVC toolchain, Release, SSE2, PCH on, LTCG off.
6. Setup checks the module exports, writes `install.json` with every file
   hash and the compatibility keys, and switches `current.json` atomically.

The install-time boot image has a versioned fixed-width header, canonical
section table, initialized-section payloads, and BSS clear range. Generation
starts from an exact RMGE01 identity check. Both its self-digest (calculated
with the digest field zeroed) and complete-file SHA-256 are fixed for the one
supported DOL. The runtime accepts only that exact format and identity, clears
BSS before copying payload sections, and cross-checks the image entry point and
DOL identity with the native module and RSO sidecar. There is no DOL parser or
DOL loader in the runtime and no fallback to `sys/main.dol`.
