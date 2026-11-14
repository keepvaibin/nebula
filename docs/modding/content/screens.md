# Content: screens and fonts

## 1. Where it goes, what it becomes (layer 1)

`layers/<l>/files/<Archive>.arc/<Name>.layout.json` (or in the archive's `blyt/`) becomes the archive's
`blyt/<Name>.brlyt`, the game's own layout file. The store cooks it as it cooks the game's layouts (NLYT) and the
game's layout loader reads it, so a mod's screen is drawn, widened to 16:9 and costed as a game screen. For
`ui.open(mod, "<Name>")` the archive is `LayoutData/<Name>.arc`. Pictures' textures are the archive's
`timg/<texture>.png` (cooked by the PNG cook to `.tpl`) or `timg/<texture>.tpl`.

Outside an archive: Cook.Layout.NotInArchive {file}.

## 2. The file

```json
{
  "size": [608, 456],
  "panes": [
    { "name": "Hud", "type": "pane", "position": [240, 170], "widen": true, "children": [
      { "name": "Icon", "type": "picture", "texture": "Star", "size": [32, 32], "anchor": "right" },
      { "name": "Count", "type": "text", "size": [160, 32], "text": "x 120", "length": 16,
        "font": "NumberFont", "font_size": 28, "place": "left", "colour": "ffffffff" }
    ]}
  ],
  "groups": [ { "name": "Counter", "panes": ["Icon", "Count"] } ]
}
```

- `size`: the layout's screen, default 608 by 456 (every game layout's), centred. `panes` is required (may be empty);
  they hang from a root pane `RootPane` the cook adds.
- Every pane:
  - `name`: required, 1 to 16 letters, digits or `_` (Cook.Layout.Name), unique in the layout without case, and not
    `RootPane` (Cook.Layout.Twice).
  - `type`: `pane` (groups others), `picture`, `text` or `bounding` (a hit area that draws nothing).
  - `position` [x, y] is where its anchor stands, from its parent's position (the root's is the screen's centre), x
    right, y up, in layout units (the screen is 608 wide).
  - `size` [w, h] is required except for `pane`; it is not negative.
  - `scale`: a number or [x, y], default 1.
  - `rotation`: degrees about the screen's axis, or [x, y, z].
  - `anchor`: which of its points stands at `position`: `top_left`, `top`, `top_right`, `left`, `center` (default),
    `right`, `bottom_left`, `bottom`, `bottom_right`.
  - `alpha`: 0 to 255, default 255.
  - `visible`: default true.
  - `alpha_to_children`: its alpha fades its children too, default true.
  - `widen`: on a wide screen its place spreads with the screen, as the game's HUD counters do, default false.
  - `children`: a list of panes.
- picture:
  - `texture`: a name; `.png`/`.tpl` may be written. The mod must have `timg/<name>.png` or `.tpl` in the archive
    (Cook.Layout.NoTexture {where, name, uri}); the name follows Cook.Effect.TextureName.
  - `colour` "rrggbbaa" for the whole picture, or `corners` [top left, top right, bottom left, bottom right]; not both
    (Cook.Layout.Both). The default is white. With a texture the colours tint it; without one they are the picture.
- text:
  - `text`: the words as they show, any language; `\n` breaks a line; other control characters are refused
    (Cook.Layout.Control).
  - `length`: the most characters (UTF-16 units) it holds, 1 to 255, default 255. `ui.set_text` cuts there; text in
    the file longer than it is refused (Cook.Layout.TextLong).
  - `font`: one of the game's fonts `MessageFont26` (default), `NumberFont`, `MenuFont64`, `PictureFont`,
    `CinemaFont26`, or a mod's own font `Luma/<id>/<path>` (section 2a; a `.font.json` or `.brfnt` ending may be
    written). The layout file names it `Luma/<id>/<path>.brfnt`, which the game finds in the store. A path with an
    empty, `.` or `..` part, fewer than three parts, a character in `\ : < > " | ? *` or a control, or more than 199
    bytes with `.brfnt`: Cook.Effect.Choice. A font of the layout's own mod (its id from the mod folder's luma.json)
    whose `assets/<path>.font.json` no layer has: Cook.Layout.NoFont {where, name, uri} (looked for when the mod is
    read from a folder, as textures are; another mod's font is that mod's).
  - `font_size`: a number or [w, h], default 26.
  - `place`: where the text sits in its pane (the anchor names), default `center`.
  - `align`: how its lines line up: `as_place` (default), `left`, `center`, `right`.
  - `colour`, or `colours` [top, bottom]: default white.
  - `char_space`, `line_space`: default 0.
