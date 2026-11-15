---
title: audio service
shortTitle: audio
intro: Play sounds and music, stream samples, replace the game's music, place sounds in the world, and listen to the mix.
contentType: reference
---

## About the audio service

The `audio` service plays your mod's cooked sounds on the game's buses, streams samples your code makes, replaces the game's music, places sounds in the world, plays the game's own sounds, and lets your mod listen to the mix.

`music` plays a cooked sound, or a cooked stream (a `sound.json` with `"kind": "stream"`, up to 20 minutes, read from disk with its own loop) when no cooked sound has that name. With `"loop": false`, the stream plays once and the game's music comes back after it. Test files can check what plays with the harness fact `audio.music` (mod, name, kind `sound`, `stream` or `placeholder`, channels, rate, frames, looping).

Capture from a microphone or camera is in the [capture services](capture.md).

| Service | Version | Permission | C import | LumaScript |
|---|---|---|---|---|
| `audio` | 1.1 | None | `LUMA_SERVICE(slot, "audio", "1.1")` | `use svc.audio` |

The header is `native/include/luma_audio.h`. In LumaScript, a service is called by the last part of its name, such as `audio.` for `audio`. Each function below shows its LumaScript form first and its C form second. For how the forms are made, see [Calling services](calling-services.md).

### Details from the header

LumaCore's audio service. `audio` 1.0, no permission: a mod plays its cooked sound effects (3.7's NSFX: `layers/<l>/assets/<path>.sound.json`, cooked at the game's start) and pushes sound it makes itself (a synthesiser, a decoded movie, a voice chat) as a stream. Both go into the game's own mixer, after the game's voices, on four buses that follow the game's volume settings: effects and ui (Sound effects), voice (Voices), music (Music); the master volume and the output device are the game's. Every call is the game thread's, never blocks, and what waits is an event at a frame's start. No mod code ever runs on the audio thread: a stream is a ring the mod fills, never a callback. A mod that stops is silent the same audio frame. A voice can be heard from a place in the world (play_at); the game's own sounds start by name (game_play); the game's music can be replaced (music); the mix can be listened to (tap_open). Capture is luma_capture.h.

## Limits and constants

| Name | Value | Description |
|---|---|---|
| `LUMA_AUDIO_VOICES_MOST` | `32` | Voices a mod plays at once. |
| `LUMA_AUDIO_STREAMS_MOST` | `4` | Streams a mod holds at once. |
| `LUMA_AUDIO_NAME_MOST` | `255` | Bytes of a sound's name. |
| `LUMA_AUDIO_RATE_LEAST` | `8000` | A stream's rate, Hz. |
| `LUMA_AUDIO_RATE_MOST` | `192000` |  |
| `LUMA_AUDIO_RING_MS` | `500` | What a stream holds pushed and not yet played. |
| `LUMA_AUDIO_NEED_MS` | `100` | Under this much, a stream asks for more (NEED). |
| `LUMA_AUDIO_VOLUME_MOST` | `4.0f` | A voice's own volume (linear; 1: as cooked). |
| `LUMA_AUDIO_FADE_MOST_MS` | `10000` | Voice_stop's, music's and music_restore's longest fade. |
| `LUMA_AUDIO_TAPS_MOST` | `2` | Taps a mod holds at once. |
| `LUMA_AUDIO_TAP_FRAMES` | `1024` | Frames in a SAMPLES block (stereo, float). |
| `LUMA_AUDIO_GAME_SOUNDS_MOST` | `16` | The game's own sounds a mod holds at once (game_play). |
| `LUMA_AUDIO_PLACE_MOST` | `1.0e7f` | A place's coordinates, game units, either way. |

## Enumerations

### `LumaAudioBus`

The buses a voice or stream plays on (an NSFX names its own; `LUMA_AUDIO_BUS_SOUND` keeps it).

| Name | Description |
|---|---|
| `LUMA_AUDIO_BUS_EFFECTS = 0` | The game's Sound effects volume. |
| `LUMA_AUDIO_BUS_UI = 1` | Sound effects too; kept apart for the mod's own bus_volume. |
| `LUMA_AUDIO_BUS_VOICE = 2` | Voices. |
| `LUMA_AUDIO_BUS_MUSIC = 3` | Music. |
| `LUMA_AUDIO_BUS_SOUND = 255` | Play: the sound's own bus. |

### `LumaAudioEvent`

Events (`LumaEvent`.kind) to the fn given at play or stream_open; `handle` is the voice's or the stream's.

