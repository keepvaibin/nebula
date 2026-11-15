---
title: About services
shortTitle: Services
intro: Services are the functions Nebula gives mod code for input, saves, files, the network, audio, drawing, time and more.
contentType: concepts
---

## About services

The game's own functions let your code change how the game plays. Services give your code what the game never had: your own controls, values saved with the player's file, internet connections, sounds you mix yourself, extra views of the world, and facts about the device.

Each service has a name and a version, such as `net.internet` 1.4. Your code asks for the services it uses, and Nebula binds them when it loads your mod. A service the game does not have, or a version newer than the game's, stops your mod from loading with a message naming the service, unless your code uses it as optional.

## Using a service

In LumaScript, write `use svc.NAME` and call its functions:

```lumascript
use svc.save

on start {
    if save.get("seen") != none {
        log "this file was played with the mod before"
    }
    save.set("seen", b"\x01")
}
```

The `save` service keeps the value with the player's game file, so each file has its own.

In C, import the service in your import list and call it through its table. For more information, see [Calling services](../reference/services/calling-services.md).

Services that wait for the outside world, such as a web request, answer with an event at the start of a frame. In LumaScript, handle it with `on`, or wait for it in a task with `wait`.

## The services

| Service | Permission | What it does |
|---|---|---|
| `anim` | None | Plays and blends animations on models. |
| `assets` | None | Reads the game's and mods' files, and gives the game files at run time. |
| `audio` | None | Plays sounds and music, and mixes sound of your own. |
| `audio.microphone` | `audio.microphone` | Reads the microphone. |
| `capture.camera` | `capture.camera` | Reads the camera. |
| `content.fx`, `content.model` | None | Makes particle effects and models at run time. |
| `draw` | None | Draws shapes, text and pictures over the game. |
| `draw.inst`, `draw.light`, `draw.post`, `draw.view` | None | Draws many copies of a model, adds lights, adds full-screen effects, and renders extra views of the world. |
| `draw.native` | `native` | Draws with the device's graphics API. |
| `fs` | None | Reads and writes files in the mod's own folder. |
| `fs.exchange` | `fs.exchange` | Reads and writes files in the Exchange folder. |
| `input` | None | The mod's own actions, which the player binds in **Controls**. |
| `input.dev` | `input.devices` | Bluetooth, USB and MIDI devices. |
| `input.inject` | `input.inject` | Presses buttons as if the mod were the player. |
| `mods`, `mods.ids`, `mods.install` | None | The list of mods, unique numbers for your names, and asking to install a mod. |
| `movie` | None | Plays movies on the screen or on a model. |
| `net.internet` | `net.internet` | Connects to hosts on the internet: HTTP, WebSocket, TCP and UDP. |
| `net.local` | `net.local` | Connects to devices on the local network and programs on the same device. |
| `player` | None | Facts about the player. |
| `save`, `save.global` | None | Values kept with the player's game file, and values kept across files. |
| `save.game` | `save.game` | Reads the game's save files and offers one to replace. |
| `system` | None | The platform, renderer, versions, language, region, window and safe areas. |
| `system.clipboard` | `system.clipboard` | Reads and writes the clipboard. |
| `system.links` | `system.links` | Opens web pages in the player's browser. |
| `time` | None | Frames and world steps, timers, slowing and holding the game. |
| `ui` | None | Opens layouts as screens, and changes their panes. |

For permissions, see [About permissions](permissions.md).

## Rules every service keeps

* Nothing costs anything without a mod. A service does no work until a mod uses it.
* Everything ends with the mod. When a mod stops, its sounds, connections, files, timers and screens end with it.
* Messages are translated. A service that reports to the player does it through the mod log, in the player's language.

## Further reading

* [Calling services](../reference/services/calling-services.md)
* [About permissions](permissions.md)
