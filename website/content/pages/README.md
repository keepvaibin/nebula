# Editing page text

Each JSON file holds the wording of one page.

| File                    | Website page             |
| ----------------------- | ------------------------ |
| `home.json`             | Main landing page        |
| `v2.json`               | Upcoming v2 update       |
| `download.json`         | Downloads                |
| `guide.json`            | Setup guide              |
| `play.json`             | Player guide hub         |
| `play-controls.json`    | Controls                 |
| `play-graphics.json`    | Graphics                 |
| `play-mods.json`        | Mods                     |
| `play-saves.json`       | Saves                    |
| `help.json`             | Help and troubleshooting |
| `help-error-codes.json` | Error code guide         |
| `credits.json`          | Credits                  |
| `legal.json`            | Legal and privacy        |
| `not-found.json`        | Page not found (404)     |

## How to change wording

1. Open the JSON for the page you want.
2. Find the wording under `text`. Each group is named after a page section, such as `hero`, `available`, or `achievements`.
3. Change the value inside the quotes, leaving field names, commas, and brackets in place.
4. Save. If `npm run dev` is running, the preview regenerates automatically. Otherwise run `npm run content`, or start the development server.

For example, change `text.hero.intro` in `home.json` to rewrite the landing-page introduction. Change `text.available.heading_1` to rewrite the Available Now heading.

`metadata` controls the browser title and search/share description. `messages` controls text that appears during interactions; preserve placeholders such as `{count}` and `{releases}`. Numbered fields follow the reading order of their section.

Text is treated as plain text, so you do not need HTML. To add a paragraph, rearrange sections, or change a link destination, edit its matching file in `templates/pages/`. The renderer reports missing fields instead of silently dropping them.

Navigation and footer wording is included in each page's JSON. Update each page if you want those words changed site-wide. The live download count remains automatic and is not an editable text value. NEBULA's animated wordmark shape is an image asset; changing its accessible title will not redraw the artwork.

Saving a file only updates the local preview. The live site updates when the change reaches `main` on GitHub.
