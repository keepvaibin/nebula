# Installing LumaLink

LumaLink is a program for Windows, Linux and macOS. It comes in two parts:

- the window (`lumalink-gui`);
- the command line (`lumalink`).

You choose either part or both when you install it. The command line can also be added to PATH, so that `lumalink`
runs from any folder.

Phones and tablets (Android, iOS) have no LumaLink. The game there loads `.luma` files: make and pack a mod on a
computer, then copy the `.luma` to the device.

## Windows

Nebula Setup installs LumaLink with the game. It has three choices:

- LumaLink window: the window, with a Start Menu entry "LumaLink";
- LumaLink command line: `lumalink.exe`;
- Add LumaLink to PATH: puts the command line's folder on your own PATH (the user's, not the machine's; Setup needs no
  administrator).

Both parts go in `lumalink\` in the installed version's folder. Updates keep the choices of the last install. Open a
new terminal after Setup to use `lumalink`. Uninstalling Nebula takes the folder off PATH again.

## Linux and macOS

LumaLink comes as one archive per system with `lumalink`, `lumalink-gui` and `install.sh`. Unpack it and run:

```sh
sh install.sh
```

The script asks for each part, then whether to add LumaLink to PATH. You can also give the choices as options:
`--window`, `--command`, `--path`. Everything goes in your own folders:

| | Linux | macOS |
|---|---|---|
| programs | `~/.local/share/lumalink` | `~/Applications/LumaLink` |
| menu entry for the window | `~/.local/share/applications/lumalink.desktop` | none |
| PATH, only when asked | one line in `~/.profile` (`~/.zprofile` for zsh) | the same |

`sh install.sh --uninstall` removes the programs, the menu entry and the PATH line.

## Finding the game

LumaLink reads the installed game's self-description, which Setup installs in `luma\` of the version folder. It needs
it for `check`, `build`, `pack`, `run` and `test`. On a computer without Nebula installed, give it the game's files
with `--game` and `--names` (lumalink/cmd-check.md).