| Name | Description |
|---|---|
| `LUMA_AUDIO_EVENT_ENDED = 1` | A voice's, once: it played to its end (status `LUMA_OK`), or it was dropped for a voice of the mod's of higher priority (status `LUMA_E_LIMIT` (reported): `Audio.Dropped`). The handle is closed after it. A voice stopped by voice_stop has no event. |
| `LUMA_AUDIO_EVENT_NEED = 2` | A stream's: less than `LUMA_AUDIO_NEED_MS` waits to be played; value: the frames stream_push takes now. Once a frame at most, and again only after a push. |
| `LUMA_AUDIO_EVENT_SAMPLES = 3` | A tap's: data, `LUMA_AUDIO_TAP_FRAMES` stereo frames of float (left, right; valid during the call only); value: the frames; flags: the output's rate (Hz); status `LUMA_E_BUSY` when blocks before this one were lost (the mod took too long). |

### `LumaAudioTapSource`

What a tap hears (tap_open).

| Name | Description |
|---|---|
| `LUMA_AUDIO_TAP_OUTPUT = 0` | The game's output: the game's sound and every mod's, after the player's volumes. |
| `LUMA_AUDIO_TAP_MODS = 1` | Every mod's voices and streams, before the master volume (the game's own not in it). |

### `LumaAudioFollow`

What a place is from (`LumaAudioPlace`.follow).

| Name | Description |
|---|---|
| `LUMA_AUDIO_AT_POINT = 0` | Pos is a point in the world. |
| `LUMA_AUDIO_AT_PLAYER = 1` | Pos is from the player's place (world axes) and moves with the player. |
| `LUMA_AUDIO_AT_CAMERA = 2` | Pos is from the camera's place (world axes) and moves with the camera. |

## Structures

### `LumaAudioPlace`

Where a sound is heard from: the camera hears it (distance and side), as the game hears its own.

| Name | Description |
|---|---|
| `uint32_t size` | Sizeof(`LumaAudioPlace`). |
| `uint32_t follow` | `LumaAudioFollow`. |
| `float pos[3]` | Game units, each within `LUMA_AUDIO_PLACE_MOST`. |
| `uint32_t reserved` |  |

### `LumaAudioPlay`

How play plays a sound (null: as cooked).

| Name | Description |
|---|---|
| `uint32_t size` | Sizeof(`LumaAudioPlay`). |
| `float volume` | Times the sound's own (0 to `LUMA_AUDIO_VOLUME_MOST`). |
| `float pitch` | Times the sound's own pick in its pitch range (0.25 to 4). |
| `uint32_t bus` | `LumaAudioBus`; `LUMA_AUDIO_BUS_SOUND`: the sound's. |
| `int32_t loop` | -1: as the sound (its loop points); 0: once; 1: its loop points, or the whole sound. |
| `uint32_t reserved` |  |

### `LumaAudioStreamDesc`

What stream_open makes.

| Name | Description |
|---|---|
| `uint32_t size` | Sizeof(`LumaAudioStreamDesc`). |
| `uint32_t rate` | Frames a second, `LUMA_AUDIO_RATE_LEAST` to `LUMA_AUDIO_RATE_MOST` (resampled to the output's). |
| `uint32_t channels` | 1 or 2 (interleaved: left, right). |
| `uint32_t bus` | `LumaAudioBus`, not `LUMA_AUDIO_BUS_SOUND`. |
| `float volume` | 0 to `LUMA_AUDIO_VOLUME_MOST`. |
| `uint32_t reserved` |  |

## `audio` functions

In C, each function takes your mod (`const LumaMod* mod`) first. LumaScript passes it for you.

### `play`

```lumascript
audio.play(name: text, how: audio.Play?, fn: fn(e: audio.Event)?) -> audio.Request?
```

```c
int32_t play(const LumaMod* mod, const char* name, const LumaAudioPlay* how, LumaEventFn fn, void* user, LumaHandle* out);
```

Plays the cooked sound `name`: "<path>", the mod's own (`layers/<l>/assets/<path>.sound.json`), or "<id>:<path>", another mod's. `LUMA_OK` and `*out` (the voice); ENDED to fn (null: no event) when it ends. A name no file has: the placeholder chime (nebula:placeholder/sound, 120 ms) plays in its place with `how`, `LUMA_OK` and `*out`, and the mod log has `Content.MissingSound` (624) naming it, once a name, counted on every call (a store without the chime: `LUMA_E_NOT_FOUND`; the start check lets no mod run on one); a file that is not a cooked sound: `LUMA_E_FORMAT` (reported) (`Audio.NotASound`); a 33rd voice: the mod's voice of the lowest priority (the new one's included; the oldest of equals) ends, ENDED `LUMA_E_LIMIT` (reported); when that is the new one, `LUMA_E_LIMIT` (reported). A null name or out, a name past `LUMA_AUDIO_NAME_MOST`, a `how` of the wrong size or out of range: `LUMA_E_ARGUMENT` (reported). `how` (null: the sound as cooked) sets its volume, pitch, bus and loop.

