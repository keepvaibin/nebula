# Nebula

Nebula runs **Super Mario Galaxy** natively on Windows. It translates the game's PowerPC
machine code into C++ ahead of time, compiles that code on your PC from your own copy of the
game, and runs it on a native runtime with a Direct3D 12 renderer and Windows audio and input.
Nebula contains no Nintendo code or assets; you need your own copy of the game.

[Download](https://github.com/keepvaibin/nebula/releases/latest) ·
[Controls](docs/KEYBOARD_CONTROLS.md) ·
[Building from source](docs/BUILDING.md) ·
[Architecture](docs/ARCHITECTURE.md)

Not affiliated with or endorsed by Nintendo.

## Project status

Nebula is a work in progress. The game is playable from the start, but some areas run below
60 FPS on weaker hardware, some lighting and bloom effects differ from the original, and bugs
remain. See [Known issues](#known-issues).

## AI-generated project

Nebula was built with AI coding agents (Anthropic's Claude). The recompiler, runtime,
installer, tests and documentation were written by AI agents and directed, reviewed and tested
by the project's author. The game's own logic, including Mario's physics, is not AI-written:
it is the original game code, translated instruction by instruction by the recompiler.

## How it works

1. **Recompiler** (`crates/`, Rust). Reads the game disc or extracted files, checks that it
   is the supported release (RMGE01, revision 0) and emits C++ for every function in the game
   executable, the HOME Menu module and the audio DSP program.
2. **Runtime** (`runtime/`, C++). Loads the compiled game module and provides what the Wii
   hardware provided: memory, a GX command processor drawn with Direct3D 12, the audio
   interface and DSP, Wii Remote input from keyboard, mouse and controllers, disc and NAND
   storage, and video timing.
3. **Installer and launcher** (`installer/`, C#). `Nebula-Setup.exe` downloads a pinned
   compiler toolchain, compiles the game modules from your copy of the game and installs the
   prebuilt runtime. `Nebula.exe` starts the game; `NebulaSettings.exe` changes settings.

## Features

- Keyboard and mouse, Xbox controllers, the Switch Pro Controller and the Switch 2 Pro
  Controller (USB), remappable. Switch controllers point with the gyro (R recenters) and tilt
  for the Star Ball and ray surfing.
  Vibration follows the game's Wii Remote motor.
- Aspect ratios from 4:3 to 32:9 and internal resolution up to 16x.
- Anti-aliasing (SMAA, FXAA), AMD FidelityFX FSR 1 upscaling, anisotropic filtering and
  optional per-pixel lighting.
- In-game settings menu (F1, View + Menu on Xbox, Minus + Plus on Switch).
- Text speed: dialogue and Rosalina's storybook at the original pace, 2x or 3x.
- The game in English, French or Spanish, the three languages of the USA release.
- Saves stored in `%LOCALAPPDATA%\Nebula\saves`, with export, import and Dolphin saves
  (see [Saves](#saves)).

## Requirements

- Super Mario Galaxy, USA (RMGE01), revision 0, as an ISO, RVZ or extracted folder
- Windows 10 (version 1803 or newer) or Windows 11, 64-bit
- A GPU with Direct3D 12 support
- 8 GB of RAM (Setup also installs on less, as an experimental install at your own risk);
  16 GB of free disk space during installation, about 5.5 GB afterwards
- An internet connection for the first installation

The least powerful system tested so far, an Intel Core Ultra 9 185H with its integrated
graphics and 16 GB of RAM, runs the game at a locked 60 FPS at 1440p (16:10).

## Install

1. Download `Nebula-Setup.exe` from the [latest release](https://github.com/keepvaibin/nebula/releases/latest).
   The file is not code-signed, so Windows SmartScreen shows "Unknown publisher"; choose
   **More info**, then **Run anyway**.
2. Select your game and choose **Install**. Setup downloads the toolchain and compiles the game
   modules; this takes a few minutes on a fast PC and longer on slower ones.
3. Start **Nebula** from the Start menu.

Setup also updates, repairs, restores the previous version and uninstalls. Updates are signed;
Setup checks the signature against the public key built into it before installing.

## Saves

Saves are kept in `%LOCALAPPDATA%\Nebula\saves`, laid out like the Wii's internal memory: the
game's save is `title\00010000\524d4745\data\GameData.bin`, next to its `banner.bin`. Each file and
folder also carries the Wii's file information in a hidden NTFS stream, which a plain copy or zip
drops, so move saves with the launcher's **Advanced** tab:

- **Export saves…** writes `Nebula-Saves-<date>.zip` with everything the game reads.
- **Import saves…** takes such a zip, another zip or folder of Nebula saves, or a save from Dolphin.
  Your current saves are moved to `saves-backup-<date>` beside them first. Close the game before
  importing or exporting.

To bring a save over from Dolphin:

1. In Dolphin, right-click Super Mario Galaxy and choose **Open Wii Save Folder**. It holds
   `GameData.bin` and `banner.bin` (in Dolphin's user folder,
   `Wii\title\00010000\524d4745\data`).
2. In Nebula, choose **Advanced**, **Import saves…**, **From a folder…**, and select that folder.

Only saves of the USA version (RMGE01) apply; the game itself can come from an ISO or an RVZ, as
Nebula and Dolphin use the same save format. A `GameData.bin` copied into the saves folder by hand
also works: at the next start Nebula rebuilds the folder around it, and keeps the previous folder
as `saves-rejected-<date>` if anything in it could not be carried over.

## Building from source

[docs/BUILDING.md](docs/BUILDING.md) covers the tools, building the recompiler, runtime and game
modules by hand, running the tests and building the installer.

## Testing

- Recompiler: more than 500 tests (`cargo test --workspace`).
- Runtime: 71 test programs (`ctest`) covering floating-point operations (checked against
  Berkeley SoftFloat), GX command parsing and texture decoding, input mapping, the audio queue
  and DSP, and settings files.
- `tools/dev/verify_thp_native.cpp` compares the native movie decoder with the game's own
  decoder frame by frame.
- Gameplay is checked by playing back recorded input in a hidden window and by manual
  playtesting.

There is no automated whole-game comparison against the original hardware or an emulator yet.
Floating-point results are close to, but not bit-exact with, the Wii.

## Known issues

- Heavy background CPU load can make the audio skip.
- Some areas, for example the Gateway Galaxy, drop below 60 FPS on weaker CPUs.
- Some lighting and bloom effects in the Comet Observatory differ from the original.

Report problems on the [issue tracker](https://github.com/keepvaibin/nebula/issues). If Nebula
closed unexpectedly, attach its crash log: in Nebula Settings choose **Advanced**, **Open
diagnostics**, then the newest file in `crash-logs`. It is a few lines of text about what failed;
for more detail, choose **Detailed recording** under **Recording (next launch)** and reproduce the
problem.

## License

Nebula is licensed under the GNU General Public License v3.0 only ([LICENSE](LICENSE)).
Third-party code and its licenses are listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)
and [docs/PROVENANCE.md](docs/PROVENANCE.md).

Nebula builds on work from Aurora, Dusklight, WiiCompiled, the Petari decompilation project,
Berkeley SoftFloat, the Dolphin project's free DSP ROM, SMAA, AMD FidelityFX and the `nod` disc
library.

"Super Mario Galaxy", "Wii" and "Nintendo" are trademarks of Nintendo.
