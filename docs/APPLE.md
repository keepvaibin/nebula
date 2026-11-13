# Apple: macOS, iOS and iPadOS

`platform/apple` builds Nebula for Apple's systems: Macs with Intel or Apple silicon, iPhones and iPads. The game is
the same module as everywhere (`native/`) and makes the same records; Metal draws them, and the host is Apple's own.

| Part | Source | In the app |
| --- | --- | --- |
| The game (Petari) | `native/` | `Contents/Frameworks/libsmg_game.dylib` |
| Host | `native/host/host_main.cpp`, `platform/apple` | `Contents/MacOS/Nebula` |
| Renderer | `runtime/src/render`, Metal in `runtime/src/render/mtl` | in the program |
| Importer | `crates/nebula-import` (its C interface, `src/c_api.rs`) | in the program |
| The programs, where the build machine has Apple's Metal compiler | `platform/apple/tools/compile_metallib.py` | `Contents/Resources/programs_2_4.metallib`, `programs_3_0.metallib` |

## State

The Apple build is native throughout: Metal draws, and the host is Apple's own frameworks (AppKit, Game Controller,
Core Haptics, Core Audio, Core Video). Nothing of it is another system's path carried over, nothing translates another
graphics API while the game runs, and the app holds no library that is not the game's or Apple's.

| Part | Today |
| --- | --- |
| Renderer (`runtime/src/render/mtl`) | Draws the game's records as the other renderers do: the scene, the post chain (SMAA, FXAA, FSR 1, RCAS), bloom, ambient occlusion, enhanced lighting and the wide scene, MSAA to 8x, SGSR 2, MetalFX (spatial everywhere it exists, temporal where the GPU has it), native frame interpolation. Checked against the Vulkan renderer picture by picture (`platform/apple/tests/compare_snapshots.py`: within one level of 255, apart from the mip bias Metal applies and MoltenVK does not) and under Metal's validation layer on a 2017 MacBook Pro's Radeon Pro 560 and Intel HD Graphics 630. |
| Mac host (`host_window_apple.mm`, `audio_device_apple.mm`, `display_clock_apple.mm`, `setup_apple.mm`) | The window (a `CAMetalLayer` view, full screen, the pointer hidden over the picture), keyboard and mouse, controllers with motion, touch surface and rumble, sound, the vsync clock, the file panel, and the first start's setup from the player's disc image. |
| Apple silicon | The universal app's Apple silicon half is compiled on an Intel Mac. It has not been run: no such Mac has been at hand. What an Apple GPU does differently is written from Apple's documentation (shared-storage textures, MetalFX's temporal scaler, the sampler limit). |
| iPhone and iPad app | Not started (a UIKit host, touch controls, the device's motion). It needs Xcode's iPhone SDK. |
| Mods, ray tracing | Not on Apple yet: the MODS page says so. A mod's programs are made on the player's machine (Shaders) but have not been run on a Mac. |

The Vulkan renderer on MoltenVK with the SDL host (`nebula` in the build tree) is kept as a development aid only: the
picture the Metal renderer's is compared against. It is not in the app.

## Toolchain

One compiler for every Apple target: LLVM's clang 19 (the game module needs it, as on every platform), with Apple's
SDK and linker. LLVM publishes Intel Mac packages up to 19.1.7.

The C++ library is LLVM's libc++, built for each target by `platform/apple/build_libcxx.sh` and linked statically
(`platform/apple/toolchain.cmake`). The system's libc++ follows the OS, so a build for macOS 12 or iOS 15 would lose
what later versions added (the host is C++20), and the archive in LLVM's package is built for the packager's OS. Both
modules keep their own copy with nothing of it exported: the system's libc++ is in the process too (the frameworks use
it), and dyld joins weak symbols of one name across images. For the same reason the game module does not export its
`operator new` and `delete` on Apple's systems (`native/CMakeLists.txt`).

clang's own runtime library (`lib/clang/19/lib/darwin/libclang_rt.osx.a`: `@available`'s check, 128-bit division) is
in each LLVM package for that package's processor only. A build for both needs the two joined with `lipo` in the
compiler's folder (the Apple silicon one is in LLVM's macOS ARM64 package).