Events: `ENDED(socket, status)`.

### `voice_set`

```lumascript
audio.voice_set(voice: audio.Request, volume: f32, pitch: f32)
```

```c
int32_t voice_set(const LumaMod* mod, LumaHandle voice, float volume, float pitch);
```

A playing voice's volume and pitch (as `LumaAudioPlay`'s), ramped over 10 ms. A handle not the mod's playing voice: `LUMA_E_HANDLE` (reported); out of range: `LUMA_E_ARGUMENT` (reported).

### `voice_stop`

```lumascript
audio.voice_stop(voice: audio.Request, fade_ms: u32)
```

```c
int32_t voice_stop(const LumaMod* mod, LumaHandle voice, uint32_t fade_ms);
```

Stops a voice over `fade_ms` (0: 10 ms, so nothing clicks; at most `LUMA_AUDIO_FADE_MOST_MS`). No event after; the handle is closed at once. A handle not the mod's voice: `LUMA_E_HANDLE` (reported).

### `bus_volume`

```lumascript
audio.bus_volume(bus: u32, volume: f32)
```

```c
int32_t bus_volume(const LumaMod* mod, uint32_t bus, float volume);
```

The mod's own share of a bus (0 to 1; 1 at its start), on top of the player's setting for it: every voice and stream of the mod on that bus, ramped over 10 ms. `LUMA_OK`; a bus past `LUMA_AUDIO_BUS_MUSIC`, a volume not a number or outside 0 to 1: `LUMA_E_ARGUMENT` (reported).

### `stream_open`

```lumascript
audio.stream_open(desc: audio.StreamDesc, fn(e: audio.Event)) -> audio.Request?
```

```c
int32_t stream_open(const LumaMod* mod, const LumaAudioStreamDesc* desc, LumaEventFn fn, void* user, LumaHandle* out);
```

A stream the mod fills with stream_push. It returns `LUMA_OK` and sets `*out`; NEED follows at once (it is empty). A 5th stream: `LUMA_E_LIMIT` (reported); a desc of the wrong size or out of range, a null desc or out: `LUMA_E_ARGUMENT` (reported).

Events: `NEED(socket, value)`.

### `stream_push`

```lumascript
audio.stream_push(stream: audio.Request, samples: list<f32>) -> i64
```

```c
int64_t stream_push(const LumaMod* mod, LumaHandle stream, const float* samples, uint64_t frames);
```

Queues `frames` frames of float samples (channels each, interleaved; -1 to 1, louder is clipped at the output). Returns the frames taken: all, or as many as the ring has room for (`LUMA_AUDIO_RING_MS` in all). A sample that is not a number is silence (reported) once a push (`Audio.NotANumber`). A null samples with frames: `LUMA_E_ARGUMENT` (reported); a handle not the mod's stream: `LUMA_E_HANDLE` (reported). A stream that runs dry plays silence and goes on when pushed again.

### `stream_close`

```lumascript
audio.stream_close(stream: audio.Request, drain: u32)
```

```c
int32_t stream_close(const LumaMod* mod, LumaHandle stream, uint32_t drain);
```

Ends a stream: `drain` 1 plays what it holds first, 0 fades it out over 10 ms. No event after; the handle is closed at once. A handle not the mod's stream: `LUMA_E_HANDLE` (reported).

### `music`

```lumascript
audio.music(name: text, fade_ms: u32)
```

```c
int32_t music(const LumaMod* mod, const char* name, uint32_t fade_ms);
```

Replaces the game's music: the cooked sound `name` (as play's) plays on the music bus, looped by its loop points (or whole), fading in over `fade_ms` (0: 10 ms) while the game's own music fades out as long. With no cooked sound of that name, the mod's cooked stream of it (sound.md 8: up to 20 minutes, read from the store as it plays, its first two channels heard) plays instead, looped as it was cooked; one cooked with "loop": false plays once and the game's music comes back after it. One music for every mod: a later call, this mod's or another's, takes over (the one before fades out as long). Its end is the mod's music_restore or its stop. Answers as play does (a name no file has: the placeholder chime, once, `LUMA_OK`, `Content.MissingSound`; FORMAT; fade_ms past `LUMA_AUDIO_FADE_MOST_MS`: `LUMA_E_ARGUMENT`).

### `music_restore`

```lumascript
audio.music_restore(fade_ms: u32)
```

```c
int32_t music_restore(const LumaMod* mod, uint32_t fade_ms);
```

The game's music back over `fade_ms` (0: 10 ms), the mod's fading out. `LUMA_OK` also when the mod's music is not playing (nothing to do).

### `tap_open`

```lumascript
audio.tap_open(source: u32, fn(e: audio.Event)) -> audio.Request?
```

