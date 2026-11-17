---
title: Services reference
shortTitle: Services
intro: Look up every service a mod can call, its version, its permission and its functions.
contentType: reference
---

## About the services reference

Each page lists a service's limits, types, events and functions as its C header declares them. For how a function looks in LumaScript, how statuses and events work, and which mistakes Nebula reports, see [Calling services](calling-services.md).

## Services

| Service | Page | What it does |
|---|---|---|
| `time` | [time](time.md) | Frames, world steps, clocks, timers and the world clock's speed. |
| `events` | [events](events.md) | The game's events (a coin, a star, a stage starting), told at the next frame's start. |
| `system` | [system](system.md) | Facts about the platform and display, the clipboard and opening links. |
| `system.clipboard` | [system](system.md) | Read and write the clipboard. |
| `system.links` | [system](system.md) | Open a web page in the player's browser. |
| `mods` | [mods](mods.md) | Other mods, options, library files and mod-wide events. |
| `mods.ids` | [mods](mods.md) | Numbers that stay the same for a mod. |
| `mods.install` | [mods](mods.md) | Ask the player to install a mod. |
| `fs` | [fs](fs.md) | Your mod's own files. |
| `fs.exchange` | [fs](fs.md) | Files the player picks to share with your mod. |
| `save` | [save](save.md) | Data stored with the player's save file. |
| `save.global` | [save](save.md) | Data for all save files. |
| `save.game` | [save](save.md) | The game's own save data. |
| `input` | [input](input.md) | Your mod's actions, what the game reads, text entry, rumble and pointer source. |
| `input.inject` | [input](input.md) | Replace what the game reads from a controller. |
| `input.dev` | [input](input.md) | Raw HID devices. |
| `net.local` | [net](net.md) | Sockets, endpoints and TLS on this machine and the local network. |
| `net.internet` | [net](net.md) | Sockets, TLS, HTTP and WebSockets to the hosts your mod names. |
| `audio` | [audio](audio.md) | Sounds, music, streams, placed sounds and the mix. |
| `audio.microphone` | [capture](capture.md) | A microphone's samples. |
| `capture.camera` | [capture](capture.md) | A camera's pictures. |
| `assets` | [assets](assets.md) | Find, read, provide and cook files. |
| `ui` | [ui](ui.md) | Screens and layouts. |
| `draw` | [draw](draw.md) | Shapes, text and pictures at draw points. |
| `draw.inst` | [draw.inst](draw-inst.md) | Many copies of a model. |
| `draw.light` | [draw.light](draw-light.md) | Lights in the scene. |
| `draw.native` | [draw.native](draw-native.md) | The graphics device, for C mods. |
| `draw.post` | [draw.post](draw-post.md) | Full-screen pixel programs. |
| `draw.view` | [draw.view](draw-view.md) | Extra cameras. |
| `anim` | [anim](anim.md) | Animations and their marks. |
| `content.fx` | [content.fx](content-fx.md) | Particle effects. |
| `content.model` | [content.model](content-model.md) | Models on actors, materials and parts. |
| `movie` | [movie](movie.md) | Video files. |
| `player` | [player](player.md) | The player character. |

## Further reading

* [About services](../../concepts/services.md)
* [Permissions](../../concepts/permissions.md)
* [Writing code in C](../../how-tos/writing-code-in-c.md)
