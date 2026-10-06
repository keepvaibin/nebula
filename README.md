# Nebula

A native Windows port of **Super Mario Galaxy**, built on your PC from your own
copy of the game. Setup recompiles the game's PowerPC code to C++ and compiles
it. Graphics use Direct3D 12 and audio XAudio2; the game's DSP audio program is
recompiled too.

Development preview. Not affiliated with Nintendo.

## Requirements

- Super Mario Galaxy, USA (RMGE01), revision 0: ISO, RVZ or extracted folder
- Windows 10/11 64-bit with a DirectX 12 GPU
- 8 GB RAM, 16 GB free disk space during setup (about 5.5 GB after)
- Internet for the first install

## Install

1. Download `Nebula-Setup.exe` from [Releases](https://github.com/keepvaibin/nebula/releases).
2. Choose your game and click Install. It takes a few minutes.

`Nebula-Setup.exe` is not code-signed, so SmartScreen may warn on first run.

## Playing

Start **Nebula** from the Start Menu, pick display settings and press **Play**.

- Keyboard and mouse or an XInput controller. See [docs/KEYBOARD_CONTROLS.md](docs/KEYBOARD_CONTROLS.md).
- Aspect ratios from 4:3 to 32:9; internal resolution up to 16x.
- Saves are in `%LOCALAPPDATA%\Nebula\saves`.

Updates, repair, restore and uninstall are in **Nebula Setup** and the launcher.

## Building from source

See [docs/BUILDING.md](docs/BUILDING.md).

## Known issues

- Heavy background CPU load can stop the game with an audio error.
- Some areas (for example Gateway/Luma) drop below 60 FPS.
- Plaza lighting and some bloom differences remain.

## Licenses and credits

GPL-3.0-only (see [LICENSE](LICENSE)). Nebula uses work from Dusklight,
Aurora, WiiCompiled, Berkeley SoftFloat, Dolphin's free DSP ROM, `nod` and
Petari's symbol work. See [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) and
[docs/PROVENANCE.md](docs/PROVENANCE.md). "Super Mario Galaxy" and "Wii" are
Nintendo trademarks.

Developed with the help of AI tools.
