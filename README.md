# Nebula

Nebula runs **Super Mario Galaxy** natively on Windows. Nebula contains no Nintendo code or assets; you need your
own copy of the game.

> **The released version is 1.0.2.** This branch (`main`) is **Nebula 2, in development and not released**: much
> of this page describes it. [What 1.0.2 has](#in-the-released-version-102) and
> [what is coming in 2.0](#in-development-for-20-not-released) are listed separately below.

[Download](https://github.com/keepvaibin/nebula/releases/latest) ·
[Controls](docs/KEYBOARD_CONTROLS.md) ·
[Building from source](docs/BUILDING.md) ·
[Architecture](docs/ARCHITECTURE.md)

Not affiliated with or endorsed by Nintendo.

## Project status

- **Released: [Nebula 1.0.2](https://github.com/keepvaibin/nebula/releases/latest).** Playable from start to finish
  with the North American disc (RMGE01). Setup translates the game's PowerPC code to C++ and compiles it on your PC.
- **In development: Nebula 2** (this branch). The game is built as native code from the
  [Petari](https://github.com/SMGCommunity/Petari) decompilation, with more discs, controllers and features (below).
  It is not released; you can build it from source. Bugs remain; see [Known issues](#known-issues).

## How Nebula was made

Nebula's host, renderer, installer, tests and documentation were written with AI coding agents (Anthropic's
Claude), directed, reviewed and tested by the project's author. The game's own logic, Mario's physics included, is
the Petari decompilation by SMGCommunity, compiled for the PC with the changes a 64-bit, little-endian host needs.

## How it works

Nebula 2 (this branch; 1.0.2 works differently, see [Project status](#project-status)):

1. **Game module** (`native/`). Petari's game code, JSystem, NW4R and RVLFaceLib, compiled with clang into
   `smg_game.dll`, with a native replacement for the Wii SDK underneath (`native/sdk`).
2. **Host** (`runtime/`, `native/host`). `NebulaNative.exe` loads the module and gives it the console's services:
   the GX command stream drawn with Direct3D 12 or Vulkan, the JAudio DSP program and audio output, the Wii Remote
   and Nunchuk, the disc image, the Wii's internal storage for saves, and the video clock.
3. **Setup and launcher** (`installer/`, C#; `crates/nebula-import`, Rust). `Nebula-Setup.exe` reads your disc
   image, checks that it is the supported release and packs its files for Nebula; `Nebula.exe` starts the game.

## Features

### In the released version (1.0.2)

- Keyboard and mouse, Xbox controllers, the Switch Pro and Switch 2 Pro Controllers (USB), remappable. Switch
  controllers point with the gyro and tilt for the Star Ball and ray surfing; vibration follows the Wii Remote's
  motor.
- Aspect ratios from 4:3 to 32:9, internal resolution up to 16x, frame rates above 60.
- Anti-aliasing (SMAA, FXAA), AMD FidelityFX FSR 1 and NVIDIA DLSS upscaling, anisotropic filtering, optional
  per-pixel lighting.
- In-game settings menu (F1, View + Menu on Xbox, Minus + Plus on Switch).
- Text speed: dialogue and Rosalina's storybook at the original pace, 2x or 3x.
- English, French or Spanish, the languages of the North American disc.
- Saves in `%LOCALAPPDATA%\Nebula\saves`, with export, import and Dolphin saves (see [Saves](#saves)).

### In development for 2.0 (not released)

None of these are in 1.0.2. They are in this branch and change until 2.0 is released.

- The European (RMGP01) and Japanese (RMGJ01) discs and all their languages ([Languages](#languages)).
- The game built natively from the Petari decompilation: no compiling during install; a Vulkan renderer beside
  Direct3D 12.
- More controllers: PlayStation (DualShock 4, DualSense, DualSense Edge), the Steam Controller and generic HID
  controllers; gyro pointing and tilt on any controller with motion sensors.
- MSAA, AMD FSR 3, ambient occlusion.
- Skippable cutscenes (+ during a cutscene) and Quit to desktop on the file select.
- 34 achievements, each with a pop-up when it is reached. From the Gateway's Grand Star on, a Luma near Mario's start
  in the Comet Observatory shows them all: tabs, filters and play stats.
- Instant text speed.
- Mii import for the file icons from a Wii, DS, 3DS, Wii U, Switch, Mii Studio or Mii Creator. Settings > GENERAL
  > IMPORT MIIS, or put the files in the `miis` folder next to the settings; imported files are kept there and read
  at every start.
- Mods, texture packs and HD movies, each in its own folder in the `mods` folder next to the settings: Riivolution
  mods (`riivolution/*.xml` and their files), disc files in the disc's layout (`files/`), texture packs in
  Dolphin's format (`textures/`, `tex1_` PNG or DDS) and movies (`movies/<THP name>.mp4`, `.webm` or `.mkv`, played
  in place of the game's THP video). Import a zip, 7z, rar or folder from the launcher's Mods tab or Settings >
  MODS, or drop it on the game window. Among enabled mods the higher one wins a file or texture.
- Nebula's own text (its buttons, questions and achievements) in all 14 of the game's languages, changeable or
  translatable by a mod: [docs/MODDING_NEBULA_TEXT.md](docs/MODDING_NEBULA_TEXT.md).
- When Nebula stops, an error code ([docs/ERROR_CODES.md](docs/ERROR_CODES.md)) naming the cause and a report in
  `%LOCALAPPDATA%\Nebula\crashes`.

## Requirements

- Super Mario Galaxy, revision 0, as an ISO, RVZ or extracted folder: North America (RMGE01) for 1.0.2; Europe
  (RMGP01) and Japan (RMGJ01) too in 2.0 (in development)
- Windows 10 (version 1803 or newer) or Windows 11, 64-bit
- A GPU with Direct3D 12 support
- A CPU with AVX2 (2.0)
- 8 GB of RAM; for 1.0.2, 16 GB of free disk space and an internet connection during installation, about 5.5 GB
  afterwards

## Install

1. Download `Nebula-Setup.exe` from the [latest release](https://github.com/keepvaibin/nebula/releases/latest).
   The file is not code-signed, so Windows SmartScreen shows "Unknown publisher"; choose **More info**, then
   **Run anyway**.
2. Select your game and choose **Install**.
3. Start **Nebula** from the Start menu.

Setup installs 1.0.2, the released version. Setup also updates, repairs and uninstalls. Updates are signed; Setup checks the signature against the public key
built into it before installing. Each update keeps the version before it: **Nebula Setup > Restore the previous
version** goes back to it (and forward again the same way).

## Languages

1.0.2 plays the North American disc in English, French or Spanish. Nebula 2 (in development) plays the North American (RMGE01), European (RMGP01) and Japanese (RMGJ01) discs, each in the languages on
it. Setup names a disc's languages before importing it. The game starts in your system's language where the disc has
it (else English; Japan's disc in Japanese), and the language setting offers the disc's languages (and those a mod
adds). Nebula's own menus follow the game's language.

| Disc | Languages |
|---|---|
| North America, RMGE01 | English, French, Spanish |
| Europe, RMGP01 | English, French, German, Spanish, Italian (GameTDB) |
| Japan, RMGJ01 | Japanese |
| Korea, RMGK01 | Korean (GameTDB); not supported yet |

GameTDB lists no Wii disc in Chinese; Simplified Chinese came with the NVIDIA Shield TV release in China (RNVW01).
The Korean release's code also names a Simplified Chinese folder (Language.cpp); no Korean disc has been read here to
see whether it holds one.

## Saves

Saves are kept in `%LOCALAPPDATA%\Nebula\saves`, laid out like the Wii's internal memory: each release keeps its own
save, as on the Wii, in `title\00010000\<title>\data\GameData.bin` (1.0.2 also writes the Wii Menu's `banner.bin` beside it;
`524d4745` North America; in 2.0 also `524d4750` Europe and `524d474a` Japan). Move saves with the launcher's **Advanced**
tab:

- **Export saves…** writes `Nebula-Saves-<date>.zip` with everything the game reads.
- **Import saves…** takes such a zip, another zip or folder of Nebula saves, or a save from Dolphin. Your current
  saves are moved to `saves-backup-<date>` beside them first. Close the game before importing or exporting.

To bring a save over from Dolphin, right-click Super Mario Galaxy in Dolphin, choose **Open Wii Save Folder**, then
in Nebula choose **Advanced**, **Import saves…**, **From a folder…** and select that folder. A save goes to the
installed disc's release; Nebula and Dolphin use the same save format.

## Building from source

[docs/BUILDING.md](docs/BUILDING.md) covers the tools, building the game module, the host and Setup, and running
the tests.

## Known issues

- 1.0.2: some players report the game closing when Mario enters or leaves water in some galaxies (Beach Bowl, Sea
  Slide, Bonefin, Deep Dark, Freezeflame).
- Heavy background CPU load can make the audio skip.
- HD replacement movies at 4K need hardware video decoding.

Report problems on the [issue tracker](https://github.com/keepvaibin/nebula/issues). If Nebula closed unexpectedly,
attach its crash report from `%LOCALAPPDATA%\Nebula\crashes`.

## License

Nebula is licensed under the GNU General Public License v3.0 only ([LICENSE](LICENSE)). Third-party code and its
licenses are listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) and [docs/PROVENANCE.md](docs/PROVENANCE.md).

Nebula builds on the Petari decompilation (SMGCommunity), Aurora, Dusklight, SMAA, AMD FidelityFX, NVIDIA
Streamline and the `nod` disc library.

"Super Mario Galaxy", "Wii" and "Nintendo" are trademarks of Nintendo.
