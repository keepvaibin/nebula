# Content: sound

## 1. Where it goes, what it becomes (layer 1)

`layers/<l>/assets/<path>.sound.json` is the mod's sound effect `<path>`. The game's start cooks it (as every
asset, at `Luma/<id>/<path>.sound.json`) into `Luma/<id>/<path>.nsfx`; `audio.play(mod, "<path>")` plays that.
The sound file it names is read from the same folder of the same layer.

A mod's `.wav`, `.flac` and `.ogg` files in `assets/` are sources only: they are not put in the store (a WAV at a
game path in `files/` is still a stream, as before). A `.sound.json` at a game path (`files/`):
Cook.Sound.NotAsset {file}.

## 2. The file

```json
{
  "kind": "effect",
  "file": "jump.wav",
  "volume": 0.8,
  "pitch": [0.95, 1.05],
  "distance": [300, 3000],
  "priority": 128,
  "bus": "effects",
  "positioned": true,
  "loop": false
}
```

| part | | default |
|---|---|---|
| `kind` | `"effect"` (the only kind of layer 1) | required |
| `file` | the sound beside it: WAV, FLAC or Ogg Vorbis, by its bytes, not its ending | required |
| `volume` | linear gain, 0 to 4 | 1 |
| `pitch` | a number, or `[lowest, highest]` (each play takes one between, evenly), 0.25 to 4 | 1 |
| `distance` | `[near, far]` in game units: full volume within near, silent beyond far; 0 to 1,000,000, near < far | [300, 3000] |
| `priority` | 0 (first dropped) to 255 | 128 |
| `bus` | `effects`, `ui`, `voice`, `music` (the options' volume it follows) | effects |
| `positioned` | false: played at the listener, distance unused | true |
| `loop` | absent: the file's own loop (WAV `smpl`, FLAC/Vorbis `LOOPSTART` with `LOOPEND` or `LOOPLENGTH`); false: none; true: the file's or the whole sound; `{"start": s, "end": e}` in the file's frames, `end` not played and defaulting to the last frame | absent |
| `replace` | layer 2: the game sound it stands in for, by name | none |

Any other part: Cook.Effect.UnknownPart. Wrong kinds of value: Cook.Effect.Is.*; out of range: Cook.Effect.Range;
not one of a list: Cook.Effect.Choice; a pair the wrong way round: Cook.Sound.Order {where}.

### What the sound file may be

- WAV: PCM 8, 16, 24 or 32 bit, float 32 or 64 bit, plain or extensible (else Cook.Sound.WavKind {uri, kind,
  width}). FLAC: every kind of subframe, CRC-8 and CRC-16 checked on every frame. Ogg Vorbis. Ogg Opus:
  Cook.Sound.Opus {uri} (no Opus decoder is in the cook).
- 1 or 2 channels (Cook.Sound.Channels {uri, count, max}); 8,000 to 384,000 Hz (Cook.Sound.Rate {uri, value, min,
  max}); 1 frame to 60 seconds (Cook.Sound.Empty {uri}, Cook.Sound.TooLong {seconds, max}: longer music is a
  stream); no NaN or infinite sample (Cook.Sound.NotANumber {uri}).
- The loop inside the sound and not empty: Cook.Sound.LoopOutside {uri, value "start-end", count}.
- Not a sound file: Cook.Sound.Unreadable {uri}; damaged: Cook.Sound.Broken {uri, value: the byte}; shorter than
  it says: Cook.Sound.Cut {uri}. The file missing: Cook.Effect.FileMissing {where, uri}; a path leaving the folder:
  Cook.Effect.FilePath.

Every refusal is L804 on the `.sound.json`, from `lumalink check` (with the game) and at the game's start.

## 3. What the cook does

- Resamples to 48,000 Hz with a windowed sinc (Blackman, 32 zero crossings a side, 4,096 phases, cut-off at the
  lower Nyquist); 48 kHz sources are copied. With a loop, the filter reads past the loop's end from its start, so the
  seam is the source's. The loop's frames are scaled to 48 kHz and rounded.
- Rounds to 16 bits (no dither: the bytes are the same every cook).
- Measures loudness, EBU R 128 integrated (BS.1770-4 K-weighting, 400 ms blocks with 75 % overlap, gates at -70 LUFS
  and -10 LU), and the sample peak. Neither changes the sound; both go in the file and the fact.

## 4. NSFX (what `audio.play` reads), little-endian, read in place

| at | what |
|---|---|
| 0 | `NSFX` |
| 4 | version, 1 |
| 8 | rate, 48000 |
| 12 | channels, 1 or 2 |
| 16 | frames |
| 20 | loop start (frame), 0xFFFFFFFF none |
| 24 | loop end (frame, not played), 0 none |
| 28 | volume (f32, linear) |
| 32, 36 | lowest and highest pitch (f32) |
| 40, 44 | near and far (f32, game units) |
| 48 | priority (u8) |
| 49 | bus (u8): 0 effects, 1 ui, 2 voice, 3 music |
| 50 | flags (u8): 1 positioned |
| 51 | 0 |
| 52 | loudness (f32, LUFS; NaN when shorter than one 400 ms block or silent) |
| 56 | peak (f32, dBFS; -inf when silent) |
| 60 | offset of the replaced game sound's name from the file's start, 0 none |
| 64 | samples, i16 interleaved, `frames × channels` |
| then | the replaced name, UTF-8, 0-ended (when 60 is not 0) |

A reader refuses a file whose magic, version or rate differ, or whose 64 + 2 × frames × channels passes its end.

## 5. The fact

`cook.sound` per cooked sound: mod (from the path), other = the `.nsfx` path, `frames` (48 kHz), `channels`, `rate`
(the source's), `looped` (0/1), `loudness` (LUFS, one decimal, or `none`), `peak` (dBFS, one decimal, or `none`),
`replaces`. A refused sound has no `cook.sound` line, and its L804 is in the mod's log.

## 6. Replacing a game sound (layer 2)

`"replace": "<name>"` makes the effect stand in for the game's sound of that name, everywhere the game plays it. The
names are the game's sound table's (BSTN): 3,487 on the disc, letters, digits and `_`, such as `SE_PM_JUMP_M` (Mario's
jump) or `SE_SY_2P_ASSIST_JUMP`. The cook reads them from the store's cooked sound archive (`AudioRes/SMR`,
nebula-import `cook::sound::sound_names`); a name the game lacks is Cook.Sound.NoGameSound {where, name}, from
`lumalink check` (with the game) and at the game's start. Without the game (LumaLink with no game.pak) the name is
not checked.

The NSFX keeps the name (offset at 60). Two mods replacing one name: the later mod in the load order wins and the cook
notes the other (written when 2.6 lands: the overlay knows the order, the NSFX alone does not).

As built (2.6 layer 1c; services-audio 8a): the mod highest in the Mods list wins a name (order 1,
as for files). The note to the other mod's log is not written yet (it needs a row in 14 languages). A sound the game
starts once plays its replacement once: the game stops its own looping sounds through their handle, which a
replacement has none of. A level sound, which the game renews every frame, keeps the replacement and its loop while
renewed and fades it out 2 frames after the last renewal.

## 7. Proven, and not yet

Proven by unit tests (nebula-import `cook::sound_effect`, `cook::flac`): every WAV kind, FLAC from ffmpeg's encoder
(LPC order 12, fixed predictors, 24-bit) decoded to its exact samples, Ogg Vorbis from ffmpeg within 20 dB SNR, the
EBU Tech 3341 case 1 and 2 levels (±0.1 LU), resampling from 8, 22.05, 44.1 and 96 kHz keeping frequency, level and
loop, every refusal said in four languages. LumaLink's `check` says the refusals on the right file from a folder and
a `.luma`.

Layer 2: `sound_names` read back from a hand-made archive (order, each once, an item with no name skipped, a name past
the names refused) and from the disc's (3,487, `the_disc_cooks`); against the imported game, `replace` of
`SE_PM_JUMP_M` cooks and of `SE_PM_JUMP_XL` is refused by name (tests/sound_replace_game.rs, LUMA_GAME_PAK).

## 8. Streams (layer 3)

Code: `cook/sound_stream.rs` (`cook`, `nstr`), `cook/sound_effect.rs` (`cook_sound`, `Made`, `Around`),
`cook/modern_audio.rs` (`read_nintendo`), `cook/store.rs` (`has_file`), `cook/overlay.rs` (`modern_files`).

```json
{ "kind": "stream", "file": "theme.ogg", "volume": 1, "loop": {"start": 88200, "end": 1323000}, "replace": "smg_galaxy01" }
```

| part | | default |
|---|---|---|
| `file` | WAV, FLAC, Ogg Vorbis, BRSTM, BCSTM or BFSTM beside it, by its bytes | required |
| `volume` | linear, 0 to 4 (the header's volume, x127) | 1 |
| `loop` | absent or true: the file's loop, else the whole sound; false: plays once; `{"start", "end"}` in the file's frames, `end` not played, default the last | absent |
| `replace` | a game stream: its file name in AudioRes/Stream without `.ast` | none |

- Makes `Luma/<id>/<path>.ast`: StreamPlayer.hpp's NSTR (tag, channels, rate, frames, loop flag, loop start, loop
  end, volume; then each channel's PCM16 in turn), what the game's StreamSound maps and plays. A mod plays it by the
  track functions (its path without `.ast`). With `replace`, the same bytes go at `AudioRes/Stream/<name>.ast` too.
- The rate is kept up to 48 kHz (the mixer reads any); above it is resampled to 48 kHz as effects are, the loop
  scaled. Loudness is measured at 48 kHz (NaN under 400 ms).
- Refused (L804): `Cook.Sound.StreamChannels` {uri, count, max 6}, `.StreamTooLong` {seconds, max 1200},
  `.StreamFile` {uri, detail} (a Nintendo stream that cannot be read), `.StreamName`, `.NoGameStream` {where, name},
  and as effects: `.Rate`, `.Empty`, `.Cut`, `.Broken`, `.LoopOutside`, `.NotANumber`, `Cook.Effect.*` for the JSON.
- A Nintendo stream's header can claim any frame count: no more is reserved than two samples a byte of the file.

## 9. Voice lines (layer 4)

`"kind": "voice"`: an effect (the parts of section 2) that must have `replace`, the game voice line it stands in for
by its sound name (as layer 2); its bus is `voice` unless it says. Makes `Luma/<id>/<path>.nsfx` as an effect.
No replace: `Cook.Effect.Missing {where /, key replace}`; a name the game does not have: `Cook.Sound.NoGameSound`.

## 10. Sequences (layer 5)

Code: `cook/sound_sequence.rs` (`read_midi`, `program`, `cook`), `cook/sound.rs` (`sequence`, the game's own cook).

```json
{ "kind": "sequence", "file": "tune.mid", "instruments": {"0": {"bank": 0, "program": 5}, "drums": {"bank": 3, "program": 0}},
  "volume": 1, "loop": true, "replace": "<path in JaiSeq.arc>" }
```

| part | | default |
|---|---|---|
| `file` | a Standard MIDI file, format 0 or 1, ticks a quarter note | required |
| `instruments` | MIDI program (`"0"` to `"127"`, as the file numbers them) or `"drums"` (channel 10) to a game instrument: `bank` and `program`, 0 to 255 | none |
| `volume` | 0 to 1, the root track's volume | 1 |
| `loop` | absent: from the marker `loopStart` (or CC 111) to `loopEnd` or the end, none without a marker; true: the markers or the whole song; false: none; `{"start", "end"}` in ticks | absent |
| `replace` | a game sequence: its path in AudioRes/Seqs/JaiSeq.arc | none |

- The song becomes a program of the game's sequence operations, then is cooked by the game's own sequence cook
  (`sound::sequence`): `Luma/<id>/<path>.nseq`; with `replace` also `AudioRes/Seqs/JaiSeq.arc/<name>`.
- The root track: timebase (register 0x62) = the file's ticks a quarter, tempo in BPM (rounded), the volume, a child
  track for each MIDI channel with notes (at most 16), then the tempo changes. Each channel's track: the bend sense
  (register 0x64 = 6, a third of a semitone each, so full bend is MIDI's 2 semitones), the instrument of the program
  its first note plays, then notes in slots 1 to 7 (key, slot, velocity; the off by slot), program changes, CC 7 as
  parameter 0, CC 10 as parameter 3, pitch bend as parameter 1, and waits in ticks.
- A loop: at its start each track sets its instrument, volume, pan and bend as they are there; at its end it lets
  every held note go and jumps back. Without one, each track ends (Finish) and the root ends at the last tick.
- Left out, counted by kind in the fact's `dropped` and a Note `Cook.Sound.MidiDropped {uri, detail}`: key and
  channel pressure, every other controller, system exclusive, and an eighth note held at once on a channel.
- Refused (L804): `Cook.Sound.Midi {uri, detail}` (not MIDI, format 2, frames-a-second time, a running status with
  none before, more than 2^20 events), `.Cut`, `.Empty` (no notes), `.NoInstrument {uri, name}` (a program played
  with no instrument), `.InstrumentKey`, `.SequenceName`, `.NoGameSequence`, `.LoopOutside`, `.Sequence {uri,
  detail}` (the game's cook refused the program: a fault of this cook).

## 11. The fact (layers 3-5)

`cook.sound` adds `kind=effect|stream|voice|sequence`, `notes=` and `dropped=`; for a sequence `frames` is its length in
ticks, `channels` its MIDI channels with notes and `rate` its ticks a quarter.

## 12. Proven, and not yet (layers 3-5)

- By compile and cargo (nebula-import + luma-validate 353 pass): a WAV stream round-trips sample for sample with its
  loop, and 100 times round the loop is the source's; the Ogg fixture keeps its rate, frames and LOOPSTART/LOOPEND;
  the loop forms; replace at the game path; 96 kHz to 48 kHz; hostile: an empty file, a WAV claiming 2 GB, a cut Ogg,
  a BRSTM claiming four billion frames. A 16-note MIDI file read back by the game's sequence reader (keys, slots,
  velocities, tempo 120, timebase, bend sense, instrument, waits) and cooked to NSEQ; the loop's jumps land on each
  track's state; what is left out is counted; the refusals; a voice line on the voice bus.
- `replace` checked against a real store (the lookups are by the store's path hashes); a mod's own samples as
  instruments (a mod bank) is not written.
