# Building Nebula from source

`Nebula-Setup.exe` does all of this. Build by hand to develop Nebula or check a release.

## Tools

- Rust stable (MSVC target), Git.
- Visual Studio 2022 C++ tools (MSVC 14.44, Windows SDK 10.0.26100) for the runtime.
- clang-cl and lld-link (LLVM 19 or newer) for the game modules. Setup uses
  llvm-mingw 20260922 (`installer/toolchain.json`).
- CMake 3.24+ and Ninja.

In PowerShell, dot-source `tools\enter_msvc_environment.ps1` first.

## 1. Recompiler and game sources

```powershell
cargo build --release --locked -p nebula-recomp
$recomp = ".\target\release\nebula-recomp.exe"
& $recomp identify D:\Games\SMG.rvz          # must report RMGE01 revision 0
& $recomp extract  D:\Games\SMG.rvz --output D:\nebula\disc
& $recomp package-content D:\nebula\disc --output D:\nebula\content
& $recomp generate D:\nebula\disc --output D:\nebula\generated
```

An extracted folder (`...\DATA\sys\main.dol`) works in place of `D:\nebula\disc`.
## 2. Runtime (MSVC)

```powershell
cmake -S . -B D:\nebula\runtime -G Ninja -DCMAKE_BUILD_TYPE=Release -DGALAXY_NATIVE_ISA=SSE2 -DNEBULA_DSP_MODULE=ON
cmake --build D:\nebula\runtime --target NebulaRuntime galaxy_ppc_float galaxy_softfloat galaxy_dsp_alu
```

`NEBULA_DSP_MODULE=ON` makes the runtime load the DSP from `RMGE01_dsp.dll`.
This is the build Setup ships.

## 3. Game, Home Menu and DSP (clang-cl)

```powershell
$rt = "D:/nebula/runtime"
cmake -S D:\nebula\generated -B D:\nebula\modules -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_LINKER=lld-link -DGALAXY_NATIVE_ISA=SSE2 `
  "-DGALAXY_RUNTIME_INCLUDE=$PWD/runtime/include" `
  "-DGALAXY_PPC_FLOAT_LIBRARY=$rt/galaxy_ppc_float.lib" `
  "-DGALAXY_SOFTFLOAT_LIBRARY=$rt/galaxy_softfloat.lib" `
  "-DGALAXY_DSP_ALU_LIBRARY=$rt/galaxy_dsp_alu.lib" -DGALAXY_MODULE_COMPILE_JOBS=16
cmake --build D:\nebula\modules
```

About 70 CPU-minutes in total; each compiler uses under 1 GB except the DSP
(about 1.6 GB) and Home (about 3.4 GB). MSVC also builds the modules but is
roughly ten times slower and needs far more memory.

## 4. Run

Put `NebulaRuntime.exe`, `RMGE01_game.dll`, `RMGE01_home_button.dll`,
`RMGE01_dsp.dll` and `RMGE01_boot_image.bin` in one folder, then:

```powershell
$env:GALAXY_NAND_ROOT = "$env:LOCALAPPDATA\Nebula\saves"
.\NebulaRuntime.exe D:\nebula\content .\RMGE01_game.dll
```

The launcher also applies `installer/runtime-env.json` and the display settings.

## Tests

```powershell
cargo test --workspace
cargo clippy --workspace --all-targets -- -D warnings
cmake -S . -B D:\nebula\tests -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DNEBULA_GENERATED_DSP_SOURCE=D:/nebula/generated/dsp/rmge01_dsp.cpp
cmake --build D:\nebula\tests
ctest --test-dir D:\nebula\tests --output-on-failure
.\tools\audit_repo.ps1
```

## Installer

```powershell
.\installer\build-release.ps1 -Version <version> -Revision <commit> -SigningKey <private key>
```

Writes `dist\Nebula-Setup.exe` (with the prebuilt runtime inside), its `.sig`,
the source zip and `build-info.json`. `Nebula-Setup.exe --source <zip>` uses a
local source zip instead of GitHub, for testing before a push.
`installer/tools/pin-toolchain.ps1` re-pins the toolchain; a new toolchain
makes updates rebuild the game modules.
