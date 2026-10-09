# Nebula

A native Windows port of **Super Mario Galaxy**, using your own copy of the game.
The engine, renderer, audio host and launcher ship prebuilt. Setup compiles the
game-derived modules on first installation; compatible updates preserve that work.

[Download the beta](https://github.com/keepvaibin/nebula/releases/tag/v0.1.1-beta.4)
· [Controls](docs/KEYBOARD_CONTROLS.md)
· [Beta 4 changes and evidence](docs/RELEASE_BETA4.md)
· [Build guide](docs/BUILDING.md)

Development preview. Not affiliated with Nintendo.

## Requirements

- Super Mario Galaxy, USA (RMGE01), revision 0: ISO, RVZ or extracted folder
- Windows 10/11 64-bit with a DirectX 12 GPU
- 8 GB RAM, 16 GB free disk space during setup (about 5.5 GB after)
- Internet for the first install

## Install

1. Download **Nebula-Beta-Setup.exe** from [Releases](https://github.com/keepvaibin/nebula/releases).
2. On a fresh installation, choose your game and click **Install**. On an existing installation, click **Update**.
3. Launch **Nebula**, choose your display settings, and press **Play**.

The release has a detached signature checked by Nebula's updater. It is not
Authenticode signed, so Windows SmartScreen may still warn on first run.

Updates install the bundled public runtime. Compatible game DLLs stay unchanged
unless updated public libraries need linking; new installations retain compiled
objects for that step, without recompiling game source. Older installations that
discarded those objects may need a one-time rebuild from their retained game input
as a last resort. The updater also rebuilds if the module ABI is incompatible or
the retained objects cannot be used safely. The old install stays available until
the new version passes validation.
Setup also offers **Uninstall** and **Restore the previous version**. Saves and
settings are preserved by default.

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