- groups: `name` as a pane's (unique among groups, not `RootGroup`); `panes` names panes of the layout
  (Cook.Layout.NoPane).
- A layout holds at most 16,000 panes (Cook.Layout.TooMany): the layout file counts its blocks in 16 bits.
- Every value is checked with the effects cook's words for JSON: Cook.Effect.UnknownPart (a key the place does not
  take, so a misspelt key is never ignored), Kind, Range (numbers past f32 too), Choice, Color, Length, Missing. Every
  refusal names the place as a JSON pointer.

What the cook writes is what the game's layouts carry (checked against LayoutData/CoinCounter.arc): one material per
picture and text, named as its pane, black to white over the texture with the game's default stages, one clamped
linear map, an identity transform and one coordinate generator.

## 2b. Animations (layer 3)

```text
"animations": [
  { "name": "In", "frames": 30, "loop": false, "tracks": [
    { "pane": "Hud", "what": "position_y", "keys": [[0, -40], [30, 0]] },
    { "pane": "Hud", "what": "alpha", "keys": [[0, 0], [10, 255, 0]] },
    { "pane": "Icon", "what": "visible", "keys": [[0, 0], [5, 1]] }
  ]}
]
```

Each animation becomes `<archive>/anim/<Layout>_<name>.brlan`, the game's own layout animation file, which the
store cooks as the game's and `ui.play(screen, "<name>")` plays. `name` as a pane's; `frames` 1 to 65535; `loop`
default false. `what`: `position_x/y/z`, `rotation_x/y/z` (degrees), `scale_x/y`, `width`, `height` (pane
transforms), `alpha`, `colour` (vertex colours), `visible` (steps, 0 or 1). A key is `[frame, value]` or
`[frame, value, slope]`; without a slope the curve passes smoothly through its neighbours (Catmull-Rom) and rests at
the first and last key. A pane the layout lacks: Cook.Layout.NoPane; one track twice: Cook.Layout.Twice.

## 2c. Windows (layer 4)

`"type": "window"`: a 9-slice frame. `frame` (required) names the frame texture as a picture's `texture`;
`inflation` [left, right, top, bottom] grows the content past the pane (default 0); `colour` or `corners` colour the
content. The frame's material is named `<pane>_F`.

## 2d. Overflow (layer 6)

A text's lines must fit its pane at its font size: more lines than the pane is high is Cook.Layout.TextTall {where,
lines, height, max}; a line at least wider than the pane (each character at least a quarter of the font width, a
full-width one the whole) is Cook.Layout.TextWide {where, line, width, max}.

## 2a. A mod's fonts (layer 2)

`layers/<l>/assets/<path>.font.json` names a TrueType or OpenType file beside it (`.ttf`, `.otf`, `.ttc`, `.otc`,
the first face). The cook draws its characters into the game's own font form and writes, at `Luma/<id>/<path>`:
- `.brfnt`: a BRFNT with I8 coverage sheets, which the store cooks as it cooks the game's fonts (single-channel
  distance sheets, NFNT), so it stays sharp at any drawn size. UTF-16 codes, up to U+FFFF.
- `.kern`: an NTBL, int columns `left`, `right` (character codes) and `x` (whole source pixels added between the
  pair). From the font's GPOS `kern` feature (pair adjustments, both formats; the first subtable of a lookup that
  holds the pair decides it, lookups add up), or from its `kern` table's horizontal pairs when it has no GPOS `kern`.
  Only characters below U+3000; pairs of 0 left out; at most 65,536 rows, the pairs that move most kept
  (Cook.Font.KernCut {uri, count, max}, a note).
- `.fallback`: the name of the game font that draws a character the font lacks, and a line end.

The source:
```json
{ "file": "Title.ttf", "size": 48, "glyphs": { "chars": "★♥", "ranges": [["U+0400", "U+04FF"]] }, "fallback": "MessageFont26" }
```
- `file` (required): a path beside the source, `/` between folders, no `..` or `:` (Cook.Effect.FilePath); a file
  the mod lacks: Cook.Effect.FileMissing; not a font: Cook.Font.Unreadable {uri}.
