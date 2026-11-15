---
title: Adding sounds and music
shortTitle: Add sounds and music
intro: Add sound effects and music to a mod, replace the game's sounds, voices and music, and play sounds from code.
contentType: how-tos
---

## About sounds in a mod

A mod's sounds are ordinary sound files, WAV, FLAC or Ogg Vorbis, each with a short JSON file that says how it plays. Nebula cooks them for the game's own mixer, so they follow the player's volume settings like the game's sounds.

There are four kinds of sound:

| Kind | What it is | Length |
|---|---|---|
| `effect` | A sound effect, or a short piece of music. Your code plays it. It can also replace one of the game's sound effects. | Up to 60 seconds |
| `voice` | A voice line that replaces one of the game's. | Up to 60 seconds |
| `stream` | Music that replaces one of the game's music tracks. | Up to 20 minutes |
| `sequence` | A MIDI song, played by the game's own instruments. | Any |

## Prerequisites

* LumaLink and an installed Nebula. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* A mod folder. For more information, see [Creating a mod](creating-a-mod.md).

## Adding a sound effect

1. Put the sound file in the mod's assets, for example `layers/base/assets/Ding.wav`.
1. Beside it, add `Ding.sound.json`:

   ```json
   { "kind": "effect", "file": "Ding.wav", "volume": 0.8, "pitch": [0.95, 1.05], "positioned": false }
   ```

1. Check the mod:

   ```shell
   lumalink check my-mod
   ```

The sound's name is its path in `assets/` without `.sound.json`: here, `Ding`.

### The sound's description

| Field | What it does | Default |
|---|---|---|
| `kind` | `effect`, `voice`, `stream` or `sequence`. | Required |
| `file` | The sound file beside it. | Required |
| `volume` | 0 to 4. 1 is the file's own volume. | 1 |
| `pitch` | 0.25 to 4. A pair `[lowest, highest]` picks a pitch between them each time it plays. | 1 |
| `distance` | `[near, far]` in game units: full volume within `near`, silent beyond `far`. | `[300, 3000]` |
| `positioned` | `false` plays it at the listener, at full volume, with no distance. | `true` |
| `priority` | 0 to 255. When too many sounds play, the lowest go first. | 128 |
| `bus` | The player's volume setting it follows: `effects`, `ui`, `voice` or `music`. | `effects` |
| `loop` | `true` loops it, `false` doesn't. A pair `{"start": S, "end": E}` loops between two sample frames of the file. Left out, the file's own loop points are used, if it has any. | Left out |
| `replace` | The name of a game sound it replaces. | None |

### The sound file

* WAV (8, 16, 24 or 32-bit, or float), FLAC or Ogg Vorbis. Ogg Opus isn't read.
* 1 or 2 channels, 8,000 to 384,000 Hz.
* Up to 60 seconds for an effect or voice. For longer music, use a stream.

Nebula converts every sound to 48,000 Hz, 16-bit. Loop points are kept exactly where the file sets them.

## Playing a sound from code

The `audio` service plays a mod's sounds by name. It needs no permission.

```lumascript
use svc.audio

on stage "EggStarGalaxy" {
    audio.play("Ding", none, none)
}
```

* `audio.play(name, how, function)` plays a sound. `how` can change its volume, pitch, bus and loop for this play. The function, if you give one, is told when it ends.
* `audio.play_at` plays it from a place in the world.
* `audio.voice_stop` stops a sound that `audio.play` started.

To name another mod's sound, write `MOD-ID:NAME`. A name with no sound plays a short placeholder chime, and the mod log says which name was missing.

### Playing music

To play a mod's music in place of the game's:

1. Add it as an effect on the `music` bus, looped:

   ```json
   { "kind": "effect", "file": "Theme.wav", "bus": "music", "positioned": false, "loop": true }
   ```

1. Play it from code. The second number is the fade, in milliseconds:

   ```lumascript
   audio.music("music/Theme", 1000)
   ```

