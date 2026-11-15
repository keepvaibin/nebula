---
title: Adding screens and fonts
shortTitle: Add screens and fonts
intro: Add screens of your own, such as a timer or a counter, change the game's screens, add fonts, and control screens from code.
contentType: how-tos
---

## About screens in a mod

The game's screens, such as the coin counter, the star counter and the pause menu, are layouts: panes of pictures and text, with animations. A mod writes a layout in JSON. Nebula cooks it into the game's own layout format, so it is drawn like the game's screens, widened for wide screens the same way.

A mod can:

* Add a screen, and show it from code.
* Change the game's screens by pane name: move, hide, resize or recolour a pane, or change its text.
* Add a font from a TrueType or OpenType file.

## Prerequisites

* LumaLink and an installed Nebula. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* A mod folder. For more information, see [Creating a mod](creating-a-mod.md).

## Adding a screen

A screen is a layout file in an archive of `LayoutData`, with its pictures beside it:

```text
layers/base/files/LayoutData/Timer.arc/Timer.layout.json
layers/base/files/LayoutData/Timer.arc/timg/Clock.png
```

The layout file:

```json
{
  "panes": [
    { "name": "Timer", "type": "pane", "position": [220, 180], "widen": true, "children": [
      { "name": "Clock", "type": "picture", "texture": "Clock", "size": [32, 32], "anchor": "right" },
      { "name": "Time", "type": "text", "size": [160, 32], "text": "0:00.00", "length": 16,
        "font": "NumberFont", "font_size": 28, "place": "left", "colour": "ffffffff" }
    ]}
  ]
}
```

The screen is 608 units wide and 456 high, with (0, 0) at its center, x to the right and y up. To use another size, set `"size": [WIDTH, HEIGHT]`.

### Panes