Deployment targets: macOS 12.0, iOS and iPadOS 15.0.

## Build (macOS)

```
platform/apple/build_libcxx.sh <llvm source> <tools>/toolchain/llvm <tools>/toolchain/libcxx macos x86_64
platform/apple/build_libcxx.sh <llvm source> <tools>/toolchain/llvm <tools>/toolchain/libcxx macos arm64
platform/apple/build_macos.sh -t <tools> -w <work folder> -a "x86_64 arm64"
```

`<tools>` holds `toolchain/llvm` (LLVM 19), `toolchain/libcxx` and `deps`: the checkouts `platform/sdl` names
(`SDL3-3.4.18`, `glslang-src` with `External/spirv-tools`, `Vulkan-Headers-vulkan-sdk-1.4.350.1`,
`volk-vulkan-sdk-1.4.350.1`, `VulkanMemoryAllocator-v3.4.0`, at `cmake/vulkan_deps.cmake`'s pins), `MoltenVK` (the
1.4.2 release's `MoltenVK-all.tar`) and `SPIRV-Cross-vulkan-sdk-1.4.350.1`. CMake 3.24 or later and Ninja are on
`PATH`, and cargo with the Rust targets of the processors built for (the importer is linked into the program). The
Command Line Tools' macOS SDK is enough for a Mac build. Apple's Metal compiler comes only with Xcode: with it the build
compiles the programs into the app's Metal libraries ("Shaders"); without it the app carries them as text. `-j <n>`
limits how many compiles run at once, `-r` makes a release build.

The build machine's own processor is built first: it makes the shader tools (`galaxy_native_spirv`,
`galaxy_utility_spirv`, `programs_msl`) that the other processor's build takes from its `host-tools` folder. The two
are joined with `lipo`, and the app is signed for the machine it was built on (an ad-hoc signature). On another Mac the
app is opened the first time from System Settings, Privacy & Security ("Open Anyway"; on macOS 14 and earlier also
Control-click, Open), unless it is signed with a Developer ID and notarized. The script checks the signature over the
whole bundle (`codesign --verify --deep --strict`) and packs `Nebula-macOS.zip` with `ditto`, the app beside "How to
open Nebula.txt": `ditto` keeps the bundle as signed, where a zip tool that adds or drops files' attributes leaves a
signature that no longer holds, which macOS reports as an app that "is damaged".

## Permissions

