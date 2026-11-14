# Writing LumaScript in an editor

`lumalink lsp` is LumaScript's language server. An editor starts it and shows, as you type, what `lumalink build` would
say: the same check, the same messages, in the language LumaLink speaks (`--language`).

What it gives:

| Feature | What you see |
|---|---|
| Errors and warnings | Underlined as you type; a mod's `luma.json` faults on `luma.json`. |
| Completion | The game's classes, functions, fields, tables, sounds and content names; your own functions and types; after `.`, `::`, `` ` `` and `/`. |
| Hover | A name's declaration, what it does, and a link to its reference page. |
| Signature help | A call's parameters while you type its arguments. |
| Go to definition, references | Your own names across the mod's modules, and the modules of mods it needs (`.lumi`). |
| Rename | A name and every use of it in the mod. |
| Symbols | The file's outline; the mod's names by search. |
| Formatting | `lumalink format`'s layout. |

## Visual Studio Code

1. Install LumaLink and put it on the PATH (its setup offers that). Check: `lumalink --version` in a new terminal.
2. Install the LumaScript extension: in LumaLink's source, `editors/vscode`. Copy that folder to your VS Code extensions
   folder (`%USERPROFILE%\.vscode\extensions\lumascript` on Windows, `~/.vscode/extensions/lumascript` on Linux and
   macOS), then restart VS Code.
3. Open a mod folder (the one with `luma.json`). Opening a `.lum` file starts the server; its messages are in the
   **LumaScript** output panel.

Settings (File > Preferences > Settings, search "LumaScript"):

| Setting | Default | Use |
|---|---|---|
| `lumascript.lumalink` | `lumalink` | LumaLink's program, when it is not on the PATH: its full path. |
| `lumascript.names` | empty | The game's self-description folder (`luma` beside Nebula's program). Empty: the installed game's. |
| `lumascript.sdk` | empty | The folder with the game's `luma.h`, for the services' functions. Empty: `LUMA_INCLUDE`, then the self-description folder's `sdk`. |

A changed setting restarts the server. **LumaScript: Restart the language server** (Command Palette) restarts it by
hand.

Without an installed game and without `lumascript.names`, the server checks the language alone: names of the game are
then unknown, and its first message says so.

## Any other editor

Any editor with a Language Server Protocol client (Neovim, Helix, Emacs, Sublime Text, Zed, Kate) runs the same
server. Register it for `*.lum` files:

- command: `lumalink lsp` (options: `--names <folder>`, `--sdk <folder>`, `--no-installed`, `--language <name>`)
- transport: standard input and output
- positions: UTF-16 (the protocol's default)
- root: the mod folder (the one with `luma.json`)

Neovim example (`init.lua`):

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

Helix (`languages.toml`):

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

## Limits

- The server reads a mod's files from the disk and the open files from the editor; a file changed outside the editor
  is read again when it is saved or opened.
- Colours: the VS Code extension's grammar (`editors/vscode/syntaxes`). Other editors colour by their own rules or
  the TextMate grammar there.

## See also

lumalink/cmd-lsp.md, lumalink/cmd-format.md, language/language.md
