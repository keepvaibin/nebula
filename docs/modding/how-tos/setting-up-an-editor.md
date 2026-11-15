---
title: Setting up an editor
shortTitle: Set up an editor
intro: See errors, completion, and help for your mod's LumaScript as you type, in Visual Studio Code or any editor with a language server client.
contentType: how-tos
---

## About the LumaScript language server

`lumalink lsp` is LumaScript's language server. Your editor starts it and shows, as you type, what `lumalink build` would report: the same check and the same messages, in the language LumaLink uses.

| Feature | What you see |
| ------- | ------------ |
| Errors and warnings | Underlines as you type. Problems in `luma.json` show on `luma.json`. |
| Completion | The game's classes, functions, fields, tables, sounds, and content names, and your own functions and types. Completion opens after `.`, `::`, `` ` ``, and `/`. |
| Hover | A name's declaration, what it does, and a link to its reference page. |
| Signature help | A call's parameters while you type its arguments. |
| Go to definition and references | Your own names across your mod's modules, and the modules of mods it needs. |
| Rename | A name and every use of it in your mod. |
| Symbols | The file's outline, and your mod's names by search. |
| Formatting | The layout `lumalink format` writes. |

## Prerequisites

* LumaLink, installed with LumaLink Setup. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* Nebula, installed with Nebula Setup. Without an installed game, the server checks the language only, and names from the game are unknown.

## Setting up Visual Studio Code

1. Install Visual Studio Code from [code.visualstudio.com](https://code.visualstudio.com/).
1. Open LumaLink.
1. Click **Settings**.
1. Click **Add LumaScript to Visual Studio Code**. LumaLink shows "Added." when the extension is installed.
1. In Visual Studio Code, open your mod folder, the folder that holds `luma.json`.
1. Open a `.lum` file. The server starts, and its messages appear in the **LumaScript** output panel.

To update the extension after you update LumaLink, click **Add LumaScript to Visual Studio Code** again.

### Visual Studio Code settings

To change a setting, open **File** > **Preferences** > **Settings** and search for "LumaScript".

| Setting | Default | Description |
| ------- | ------- | ----------- |
| `lumascript.lumalink` | `lumalink` | LumaLink's program. The default uses the copy LumaLink Setup installed, then the one on your `PATH`. To use another copy, enter its full path. |
| `lumascript.names` | Empty | The game's self-description folder, the `luma` folder beside Nebula's program. When empty, the server uses the installed game's. |
| `lumascript.sdk` | Empty | The folder with the game's `luma.h`, for service functions. When empty, the server uses `LUMA_INCLUDE`, then the `sdk` folder in the self-description folder. |

Changing a setting restarts the server. To restart it yourself, open the Command Palette and run **LumaScript: Restart the language server**.

### Installing the extension without LumaLink's window

LumaLink Setup installs the extension file as `editors/lumascript.vsix` in LumaLink's folder. To install it from a terminal, run:

```shell
code --install-extension PATH-TO-LUMALINK/editors/lumascript.vsix
```

Replace PATH-TO-LUMALINK with LumaLink's folder:

| System | LumaLink's folder |
| ------ | ----------------- |
| Windows | `%LOCALAPPDATA%\Programs\LumaLink` |
| macOS | `~/Library/LumaLink` |
| Linux | `~/.local/share/lumalink` |

## Setting up another editor

Any editor with a Language Server Protocol client runs the same server, for example Neovim, Helix, Emacs, Sublime Text, Zed, or Kate. Register the server for `.lum` files with these values:

| Value | Setting |
| ----- | ------- |
| Command | `lumalink lsp` |
| Options | `--names FOLDER`, `--sdk FOLDER`, `--no-installed`, `--language LANGUAGE` |
| Transport | Standard input and output |
| Positions | UTF-16, the protocol's default |
| Root | The mod folder, the folder that holds `luma.json` |

If `lumalink` is not on your `PATH`, use the full path to LumaLink's program. For more information on the options, see [lumalink lsp](../reference/lumalink-commands/lsp.md).

### Example for Neovim

Add this to `init.lua`:

```lua
vim.filetype.add({ extension = { lum = "lumascript" } })
vim.api.nvim_create_autocmd("FileType", {
  pattern = "lumascript",
  callback = function(args)
    vim.lsp.start({
      name = "lumalink",
      cmd = { "lumalink", "lsp" },
      root_dir = vim.fs.root(args.buf, { "luma.json" }),
    })
  end,
})
```

### Example for Helix

Add this to `languages.toml`:

```toml
[language-server.lumalink]
command = "lumalink"
args = ["lsp"]

[[language]]
name = "lumascript"
scope = "source.lumascript"
file-types = ["lum"]
roots = ["luma.json"]
language-servers = ["lumalink"]
```

### Syntax colors

The Visual Studio Code extension colors LumaScript with its own grammar. Other editors color by their own rules, or with the TextMate grammar in LumaLink's source, `editors/vscode/syntaxes/lumascript.tmLanguage.json`.

## Troubleshooting

* "Visual Studio Code was not found." LumaLink looks for the `code` command on your `PATH` and in Visual Studio Code's usual install folders. Install Visual Studio Code, or add its `code` command to your `PATH`, then click **Add LumaScript to Visual Studio Code** again.
* "This copy of LumaLink has no LumaScript extension." Your LumaLink was not installed with LumaLink Setup. Install LumaLink with LumaLink Setup.
* "LumaLink could not start." in Visual Studio Code. Set `lumascript.lumalink` to the full path of LumaLink's program.
* Names from the game are unknown. The server found no installed game. Install Nebula with Nebula Setup, or set `lumascript.names`.
* A file changed outside the editor shows old errors. The server reads open files from the editor and other files from disk. Save or reopen the file.

## Further reading

* [Creating a mod](creating-a-mod.md)
* [lumalink lsp](../reference/lumalink-commands/lsp.md)
* [lumalink format](../reference/lumalink-commands/format.md)