The app asks the player for nothing. It is signed with the hardened runtime and `Nebula.entitlements`, which names
none of the things the system guards (microphone, camera, location, contacts, Apple Events), so the system refuses
them without a question. Without that, macOS asks for the microphone on the game's behalf whenever the sound goes to
a device that also has inputs (a headset, a controller's audio), though the game only plays. The rumble engine plays
haptics only. The disc image and a Mii file are read where the player points in the system's own panel, which is the
permission.

## Setting up

The game's files come from the player's own disc image, once. With no `game.pak` the app's first window asks for the
image (`setup_apple.mm`), has the importer check it and build `game.pak`, and shows how far that is; the image is only
read. A `game.pak` the game cannot open (an earlier store version after an update, or a damaged file: the same check
the game makes, `AssetStore::open` and the release's id) brings the window back with words saying so, where the game
would otherwise end at its start. Before writing, the window makes sure the drive has room (4.4 GB, the space of a
`game.pak` it replaces counted), clears what an import cut off by a power cut left, and holds an `NSProcessInfo`
activity for the import, so macOS neither slows it (App Nap, for an app whose window is behind others) nor sleeps. The
finished file is opened again as the game opens it before the game starts.

The importer reads the disc's files in order on one thread and cooks them on every core, writing each in its turn:
`game.pak` is the same, byte for byte, as one cooked a file at a time. On a 2017 MacBook Pro (4 cores) the North
American disc takes 70 s where one core took 190 s.

The app is one AppKit application from its first moment (`galaxy::apple::start_app`, host_window_apple.mm): its
delegate, menus and launch come before the setup window or the game's window, whichever is first. AppKit asks an app's
delegate early whether window state is restored with secure coding (`applicationSupportsSecureRestorableState:`), and
stops a process (SIGILL, "Secure coding for state restoration requested after it was initialized without.
NSApplicationDelegate was probably established too late.") whose answer arrives after AppKit has started without it, as
it did when the setup window ran before the game's delegate existed. No window is restored at a start
(`restorable = NO`, `NSQuitAlwaysKeepsWindows` off): the game opens its window as its settings say.

The window's controls are placed by their alignment rects (`frameForAlignmentRect:`), not their frames: a push
button's frame holds a margin around what is drawn, so frames at equal distances look uneven. Text and buttons stand 20
points from the window's edges and the buttons 12 apart, as Apple's layout guidance has them.

A command line builds `game.pak` as the window does:

```
cargo build --release --locked -p nebula-import
target/release/nebula-import import <disc image> --output "$HOME/Library/Application Support/Nebula/game"
```

## On the Mac

`~/Library/Application Support/Nebula` (`--data` names another):

| Path | |
| --- | --- |
| `game/game.pak` | the game, written by the importer |
| `saves/` | saves and the Mii database |
| `config.ini`, `runtime_settings.ini` | settings |
| `logs/game.log`, `logs/game.previous.log` | the game's output, this run and the one before, when the app was opened from the Finder; a terminal, pipe or file given to the program keeps it |
| `shadercache/` | the other renderers' pipeline files; Metal's are in the system's own cache (below) |

An app opened from the Finder has no command line: `galaxy::apple::launch_arguments` (`apple_host.mm`) names the data
folder, the game, the saves and the module from the app's own layout, and the system's language as `LANG`. A command
line's own `--data`, `--pak`, `--saves` and `--module` stay ([COMMAND_LINE.md](COMMAND_LINE.md)).

Discord's status (General, "Discord status") reaches the Discord app over its local socket, `discord-ipc-0` to `9`
in the user's temporary folder (`$TMPDIR`; `XDG_RUNTIME_DIR`, `TMP` and `TEMP` first where set), as Discord's own
libraries find it on a Mac, with the same frames as Windows' pipe (`discord_presence.cpp`). Without the Discord app
nothing is sent and the game is unaffected.

The menu centres its pictures (a setting's arrows, a controller's button letters) by their ink, read from the font's
sheets. The importer stores the game's text fonts as signed distance fields at twice their size
(`cook::distance`): the ink is measured there as the font's shader draws it, where the field crosses one half between
two texels (`NebulaUi::glyphInkBox`). Read as plain pictures at their original size, the arrows sat 4 units off their
buttons' middles (a game.pak cooked with distance fonts, any platform).

## Runtime

- The clock (`galaxy::qpc_now`) is Mach absolute time in nanoseconds, the display link's and the presents' time base.
- The game's frames follow the display's vsync where it divides the frame period (`display_clock_apple.mm`); a frame is
  shown as soon as it is drawn, at the next vsync, never held for a named time.
- A mouse is the pointer wherever it is over the picture, in menus as in play (pixels: window units times the pixel
  density, two on a Retina display).
- An audio system that does not answer within two seconds at start (a hung driver) leaves that run without sound
  rather than waiting for ever.
- The frame's threads (the game's and the renderer's) ask for the system's user-interactive class (`perf_hint.cpp`):
  their timers are not gathered with others' and, on Apple silicon, they run on the performance cores. On a 2017
  MacBook Pro a frame's wake-up came 1.7 ms after its vsync on average (6.8 at worst) without it, 0.2 ms with it. The
  threads that prepare pipelines ask for the utility class (`core/pipelines.cpp`), so they never take a core a frame
  wants: frames keep their 16.67 ms through a whole first start's compiling.
- The layer is synchronised with the display unless VSYNC is turned off (it is on by default on Apple's systems): each
  picture goes up at a refresh, and the display's own times (`--diagnostics present-stats`, `shown-for-refreshes`)
  show every one up for exactly one. A synchronised layer shows what it is handed in order and never replaces a
  waiting picture, so pictures that once queued stay queued and every later one is seen late: 48.6 ms from hand-over
  to screen with the line left full, in a window on a 2017 MacBook Pro. The layer says when the line is full (the
  frame waits for a drawable); that frame's picture is then not handed over, and the line stays one shorter: 37 ms,
  the render thread never waiting (`present`, `renderer_mtl.mm`). Two drawables instead of three would do the same
  by blocking, but one slow frame then holds every frame after it to every second refresh.
- A Mac's window has ten bits a channel (`BGR10A2`), as the scene has: the picture goes to it as it is, where an 8-bit
  window takes it rounded and dithered.
- A first start on an Intel Mac drawing with its integrated graphics, on a Retina display, draws the scene at the
  display's size in points and enlarges it with FSR 1 (the UPSCALER row; `choose_first_start_scale`): Intel HD
  Graphics 630 takes 8 ms for a frame of 1708 by 960, a quarter of a 15-inch panel's pixels. MetalFX's spatial scaler
  is slow on Intel Macs' GPUs (10.9 ms for the pass to the screen at 3360 by 2100 on a Radeon Pro 560, against FSR 1's
  6.5), so it is offered and not chosen there. A discrete GPU and every Apple GPU keep the display's own pixels: the
  Radeon Pro 560 draws the heavier galaxies at 3360 by 1856 in 7.9 ms with the GPU kept busy, every picture up for
  one refresh.
- At the target's own size, into a window that keeps the scene's ten bits, the picture is copied plainly: 0.6 ms at
  3360 by 1856 where the box filter's pass took 2.1.
- Letter keys are reported twice, as on Windows: by the letter the keyboard's layout prints on them, and by their place
  (a Mac's key codes are the hardware's), which is what the default layout binds movement to.

## Pipelines and the first start

The game registers the cook's whole pipeline list at the start (6,344 pipelines of 5,217 programs at the default
settings) and the title waits behind the game's own "Preparing graphics" screen until they are made
([GRAPHICS_ARCHITECTURE.md](GRAPHICS_ARCHITECTURE.md), "Pipelines").

Metal keeps every program and pipeline it compiles in the system's cache for the app, on disk, by program, state, GPU
and system build. That cache is the Apple build's pipeline file: the first start compiles (about a minute on a 2017
MacBook Pro when the programs are text: a tenth of a second each for Metal's compiler, then the GPU's code), and every
later start makes the whole list from the cache in about a second. A new system build or GPU compiles once again.

An archive of the app's own (`MTLBinaryArchive`) would hold the same code under the same conditions and was tried in
the cache's place. On the Radeon, pipelines that share programs and differ in blend state were refused once the file
was read back ("Failed to find linking data in binary archives", with no fall back to compiling), and threads adding
to one archive together lost entries; others report crashes inside its write on later systems. It is not used.

`--diagnostics gpu-passes` times the GPU by pass (`[metal-gpu] passes`: the frame's commands split into a command
buffer a pass, whose start and end Metal reports on every GPU) and says what the compiling has cost so far.

## Shaders

One HLSL source on every renderer. The Metal renderer's are the Vulkan renderer's SPIR-V turned to the Metal Shading
Language by SPIRV-Cross at build (`runtime/src/render/mtl/shader_msl.cpp`, `platform/apple/tools/programs_msl.cpp`):
`programs_msl.bin` has the header and rows of `programs_spirv.bin` and each stage's MSL text. Direct3D's clip space is
Metal's, so nothing is turned.

A draw's bindings, the same numbers in the vertex and the fragment function (`shader_msl.h`):

| Metal index | Vulkan binding | |
| --- | --- | --- |
| buffer 0, 1, 2 | set 0, bindings 0 to 2 | b0 (view), b1 and b2 (a material's vertex and pixel rows) |
| buffer 3, 4, 5 | set 0, bindings 3 to 5 | the composed rows, the instances, the draws' texture lists |
| buffer 6 | push constants | b3 (the draw) |
| buffer 7, 8 | sets 1 and 2 | every texture and every sampler, as argument buffers (tier 2: arrays without a length) |
| texture 0 to 15, sampler 0 to 15 | sets 1 and 2 | a draw's bound table, where the device has argument buffers tier 1 only |
| buffer 10 to 15 | vertex buffers | the vertex streams (`gpu::StreamSlot`) |

`platform/apple/tools/msl_check.mm` compiles every stage with each of the machine's GPUs.

A mod's programs (its materials' HLSL, as the .luma carries it for every renderer) are made on the player's machine
when the mod is converted, as on the other renderers: the Vulkan renderer's HLSL front end (`shader_compile_vk.cpp`
`compile_mod_stage`, linked into the Metal renderer with glslang) makes the SPIR-V, `spirv_to_msl` turns it with the
options of the game's own programs (the pixel programs in both forms), and the MSL text is stored. `function_of`
compiles it when a draw first needs it. Its revision is the front end's and SPIRV-Cross's (`mod_shader_compiler_revision`).

