# Building Nebula from source

`Nebula-Setup.exe` installs a prebuilt Nebula. Build by hand to develop Nebula or to check a release.

## Tools

- Visual Studio 2022 C++ tools and the Windows SDK, for the host.
- LLVM/clang 21 or newer (`C:\Program Files\LLVM`), for the game module: its row generator calls libclang's
  `clang_getFullyQualifiedName`. Configure stops with the version it found when the libclang is older.
- CMake 3.24+ and Ninja.
- Rust stable (MSVC target), for the disc import tool.
- The .NET 8 SDK, for Setup and the launcher.

In PowerShell, dot-source `tools\enter_msvc_environment.ps1` first. CMake downloads the remaining dependencies at
configure time from pinned archives checked by SHA-256: the FidelityFX SDK (FSR 3), and for the Vulkan renderer
Vulkan-Headers, volk, VulkanMemoryAllocator, glslang, SPIRV-Tools and SPIRV-Headers, and the DirectX Shader Compiler's
release (a build tool: it compiles the ray tracing shaders to SPIR-V; `GALAXY_DXC_SPIRV` names another dxc that can).

## 1. Game files

```powershell
cargo build --release --locked -p nebula-import
.\target\release\nebula-import.exe --key common.key identify D:\Games\SMG.rvz      # must report RMGE01, RMGP01 or RMGJ01
.\target\release\nebula-import.exe --key common.key import D:\Games\SMG.rvz --output D:\nebula\content
```

An extracted folder works in place of the disc image; `--features folders-only` builds an importer that takes folders only. The content folder holds `game.pak`.

## 2. Host

```powershell
cmake -S . -B build\host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build\host --target NebulaNative
```

`-DNEBULA_VULKAN=ON` also builds `NebulaNativeVk.exe`; `-DNEBULA_FSR3=OFF` leaves out FSR 3. DLSS uses NVIDIA's
signed Streamline libraries, which the host loads at run time from the vendor folder.

The host is built for AVX2. For CPUs without it, build it again in a second folder:

```powershell
cmake -S . -B build\host-v2 -G Ninja -DCMAKE_BUILD_TYPE=Release -DNEBULA_HOST_BASELINE=ON
cmake --build build\host-v2 --target NebulaNative
```

This gives `NebulaNative-x86-64-v2.exe` (SSE4.2), which goes beside `NebulaNative.exe`. `NebulaNative.exe` starts it
on a CPU without AVX2.

## 3. Game module

```powershell
.\native\build.ps1 -Petari native\petari -Build build\game
```

Builds `build\game\smg_game.dll` with clang and Ninja, and `build\game\x86-64-v2\smg_game.dll` for CPUs without
AVX2 (`-NoBaseline` leaves it out).

A build for players adds `-Pgo`: ThinLTO with the profile in `native/pgo` (`tools/verify/pgo_profile.sh` makes it). It takes longer to link and spends about 17 % less CPU on the game.

## 4. Run

Put `NebulaNative.exe` and `smg_game.dll` in one folder, then:

```powershell
.\NebulaNative.exe --pak D:\nebula\content\game.pak --saves "$env:LOCALAPPDATA\Nebula\saves"
```

The other arguments are in COMMAND_LINE.md.

## Linux

The game module builds from the same project with LLVM 21 or newer; an older clang (Ubuntu 24.04's is 18) stops at
configure:

```sh
cmake -S native -B build/game -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=<llvm>/bin/clang -DCMAKE_CXX_COMPILER=<llvm>/bin/clang++
cmake --build build/game
```

It writes `libsmg_game.so` and `libsmg_game_plain.so`. The SDL host (`platform/sdl`) takes `-DNEBULA_HOST_TOOLS=<folder>`,
a folder with `galaxy_native_spirv` and `galaxy_utility_spirv` built for the build machine.

## Tests

```powershell
ctest --test-dir build\host --output-on-failure
cargo test -p nebula-import
sh native/tests/run_tests.sh
.\tools\audit_repo.ps1
```

## Setup

```powershell
.\installer\build-native-setup.ps1 -Version <version> -Dxc <unpacked DirectX Shader Compiler release> -Bin <folder with both host programs, smg_game.dll and x86-64-v2\smg_game.dll>
```

Builds the disc import tool, the launcher and Setup, and writes `Nebula-Setup.exe` with the host and game module
inside it.
