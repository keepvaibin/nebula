# Error codes

When Nebula stops, it shows a code and saves a report in `%LOCALAPPDATA%\Nebula\crashes` (a `.txt` with the code, the
mods that were on and the technical details, and a `.dmp` for the developers). The codes come from
`runtime/src/crash_report.cpp`.

## The game

| Code | Meaning | What to do |
|---|---|---|
| NEB-C01 | The game's code crashed, with no mod changing the game. | Start again; if it repeats at the same place, report it with the report file. |
| NEB-C02 | The game stopped itself with an error (its own fatal check). The report quotes its message. | As C01. |
| NEB-C03 | The game's code ran out of stack space. | As C01. |

## Mods

| Code | Meaning | What to do |
|---|---|---|
| NEB-M01 | The game crashed while mods changed its files or code (Riivolution or file mods). The report lists them. | Turn the mods off and play again; if it no longer happens, turn them on one at a time to find the one. |
| NEB-M02 | The game crashed within 15 seconds of reading a file a mod supplies. The report names the mod and the file. | Turn that mod off, or check that it is made for this game's region and version. |
| NEB-T01 | A texture pack image stopped Nebula while it loaded. The report names the file. | Remove the image from the pack or save it again as a PNG. |
| NEB-V01 | A replacement movie stopped Nebula while it played. The report names the file. | Remove the video or convert it to an H.264 MP4. |

Texture packs and movies change no game code, so a crash in the game with only those on is C01 or C02, not M01.

## Graphics and memory

| Code | Meaning | What to do |
|---|---|---|
| NEB-G01 | The graphics driver reset the graphics card (device removed, hung or lost). | Update the graphics driver; turn off ray tracing and frame generation or lower the internal resolution. |
| NEB-G02 | The renderer stopped on another error. The report quotes it. | Update the graphics driver; report it if it repeats. |
| NEB-R01 | Nebula ran out of memory. | Close other programs, set texture packs to ON instead of PRELOAD, or lower the internal resolution. |

## Nebula

| Code | Meaning | What to do |
|---|---|---|
| NEB-N01 | Nebula's own code crashed (not the game's). | Start again; report it if it repeats. |
| NEB-N02 | Nebula stopped on an unexpected error. The report quotes it. | As N01. |
| NEB-N03 | Nebula stopped itself (abort). | As N01. |
| NEB-N04 | Nebula stopped on an internal error while starting. | As N01. |

## Starting

| Code | Meaning | What to do |
|---|---|---|
| NEB-S01 | The game content (`game.pak`) was not found or could not be opened. | Run Nebula Setup again, or choose the content in Nebula settings > Advanced. |
| NEB-S02 | The game content is damaged. | Run Nebula Setup again to rebuild it from your disc image. |
| NEB-S03 | Nebula's files do not match each other (the game module is missing or from another version). | Install or update Nebula again. |
| NEB-S04 | The graphics card could not start the game. | Update the graphics driver; with two graphics cards, choose the other in Nebula settings > Display. |
| NEB-S05 | Nebula's settings file could not be read. | Rename `config.ini` in Nebula's settings folder to start with default settings. |
| NEB-S06 | Nebula was started with options it does not know. | Start Nebula from its launcher or shortcut. |
