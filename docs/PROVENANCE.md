# Provenance

This file records where code in Nebula comes from. License texts and copyright notices are in
[THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).

## Repository origin

Nebula's first public commit is a source snapshot of a private project, GalaxyRecomp. Earlier
history is not published because it contained local build outputs and diagnostics that must not
be redistributed. The author is the GitHub user `keepvaibin`.

## Vendored code (`third_party/`)

| Project | License | Use |
|---|---|---|
| Berkeley SoftFloat 3e | BSD-3-Clause | Exact IEEE operations for PowerPC floating-point corner cases |
| Dolphin free DSP ROM | GPL-2.0-or-later | Replacement DSP instruction and coefficient ROMs |
| SMAA | MIT | Anti-aliasing shaders and lookup textures |
| AMD FidelityFX FSR 1 | MIT | Upscaling and sharpening shaders |

The optional FSR 3 path builds against the AMD FidelityFX SDK (MIT), which is not stored in this
repository. Rust dependencies are fetched by Cargo; their licenses are collected in
[docs/licenses/rust-dependencies.txt](licenses/rust-dependencies.txt).

## Adapted code

| Project | License | Adapted for | Nebula files |
|---|---|---|---|
| [Aurora](https://github.com/encounter/aurora) | MIT | THP movie decoding (parser, Huffman tables, IDCT) | `runtime/src/native_thp_video.cpp`, `runtime/include/galaxy/native_thp_video.h` |
| Aurora | MIT | GX blend and pipeline state canonicalization | `runtime/src/gx/shader_keys.cpp` |
| Aurora | MIT | Indirect texture stage sampling, indirect matrix scale decoding | `runtime/include/galaxy/gx/texture_sampling.h`, `runtime/src/gx/gx_backend.cpp` |
| Aurora | MIT | Content-keyed texture cache (optional) | `runtime/src/gx/texture_cache.cpp` |
| Aurora | MIT | Frame packet recording | `runtime/include/galaxy/gx/owned_fifo_packet.h`, `runtime/include/galaxy/gx/owned_fifo_event_replay.h` |
| Aurora | MIT | Presentation viewport, logical-to-physical viewport and scissor mapping | `runtime/include/galaxy/content_viewport.h`, `runtime/src/gx/render_config.cpp`, `runtime/src/gx/renderer_d3d12.cpp` |
| [Dusklight](https://github.com/TwilitRealm/dusklight) | CC0 1.0 | Button press and trigger state per game frame | `runtime/include/galaxy/synthetic_kpad_buttons.h` |
| Dusklight | CC0 1.0 | AFC ADPCM decoding, DSP voice and mix kernels (optional high-level DSP path) | `runtime/include/galaxy/native_audio_adpcm.h`, `runtime/include/galaxy/native_audio_dusk_dsp.h` |
| Dusklight | CC0 1.0 | Layout fitting and pointer focus handling | `runtime/include/galaxy/content_viewport.h`, `runtime/include/galaxy/native_input.h`, `runtime/include/galaxy/host_pointer_events.h` |
| Dusklight | CC0 1.0 | Widescreen camera and HUD scaling policy | `runtime/include/galaxy/experimental_ultrawide_aspect.h`, `crates/nebula-recomp-core/src/translate.rs` |
| Dusklight | CC0 1.0 | Movie audio queue ownership | `runtime/include/galaxy/thp_audio_worker.h` |
| Dusklight | CC0 1.0 | PC-only exclusion of the JAudio DSP safety kill (RMGE01 `0x8049452C`) | `crates/nebula-recomp-core/src/translate.rs` |
| [WiiCompiled](https://github.com/patchzyy/Wiicompiled) | GPL-3.0 | KPAD motion conversion | `runtime/include/galaxy/synthetic_kpad_motion.h` |
| WiiCompiled | GPL-3.0 | Guest ABI contracts, helper effects, basic blocks and register liveness | `crates/nebula-recomp-core/src/guest_abi_effect.rs`, `crates/nebula-recomp-core/src/guest_cfg_liveness.rs`, `crates/nebula-recomp-core/src/guest_ppc_effect.rs` |
| WiiCompiled | GPL-3.0 | Register residency at call boundaries, shared return dispatch | `crates/nebula-recomp-core/src/translate.rs` |
| WiiCompiled | GPL-3.0 | Flat guest memory mapping and reads | `runtime/src/flat_guest_memory.cpp`, `runtime/include/galaxy/flat_guest_memory.h`, `runtime/include/galaxy/flat_guest_read.h` |
| WiiCompiled | GPL-3.0 | Wii audio interface PCM byte order | `runtime/include/galaxy/native_audio_queue.h` |
| WiiCompiled (from Dolphin) | GPL-2.0-or-later | Paired-single float arithmetic (optional) | `runtime/src/ppc_float.cpp` |
| [Dolphin](https://github.com/dolphin-emu/dolphin) | GPL-2.0-or-later | Reciprocal and reciprocal square root estimate tables | `runtime/include/galaxy/ppc_estimate.h`, `runtime/src/ppc_estimate_reference_tests.h` |
| [Petari](https://github.com/SMGCommunity/Petari) | CC0 1.0 | Function boundaries (addresses and sizes only) | `metadata/rmge01/functions.csv` |
| Petari | CC0 1.0 | JAudio2 structure layouts and command formats for the native audio host | `runtime/src/jaudio_host_mixer.cpp`, `runtime/include/galaxy/native_audio_bus.h` |

GPL-2.0-or-later code is used under version 3 of the license in this GPL-3.0-only work.

Design references, with no code copied: Dolphin's native-resolution bloom graphics mod for Super
Mario Galaxy (low-resolution bloom copies) and Dolphin's scaled EFB copy filtering.

## Notes

- Dusklight and Petari are based on decompilations of Nintendo games. Their CC0 dedications
  cover their contributors' own work. Nebula uses them for interface layouts, behaviour and
  design; it does not contain decompiled game code.
- `metadata/rmge01/functions.csv` holds only function addresses and sizes for the user's own
  executable: no names, code or bytes.
- The `nod` disc library contains the Wii common keys needed to read a Wii disc. They are
  compiled into `nebula-recomp.exe`, which reads the user's own disc.
- Hardware register layouts and SDK structure layouts (GX, AX and DSP mailboxes, KPAD) are
  interface facts used for interoperability, written from public documentation.

## Not distributed

Nebula does not distribute Nintendo code or assets, disc images, extracted game files, boot
images, generated module sources or binaries, DSP microcode, saves, shader caches, logs,
Microsoft toolchain files, CMake or Ninja. Setup builds the game modules on the user's PC from
the user's own copy of the game.
