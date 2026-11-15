---
title: Installing Nebula
shortTitle: Install Nebula
intro: Install Nebula on Windows or Linux from your own Super Mario Galaxy disc image.
contentType: how-tos
---

## About installing Nebula

Nebula plays Super Mario Galaxy from your own disc image. The installer reads the disc image you choose and prepares the game from it.

## Installing Nebula on Windows

1. Download `Nebula-Setup.exe` from the [latest release](https://github.com/keepvaibin/nebula/releases/latest).

   The file is not code-signed, so Windows SmartScreen shows "Unknown publisher". Click **More info**, then **Run anyway**.

1. Select your game and click **Install**.
1. Start **Nebula** from the Start menu.

Nebula Setup also updates, repairs and uninstalls Nebula. Updates are signed, and Setup checks the signature before it installs one. Each update keeps the version before it. To go back to it, open Nebula Setup and click **Restore the previous version**.

### Windows on ARM

Nebula runs on Windows on ARM through Windows' x64 emulation. Install it with `Nebula-Setup.exe` as on any other Windows PC.

## Installing Nebula on Linux

Nebula for Linux comes as a package named `Nebula-<version>-linux-<arch>.tar.gz`.

1. Unpack the package and open a terminal in its folder.
1. Run the installer.

   ```shell
   ./install.sh
   ```

1. When the installer asks, choose your Super Mario Galaxy disc image, such as an `.iso`, `.rvz` or `.wbfs` file. The installer opens a file chooser when your desktop has one, and asks in the terminal otherwise.

By default, the installer installs Nebula for your user in `~/.local/share/Nebula`, adds a menu entry, and makes `.luma` files open in Nebula.

### Installer options

To list the options, run `./install.sh --help`.

| Option | Effect |
|---|---|
| `--disc IMAGE` | Uses this disc image. Without it, the installer asks. An update of an install that already has the game does not need it. |
| `--portable FOLDER` | Installs everything in this one folder, with saves and settings in its data folder. Nothing else on the computer changes. |
| `--yes` | Asks nothing. Installs for your user unless `--portable` is given. |

### Uninstalling Nebula on Linux

Run `uninstall.sh` in the installed folder.

```shell
./uninstall.sh
```

Your saves, settings, mods and the prepared game stay. To remove them too, run `./uninstall.sh --all`.

## Further reading

* [Command-line arguments](command-line-arguments.md)
* [Keyboard and controller controls](keyboard-and-controller-controls.md)