- `size`: pixels an em it is drawn at, 8 to 96, default 48. This is the sheets' detail, not the size on screen.
- `glyphs`: the characters drawn are always Latin (U+0020-007E, U+00A0-024F), its punctuation (U+2010-2027,
  U+2030-203A) and the euro sign, those the font has, and also:
  - `chars`: a string of characters (past U+FFFF: Cook.Font.Beyond {where, char});
  - `ranges`: [first, last] pairs, each a number or `"U+hhhh"` (Cook.Font.Code {where, value};
    first after last: Cook.Font.RangeOrder {where}; past U+FFFF: Cook.Font.Beyond);
  - `all`: true draws every character the font maps up to U+FFFF.
  Characters asked for by `chars` or `ranges` that the font lacks are named in a note, Cook.Font.Missing {uri,
  count, chars (the first 20), fallback}; the fallback draws them. None drawn at all: Cook.Font.NoGlyph {uri}.
- `fallback`: one of the game's five fonts, default `MessageFont26` (Cook.Effect.Choice).
- Every key is checked as section 2's (Cook.Effect.UnknownPart and the rest).

How it sits: a layout's `font_size` N makes the font's ascent plus descent N high (its `height`, and its `width` the
same, so [N, N] keeps its shapes); lines are its ascent + descent + line gap apart. The cursor stands on the
baseline, the font's ascent below the line's top. A cell holds the tallest and deepest ink of the font with 2 pixels
of margin (the distance field's reach); a glyph's `left` is its ink's left less the margin and its advance is the
font's, rounded to whole pixels. The game's form holds a line, an advance or a left of at most 127 pixels and a cell
of at most 255. Past that: Cook.Font.TooLarge {uri, size, most}, where `most` is the largest size that fits.
Sheets are at most 1024 x 1024 and a font at most 32 sheets: Cook.Font.TooMany {uri, count, max, size}.

A `.font.json` outside `Luma/` (in `files/`): Cook.Font.NotAsset {file}. The font file itself is not a game file:
the overlay skips `.ttf`/`.otf`/`.ttc`/`.otc` under `Luma/`.

The fact: `cook.font` for the file made (`Luma/<id>/<path>.brfnt`) with `other` = its fallback, then `glyphs`,
`sheets`, `kern`, `size`, `missing`.

## 3. Checks before a pack

`.layout.json` is in `cooked_source`: `lumalink check` and `pack` with `--game` cook it (L804 on the layout file, in
the player's language). The texture check needs the mod's folder: on a packed `.luma` LumaLink cannot look, and the
game's start does (it cooks from the unpacked mod).

## 4. The fact

`cook.layout` with `other` = the layout file made (`LayoutData/<Name>.arc/blyt/<Name>.brlyt`), then `panes` (the
mod's, without the root), `pictures`, `texts`, `textures`, `groups`.

## 5. Patching a game layout by pane name (layer 5)

`layers/<l>/patches/<Archive>.arc/blyt/<Name>.brlyt.json`:

```json
{ "patch": 1, "panes": { "Count": { "text": "x 99", "position": [5, 6] }, "Icon": { "visible": false } } }
```

The game start applies every mod's patch of one layout in the mods' order to the layout as the store holds it, so
two mods changing different panes of one HUD both take effect, and a later mod's value for one field wins. Any pane
takes `position` [x, y], `size` [w, h], `scale` (number or [x, y]), `rotation` (degrees about the screen's axis, or
[x, y, z]), `alpha` 0-255, `visible`; a text pane also `text` (at most what the game's pane holds: its buffer,
Cook.Layout.TextLong) and `colour` / `colours` [top, bottom]; a picture `colour` or `corners`. Other keys:
Cook.Effect.UnknownPart. A pane the layout lacks: Cook.Layout.NoPane. A patch that cannot apply is named in the
store's left-out file (`patch/<mod>/<entry>`) and the others still apply.

The validator checks the file's form (`patch` 1: Patch.NoVersion/Version; `panes` an object: Patch.NoPanes/
PanesShape; no other key: Patch.UnknownKey) and that the game has the layout (L302); the panes are the cook's. A
`"patch"` key in a `.layout.json` is Cook.Layout.PatchPlace, which names this place.

Not built: text by message key (today: literal text, or `ui.set_text`); 10,000 panes drawn at 4K is a run, not a
cook check.

## 6. Conformance

- 3851-layout-cooked: gl-hud's layout (every layer-1 pane kind, English and Japanese text, a group, a texture) is
  made, with its counts in `cook.layout`;
- 3852-layout-no-texture: gl-hud-bad names a texture the mod lacks and is refused by name.

Capability `layout.cook`. Not yet proven in a game run; drawing it needs the `ui.open` test on gl-hud.
