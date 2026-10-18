# Vulkan backend and the other platforms (P5.4, P7.1)

The game module already builds and runs on Linux (clang 18, all 102 tour stops headless). What Linux, Android and macOS
lack is the GX renderer, which is D3D12, and the host's window, audio and input, which are Win32.

## What exists (2026-10-18)

| Part | Lines | D3D12/Win32 | Notes |
|---|---|---|---|
| fifo_parser, gx_state, shader_gen, vertex_loader, shader_keys | ~7,800 | none | API-neutral: parse the FIFO, track GX state, generate HLSL |
| gx_backend | 12,700 | 53 sites | records draws itself (PSO, descriptor tables, vertex/index views), rings, threads, events |
| renderer_d3d12 | 11,000 | throughout | EFB targets, copies, peeks, present, telemetry, capture/replay, temporal, RT hooks |
| pipeline_cache | 3,300 | 170 | FXC, PSO library, uber/specialised hybrid |
| texture_cache, efb_copy, post_process, placed_texture_allocator | ~4,500 | ~290 | |

The renderer interface GxBackend uses has 75 methods; about half are diagnostics (captures, replay, readbacks).

## Decisions

- **One renderer interface, two backends.** GxBackend's direct D3D12 recording moves behind the renderer (draw with a
  pipeline handle, texture/sampler tables, ring allocations as {pointer, buffer, offset}); RendererD3D12 stays the
  Windows default and is measured unchanged; RendererVulkan implements the same interface. Diagnostics stay D3D12-only
  at first.
- **Shaders.** shader_gen keeps emitting HLSL. Vulkan compiles it to SPIR-V with glslang's HLSL front end (BSD/Apache,
  ~3 MB, builds for Windows, Linux, macOS, Android; DXC's SPIR-V build is LLVM-sized and has no Android build).
  Register spaces map to Vulkan bindings with glslang's binding shifts. The uber shaders are compiled at build time, so
  the first Vulkan milestone needs no runtime compiler: uber-only, as the D3D12 path draws before a specialisation
  exists.
- **Platform layer: SDL3** (zlib) for the window, Vulkan surface, audio, gamepads, keyboard/mouse and the Android
  activity, on Linux, macOS and Android. Windows keeps its Win32 host.
- **Portability limits to design around** (the GPU features the D3D12 path assumes):
  - geometry shaders (wide lines and points): none on Metal/MoltenVK and optional on Android -> expand in the vertex
    shader;
  - dual-source blending: optional on Android -> single-source fallback for the blend states that read SRC1 alpha;
  - logic-op blending: absent on Metal and most Android GPUs -> shader emulation where the game uses it;
  - BC1 (CMPR textures): absent on most Android GPUs -> decode to RGBA8 there.
- **macOS** runs the Vulkan backend on MoltenVK (Apache-2.0), with the same limits as Android for geometry shaders and
  logic ops.
- **arm64** (Android, Apple silicon) needs its own context switch (`context_switch.c` is x86-64 assembly) and its own
  division semantics check (arm64 does not trap; `__nebula_divw` already gives PowerPC's results).

## Phases

1. V0: GxBackend's D3D12 sites behind RendererD3D12 methods; frames identical, CPU unchanged (A/B).
2. V1: RendererVulkan bring-up on Windows (the RTX and the iGPU run Vulkan): device, EFB targets, swapchain, clear and
   present.
3. V2: draws with the uber shaders (SPIR-V built offline), vertex/index/constant rings, textures and samplers.
4. V3: EFB copies to textures and to the XFB, peeks (pointer depth), PE completion through fences, post-process.
5. V4: specialised shaders through glslang at run time, pipeline cache on disk.
6. V5: Linux host on SDL3 (window, audio, input), run in WSL on lavapipe for correctness and on real GPUs.
7. V6: macOS (MoltenVK, arm64 context switch) and Android (NDK, APK, arm64) builds.

Each phase: pictures compared with D3D12 on the same route, CPU and GPU measured, evidence on the dashboard.
