---
title: Error codes
shortTitle: Error codes
intro: Find out what an error code means when Nebula stops, and what to do next.
contentType: reference
---

## About error codes

When Nebula stops, it shows an error code and saves a report in `%LOCALAPPDATA%\Nebula\crashes`. The report is two files:

* A `.txt` file with the code, the mods that were on, and the technical details.
* A `.dmp` file for the developers.

When you report a problem, attach both files.

## Game errors

| Code | Meaning | What to do |
|---|---|---|
| NEB-C01 | The game's code crashed, and no mod was changing the game. | Start again. If it happens again at the same place, report it with the report files. |
| NEB-C02 | The game stopped itself on one of its own checks. The report quotes its message. | Start again. If it happens again at the same place, report it with the report files. |
| NEB-C03 | The game's code ran out of stack space. | Start again. If it happens again at the same place, report it with the report files. |

## Mod errors

| Code | Meaning | What to do |
|---|---|---|
| NEB-M01 | The game crashed while mods were changing its files or code, for example Riivolution or file mods. The report lists them. | Turn the mods off and play again. If the crash stops, turn the mods on one at a time to find the one that causes it. |
| NEB-M02 | The game crashed within 15 seconds of reading a file that a mod supplies. The report names the mod and the file. | Turn that mod off, or check that it is made for your game's region and version. |
| NEB-M03 | The game crashed in a mod's code, or in game code that the mod's code called. The report names the mod. Windows only. | Turn that mod off and send its author the report files. |
| NEB-T01 | An image in a texture pack stopped Nebula while it loaded. The report names the file. | Remove the image from the pack, or save it again as a PNG. |
| NEB-V01 | A replacement movie stopped Nebula while it played. The report names the file. | Remove the video, or convert it to an H.264 MP4. |

Texture packs and movies change no game code. A crash in the game with only these turned on is NEB-C01 or NEB-C02, not NEB-M01.

## Graphics and memory errors

| Code | Meaning | What to do |
|---|---|---|
| NEB-G01 | The graphics driver reset the graphics card: the device was removed, hung or lost. | Update the graphics driver. Turn off frame generation, or lower the internal resolution. |
| NEB-G02 | The renderer stopped on another error. The report quotes it. | Update the graphics driver. If it happens again, report it. |
| NEB-R01 | Nebula ran out of memory. | Close other programs, set texture packs to **On** instead of **Preload**, or lower the internal resolution. |

## Nebula errors

| Code | Meaning | What to do |
|---|---|---|
| NEB-N01 | Nebula's own code crashed, not the game's. | Start again. If it happens again, report it. |
| NEB-N02 | Nebula stopped on an unexpected error. The report quotes it. | Start again. If it happens again, report it. |
| NEB-N03 | Nebula stopped itself (abort). | Start again. If it happens again, report it. |
| NEB-N04 | Nebula stopped on an internal error while starting. | Start again. If it happens again, report it. |

## Start-up errors

| Code | Meaning | What to do |
|---|---|---|
| NEB-S01 | Nebula could not find or open the game content, `game.pak`. | Open Nebula Setup and choose **Repair**. |
| NEB-S02 | The game content is damaged. | Run Nebula Setup again to rebuild it from your disc image. |
| NEB-S03 | The game module is missing, the system refused to load it, or it is from another version of Nebula. The title says which ("A part of Nebula is missing", "The system refused a part of Nebula", "Nebula's files do not match"), and the report's **Technical details** name the file and, when the system refused it, the system's own reason. | Install or update Nebula again, from the original download. If the system refused it, report it with the report file. On a Mac, see **How to open Nebula.txt** in the download. |
| NEB-S04 | The graphics card could not start the game. | Update the graphics driver. If your computer has two graphics cards, choose the other one in **Settings** > **SYSTEM** > **GRAPHICS CARD**. |
| NEB-S05 | Nebula could not read its settings file. | Rename `config.ini` in Nebula's settings folder, then start Nebula to use the default settings. |
| NEB-S06 | Nebula was started with arguments it does not know. | Start Nebula from its shortcut or app menu entry. |

## Further reading

* [Diagnostics](diagnostics.md)
* [Command-line arguments](command-line-arguments.md)
