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
| SMAA | MIT | Anti-aliasing shaders and lookup textures |
| AMD FidelityFX FSR 1 | MIT | Upscaling and sharpening shaders |
| libarchive, xz, zlib | BSD-2-Clause, 0BSD, Zlib | Reading mod archives (zip, 7z, rar) |

The game core is vendored too: [Petari](https://github.com/SMGCommunity/Petari) (CC0 1.0),
SMGCommunity's decompilation of Super Mario Galaxy, in `native/petari` with Nebula's native
changes (its README has the base revision).

The FSR 3 upscaler comes from the AMD FidelityFX SDK (MIT), and the Vulkan renderer's libraries from their
upstream projects; CMake downloads both at configure time from pinned archives checked by SHA-256. Rust dependencies are fetched by Cargo; their licenses are collected in
[docs/licenses/rust-dependencies.txt](licenses/rust-dependencies.txt).

## Adapted code

| Project | License | Adapted for | Nebula files |
|---|---|---|---|
| [Aurora](https://github.com/encounter/aurora) | MIT | THP movie decoding (parser, Huffman tables, IDCT) | `runtime/src/movie_video.cpp`, `runtime/include/galaxy/movie_video.h` |
| Aurora | MIT | Blend and pipeline state canonicalization | `runtime/src/render/shader_keys.cpp` |
| Aurora | MIT | Indirect texture stage sampling, indirect matrix scale decoding | `runtime/include/galaxy/render/texture_sampling.h`, `runtime/src/render/render_core.cpp` |
| Aurora | MIT | Content-keyed texture cache (optional) | `runtime/src/render/texture_cache.cpp` |
| Aurora | MIT | Presentation viewport, logical-to-physical viewport and scissor mapping | `runtime/include/galaxy/content_viewport.h`, `runtime/src/render/render_config.cpp`, `runtime/src/render/renderer_d3d12.cpp` |
| Dusklight | CC0 1.0 | Layout fitting and pointer focus handling | `runtime/include/galaxy/content_viewport.h`, `runtime/include/galaxy/native_input.h`, `runtime/include/galaxy/host_pointer_events.h` |
| Dusklight | CC0 1.0 | Widescreen camera and HUD scaling policy | `runtime/include/galaxy/experimental_ultrawide_aspect.h` |
| Petari | CC0 1.0 | Audio engine behaviour and command formats for the native audio host | `native/petari/src/Game/Sound`, `runtime/include/galaxy/native_audio_bus.h` |

GPL-2.0-or-later code is used under version 3 of the license in this GPL-3.0-only work.

Design references, with no code copied: Dolphin's native-resolution bloom graphics mod for Super
Mario Galaxy (low-resolution bloom copies) and Dolphin's scaled EFB copy filtering.

## Notes

- Petari and Dusklight are decompilations of Nintendo games, dedicated to the public domain (CC0) by their
  contributors for their own work. Nebula's game module is compiled from Petari's source in `native/petari`;
  Dusklight is used for interface layouts, behaviour and design.
- The importer uses the `nod` disc library, which contains the Wii common keys needed to read a Wii disc partition.
  An extracted game folder needs none, and a build with the importer's `folders-only` feature reads folders only.

## Not distributed

Nebula does not distribute Nintendo assets, disc images, extracted game files, saves, shader caches or logs. Setup
installs the host and game module built from this repository and packs the game's files from the user's own disc.
