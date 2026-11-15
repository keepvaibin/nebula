---
title: About permissions
shortTitle: Permissions
intro: Mods that run code ask the player before they start, and list every sensitive service they use as a permission the player can allow or refuse.
contentType: concepts
---

## About permissions

Mod code can call Nebula's services. Most services are safe for any mod, such as drawing on the screen or reading the controls. Some services reach outside the game: the internet, the player's files, the camera or the clipboard. A mod that uses one of these lists it as a permission in its `luma.json`, with a line that says why.

The player answers once for each mod. Nebula asks again only when an update changes the mod's code or its permissions.

## Permissions

| Permission | What the player sees | What it allows |
|---|---|---|
| `audio.microphone` | Microphone | Hears the microphone. |
| `capture.camera` | Camera | Sees the camera. |
| `fs.exchange` | Exchange folder | Reads and writes files in the Exchange folder, which the player and other mods can open. |
| `input.devices` | Other devices | Uses Bluetooth, USB and MIDI devices. |
| `input.inject` | Controls | Presses buttons and moves the controls as if the mod were the player. |
| `native` | Native libraries | Loads libraries of its own, with full access to the device. |
| `net.internet` | Internet | Connects to the internet hosts the mod lists. |
| `net.local` | Local network | Connects to devices on the local network and to programs on the same device. |
| `save.game` | Game saves | Reads the player's save files, and offers a save to replace one. The player confirms each replacement. |
| `system.clipboard` | Clipboard | Reads what the player copies, text and pictures, and replaces it. |
| `system.links` | Web pages | Opens web pages in the player's browser. |

## Declaring a permission

List each permission in the `permissions` array of `luma.json`. Every entry has a `name` and a `why`. An `net.internet` entry also lists its `hosts`.

```json
"permissions": [
  {
    "name": "net.internet",
    "hosts": ["api.example.org"],
    "why": "Uploads your best times."
  }
]
```

The player sees `why` as you write it. Write it in plain words and say what the mod does with the permission. To translate it, add it under `text` in `luma.json`. For more information, see [luma.json](../reference/luma-json.md).

A mod without code cannot use a service, so a permission in a mod without code has no effect. LumaLink and Nebula report it as a note.

## Required and optional permissions

A permission is **required** when the mod's code cannot start without it. The `native` permission is always required.

A permission is **optional** when the code uses its service as optional. When the player refuses an optional permission, the mod starts and the service is `none`.

* In LumaScript, write `use optional svc.NAME`, and test the service before each call, for example with `if net != none`. A call without that test is a compile error.
* In C, import the service with `LUMA_OPTIONAL`. Its table pointer is null when the service is not available.

The question shows "Needed to start" under each required permission and "It starts without this" under each optional one.

## When Nebula asks

Nebula asks about a mod with code at these moments:

* When the player imports the mod. The mod is installed either way. **Allow** turns it on, and **Not now** installs it turned off.
* When the player turns the mod on, on the **Mods** page.
* At the start of the game, when an update has changed the mod's code or a permission since the player answered. The mod does not start in that session. The question opens at the title screen, and the answer applies at the next start.

Changing only the `why` line does not ask again. Adding a host to `net.internet`, or adding a permission, does.

The question lists the mod's name and version, a warning that the mod runs its own code, and one row for each permission. Its focus starts on **Not now**, so a button pressed by accident allows nothing. **Choose** lets the player allow some optional permissions and refuse others.

Mod code cannot open, answer or skip the question.

## Changing answers later

On the mod's page on the **Mods** page, the player can change the answer for the mod's code and for each permission. A refused permission is listed under "Not allowed". A changed answer applies at the next start.

## Further reading

* [Calling services](../reference/services/calling-services.md)
* [When a mod fails](when-a-mod-fails.md)