```c
int32_t tap_open(const LumaMod* mod, uint32_t source, LumaEventFn fn, void* user, LumaHandle* out);
```

Listens to `source` (`LumaAudioTapSource`): SAMPLES to fn (not null) once every `LUMA_AUDIO_TAP_FRAMES` frames, for visualisers, recorders, beat detection. A 3rd tap: `LUMA_E_LIMIT` (reported). It hears only the game's sound and the mods', nothing of the player's (the microphone is the audio.microphone service).

Events: `SAMPLES(socket, status, data, value, flags)`.

### `tap_close`

```lumascript
audio.tap_close(tap: audio.Request)
```

```c
int32_t tap_close(const LumaMod* mod, LumaHandle tap);
```

Ends a tap; no event after. A handle not the mod's tap: `LUMA_E_HANDLE` (reported).

### `play_at`

```lumascript
audio.play_at(name: text, how: audio.Play?, at: audio.Place?, fn: fn(e: audio.Event)?) -> audio.Request?
```

```c
int32_t play_at(const LumaMod* mod, const char* name, const LumaAudioPlay* how, const LumaAudioPlace* at, LumaEventFn fn, void* user, LumaHandle* out);
```

As play, heard from `at` (null: as play, everywhere): the sound's near and far (sound.md 3: full volume within near, silent beyond far, linear between) and its side from the camera (constant power). A place that is not there (no camera, no player) keeps the last one heard (none yet: heard as with no place). Answers as play; a place of the wrong size, a follow past `LUMA_AUDIO_AT_CAMERA` or a pos not finite or past `LUMA_AUDIO_PLACE_MOST`: `LUMA_E_ARGUMENT` (reported).

Events: `ENDED(socket, status)`.

### `voice_place`

```lumascript
audio.voice_place(voice: audio.Request, at: audio.Place)
```

```c
int32_t voice_place(const LumaMod* mod, LumaHandle voice, const LumaAudioPlace* at);
```

A playing voice's place (null: heard everywhere again), from the next audio block, the change ramped over it. A handle not the mod's playing voice: `LUMA_E_HANDLE` (reported); a bad place: `LUMA_E_ARGUMENT` (reported).

### `game_play`

```lumascript
audio.game_play(name: text, at: audio.Place) -> audio.Request?
```

```c
int32_t game_play(const LumaMod* mod, const char* name, const LumaAudioPlace* at, LumaHandle* out);
```

Starts the game's own sound `name`, of its sound table (SE_PM_JUMP_M, SE_SY_STAR_GET, as sound.md 6 names them), through the game's sound engine: `at` null, heard everywhere (as the game's menu sounds); otherwise from that place with the game's own distances and pan. It returns `LUMA_OK` and sets `*out`. Answers: `LUMA_E_NOT_FOUND`, no sound of that name; `LUMA_E_BUSY`, the game does not start one now (its effects are held, as in the pause menu; its voices are full; a place that follows what is not there). A 17th sound of the mod's: `LUMA_E_LIMIT` (reported) (a sound that ended is no longer counted). No event. A null name or out, a name past `LUMA_AUDIO_NAME_MOST`, a bad place: `LUMA_E_ARGUMENT` (reported).

### `game_place`

```lumascript
audio.game_place(sound: audio.Request, at: audio.Place)
```

```c
int32_t game_place(const LumaMod* mod, LumaHandle sound, const LumaAudioPlace* at);
```

Moves a game sound started with a place. `LUMA_OK` also when it has ended. A handle not the mod's game sound: `LUMA_E_HANDLE` (reported); one started with no place, a bad place: `LUMA_E_ARGUMENT` (reported).

### `game_stop`

```lumascript
audio.game_stop(sound: audio.Request, fade_ms: u32)
```

```c
int32_t game_stop(const LumaMod* mod, LumaHandle sound, uint32_t fade_ms);
```

Stops a game sound over `fade_ms` (0: at once; at most `LUMA_AUDIO_FADE_MOST_MS`), rounded up to the game's frames. `LUMA_OK` also when it has ended; the handle is closed. A handle not the mod's game sound: `LUMA_E_HANDLE` (reported).

### `stream_channels`

```lumascript
audio.stream_channels(stream: audio.Request) -> u32
```

```c
int32_t stream_channels(const LumaMod* mod, LumaHandle stream, uint32_t* out);
```

Needs `audio` 1.1 or later.

A stream's channels as its desc gave them (1 or 2): stream_push reads `frames` x that many floats. It returns `LUMA_OK` and sets `*out`. A handle not the mod's stream: `LUMA_E_HANDLE` (reported); a null out: `LUMA_E_ARGUMENT` (reported).

## Further reading

* [Calling services](calling-services.md)
* [Services reference](index.md)
* [About services](../../concepts/services.md)
* [Permissions](../../concepts/permissions.md)
