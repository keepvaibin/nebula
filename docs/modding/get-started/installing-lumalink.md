---
title: Installing LumaLink
shortTitle: Install LumaLink
intro: Install LumaLink, the tool that makes, checks, builds, tests and packs Nebula mods, on Windows, Linux or macOS, on x64 or ARM.
contentType: get-started
---

## About LumaLink

LumaLink runs on Windows, Linux and macOS. It has two parts:

* The LumaLink window.
* The `lumalink` command-line tool.

LumaLink Setup installs both, with the C compiler that builds mod code (clang and lld) and LumaKit. Setup installs LumaLink for your own account only, and never asks for an administrator password.

Phones and tablets (Android and iOS) have no LumaLink. Nebula on these devices loads `.luma` files, so make and pack a mod on a computer, then copy the `.luma` file to the device.

## Choosing a Setup

| System | Setup |
|---|---|
| Windows on x64 | `LumaLink-Setup.exe` |
| Windows on ARM | `LumaLink-Setup-windows-arm64.exe` |
| Linux on x64 | `LumaLink-Setup-linux-x64` |
| Linux on ARM64 | `LumaLink-Setup-linux-arm64` |
| macOS on Apple silicon | `LumaLink-Setup-macos-arm64` |
| macOS on Intel | `LumaLink-Setup-macos-x64` |

On Windows on ARM, `lumalink test` is not available, because its test parts are built for Windows x64 and Linux x64 only. Every other command works.

## Installing LumaLink on Windows

1. Download `LumaLink-Setup.exe` from the [latest LumaLink release](https://github.com/keepvaibin/lumalink/releases/latest). On Windows on ARM, download `LumaLink-Setup-windows-arm64.exe`.

   To download it from PowerShell instead, run one of these commands:

   ```shell
   curl.exe -fLO https://github.com/keepvaibin/lumalink/releases/latest/download/LumaLink-Setup.exe
   curl.exe -fLO https://github.com/keepvaibin/lumalink/releases/latest/download/LumaLink-Setup-windows-arm64.exe
   ```

1. Run it, choose your options, and click **Install**.

## Installing LumaLink on Linux

LumaLink Setup works on systems with glibc 2.35 or later, such as Ubuntu 22.04, Debian 12, Fedora 36 and newer.

1. In a terminal, download Setup and run it. On x64:

   ```shell
   curl -fLO https://github.com/keepvaibin/lumalink/releases/latest/download/LumaLink-Setup-linux-x64
   chmod +x LumaLink-Setup-linux-x64
   ./LumaLink-Setup-linux-x64
   ```

   On ARM64:

   ```shell
   curl -fLO https://github.com/keepvaibin/lumalink/releases/latest/download/LumaLink-Setup-linux-arm64
   chmod +x LumaLink-Setup-linux-arm64
   ./LumaLink-Setup-linux-arm64
   ```

1. Choose your options, and click **Install**.

## Installing LumaLink on macOS

1. In Terminal, download Setup and run it. On a Mac with Apple silicon:

   ```shell
   curl -fLO https://github.com/keepvaibin/lumalink/releases/latest/download/LumaLink-Setup-macos-arm64
   chmod +x LumaLink-Setup-macos-arm64
   ./LumaLink-Setup-macos-arm64
   ```

   On a Mac with an Intel processor:

   ```shell
   curl -fLO https://github.com/keepvaibin/lumalink/releases/latest/download/LumaLink-Setup-macos-x64
   chmod +x LumaLink-Setup-macos-x64
   ./LumaLink-Setup-macos-x64
   ```

1. Choose your options, and click **Install**.

> [!NOTE]
> LumaLink is not signed by Apple. Download it with `curl` as shown, because macOS stops a copy downloaded with a browser when it starts.

## Setup options

| Option | Default | Effect |
|---|---|---|
| **Add LumaLink to PATH** | On | `lumalink` works in any new terminal. |
| **Start menu shortcut** (Windows), **Applications menu entry** (Linux) or **Add to Applications** (macOS) | On | Lists the LumaLink window with your other programs. |
| **Desktop shortcut** | Off | Puts the LumaLink window on your desktop. |
| **Check for updates when LumaLink starts** | On | Offers new versions. For more information, see [Updating LumaLink](#updating-lumalink). |

You can also change the folder LumaLink is installed in. When you run Setup again, it keeps your last choices.

### Where LumaLink is installed

| Item | Windows | Linux | macOS |
|---|---|---|---|
| Program folder | `%LOCALAPPDATA%\Programs\LumaLink` | `~/.local/share/lumalink` | `~/Library/LumaLink` |
| The window | Start menu: **LumaLink** | Applications menu: **LumaLink** | `~/Applications/LumaLink.app` |
| Uninstalling | **Settings** > **Apps** > **Installed apps** | `~/.local/share/lumalink/LumaLink-Setup --uninstall` | `~/Library/LumaLink/LumaLink-Setup --uninstall` |

Uninstalling LumaLink leaves your mods where they are.

### How Setup changes PATH

With **Add LumaLink to PATH** on, a new terminal runs `lumalink` from any folder. Terminals that are already open do not see the change, so open a new one.

* On Windows, Setup adds LumaLink's folder to your own PATH, not the system's.
* On Linux and macOS, Setup writes `env` and `env.fish` in LumaLink's folder, and adds one line that reads it to each shell's startup file: `~/.profile`, `~/.bashrc` and `~/.bash_profile` for bash, `~/.zshenv` for zsh, and `~/.config/fish/conf.d/lumalink.fish` for fish. Each line ends with `# LumaLink Setup`, and uninstalling LumaLink removes them.

Without PATH, run LumaLink by its full path, for example `~/.local/share/lumalink/lumalink --help`.

### Opening .luma files

On Windows and Linux, Setup adds LumaLink to the **Open with** list for `.luma` files. When Nebula is installed, opening a `.luma` file still starts Nebula.

## Installing LumaLink from a script

To install without a window, run Setup with `--silent`. Setup prints the result.

```shell
LumaLink-Setup --silent
```

| Option | Effect |
|---|---|
| `--desktop-shortcut` | Adds the desktop shortcut. |
| `--folder FOLDER` | Installs in FOLDER, given as a full path. |
| `--no-menu` | Adds no Start menu shortcut, Applications menu entry or Applications entry. On Windows, `--no-start-menu` also works. |
| `--no-path` | Does not add LumaLink to PATH. |
| `--no-update-check` | Turns off the update check. |
| `--uninstall` | Uninstalls LumaLink. With `--silent`, uninstalls without a window. |

On Linux, Setup needs `--silent` when there is no display, for example over SSH.

## Updating LumaLink

With **Check for updates when LumaLink starts** on, the window checks for a new version when it opens, at most once an hour, and the command line checks at most once a day. When a new version is out, LumaLink offers it.

To install a new version now, run:

```shell
lumalink update
```

LumaLink runs a downloaded Setup only when it is signed with the LumaLink release key and is the version the release names. It deletes anything else without running it.

## Connecting LumaLink to the game

LumaLink reads the installed game's self-description, which Nebula Setup installs in the `luma` folder of Nebula's version folder. The `check`, `build`, `pack`, `run` and `test` commands need it.

On a computer without Nebula installed, pass the game's files with `--game` and `--names`. For more information, see [lumalink check](../reference/lumalink-commands/check.md).

## Next steps

* [Quickstart for Nebula mods](quickstart.md)
* [LumaLink commands](../reference/lumalink-commands/index.md)
