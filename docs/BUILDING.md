# Building Nebula from source

`Nebula-Setup.exe` installs a prebuilt Nebula. Build by hand to develop Nebula or to check a release.

## Tools

- Visual Studio 2022 C++ tools and the Windows SDK, for the host.
- LLVM/clang 19 or newer (`C:\Program Files\LLVM`), for the game module.
- CMake 3.24+ and Ninja.
- Rust stable (MSVC target), for the disc import tool.
- The .NET 8 SDK, for Setup and the launcher.

In PowerShell, dot-source `tools\enter_msvc_environment.ps1` first. CMake downloads the remaining dependencies at
configure time from pinned archives checked by SHA-256: the FidelityFX SDK (FSR 3), and for the Vulkan renderer
Vulkan-Headers, volk, VulkanMemoryAllocator, glslang, SPIRV-Tools and SPIRV-Headers.

## 1. Game files

```powershell
cargo build --release --locked -p nebula-import
.\target\release\nebula-import.exe identify D:\Games\SMG.rvz      # must report RMGE01
.\target\release\nebula-import.exe import D:\Games\SMG.rvz --output D:\nebula\content
```

An extracted folder works in place of the disc image. The content folder holds `game.pak` and
`files\LayoutData\FileSelect.arc`.

## 2. Host

```powershell
cmake -S . -B build\host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build\host --target NebulaNative
```

`-DNEBULA_VULKAN=ON` also builds `NebulaNativeVk.exe`; `-DNEBULA_FSR3=OFF` leaves out FSR 3. DLSS uses NVIDIA's
signed Streamline libraries, which the host loads at run time from the vendor folder.

## 3. Game module

```powershell
.\native\build.ps1 -Petari native\petari -Build build\game
```

Builds `build\game\smg_game.dll` with clang and Ninja.

## 4. Run

Put `NebulaNative.exe` and `smg_game.dll` in one folder, then:

```powershell
.\NebulaNative.exe --pak D:\nebula\content\game.pak --nand "$env:LOCALAPPDATA\Nebula\saves"
```

## Tests

```powershell
ctest --test-dir build\host --output-on-failure
cargo test -p nebula-import
sh native/sdk/os/tests/run_tests.sh
.\tools\audit_repo.ps1
```

## Setup

```powershell
.\installer\build-native-setup.ps1 -Version <version> -Bin <folder with NebulaNative.exe and smg_game.dll>
```

Builds the disc import tool, the launcher and Setup, and writes `Nebula-Setup.exe` with the host and game module
inside it.
