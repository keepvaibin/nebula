# Service: audio and capture

## 1. Voices (layer 1)

`play(mod, name, how, fn, user, &voice)` plays a cooked sound effect (contract sound.md 4, the NSFX the game's start
cooks from `layers/<l>/assets/<path>.sound.json`): `"<path>"` is the mod's own (`Luma/<id>/<path>.nsfx`),
`"<id>:<path>"` another mod's. The file is read in place from the store's mapping; nothing is copied.

| how (`LumaAudioPlay`, null: as cooked) | |
|---|---|
| volume | times the sound's, 0 to 4 |
| pitch | times this play's pick in the sound's pitch range (evenly), 0.25 to 4 |
| bus | 0 effects, 1 ui, 2 voice, 3 music; 255 the sound's |
| loop | -1 the sound's loop points; 0 once; 1 its loop points, or the whole sound |

ENDED (kind 1) once: status OK when the mix played it to its end; LIMIT when it was dropped. The handle is closed
after it. `voice_set` replaces play's volume and pitch (ramped 10 ms); `voice_stop` fades over `fade_ms` (0: 10 ms; at
most 10 s), closes the handle at once and gives no event. A voice that ended this frame, its ENDED not yet given, is
still open: voice_set changes nothing, voice_stop closes it and its ENDED is not given.

