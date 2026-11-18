---
title: Adding a level timer to the HUD
shortTitle: A level timer
intro: Add a screen of your own to the HUD, move one of the game's counters to make room, and show text in every language the game has.
contentType: tutorials
example: a-level-timer
---

## Introduction

In this tutorial, you add a timer to the HUD. It sits at the bottom right of the screen, where the coin counter was, and counts how long the player has been in the level. The coin counter moves up to make room. The timer's label is in the player's language.

You learn how to:

* Make a screen of your own: a layout with panes, a picture, text and an animation.
* Move a pane of one of the game's screens with a patch.
* Add message lines in every language and show them.
* Open a screen with a stage and change its text each step.
* Test text in every language of the game.

## Prerequisites

* Nebula, installed with Nebula Setup.
* LumaLink. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* The [Hiding the HUD](hide-the-hud.md) tutorial.

## Mod folder layout

```text
a-level-timer/
  luma.json
  layers/base/files/LayoutData/LevelTimer.arc/LevelTimer.layout.json   the screen
  layers/base/files/LayoutData/LevelTimer.arc/timg/Clock.png           its picture
  layers/base/patches/LayoutData/CoinCounter.arc/blyt/coincounter.brlyt.json   the coin counter moved
  layers/base/merge/MessageData/messages.json                           the label in every language
  source/main.lum
  tests/
```

## Step 1: Make the screen

A screen is a layout: panes placed from the screen's middle, in the game's units (608 by 456 for 4:3; `widen` keeps a pane at its edge on a wide screen). The timer is one pane with a dark back, a clock picture and two texts:

```json
{
  "panes": [
    {
      "name": "Timer", "type": "pane", "position": [222, -168], "widen": true,
      "children": [
        { "name": "Back", "type": "picture", "size": [150, 52], "colour": "00000080" },
        { "name": "Clock", "type": "picture", "texture": "Clock", "size": [28, 28], "position": [-56, 8] },
        { "name": "Label", "type": "text", "size": [104, 20], "position": [14, 14], "text": "Time", "length": 24,
          "font": "MessageFont26", "font_size": 18, "place": "left", "colour": "ffffffff" },
        { "name": "Time", "type": "text", "size": [104, 26], "position": [14, -10], "text": "0:00.00", "length": 12,
          "font": "NumberFont", "font_size": 22, "place": "left", "colour": "ffe060ff" }
      ]
    }
  ]
}
```

A text's `length` is the most characters it holds. `MessageFont26` has every language's characters; the game's `NumberFont` has digits and a few signs. The picture `Clock` is `timg/Clock.png` in the same archive. The finished layout also has an animation, `In`, that slides the timer in.

For more information, see [Adding screens and fonts](../how-tos/adding-screens-and-fonts.md).

## Step 2: Move the coin counter

The game's coin counter stands where the timer goes. A patch of its layout moves it up, and changes nothing else of it:

```json
{
  "patch": 1,
  "panes": {
    "CoinCounter": { "position": [276, -100] }
  }
}
```

## Step 3: Add the label in every language

Message lines are merged into the game's messages. Each line has a `default` and the languages that differ from it:

```json
{
  "lines": {
    "LevelTimer_Label": {
      "default": "Time",
      "fr": "Temps", "es": "Tiempo", "de": "Zeit", "it": "Tempo", "nl": "Tijd", "pt": "Tempo",
      "ja": "タイム", "ko": "타임", "zh-Hans": "时间", "zh-Hant": "時間"
    }
  }
}
```

The player's language takes its own line, else its base language's (`fr` for `UsFrench` and `EuFrench`), else `default`. For more information, see [Text and service values](../reference/lumascript/text-and-service-values.md).

## Step 4: Open the screen and count

```lumascript
use svc.ui

var screen: ui.Screen? = none
var time_pane: ui.Pane? = none
var steps: u32 = 0

on stage ready {
    steps = 0
    if MR::isStageFileSelect() or MR::isStageAstroLocation() {
        return
    }
    guard let s = ui.open("LevelTimer", ui.Layer.Hud) else { return }
    if let label = ui.pane(s, "Label") {
        ui.set_text(label, text "LevelTimer_Label")
    }
    time_pane = ui.pane(s, "Time")
    ui.play(s, "In", false, none)
    screen = s
}
```

`text "LevelTimer_Label"` is the message line, in the player's language. The timer doesn't show on the file select or in the observatory.

The time counts the world's steps, so it stops while the game is paused, as the world does. The world steps 60 times a second whatever the display's frame rate:

```lumascript
fn clock(n: u32) -> text {
    let hundredths = n * 100 / 60
    let seconds = hundredths / 100
    let cc = hundredths % 100
    let ss = seconds % 60
    return "{seconds / 60}:{if ss < 10 { "0" } else { "" }}{ss}.{if cc < 10 { "0" } else { "" }}{cc}"
}

on step {
    guard let p = time_pane else { return }
    steps += 1
    ui.set_text(p, clock(steps))
}

on stage ended {
    screen = none
    time_pane = none
}
```

A screen closes with its stage, so the mod forgets its panes on `stage ended`.

## Step 5: Test it in every language

The game writes every text pane it draws as a fact, `ui.text.N`, with the pane's name and its text. A test for each language sets the language and checks the label:

```json
{
  "harness": 1,
  "name": "level-timer-label-jpjapanese",
  "about": "In JpJapanese the timer's label reads タイム (the mod's message line in the player's language)",
  "mods": ["../.."],
  "stage": { "name": "EggStarGalaxy", "scenario": 1 },
  "language": "JpJapanese",
  "disc": "RMGJ01",
  "steps": [{ "at": 30, "end": true }],
  "expect": [
    { "fact": "ui.text.2", "has": { "pane": "Label", "text": "タイム" }, "when": "last" },
    { "fact": "mod.level-timer.opened", "is": true }
  ]
}
```

The finished mod has one such test for each of the 10 languages of the game's discs. A European language needs the European disc's game, and Japanese the Japanese disc's: `disc` names it. For more information, see [Test files](../reference/test-files.md).

```shell
lumalink test a-level-timer
```

## Further reading

* [Adding screens and fonts](../how-tos/adding-screens-and-fonts.md)
* [Hiding the HUD](hide-the-hud.md)
* [Test files](../reference/test-files.md)
