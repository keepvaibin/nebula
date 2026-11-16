# Writing the modding docs

The website's modding documentation (`nebula.keepvaibin.com/modding/docs/`) is generated from the Markdown files in this folder. In `website/`, `npm run content` (and `npm run dev` and `npm run build`) turns them into HTML with the sidebar, search, "In this article" links and previous and next links. The pages also read well on GitHub, and the code's comments link to them by path, so keep file names stable; when a page must move, add its old address to `redirects.json`.

## Folders

| Folder | Sidebar section | Content type |
| --- | --- | --- |
| `get-started/` | Get started | Introductions, installation and the quickstart. |
| `concepts/` | Concepts | Explanations of how mods and their parts work. |
| `how-tos/` | How-tos | One task at a time, as numbered steps. |
| `tutorials/` | Tutorials | Complete mods built from start to finish. |
| `reference/lumascript/` | LumaScript | The language. |
| `reference/lumakit/` | LumaKit | One page for each LumaKit module. |
| `reference/services/` | Services | One page for each service. |
| `reference/lumalink-commands/` | LumaLink commands | One page for each `lumalink` command. |
| `reference/` (selected files) | Mod files, Game data | Formats, validator codes, the C API, and the game's names. |
| `examples/` | None | The finished mod of each tutorial. Each folder is published as a `.zip` file. |

`sections.json` maps folders to sidebar sections and groups. `site.json` holds the text of the page chrome and the docs home page. `redirects.json` maps addresses that no longer exist to their new pages.

## Order of pages

Each folder's `index.md` is the section's landing page. Pages appear in the sidebar in the order the landing page links to them. A table on a landing page whose first column is **Article**, **Tutorial** or **Section** is shown as a grid of cards, with each page's `intro` as its description.

## Front matter

```markdown
---
title: Testing a mod
shortTitle: Test a mod
intro: Write tests that start the game with your mod, play it for you, and check what happened.
contentType: how-tos
example: first-mod
---
```

| Field | Required | Description |
| --- | --- | --- |
| `title` | Yes | The page's heading and browser title. Use sentence case. For how-tos and tutorials, start with a gerund ("Adding models"). |
| `shortTitle` | No | A shorter label for the sidebar and breadcrumbs. |
| `intro` | Yes | One or two sentences under the title. Also used on cards, in search results and for search engines. |
| `contentType` | Yes | `landing`, `get-started`, `concepts`, `how-tos`, `tutorials` or `reference`. |
| `example` | No | The name of a folder in `examples/`. The page shows a button to download it. |

Do not write a `#` heading: the title comes from the front matter. Use `##` for the page's main parts, which appear in "In this article", and `###` below them.

## Links

Link to another page with a relative path, as on GitHub, with an optional heading anchor:

```markdown
For more information, see [LumaKit: hud](../reference/lumakit/hud.md#counter).
```

Anchors follow GitHub's rule: the heading's text in lower case, with punctuation removed and spaces replaced by `-`. The build stops with an error naming any link that does not lead to a page or heading.

## Code blocks

Name the language after the opening fence: `lumascript`, `json`, `shell`, `c`, `hlsl` or `text`. A second word is shown as the file name. Before a code block, say which file it belongs in or where to run it. After a command, say what the reader should see.

## Callouts

```markdown
> [!NOTE]
> Information the reader should notice.

> [!TIP]
> A shortcut or a better way to do something.

> [!WARNING]
> Something that can break a mod or lose a player's data.
```

## Style

Follow the [GitHub Docs style guide](https://docs.github.com/en/contributing/style-guide-and-content-model/style-guide).

* Write for someone who has played the game and may have written a little code.
* Use "you", the present tense and the active voice. Keep sentences short.
* Put what most readers need first. Move details to later headings or to a reference page, and link to it.
* Write UI text in bold, as it appears: the **Mods** page, **Play in Nebula**. Write keys and buttons with `<kbd>`: <kbd>A</kbd>, <kbd>Enter</kbd>.
* Write placeholders in capitals with hyphens, such as `MOD-FOLDER`, and say what to replace them with.
* State known problems in a `[!NOTE]` that starts "In the current version".
* Keep facts exact: names, codes, limits and defaults must match the toolkit.