A mod plays at most 32 voices. The 33rd: the voice of the lowest priority (the NSFX's, 0 to 255) ends, the new one
counted, the oldest of equals first; when that is the new one, play answers LIMIT. Either way Audio.Dropped.

## 2. Streams (layer 2)

`stream_open(desc)`: rate 8,000 to 192,000 Hz, 1 or 2 channels (interleaved), bus 0-3, volume 0-4; at most 4 a mod
(the 5th: LIMIT, Service.Limit). The ring holds 0.5 s (`ceil(rate × 0.5)` frames). NEED (kind 2) at once, and again
whenever less than 0.1 s waits and the mod has pushed since the last NEED; its value is the frames `stream_push` takes
now. `stream_push` returns the frames taken (all, or as many as fit). A sample that is not finite is silence:
Audio.NotANumber {count} once a push. A stream that runs dry plays silence and starts from silence when pushed again.
`stream_close(drain)`: 1 plays what it holds, 0 fades it over 10 ms; no event after.
`stream_channels` (1.1) gives a stream's channels: a push of `frames` reads `frames` x channels floats.

## 3. The mix

On the audio frame's thread, after the game's mixer (`AudioMixer::end_frame`) and before the master volume: every
voice and stream into the front left and right, each times its volume, the mod's `bus_volume` share (0 to 1) and the
player's volume for its bus (effects and ui: Sound effects; voice: Voices; music: Music). The master, the background
mute and Mono are then the game's. Resampling is linear: a voice from 48 kHz times its pitch, a stream from its rate.
Every gain change ramps over 10 ms. The mix allocates nothing, frees nothing and runs no mod code; it marks a voice
ended and posts its event node. No mod sound: one atomic load a frame.

A mod's stop silences all its voices and streams the same audio frame (mod_gone).

## 4. Rows

| code | id | text | args |
|---|---|---|---|
| 350 | Audio.NotASound | `{mod}: {where} was given {name}, which is not a cooked sound; it did nothing.` | where, name |
| 351 | Audio.Dropped | `{mod} plays more than 32 sounds at once: {name}, its sound of the lowest priority, was stopped ({where}).` | where, name |
| 352 | Audio.NotANumber | `{mod}: {where} was given {count} samples that are not numbers; they play as silence.` | where, count |

Beside the registry's 101 (Argument), 102 (Handle), 103 (StructSize), 104 (Limit).

## 5. Tests

ENDED OK and the second NEED are given only when the mix has played the sound, so they prove the mix ran.

## 6. Music and the tap (layers 3-4)

- `music(name, fade_ms)`: a cooked sound (as play's) on the music bus, looped by its loop points or whole, fading in
  over `fade_ms` (0: 10 ms; at most 10 s) while the game's own music fades out as long: the audio frame gives the
  game's mixer the player's music volume times the game music's share (1 heard, 0 replaced). One music for all mods:
  a later call takes over, the one before fading out as long. `music_restore(fade_ms)` gives the game's back;
  LUMA_OK also when the mod's music is not playing. A mod's stop, or its music's end, gives the game's back over 1 s.
- `tap_open(source, fn)`: OUTPUT (0) hears the game's output after the master, mute and mono; MODS (1) every mod's
  voices and streams before the master. SAMPLES (kind 3) every 1,024 stereo float frames; `flags` the output's rate;
  status BUSY when blocks before it were lost (8 wait at most). 2 taps a mod (a 3rd: LIMIT, Service.Limit).
  `tap_close` gives no event after. The microphone is never in a tap (section 7).

## 7. Capture (layers 5-6): `audio.microphone` 1.0 and `capture.camera` 1.0

Header `native/include/luma_capture.h`, host `runtime/src/luma_capture.cpp`. Each is a permission of its own name
(registry 5.1), granted on the consent screen and checked at bind. One open of each a mod (a 2nd: LIMIT).

- Microphone: `open({rate 8,000-192,000, 0 = 48,000; channels 1|2}, fn)`: SAMPLES (kind 1) of 1,024 float frames at
  the mod's rate (`flags`) and channels, linear resampling from the device; mono is (left + right) / 2. Eight blocks
  wait at most; a lost one marks the next SAMPLES BUSY.
- Camera: `open({width, height, fps}, fn)` (0: the camera's own; a size it does not give falls back to its own):
  FRAME (kind 2) with a `LumaCaptureCameraFrame` (RGBA, rows top first, stride width × 4, steady-clock time), the newest
  picture each game frame; pictures in between are skipped (BUSY).
- ENDED (kind 3): ABSENT (no device, or the system's privacy setting refuses it), IO (unplugged or taken); the handle
  is closed after it. `close` gives no event after.
- No call waits for a device: open starts the device's own thread; close and a mod's stop only mark it.

Platforms: Windows (WASAPI shared capture of the default endpoint; Media Foundation's first video source through its
video processor) and Android (AAudio's default input; the first camera through Camera2, YUV 4:2:0 converted to RGBA).
Linux, macOS and iOS have no capture backend: there `open` answers LUMA_E_ABSENT. Tests: h/tests/audio 2606-2607.

## 8. Places and the game's own sounds (layer 1b)

- `play_at(name, how, at, fn, user, &voice)` is play heard from `at` (`LumaAudioPlace`, 24 bytes: follow, pos):
  a point in the world (follow 0), or a place from the player (1) or the camera (2), world axes, moving with it.
  Gain: 1 within the NSFX's near, 0 beyond its far, linear between (a file with 0, 0 there: 300, 3000).
  Side: constant power from the camera's right (front × up), as loud ahead as a sound with no place; a stereo sound
  placed is its two channels together. The camera and the player are read at each frame's start by the mod's placer
  (one event node a mod, queued again while it has a placed voice or a following game sound); the mix moves to the
  new gains over one block. A place not there (no camera; no player for follow 1) keeps the last gains (none yet:
  heard as with no place). `voice_place(voice, at)` moves a playing voice; null: no place again.
- `game_play(name, at, &sound)` starts the game's sound `name` (its sound table's) through the game's
  own engine (LumaGameSound.cpp: `SoundStarter` with a handle of its own, at `at` or with no place): the game's
  distances, pan, reverb and voice limits. Answers: NOT_FOUND (no such name), BUSY (the game holds its effects, as in
  the pause menu; no free voice; a follow whose base is not there). 16 a mod at once (a 17th: LIMIT, Service.Limit);
  a sound that ended still answers OK to `game_place` and `game_stop` until its slot is taken by a new sound (64 for
  all mods), then HANDLE. `game_place` moves one started with a place (one with none: ARGUMENT); `game_stop(fade_ms)`
  fades over the game's frames (rounded up) and closes the handle. No event. A mod's stop stops its game sounds the
  same frame.
- No camera or player read and no node queued while no mod has a placed voice or a following game sound.
- The game module's half is found by name (`luma_game_sound_play`, `_place`, `_stop`, `_gone`, `_follow`,
  `luma_game_listener`); a host without all of them answers game_play LUMA_E_ABSENT and hears placed voices as voices
  with no place.

Tests: h/tests/audio 2608 (audio-place), 2609 (audio-game).

## 8a. Game sounds a mod's sound stands in for (layer 1c)

- The index: at the mods' start (host_main, after the mods' store is attached and the mod log begins, whether any mod
  has code or not), `set_replacements` walks each enabled .luma's `Luma/<id>/` in the store for `.nsfx` files whose
  offset 60 names a game sound, each 0-ended within the file and at most LUMA_AUDIO_NAME_MOST
  bytes. Two mods naming one sound: the one higher in the Mods list (lower `order`) wins, as for files.
- With at least one name, the module's `luma_game_sound_replacer(start, frame)` gets the service's two calls; with
  none it stays null, and AudSystem::startSound / startLevelSound cost one pointer test.
- `start(name, pos, level)` runs in AudSystem::startSound and startLevelSound after every rule that would keep the
  game's sound from starting (reset, mute, blocks, the volume setting, the HOME menu). Found: the NSFX plays as a
  voice with no mod (no handle, no event) and the game's sound is not started (startSound answers false). It plays
  with the NSFX's volume, pitch range, distances, priority and bus, from `pos` (copied; null: heard everywhere).
- A level sound (startLevelSound, renewed each frame) is one voice per name and place pointer: a renewal moves it,
  and `frame` (AudSystem::frameWork, once a frame) fades it out 2 frames after its last renewal. It keeps the NSFX's
  loop. A sound started once plays once (the game's handle, which stops a looping sound, does not reach a
  replacement).
- `frame` also aims every placed replacement from where the camera is now.
- At most LUMA_AUDIO_VOICES_MOST (32) replacements at once: the lowest priority ends (the oldest of equals). A new one
  lower than all of them is not played, and the game's stays silent too.
- Facts in a harness run: `audio.replacing {count}` at the start; `audio.replaced {name, mod, level, placed}` for each
  voice started.
- Test: h/tests/audio 2610-audio-replace (unrun).
- Not yet: the note to the losing mod's log (needs a row in 14 languages); a mod turned off mid-session keeps its
  replacements until the next start.

## 9. Not yet

- Capture backends other than Windows.