Every pane has a `name` (1 to 16 letters, digits or `_`, different from every other pane's) and a `type`:

| Type | What it is |
|---|---|
| `pane` | A group. It draws nothing, and moves its `children` together. |
| `picture` | A picture, from a texture or a color. |
| `text` | Text. |
| `window` | A frame that stretches without stretching its corners. |
| `bounding` | An area that draws nothing, for pointing and touching. |

What every pane can set:

| Field | What it does | Default |
|---|---|---|
| `position` | `[x, y]`: where its anchor stands, from its parent's position. | `[0, 0]` |
| `size` | `[width, height]`. Required for every type but `pane`. | |
| `anchor` | Which point of the pane stands at `position`: `top_left`, `top`, `top_right`, `left`, `center`, `right`, `bottom_left`, `bottom` or `bottom_right`. | `center` |
| `scale` | A number, or `[x, y]`. | 1 |
| `rotation` | Degrees, or `[x, y, z]`. | 0 |
| `alpha` | 0 (clear) to 255 (solid). | 255 |
| `alpha_to_children` | Its alpha fades its children too. | `true` |
| `visible` | Whether it shows. | `true` |
| `widen` | On a wide screen, its place moves out with the screen's edges, as the game's counters do. | `false` |
| `children` | The panes inside it. | None |

### Pictures

A picture's `texture` is a PNG in the archive's `timg/` folder, by its name without `.png`. `colour` (`rrggbbaa`) tints the whole picture, or `corners` gives four colors: top left, top right, bottom left, bottom right. Without a texture, the colors are the picture.

### Text

| Field | What it does | Default |
|---|---|---|
| `text` | The words, in any language. `\n` starts a new line. | |
| `length` | The most characters the pane holds, 1 to 255. Text your code sets is cut there. | 255 |
| `font` | One of the game's fonts, `MessageFont26`, `NumberFont`, `MenuFont64`, `PictureFont` or `CinemaFont26`, or a mod's font. See [Adding a font](#adding-a-font). | `MessageFont26` |
| `font_size` | A number, or `[width, height]`. | 26 |
| `place` | Where the text sits in its pane, by the anchor names. | `center` |
| `align` | How its lines line up: `as_place`, `left`, `center` or `right`. | `as_place` |
| `colour`, or `colours` | One color, or `[top, bottom]`. | White |
| `char_space`, `line_space` | Space added between characters and between lines. | 0 |

The text in the file must fit its pane at its font size. Text that is too long, too wide or too tall is refused, with the line and the room it has.

### Windows

A `window` pane names its frame picture in `frame`. `inflation`, `[left, right, top, bottom]`, grows its inside past the pane. `colour` or `corners` color its inside.

### Groups

`groups` names sets of panes, for your code:

```json
"groups": [ { "name": "Counter", "panes": ["Clock", "Time"] } ]
```

## Animating a screen

A layout's `animations` move its panes over frames, 60 a second:

```json
"animations": [
  { "name": "In", "frames": 20, "loop": false, "tracks": [
    { "pane": "Timer", "what": "alpha", "keys": [[0, 0], [20, 255]] },
    { "pane": "Timer", "what": "position_y", "keys": [[0, -40], [20, 0]] }
  ]}
]
```

Each track moves one value of one pane:

| `what` | What it moves |
|---|---|
| `position_x`, `position_y`, `position_z` | Its position. |
| `rotation_x`, `rotation_y`, `rotation_z` | Its rotation, in degrees. |
| `scale_x`, `scale_y` | Its scale. |
| `width`, `height` | Its size. |
| `alpha` | Its alpha. |
| `colour` | Its colors. |
| `visible` | Whether it shows: 0 or 1. |

A key is `[frame, value]`, or `[frame, value, slope]`. Without slopes, the value moves smoothly through the keys.

## Showing a screen from code

The `ui` service opens a layout as a screen, changes its panes and plays its animations. It needs no permission.

```lumascript
use svc.ui

on stage "EggStarGalaxy" {
    guard let screen = ui.open("Timer", ui.Layer.Hud) else {
        return
    }
    if let time = ui.pane(screen, "Time") {
        ui.set_text(time, "1:23.45")
    }
    ui.play(screen, "In", false, none)
}
```

* `ui.open(layout, layer)` opens `LayoutData/LAYOUT.arc`, yours or the game's. `ui.Layer.Hud` shows it with the game's HUD, stopped under the pause menu. `ui.Layer.Pause` shows it over the pause menu too.
* `ui.pane(screen, name)` finds a pane. `ui.set_text`, `ui.set_visible`, `ui.set_position` and `ui.set_scale` change it.
* `ui.play(screen, animation, loop, function)` plays an animation. The function, if you give one, is told when it ends.
* `ui.show(screen, false)` hides the screen, and `ui.close(screen)` closes it.

A screen opens while a stage runs. All of a mod's screens close when the stage changes or the mod stops.

LumaKit builds on this for HUD counters, menus and talk boxes. For more information, see [About LumaKit](../concepts/lumakit.md).

## Changing a game screen

You change one of the game's screens with a patch that names its panes. Only the panes and values you name change, so several mods can change the same screen.

1. Extract the screen's archive, for example the coin counter:

   ```shell
   lumalink extract LayoutData/CoinCounter.arc --output game-files
   ```

   Beside the layout, LumaLink writes `blyt/coincounter.brlyt.json`: every pane of the screen by name, with the values a patch can change, as the game has them.

1. Add a patch at the layout's path, with `.json` added:

   ```text
   layers/base/patches/LayoutData/CoinCounter.arc/blyt/coincounter.brlyt.json
   ```

1. Copy in only the panes you change, with only the values you change:

   ```json
   {
     "patch": 1,
     "panes": {
       "CoinCounter": { "position": [276, -120] },
       "PicCoin": { "visible": false },
       "TxtNumber": { "colors": ["ffd040ff", "ffa020ff"] }
     }
   }
   ```

1. Check the mod. LumaLink applies the patch to the game's screen, as the game does when it starts, and names a pane the screen doesn't have, or a value a pane doesn't take:

   ```shell
   lumalink check my-mod
   ```

Each pane, by name, can take `position`, `size`, `scale`, `rotation`, `alpha` and `visible`. A text pane can also take `text` and `colour` or `colours`. A picture can take `colour` or `corners`. Text can't be longer than the game's pane holds.

When several mods patch the same screen, each value comes from the mod highest in the load order that sets it, so two mods that change different panes both work.

## Adding a font

A mod's font is a TrueType or OpenType file with a short JSON file beside it, in `assets/`:

```text
layers/base/assets/fonts/Title.ttf
layers/base/assets/fonts/Title.font.json
```

```json
{ "file": "Title.ttf", "size": 48, "glyphs": { "chars": "★♥" }, "fallback": "MessageFont26" }
```

| Field | What it does | Default |
|---|---|---|
| `file` | The font file beside it: `.ttf`, `.otf`, `.ttc` or `.otc`. | Required |
| `size` | The detail it is drawn at, 8 to 96 pixels. Text stays sharp at any size on screen. | 48 |
| `glyphs` | Characters to draw besides Latin, its punctuation and the euro sign. `chars` lists characters, `ranges` lists `["U+0400", "U+04FF"]` pairs, and `"all": true` draws every character the font has. | Latin |
| `fallback` | The game font that draws characters your font lacks. | `MessageFont26` |

A text pane names the font as `Luma/MOD-ID/PATH`, its path in `assets/` without `.font.json`:

```json
{ "name": "Title", "type": "text", "size": [400, 60], "text": "Gold Rush", "font": "Luma/my-mod/fonts/Title" }
```

Kerning comes from the font. When the font lacks a character you asked for, LumaLink names it in a note, and the fallback font draws it.

## Troubleshooting

| Message | What to do |
|---|---|
| A layout goes in an archive | Put the file in `LayoutData/NAME.arc/`. |
| The texture NAME is not in the archive | Add `timg/NAME.png` beside the layout. |
| ... names the pane "NAME", which the layout does not have | Check the pane's name in the extracted `.brlyt.json`. |
| The text is longer, wider or taller than its pane | Make the pane bigger, the font smaller, or the text shorter. |
| The font has no glyph for some characters | A note: the fallback font draws them. Use a font that has them. |

While the game runs, a layout in no store is a line in the mod log, `Ui.MissingLayout`. For more information, see [Reading the mod log](reading-the-mod-log.md).

## Further reading

* [Changing Nebula's text](changing-nebulas-text.md)
* [About LumaKit](../concepts/lumakit.md)
* [Services](../concepts/services.md)
