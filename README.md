# Nebula

Nebula is an unofficial native port of **Super Mario Galaxy** for Windows, Linux, macOS and Android. You need your
own copy of the game.

The latest release is [Nebula 1.0.2](https://github.com/keepvaibin/nebula/releases/latest) (Windows). This branch
(`main`) is Nebula 2.

[Download](https://github.com/keepvaibin/nebula/releases/latest) ·
[Install](docs/using-nebula/installing-nebula.md) ·
[Controls](docs/using-nebula/keyboard-and-controller-controls.md) ·
[Modding](docs/modding/index.md) ·
[Build from source](#build-from-source)

Not affiliated with or endorsed by Nintendo.

## What's in this repository

| Part | What it is | Source |
|---|---|---|
| Nebula | The game. Runs Super Mario Galaxy and includes the settings menu, mods, saves and updates. | `runtime/`, `native/`, `platform/` |
| Game module | The game code from the [Petari](https://github.com/SMGCommunity/Petari) decompilation, compiled for each platform (`smg_game.dll` on Windows). | `native/` |
| Nebula Setup | Windows installer. Installs, updates, repairs and uninstalls Nebula, or sets up a portable copy. | `installer/native/` |
| Nebula starter | The `Nebula.exe` in the install folder. Starts the installed version of the game. | `installer/native/start.cpp` |
| Game importer | Reads your disc image and packs the game files for Nebula. Setup and the Android app use it. | `crates/nebula-import` |
| Android app | Sets up the game from your disc image and runs Nebula on Android. | `platform/android/` |
| Linux package | A folder with `install.sh` that installs Nebula for the current user or as a portable copy. | `installer/package-linux.sh`, `installer/linux/` |
| macOS app | `Nebula.app`. | `platform/apple/` |
| LumaKit | The library mod that comes with the game, built from LumaLink's source. | `installer/lumakit/` |

[LumaLink](https://github.com/keepvaibin/lumalink) is a separate app for making, checking and converting mods.

## How Nebula was made

Nebula's host, renderer, installer, tests and documentation were written with AI coding agents (Anthropic's
Claude), directed, reviewed and tested by the project's author. The game's own logic, Mario's physics included, is
the Petari decompilation by SMGCommunity, compiled for the PC with the changes a 64-bit, little-endian host needs.

## Requirements

- Super Mario Galaxy (revision 0) as a disc image (ISO, RVZ, WIA, WBFS, CISO, GCZ) or extracted folder:
  North America (RMGE01), Europe (RMGP01) or Japan (RMGJ01)
- Windows 10 (1803 or later) or Windows 11, 64-bit; a CPU with SSE4.2; 8 GB of RAM; a GPU with Direct3D 12 or
  Direct3D 11
- Linux (x86-64 or ARM64) with Vulkan
- macOS 12 or later
- Android 12 or later with Vulkan

## Install

- **Windows:** run `Nebula-Setup.exe`, choose your game and select **Install**. Start Nebula from the Start menu.
  Choose **Portable (choose a folder)** to keep Nebula in one folder instead.
- **Linux:** unpack the package and run `./install.sh`.
- **macOS:** open `Nebula.app`.
- **Android:** install the app and choose your disc image.

Nebula checks GitHub for a new version at start and shows it on the home screen. **Settings > SYSTEM > UPDATES** has
**Check for updates**, and **Check at start** turns the start's check off. Updates are signed; Nebula verifies each
download before installing it. On Windows, Nebula Setup installs the update; on macOS, Nebula installs it itself; on
Linux, install the new package with `./install.sh` as before. To go back to the previous version on Windows, choose
**Restore** in Nebula Setup.

See [Installing Nebula](docs/using-nebula/installing-nebula.md) for details.

## Settings

Press **F1** on a keyboard, **View + Menu** on an Xbox controller or **Minus + Plus** on a Switch controller to open
the settings in the game. Every setting is available on every platform.

## Saves

Each release of the game keeps its save as `<code>/Nebula.sav` in the saves folder (`RMGE` North America, `RMGP`
Europe, `RMGJ` Japan). On Windows the saves folder is `%LOCALAPPDATA%\Nebula\saves`; on Linux,
`~/.local/share/Nebula/saves`.

To import or export saves, open **Settings > GENERAL > SAVES**. **Import saves** reads a `.zip` exported by Nebula, a
`Nebula.sav`, a `GameData.bin` from Dolphin, a Wii or the Nintendo Switch release, or a Wii SD card save
(`data.bin`). Choose **Replace** to confirm; Nebula moves the save it replaces to a dated backup folder beside the
saves folder and restarts. **Export saves** writes all saves to one `.zip` file where you choose.

To import a Dolphin save, right-click Super Mario Galaxy in Dolphin, choose **Open Wii Save Folder**, and import the
`GameData.bin` from that folder.

## Languages

Each disc plays in its own languages. Nebula's menus use the game's language and are available in all 14 of the
game's languages.

| Disc | Languages |
|---|---|
| North America (RMGE01) | English, French, Spanish |
| Europe (RMGP01) | English, French, German, Spanish, Italian |
| Japan (RMGJ01) | Japanese |

## Mods

A mod is a single `.luma` file that works on every platform and every supported disc. To add one, drop it on the game
window or open **Settings > MODS**. Use [LumaLink](https://github.com/keepvaibin/lumalink) to make mods and to convert
Riivolution mods and texture packs. See the [modding documentation](docs/modding/index.md).

## Build from source

Tools:

- CMake 3.24 or later, and Ninja
- LLVM 21 or later (game module)
- Rust stable with the MSVC target (game importer)
- Visual Studio 2022 C++ tools and the Windows SDK (Windows host and Setup)

In PowerShell, run `. .\tools\enter_msvc_environment.ps1` first. CMake downloads the other dependencies from pinned
archives and checks each by SHA-256.

### Import the game

```powershell
cargo build --release --locked -p nebula-import
.\target\release\nebula-import.exe --key common.key identify D:\Games\SMG.rvz
.\target\release\nebula-import.exe --key common.key import D:\Games\SMG.rvz --output D:\nebula\content
```

`identify` must report RMGE01, RMGP01 or RMGJ01. An extracted game folder also works. The content folder then holds
`game.pak`.

### Build the host

```powershell
cmake -S . -B build\host -G Ninja -DCMAKE_BUILD_TYPE=Release -DNEBULA_VULKAN=ON -DNEBULA_D3D11=ON
cmake --build build\host --target Nebula NebulaVk NebulaD11
```

This builds one program for each renderer: `Nebula.exe` (Direct3D 12), `NebulaVk.exe` (Vulkan) and `NebulaD11.exe`
(Direct3D 11). `Nebula.exe` starts the other two when **Renderer** in **Settings** or `--renderer` asks for them. The
programs are built for AVX2. For CPUs without AVX2, configure a second folder with `-DNEBULA_HOST_BASELINE=ON`. It
builds `Nebula-x86-64-v2.exe`, `NebulaVk-x86-64-v2.exe` and `NebulaD11-x86-64-v2.exe`, which go beside the others.

### Build the game module

```powershell
.\native\build.ps1 -Petari native\petari -Build build\game
```

This builds `build\game\smg_game.dll` and `build\game\x86-64-v2\smg_game.dll` (CPUs without AVX2). Add `-Pgo` for a
release build with ThinLTO and the profile in `native/pgo`.

### Build on Linux

```sh
sh tools/fetch_build_deps.sh deps
sh platform/linux/build_linux.sh -d deps -l <LLVM folder> -w build/linux
```

`build_linux.sh` builds the host (`nebula`), the game module (`libsmg_game.so` and `libsmg_game_plain.so`) and the
game importer into `build/linux/bin`, with clang from LLVM 21 or later. It also needs the development packages of
SDL's system libraries (X11, Wayland, ALSA, PulseAudio, D-Bus and udev).

### Run

Put `Nebula.exe` and `smg_game.dll` in one folder, then run:

```powershell
.\Nebula.exe --pak D:\nebula\content\game.pak --saves "$env:LOCALAPPDATA\Nebula\saves"
```

### Run the tests

```powershell
cmake -S . -B build\host -DNEBULA_EXTRA_CMAKE=tools/test_driver/test_driver.cmake
cmake --build build\host
ctest --test-dir build\host --output-on-failure
cargo test -p nebula-import
sh native/tests/run_tests.sh
.\tools\audit_repo.ps1
```

### Build the packages

- Windows: `installer\build-native-setup.ps1` builds `Nebula-Setup.exe` and the Nebula starter.
- Android: `platform\android\build_apk.ps1` builds the app.
- Linux: `installer/package-linux.sh --bin build/linux/bin --version <version> --shipped <folder>` builds
  `Nebula-Linux-<arch>.tar.gz`.
- macOS: `platform/apple/build_macos.sh` builds `Nebula.app` and `Nebula-macOS.zip`.

Each script lists its arguments at the top. Each takes a folder holding `lumakit.luma` (`-ShippedMods`, `--shipped`
or `-m`): copy `installer/lumakit/lumakit.luma` there. `tools/build_lumakit.py` builds LumaKit from LumaLink's
`lumakit` folder, at the LumaLink version in `installer/lumakit/lumalink-version.txt`, and checks that the file in
`installer/lumakit` is that build.

### Make a release

1. Write the release notes in `.github/release-notes/<version>.md`.
2. Push the tag `v<version>`. The [release workflow](.github/workflows/release.yml) builds every package into a draft
   release. To build the packages without a release, run the workflow with **dry run**.
3. On the PC with the release keys, run `installer\sign-release.ps1 -Tag v<version> -AndroidSigning <file>`. It signs
   the Android app and adds the signatures that the updates check.
4. Publish the draft release.

## Known issues

- Heavy background CPU load can make the audio skip.
- HD replacement movies at 4K need hardware video decoding.

Report problems on the [issue tracker](https://github.com/keepvaibin/nebula/issues). If Nebula closes unexpectedly,
attach the crash report from `%LOCALAPPDATA%\Nebula\crashes`.

## License

Nebula is licensed under the GNU General Public License v3.0 only ([LICENSE](LICENSE)). Third-party code and its
licenses are listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

Nebula builds on the Petari decompilation (SMGCommunity), Aurora, Dusklight, SMAA, AMD FidelityFX, NVIDIA
Streamline and the `nod` disc library.

"Super Mario Galaxy", "Wii" and "Nintendo" are trademarks of Nintendo.
