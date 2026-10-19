# Android

`platform/android` is the app: a launcher (Kotlin, Jetpack Compose) that sets the game up from the
player's disc image and starts it, and SDL's activity running the native game.

| Part | Source | Library |
| --- | --- | --- |
| Launcher, setup, settings | `platform/android/app/src/main/kotlin` | — |
| Game activity, touch controls, HOME Menu | `platform/android/app/src/main/java` | — |
| Setup's importer (disc image to `game.pak`, one pass) | `crates/nebula-import` | `libnebula_import.so` |
| Host, Vulkan renderer, SDL input and sound | `platform/sdl`, `native/host/host_main.cpp` | `libmain.so`, `libSDL3.so` |
| The game (Petari) | `native/` | `libsmg_game.so` |

## Build

```
platform/android/build_apk.ps1 -Deps <deps> -Work <build folder> [-SeedTool <vk_shader_seed.exe>] [-Signing <signing.properties>]
```

`-SeedTool` adds SPIR-V for the shipped pipeline keys; the phone uses it only if its compiler identity matches
(docs/VULKAN.md, "Shipped pipelines"), so release builds leave it out.

`<deps>` holds `SDL3-3.4.18`, `glslang-src` (with `External/spirv-tools`), `Vulkan-Headers-vulkan-sdk-1.4.350.1`,
`volk-vulkan-sdk-1.4.350.1` and `VulkanMemoryAllocator-v3.4.0`. Needs NDK 28.2.13676358, SDK platform 36, the Rust
targets `aarch64-linux-android` and `x86_64-linux-android`, and JDK 17 or later (Android Studio's is used when
`JAVA_HOME` is unset). Without `-Signing` the APK has the debug key. An update installs over the app only when it
has the same key as the installed one.

## On the device

The app's private storage (`NEBULA_DATA_DIR` for the native game):

| Path | |
| --- | --- |
| `game/game.pak`, `game/install.properties` | the game, written by setup; the second last, so its presence means a complete game |
| `nand/` | saves and the Mii database |
| `config.ini` | renderer settings (`runtime/src/gx/render_config.cpp`); the launcher writes its keys only |
| `logs/game.log`, `logs/game.previous.log` | the game's output, this run and the one before |
| `last_error.txt` | a fatal error's message, shown by the launcher |

Uninstalling asks whether to keep this data (`hasFragileUserData`).

## Runtime

- The game activity has its own process (`:game`), which ends with the game: the native game starts once per
  process.
- Every touch reaches the touch controls. A finger that lands on a control presses it and may slide to another; the
  first finger on the left of the picture is a floating stick; any other finger is the pointer
  (`GameActivity.nativePointer`). With a controller in use they hide (Automatic), and come back at a touch.
- Back opens the HOME Menu, as the HOME button does.
- Out of sight, the host stops the display and audio clocks, so the game waits at its next retrace
  (`SDL_HINT_ANDROID_BLOCK_ON_PAUSE` is off: SDL's thread keeps pumping events).
- When Android takes the window's surface away (the app leaving the screen, a fold moving it to the other display),
  `surfaceDestroyed` returns once no present uses it; the renderer drops its swap chain and surface and makes new
  ones when the surface is back (`host_window.h`, `surface_withdrawn`).
- The picture is pre-rotated: the swap chain keeps the display's natural orientation and the present pass turns
  the picture into it, so the compositor does no rotation of its own.
- Every GPU and driver: docs/VULKAN.md, "Driver compatibility". Before a release, `vk_profile_matrix.sh` runs every
  Android GPU profile under validation on the PC.
- The surface asks for 60 frames a second (`ANativeWindow_setFrameRate`). The game's retrace runs on the display's
  vsync grid, predicted from Choreographer's vsync times (every second vsync at 120 Hz, `display_clock.cpp`), so a
  late callback never delays it; the host's clock takes over when the display gives no vsync or runs at a rate 60
  does not divide. Each frame is presented for the retrace slot after its retrace (`VK_GOOGLE_display_timing`), so
  the compositor shows it for exactly one retrace period.
- The game and GX render threads are a performance-hint session (ADPF, Android 13+): each frame's work (the busier
  thread's CPU time) is reported against 16.7 ms, so the CPU clocks rise before a frame runs late.
- The log reports every 10 s: `[pacing]` (retraces on the display's vsync, frame work), `[present-timing]` (frames
  the display showed at their slot, and one retrace after the frame before), `[vk-cpu]` (the render thread's CPU and
  the driver calls' share), `[host-events]` (the main thread).
- Profiling over adb: the app is `profileable` (simpleperf, Android Studio), and
  `adb shell setprop debug.nebula.profile <seconds>` makes the next launch sample its busy threads that many seconds
  in and log where they are (`thread_sampler.cpp`; offsets for `llvm-symbolizer` with the build's unstripped
  libraries).