The game's own music fades out while yours fades in. `audio.music_restore(1000)` brings the game's back. If two mods play music, the later call wins.

For a long track, add a stream instead of an effect. A stream can be up to 20 minutes long, and Nebula reads it from disk as it plays:

```json
{ "kind": "stream", "file": "Theme.ogg" }
```

When no cooked sound has the name you pass, `audio.music` plays your mod's stream of that name. Nebula plays its first two channels and loops it as it was cooked. A stream cooked with `"loop": false` plays once, and then the game's music comes back.

LumaKit can set music for a whole galaxy, so it plays whenever that stage's music would. For more information, see [About LumaKit](../concepts/lumakit.md).

## Replacing a game sound

1. Find the game sound's name. To list the names that contain `JUMP`:

   ```shell
   lumalink list sounds JUMP
   ```

   For example, `SE_PM_JUMP_M` is Mario's jump.
1. Add a sound with `replace`:

   ```json
   { "kind": "effect", "file": "Boing.wav", "replace": "SE_PM_JUMP_M" }
   ```

Everywhere the game plays that sound, it plays yours. A name the game doesn't have is an error. When two mods replace the same sound, the mod higher in the load order wins.

### Replacing a voice line

A voice line is an effect on the `voice` bus that must replace one of the game's sounds:

```json
{ "kind": "voice", "file": "Hello.wav", "replace": "SE_PV_JUMP_M" }
```

## Replacing the game's music

The game's music tracks are streams. To replace one:

1. Find its name. To list the game's music tracks:

   ```shell
   lumalink list files AudioRes/Stream
   ```

   The name is the file name without `.ast`, for example `SMG_astrodome02_strm`.
1. Add a stream with `replace`:

   ```json
   { "kind": "stream", "file": "Observatory.ogg", "replace": "SMG_astrodome02_strm" }
   ```

A stream can be WAV, FLAC, Ogg Vorbis, or a BRSTM, BCSTM or BFSTM stream from another game, with up to 6 channels and up to 20 minutes long. Left out, `loop` uses the file's own loop points, or loops the whole track. Set `"loop": false` to play it once.

## Adding a MIDI song

A sequence is a Standard MIDI file (format 0 or 1), played by the game's own instruments:

```json
{
  "kind": "sequence",
  "file": "tune.mid",
  "instruments": {
    "0": { "bank": 0, "program": 5 },
    "drums": { "bank": 3, "program": 0 }
  },
  "loop": true,
  "replace": "PATH-IN-JaiSeq.arc"
}
```

* `instruments` maps each MIDI program the song uses (`"0"` to `"127"`, or `"drums"` for channel 10) to one of the game's instruments, by `bank` and `program`.
* `loop` set to `true` loops between the MIDI markers `loopStart` and `loopEnd` (or controller 111), or the whole song without markers.
* `replace` names a game song by its path in `AudioRes/Seqs/JaiSeq.arc`.

Notes, program changes, volume (controller 7), pan (controller 10) and pitch bend are played. Other controllers, aftertouch and system exclusive messages are left out, and LumaLink counts them in a note.

## Troubleshooting

| Message | What to do |
|---|---|
| The sound lasts SECONDS seconds; a sound effect lasts at most 60 | Use a `stream` for long music. |
| ... names the game sound "NAME", which the game does not have | Find the name with `lumalink list sounds`. |
| ... names the game stream "NAME", which the game does not have | Find the name with `lumalink list files AudioRes/Stream`. |
| FILE is Ogg Opus | Export it as Ogg Vorbis, FLAC or WAV. |
| The loop of FILE is not inside its frames, or is empty | Set loop points inside the file's length. |
| FILE plays the MIDI program N, which instruments does not map | Add that program to `instruments`. |

While the game runs, a sound name with no sound is a line in the mod log, `Content.MissingSound`. For more information, see [Reading the mod log](reading-the-mod-log.md).

## Further reading

* [Adding animations](adding-animations.md)
* [Services](../concepts/services.md)
* [About mod files and layers](../concepts/mod-files-and-layers.md)
