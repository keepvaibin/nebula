# Nebula

Nebula is an unofficial, native Windows x64 build of **Super Mario Galaxy**,
compiled on your PC from your own copy of the game. It is a static
recompiler: at install time it translates the game's PowerPC code into C++
and compiles it with Microsoft's C++ compiler. When you play, no PowerPC code
is interpreted or JIT-compiled. Graphics run through Direct3D 12, audio through
XAudio2, and the game's own DSP audio program is recompiled to native code.

> **Development preview.** Nebula is playable but not finished. Locked 60 FPS
> and complete game compatibility are **not** established. See
> [Tested coverage](#tested-coverage) and [Known limitations](#known-limitations).

Nebula is not affiliated with or endorsed by Nintendo. It contains no
Nintendo code or game data and never downloads any. You need your own copy of
the game.

## Supported game

Only the North American release, revision 0:

| Game ID | Revision | `main.dol` SHA-1 |
| --- | --- | --- |
| `RMGE01` | 0 | `9a71008ae1ee9010e267fa67d1f0b0d4f0e895dd` |

Other regions (PAL `RMGP01`, Japan `RMGJ01`, Korea `RMGK01`), other revisions
and modified images are rejected before anything is generated, with a message
saying what was found.

Accepted inputs:

- an **ISO** disc image;
- an **RVZ** image (WIA, WBFS, CISO and GCZ are read by the same library but
  have not been tested);
- an **extracted folder** in Dolphin's "Extract Entire Disc" layout: a folder
  containing `DATA\sys\main.dol`, or the `DATA` folder itself. The folder
  must include `DATA\tmd.bin` and `DATA\ticket.bin`.

Your input is only read. It is never moved, changed or uploaded.

## Requirements

- Windows 10 or 11, 64-bit, with a DirectX 12 GPU.
- **16 GB RAM** minimum (32 GB recommended). One generated module is a single
  large C++ file that needs about 13 GB to compile.
- **22 GB free** on an NTFS drive during setup (extraction and build files are
  temporary), about 7 GB afterwards: game content 4.1 GB, compiler toolchain
  1.7 GB, about 0.5 GB per installed version (one previous version is kept),
  7 MB of retained game files.
- An internet connection for the first install: about **400 MB** of Microsoft
  compiler and Windows SDK packages, 45 MB CMake, the Nebula source (about
  5 MB). Updates download only what changed.
- Time: compiling is the long step. It took 71 minutes on the 16-core test PC
  (see [Tested coverage](#tested-coverage)); expect longer on PCs with fewer
  cores or less RAM.

No administrator rights are needed. Nebula installs per user under
`%LOCALAPPDATA%\Programs\Nebula`. You do not need to disable any security
feature. `Nebula-Setup.exe` is **not code-signed**, so Windows SmartScreen may
warn the first time; choose "More info" then "Run anyway" only if you
downloaded it from this repository's Releases page.

## Install

1. Download `Nebula-Setup.exe` from the
   [Releases](https://github.com/keepvaibin/nebula/releases) page.
2. Run it, choose your ISO, RVZ or extracted folder, keep or change the install
   folder, and accept the Microsoft Visual Studio Build Tools license for the
   compiler Setup downloads.
3. Setup then, showing progress throughout:
   1. checks the game identity and `main.dol` hash;
   2. extracts the image (ISO/RVZ) into a temporary folder;
   3. builds the content package (`game.pak`) and keeps a few small game files
      needed to rebuild later;
   4. downloads the pinned compiler toolchain from Microsoft, CMake and Ninja
      (each file checked against a pinned SHA-256) and unpacks it per user;
   5. downloads the exact Nebula source revision from GitHub and checks every
      file against a manifest built into Setup;
   6. recompiles the game, Home Menu, DSP audio program and boot image;
   7. compiles everything (compiler jobs are limited by your CPU and RAM);
   8. validates the result, then switches to it.

Cancel at any time. Work happens in a staging folder; a canceled or failed run
never touches an existing installation.

Setup creates Start Menu shortcuts **Nebula**, **Update Nebula** and
**Nebula Setup** (repair, restore, uninstall), and an entry in Windows'
Installed apps.

## Playing

Start **Nebula**. The launcher shows the display settings and **Play**.

### Controls

Nebula plays with keyboard and mouse or an XInput controller. The mouse is the
Wii Remote pointer (IR). The full mapping is in
[docs/KEYBOARD_CONTROLS.md](docs/KEYBOARD_CONTROLS.md).

### Display settings

- **Output / window**: Match Display (borderless at your screen's resolution)
  or a window preset (480p–4K). Width follows the aspect.
- **Gameplay aspect**: 4:3 (original), 16:9, 16:10, 21:9, 32:9, or a typed
  ratio such as `43:18` (3440×1440). Widescreen layout, HUD and Home Menu
  repairs are included; universal layout correctness is not established.
- **Internal 3D resolution**: Match Output, a preset, or custom. 480 is the
  Wii's own resolution; higher values render the scene at a genuinely higher
  resolution in integer steps up to 16x (10240×8448), enough for 32:9 at
  2160p. High values need a powerful GPU and a lot of video memory.
- Movies play natively at their original resolution.

Settings are remembered in `%LOCALAPPDATA%\Nebula\settings.json`.

### Saves

Saves live in `%LOCALAPPDATA%\Nebula\saves`, outside the program folder, and
survive updates, repair, rollback and uninstall. Each save file carries a
`galaxy.isfs.meta` NTFS alternate data stream; copy saves with Nebula's
importer or a stream-aware tool, not by copying visible files alone.

**Import a GalaxyRecomp save:** in the launcher choose *Import a GalaxyRecomp
save…* and pick the old `save-data` folder. Nebula copies every file and data
stream, verifies the copy, and keeps any existing Nebula saves in a
`saves-backup-<time>` folder. The source folder is only read.

### Optional diagnostics

Monitoring is **off** by default. Ticking *Record diagnostics for this
session* writes runtime traces and once-per-second CPU/memory samples to a new
folder under `%LOCALAPPDATA%\Nebula\sessions\diagnostics\`. Every launch gets
its own folder (UTC time plus a random ID); earlier sessions are never
overwritten. Ordinary launches keep only the runtime's console output under
`sessions\launches\`. The runtime also publishes a small in-memory frame
counter for overlays; [docs/FRAME_TELEMETRY.md](docs/FRAME_TELEMETRY.md) and
`tools/telemetry/` describe it. Nebula never changes other programs (such as
Rainmeter or overlays) on its own.

## Updates, repair, rollback and uninstall

- **Update Nebula** (Start Menu) or *Check for updates* in the launcher asks
  GitHub for the newest release of this repository, including development
  previews, and shows its version and notes. The new `Nebula-Setup.exe` runs
  only if its RSA signature verifies against the release key built into your
  installed Setup.
- The update is staged completely before switching. Compiled game modules are
  reused when their compatibility key (recompiler, runtime headers, toolchain
  and build recipe) is unchanged; otherwise they are rebuilt locally from the
  game files kept at install time. Game-derived binaries are never downloaded.
- The previous version is kept. *Nebula Setup → Restore the previous version*
  switches back.
- *Nebula Setup → Repair* rebuilds the installed version. Choose your game
  again if the kept game files or content were removed.
- *Nebula Setup → Uninstall* (or Windows' Installed apps) removes the program,
  content, toolchain and kept game files. Saves, settings and the shader cache
  in `%LOCALAPPDATA%\Nebula` stay unless you choose to delete them.

## Building from source

The installer does exactly what follows; see [docs/BUILDING.md](docs/BUILDING.md)
for details.

```powershell
cargo build --release -p nebula-recomp
.\target\release\nebula-recomp.exe identify D:\Games\SMG.rvz
.\target\release\nebula-recomp.exe extract D:\Games\SMG.rvz --output D:\nebula-work\disc
.\target\release\nebula-recomp.exe generate D:\nebula-work\disc --output D:\nebula-work\generated
.\target\release\nebula-recomp.exe package-content D:\nebula-work\disc --output D:\nebula-work\content
```

then build the runtime, the game module and the Home module with CMake (MSVC
14.44, Ninja Multi-Config, Release, `GALAXY_NATIVE_ISA=SSE2`). To build
`Nebula-Setup.exe` itself: `.\installer\build-release.ps1 -Version <version>`.

Tests: `cargo test --workspace` and `ctest --test-dir <runtime build> -C Release`.

## Tested coverage

Measured on the development PC (AMD Ryzen 9 9950X3D, 16 cores / 32 threads,
64 GB RAM, RTX 5090, Windows 11) with `Nebula-Setup.exe` and the pinned
toolchain it downloads. Results from the Windows Sandbox clean-machine run
are recorded in the release notes.

| Area | Result |
| --- | --- |
| Fresh install from an extracted folder, into a path with spaces | Passed in 71 min (runtime 4 min, game module 59 min, Home module 7.5 min); about 6.3 GB on disk afterwards |
| Peak build memory | One game shard compiler up to about 2.4 GB; 16 parallel jobs peaked near 38 GB; the Home module compile peaked near 12.5 GB |
| Generated sources | Game (705 files), Home and DSP sources and the boot image are byte-identical to the qualified release inputs (ignoring line endings). Compiled binaries are not claimed bit-for-bit reproducible |
| ISO input | Extraction, content packaging and generation produce sources identical to the folder input. Image rebuilt from the user's own dump with `wit` (not a retail image) |
| RVZ input | Extraction verified file-by-file against the dump. Image converted from that ISO with DolphinTool (not a retail image) |
| Unsupported input (Mario Kart Wii ISO, Twilight Princess RVZ, non-disc file) | Rejected before any work with the game ID and title; existing install untouched |
| Interrupted install | Abandoned staging cleaned up on the next run; verified downloads reused |
| First launch, title, file select, mouse-to-IR pointer, new save file, gameplay, Home Menu open/close | Passed at 1280×720 16:9; normal exit, audio without dropouts |
| 12x internal resolution at 32:9 (7680×6336) | Ran and exited normally on an idle PC. Under heavy CPU load (a 16-job compile running) the audio deadline guard stopped the game |
| Save import from GalaxyRecomp | 22 entries and 28 data streams (including directory streams) copied and verified; source unchanged |
| Update preview.1 → preview.2 (compatible modules) | 2 s, modules reused after hash checks |
| Update preview.2 → preview.3 (recompiler changed) | 66 min, regenerated from retained game files without asking for the game |
| Rollback, uninstall | Passed; saves, settings and caches preserved |
| Native THP movies | Not re-run in this build. Runtime and generated module sources are identical to the qualified release in which both movies played to completion |
| Automated tests | 406 Rust tests and 47 C++ runtime test programs pass; clippy clean |

## Known limitations

- Heavy background CPU load (for example compiling) can make the game stop
  with an audio deadline error instead of stuttering; close heavy programs.
- Sustained, correctly paced 60 FPS is not achieved in every scene; some
  areas (for example Gateway/Luma) can drop sharply.
- Full-game compatibility has not been verified; only parts of the game have
  been played through on Nebula.
- Plaza black-world/strip lighting and some bloom differences remain.
- Only RMGE01 revision 0 is supported.
- `Nebula-Setup.exe` is not code-signed; update authenticity uses Nebula's own
  release signature.
- Fresh generation reproduces the qualified module sources exactly, but the
  compiled binaries are not claimed to be bit-for-bit reproducible.

## Licenses and credits

Nebula is free software under the **GNU General Public License version 3**
(`GPL-3.0-only`, see [LICENSE](LICENSE)). The complete corresponding source of
every release is this repository at the release tag; `Nebula-Setup.exe`
downloads exactly that revision.

Nebula incorporates or adapts work from Dusklight (CC0), Aurora (MIT),
WiiCompiled (GPLv3), Berkeley SoftFloat (BSD-3-Clause), Dolphin's free DSP ROM
(GPL-2.0-or-later) and the `nod` disc library (MIT), plus the Rust crates in
`Cargo.lock`. See [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md),
[docs/PROVENANCE.md](docs/PROVENANCE.md) (including known provenance
uncertainties) and [docs/CONTENT_POLICY.md](docs/CONTENT_POLICY.md). The Petari
decompilation community's CC0 symbol work provided the function boundaries.

None of these licenses grant rights in Nintendo software, game assets or
trademarks. "Super Mario Galaxy" and "Wii" are trademarks of Nintendo.

### Use of AI

AI tools were used in developing this project, including code generation,
refactoring and documentation. Before this first public release, the
publication tree was reviewed and cleaned up: unused and experimental material
was removed, the generated-code path was checked by regenerating the qualified
release sources byte-for-byte from a game dump, and the installer was tested
as described under [Tested coverage](#tested-coverage). As with any
development preview, review the code before relying on it.
