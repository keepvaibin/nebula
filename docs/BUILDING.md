# Building Nebula from source

`Nebula-Setup.exe` automates everything below. Build by hand to develop
Nebula or to verify a release.

## Tools

- Rust stable (1.74 or newer, MSVC target) for `nebula-recomp`.
- MSVC 14.44 x64 and Windows SDK 10.0.26100 (Visual Studio 2022 or Build
  Tools), CMake 3.24+ and Ninja. Setup pins MSVC 14.44.35207, SDK 10.0.26100,
  CMake 3.31.6 and Ninja 1.12.1 (`installer/toolchain.json`).
- Git, and the in-box .NET Framework 4.8 C# compiler for the installer.

Open a "x64 Native Tools" prompt, or dot-source
`tools\enter_msvc_environment.ps1` in PowerShell.

## 1. Recompiler

```powershell
cargo build --release --locked -p nebula-recomp
$recomp = ".\target\release\nebula-recomp.exe"
```

## 2. Game inputs (from your own copy)

```powershell
& $recomp identify D:\Games\SMG.rvz              # must report RMGE01 revision 0
& $recomp extract  D:\Games\SMG.rvz --output D:\nebula\disc
& $recomp package-content D:\nebula\disc --output D:\nebula\content
& $recomp generate D:\nebula\disc --output D:\nebula\generated
```

An extracted folder (`...\DATA\sys\main.dol`) can be passed instead of
`D:\nebula\disc`. `generate` writes `game\`, `home\`, `dsp\rmge01_dsp.cpp`,
`RMGE01_boot_image.bin` and `generation.json`. These are derived from the game
and must stay on your machine.

## 3. Native build

```powershell
$src = (Get-Location).Path
cmake -S . -B D:\nebula\build\runtime -G "Ninja Multi-Config" -DGALAXY_NATIVE_ISA=SSE2 `
  -DNEBULA_GENERATED_DSP_SOURCE=D:/nebula/generated/dsp/rmge01_dsp.cpp
cmake --build D:\nebula\build\runtime --config Release --target NebulaRuntime galaxy_ppc_float galaxy_softfloat

$common = "-DGALAXY_RUNTIME_INCLUDE=$src/runtime/include",
  "-DGALAXY_PPC_FLOAT_LIBRARY=D:/nebula/build/runtime/Release/galaxy_ppc_float.lib",
  "-DGALAXY_SOFTFLOAT_LIBRARY=D:/nebula/build/runtime/Release/galaxy_softfloat.lib",
  "-DGALAXY_NATIVE_ISA=SSE2"
cmake -S D:\nebula\generated\game -B D:\nebula\build\game -G "Ninja Multi-Config" @common -DGALAXY_MODULE_COMPILE_JOBS=8
cmake --build D:\nebula\build\game --config Release
cmake -S D:\nebula\generated\home -B D:\nebula\build\home -G "Ninja Multi-Config" @common
cmake --build D:\nebula\build\home --config Release
```

Limit `GALAXY_MODULE_COMPILE_JOBS` to about (RAM in GB − 4) / 2.5; compiling one
game shard can use up to about 2.4 GB. The Home module is one file that needs
about 13 GB.

## 4. Run

Copy `NebulaRuntime.exe`, `RMGE01_game.dll`, `RMGE01_home_button.dll` and
`RMGE01_boot_image.bin` into one folder, then:

```powershell
$env:GALAXY_NAND_ROOT = "$env:LOCALAPPDATA\Nebula\saves"
.\NebulaRuntime.exe D:\nebula\content .\RMGE01_game.dll
```

The launcher sets the full qualified environment from
`installer/runtime-env.json` plus the display settings; see
`installer/src/Launcher/Launcher.cs`.

## Tests

```powershell
cargo test --workspace
cargo clippy --workspace --all-targets -- -D warnings
cmake --build D:\nebula\build\runtime --config Release   # all test targets
ctest --test-dir D:\nebula\build\runtime -C Release --output-on-failure
.\tools\audit_repo.ps1                                   # content policy
```

## Installer

```powershell
.\installer\build-release.ps1 -Version 0.1.0-preview.1 -Revision <commit> `
  -SigningKey <path to the release private key>
```

It exports the revision with `git archive`, builds `nebula-recomp` from that
export with a static C runtime, compiles the launcher and setup, embeds the
per-file source manifest and writes `dist\Nebula-Setup.exe`, its `.sig`, the
source zip and `build-info.json`. `--source <zip>` lets a locally built setup
use that zip instead of downloading from GitHub, for testing before a push.

`installer/tools/pin-toolchain.ps1` regenerates `installer/toolchain.json`
from Microsoft's Visual Studio 2022 release manifest. Changing the toolchain
changes the module compatibility key, so updates rebuild the game modules.

## Regeneration compatibility

Fresh generation from a supported dump reproduces the qualified release
module sources byte for byte (ignoring line endings). Compiled binaries are
not claimed to be bit-for-bit reproducible across machines.