With Apple's Metal compiler on the build machine (`xcrun metal`: Xcode), `platform/apple/tools/compile_metallib.py`
compiles the texts there: forty-eight programs to a source file, each in a namespace of its own (their types share
names; a function is `p<number>::m`), into `programs_2_4.metallib` (MSL 2.4: the vertex programs and the pixel
programs' bound-table forms) and `programs_3_0.metallib` (MSL 3.0: the array forms, read only where the system has
Metal 3). The programs' file then holds, in place of each text, the record that names its function
(`pipelines_mtl.mm` `ProgramRecord`), and no player's machine compiles a text: a first start makes only the GPU's
code. The sources the tool writes have been compiled whole with the system's compiler (17,314 functions, every record
found by name); the tool's own compile step has not been run, for want of Xcode on the machine this was written on.

The arrays of every texture and sampler are buffers of resource ids (`gpuResourceID`, macOS 13 and iOS 16), read by
index without a length. A 2017 MacBook Pro's two GPUs (Radeon Pro 560, Intel HD Graphics 630) both draw through
them, the Radeon though it is not of the Metal 3 family; on macOS 12 and on argument buffers tier 1 a draw binds its
table of sixteen.

## Devices

What Apple's feature tables (October 2026) give each family, which the renderer asks the device for rather than
naming chips:

| | Devices | Textures a draw | MetalFX | MSAA | BC textures |
| --- | --- | --- | --- | --- | --- |
| Apple2, Apple3 | A8 to A10 | bound tables | spatial (A9 on) | 4x | no |
| Apple4, Apple5 | A11, A12 | bound tables | spatial; frame interpolation (A12, OS 26) | 4x | no |
| Apple6 | A13 | arrays | spatial, frame interpolation | 4x | no |
| Apple7, Apple8 | A14 to A16, M1, M2 | arrays | spatial, temporal, frame interpolation | 4x | every Mac; some iPads |
| Apple9 on | A17 Pro on, M3 on | arrays | all, with denoised upscaling | 4x (8x from A19 and M5) | yes |
| Mac2 | Intel Macs (Intel, AMD) | arrays where the GPU says tier 2 | spatial where the GPU says so; no temporal | 8x | yes |

An Intel Mac's temporal upscaler is the renderer's own (SGSR 2, the same shaders as on every renderer). The settings'
FSR 3, DLSS and XeSS choices are MetalFX on Apple.
