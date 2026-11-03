# The native game

The game runs as native code. The user's disc supplies only the game's files, which the importer turns into the store once.

## Modules

- **Game module** (`native/petari`, a shared library): built with clang, with the flags below.
- **Host** (`native/host`, `runtime`): renderer, audio, input, files, settings and window.
- Between the two sits a C function table (`native/include/nebula_game_api.h`). They are separate modules so the game's `operator new` stays out of the host: a DLL's own definition on Windows, hidden visibility on ELF and Mach-O.

## Build of the game code

clang on every host, with the same flags everywhere:

| flag | why |
|---|---|
| `-std=gnu++17`, C `-std=gnu11` | C++20 adds rewritten comparison candidates that can change which `operator==` runs. |
| force-included `nebula_prelude.h` | Text is UTF-16, `char16_t` (`TextString.hpp`). |
| `-fno-strict-aliasing` | Some code still views objects through pointer casts. |
| `-fno-math-errno` | Nothing reads `errno`; `sqrtf` is one instruction. |
| `-fno-stack-protector` | Pinned off for every toolchain, so the module performs the same everywhere. |
| `-fno-strict-float-cast-overflow` | Float-to-int casts saturate. |
| `-fno-rtti -fno-exceptions -fno-sized-deallocation -fno-aligned-allocation` | `NebulaArena`'s `operator new` and `delete` take every allocation; nothing uses `dynamic_cast` or `typeid`. |
| `-fno-ms-compatibility -fno-delayed-template-parsing`; `-Wno-everything`, then pointer/int truncation, return type and prototype checks as errors | The same code compiles on every host. |
| `-ffp-contract=off` | The same results on every CPU, with or without FMA. |

The base types are the same fundamental types on every host (`types.h`): `s32` is `int`, `u32` is `unsigned int`, `s64` is `long long`.

## Data

- Everything is host order. The importer turns the game's files once (`crates/nebula-import/src/cook`): models, particles, collision, placement, text, layouts, audio, ghosts. Nothing swaps at access.
- Offsets in loaded files hold the target's distance from the slot.
- Where code reads a bitfield's word, the declaration lists the fields from the least significant bit up, with explicit padding.
- Saves are written in host order. An older save reads through `SaveBinary` and is written back in host order.

## Memory

- The game's memory is arenas (`NebulaArena`): each has its own 4 GB of addresses from the host and commits memory as it grows, 1 MB at a time. No sizes are given, and only the machine's memory limits them.
- The store has no archives: a folder's files lie side by side (`store.rs`), and the game mounts it as one view of those bytes (`AssetFolder`).
- The store is mapped read-only for the process's life (`AssetStore::map`). Waves and streams play from the mapping. A file the game loads whole, from 8 KB up, is a private view of the store.
- Images and shapes reach the renderer as records; an arena's going tells the renderer to forget what lay in it (`shape_forget`).
- Each game thread is a host thread with its own stack, guarded by the OS.

## Platform boundary

- The game's objects reference only the C and C++ runtimes and the host's services (`tools/progress/census.py`, `native/tools/boundary.py`).
- **Drawing**: typed records (docs/GRAPHICS_ARCHITECTURE.md). Rectangles count `NEBULA_FRAME_UNITS` across a frame or target; `J3DNativeView` and `J3DNativeTarget` turn the game's screen units into them.
- **Depth and brightness questions**: screen probes. The game names its points, the renderer reads those pixels where the drawing has reached, and the answers come back a frame later.
- **Audio**: voices and effect lines are records the host's mixer reads (`voices_setup`, `voices_mix`, `voices_frame`).
- **Frame timing**: the game waits for its frame on the host's clock (`frame_wait`, `Game/System/NebulaFrameClock`).
- **Controls**: the game reads typed input samples (`input_read`, `NebulaInputSample`).
- **HOME Menu**: the game's entry points open Nebula's panel over the frame.
- **Power**: the game asks the host to close or start again (`app_exit`, `app_restart`).
- **Movies**: the importer turns movies into frames the host decodes (`movie_video_decode`), with PCM16 sound.
- **Display shape**: the host latches the output's shape once the frame is presented (`display_extent`), and the game draws for it: 4:3 to 32:9.
- **Crash reports**: a fatal exception or the game's fatal call writes a minidump from a thread kept for it, and logs the module offset.
- **Threads**: every game thread is a host thread (`Game/System/GameThreads`). One at a time changes the game world, under the world lock.
