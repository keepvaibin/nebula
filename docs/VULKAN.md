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

## Driver compatibility

Every Vulkan 1.1 GPU and driver (Android's in particular), with no per-device work after this:

- **Identity.** `device_quirks_vk.cpp` names the driver from `VK_KHR_driver_properties` (else the vendor ID) and
  decodes its version; the log's first lines give the device, driver build, workarounds and features.
- **Known driver bugs** (the GX-relevant entries of Dolphin's DriverDetails, ANGLE's vk_renderer features and Dawn's
  Vulkan toggles; each entry cites its source): Qualcomm 512.700+ takes TEV zero colour inputs from a constant;
  Qualcomm before 512.762.12 draws its clears; Mali and Adreno read back from coherent memory; Qualcomm reads back
  through a linear image (its optimal image-to-buffer copies make a temporary image each); PowerVR clears render
  passes by command (its load-op clears do nothing); one PSO thread on Android (parallel precompiles crash Android
  drivers). On every device:
  SPIR-V 1.3 (Qualcomm and ARM drivers mishandle 1.4), no maximal-reconvergence mode (Dusklight issue 2563), TEV byte
  masks per component (Mali's vector AND), D24S8 depth where present (Adreno's D32F clears), no reversed depth ranges.
- **Capabilities.** Only what the device reports is enabled; without it: single-source blending, plain writes for
  logic ops, one-pixel lines and points (no geometry shaders; the vertex shader then writes the point size), depth
  clamp only (no depth-clip control), D32F depth, nearest sampling of a depth format the device cannot filter
  linearly (Xclipse, PowerVR), CMPR decoded to RGBA8 on every device. The swap chain takes only the usages, formats
  and alpha modes the surface lists. A Vulkan 1.0 driver gets a clear message instead of a start (1.1 is required;
  every Android 12 device has it).
- **Refusals** (`pipeline_cache_vk.h`): a refused pipeline is built again without each optional feature it uses,
  then with the other TEV zero form; a feature whose removal fixed two pipelines is left out of all of them. A key
  no form of which is accepted draws with its uber pipeline, else is skipped; the game never stops for a pipeline
  (`GALAXY_STRICT_PIPELINES=1` makes it). A new driver build's first launch tests the uber pipelines of every
  variant and feature in the background. Results stay in `vk_compat.txt`, per driver build.
- **Uber shaders a driver cannot compile** (Adreno 840, 512.842: the uber vertex shader's texture coordinates by
  dynamic array index; the self-test's half diagnosis and cut-down probes found it): the uber vertex shader reads and
  writes them by switch, as Dolphin's does. A variant a driver still refuses is never tried again on it, and its keys
  are built specialised at their first draw instead of skipped.
- **Shipped pipelines.** The Android app ships the keys the whole game uses (`platform/android/vk_seed_keys.bin`, the
  PC's 102-galaxy tour), which the workers build from the first launch in the order the game needs them, then delete.
  SPIR-V on disk carries its compiler's identity (`compiler_identity`: the SPIR-V of two fixed shaders, hashed);
  a file of another compiler, and the refusals recorded with it in `vk_compat.txt`, are dropped. The same glslang
  version built elsewhere makes other SPIR-V: the Adreno 840 refused the PC build's vertex shaders that it accepts from
  the phone's own (test 3), so `vk_shader_seed` SPIR-V is used only on a matching compiler.
- **Driver crashes** inside a compile (the class Dusklight hit on Adreno 740): `compile_journal.h` maps the compiles
  in progress into a file; the next launch never builds that pipeline again.
- **Reports.** The first refusal logs the VkResult, the pipeline's state, this process's driver log (Android's
  logcat) and the shaders' HLSL. `GALAXY_VK_SHADER_DUMP=<dir>` writes every module.
- **Checks** (D:/NebulaWork/port/tools): `vk_compat_runs.sh` runs a simulated driver
  (`GALAXY_VK_SIMULATE_DRIVER=qualcomm:512.700.0`), forced refusals (`GALAXY_VK_TEST_REFUSE=dual-source,...`) and a
  forced crash (`GALAXY_VK_TEST_CRASH_AT=<n>`); `vk_profile_matrix.sh` runs the game under the validation layer with
  each Android GPU's gpuinfo.org profile (`fetch_device_profiles.py`: Adreno 610-840, Turnip, Mali-G52-G715,
  PowerVR, Xclipse, Maleoon; newest and oldest driver each); `galaxy_vk_shader_tests <keys.bin>` compiles every
  captured shader in every form.

## Android controls (the user's design, 2026-10-18)

- The touch screen is the IR canvas: a touch points the Wii Remote there (Star Bits by sweeping a finger, menus by
  tapping).
- On-screen controls: a stick (Nunchuk) on the left; A, B, spin, Z, C and pause on the right; movable and resizable.
- Motion: a shake also spins; the accelerometer drives the tilt sections, with a stick fallback.
- Controllers through SDL3 with the PC mapping; the right stick points when no touch does.

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

## State

- V0 done: GxBackend, TextureCache and EfbCopyManager are API-neutral; the window, input and settings UI are shared
  (host_window_win32.cpp, UiCanvas). D3D12 frames identical after each step.
- Shader path (M): every shader the Vulkan backend uses compiles through glslang to SPIR-V that passes the validator,
  each descriptor at its layout's binding (`galaxy_vk_shader_tests`): the uber shaders' variants, line/point geometry,
  EFB conversion, and the game's own 229 vertex and 719 pixel shaders captured over the 102-stop tour.
- V1-V4 on Windows (M): RendererVk (renderer_vk.cpp, renderer_vk_efb.cpp), PipelineCacheVk (uber stand-ins while
  workers build specialised pipelines; SPIR-V, keys and the driver cache on disk; GALAXY_GX_UBER as on D3D12) and the
  texture backend build into `NebulaNativeVk`. The title over 40 s matches D3D12 within 1/255 at presents 600, 1200,
  1800 and 2280 (0 pixels differ by more than 8); the Khronos validation layer (built from vulkan-sdk-1.4.350.1 into
  deps/vvl, `GALAXY_VULKAN_VALIDATION=1 VK_LAYER_PATH=...\vvl\bin`) reports nothing.
- What D3D12 gives implicitly and Vulkan sets explicitly: stage variables at their HLSL semantics' locations
  (`compile_hlsl`); constant-buffer matrices row-major (the D3D12 compile's `D3DCOMPILE_PACK_MATRIX_ROW_MAJOR`;
  glslang's default is the transpose, which drew every 2D layout off-screen); fragment depth clamped to the viewport's
  range while primitives are still clipped (depth clamp with VK_EXT_depth_clip_enable); destruction of released
  objects after the frame's own submission, not the next one (GXPeek submits in between).
- Next: the 102-stop tour and Battlerock against D3D12, the iGPU (`GALAXY_GPU_PREFERENCE=low`), CPU and GPU cost; then
  V5 (Linux host on SDL3).
