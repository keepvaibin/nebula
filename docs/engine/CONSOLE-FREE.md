# Console free: the inventory

What of Nebula is still the console's implementation, each part with its modern form, its row and its proof. The
100% statement of `D:/NebulaWork/plan/08-CONSOLE-FREE-ORDER.md` (section 6) is made from this file and from nothing
else. Its test (that file, section 3): code, a data format, a limit or a timing that is there because of the console's
hardware or SDK, not because of what the game does. The game's own rules, the fixed 60 Hz game step, its content and
the importer reading the disc once at setup stay.

State (2026-11-18, the code of a1f423dd8): pass one run; pass two has read each area of `FRAME-MODEL.md` 1.10 for
the console's shape, the 921 actor files by a pattern sweep with every hit read. Not read yet: the 921 files whole,
`SoundArchive`, `ParticleResourceHolder`, `ErrorMessageArchive`, `EffectKeeper`'s extra emitters. Items open: the
frame's rows (FRAME-MODEL 1.9), P-155, P-184, P-185, P-186, P-187. Not 100%.

## 1. Pass one: whole-tree searches

Run with `git grep -c -I -E <pattern> HEAD -- runtime native/host native/petari native/include native/math platform
crates ':(exclude)*_tests.cpp' ':(exclude)*_test.cpp' ':(exclude)*/tests/*' ':(exclude)third_party'` at a1f423dd8,
by the script `tools/verify/console_free_search.py` (to be added with the first item that closes a search; until then
the patterns are these). Each cell is files/lines. `native/math` was searched too and had no hit in any search.

| Search | Pattern | runtime | native/host | native/petari | native/include | platform | crates (importer) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| GX (graphics SDK) | `\bGX[A-Z][A-Za-z0-9]*\b\|\bGX_[A-Z0-9_]+\b` | 0/0 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0 |
| GD | `\bGD[A-Z][a-z][A-Za-z0-9]*\b` | 0/0 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0 |
| VI | `\bVI[A-Z][a-z][A-Za-z0-9]*\b\|\bVI_[A-Z0-9_]+\b` | 0/0 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0 |
| OS | `\bOS[A-Z][a-z][A-Za-z0-9]*\b` | 0/0 | 0/0 | 0/0 | 0/0 | 1/1 | 0/0 |
| DVD, NAND, PAD | `\b(DVD\|NAND\|PAD)[A-Z][a-z][A-Za-z0-9]*\b` (three searches) | 0/0 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0 |
| WPAD, KPAD | `\b(W\|K)PAD[A-Z_][A-Za-z0-9_]*\b` (two searches) | 0/0 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0 |
| AX (DSP sound) | `\bAX[A-Z][a-z][A-Za-z0-9]*\b\|\bAX_[A-Z0-9_]+\b` | 0/0 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0 |
| THP | `\bTHP[A-Z_][A-Za-z0-9_]*\b` | 2/2 | 0/0 | 0/0 | 1/1 | 0/0 | 0/0 |
| RVL | `\bRVL\b\|\bRVL[A-Z_][A-Za-z0-9_]*\b\|<revolution` | 2/4 | 0/0 | 0/0 | 0/0 | 1/2 | 0/0 |
| J3D | `\bJ3D[A-Z][A-Za-z0-9]*\b` | 0/0 | 0/0 | 0/0 | 0/0 | 0/0 | 1/1 |
| JKR, JUT, JAI, JAS | `\bJ(KR\|UT\|AI\|AS)[A-Z][A-Za-z0-9]*\b` (four searches) | 0/0 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0 |
| JPA | `\bJPA[A-Z][A-Za-z0-9]*\b` | 0/0 | 0/0 | 0/0 | 0/0 | 0/0 | 2/3 |
| J2D | `\bJ2D[A-Z][A-Za-z0-9]*\b` | 0/0 | 0/0 | 0/0 | 1/1 | 0/0 | 0/0 |
| JGeometry, JMath, JSupport, JGadget, JStage, JDrama, JStudio | `\bJ(Geometry\|Math\|Support\|Gadget\|Stage\|Drama\|Studio)\b` | 0/0 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0 |
| NW4R | `\bnw4r\b\|\bNW4R\b` | 0/0 | 0/0 | 0/0 | 0/0 | 0/0 | 1/1 |
| The console's heaps | `\b(JKRHeap\|JKRExpHeap\|JKRSolidHeap\|MEMHeap\|MEM[A-Z][a-z]+Heap\|OSAlloc\|mem1\|mem2\|MEM1\|MEM2)\b` | 0/0 | 0/0 | 0/0 | 0/0 | 0/0 | 0/0 |
| Retrace waits | `\bVIWaitForRetrace\|\bwaitForRetrace\|\bretrace\b\|\bRetrace\b` | 0/0 | 0/0 | 1/1 | 0/0 | 0/0 | 0/0 |
| Disc model and animation formats | `\b(BMD\|BDL\|BTK\|BRK\|BCK\|BTP\|BVA\|BPK\|BLK\|BMT)\b\|\.(bmd\|bdl\|btk\|brk\|bck\|btp\|bva\|bpk\|blk\|bmt)\b` | 2/2 | 0/0 | 7/7 | 3/3 | 0/0 | 12/78 |
| Disc archive formats | `\b(RARC\|Yaz0\|SZS\|U8)\b\|\.(arc\|szs)\b` | 11/18 | 0/0 | 6/6 | 4/10 | 0/0 | 31/283 |
| Disc table, text, layout and font formats | `\b(BCSV\|BMG\|BLO\|BRFNT\|BRLYT\|BRLAN\|BNR\|BCAM\|JMP)\b\|\.(bcsv\|bmg\|blo\|brfnt\|brlyt\|brlan\|bcam)\b` | 3/5 | 0/0 | 29/53 | 1/1 | 0/0 | 32/583 |
| Disc sound formats | `\b(BRSAR\|BRSTM\|BRWSD\|BRBNK\|BRSEQ\|BRWAR\|BAS\|AST)\b\|\.(brsar\|brstm\|brwsd\|brbnk\|brseq\|brwar\|bas\|ast)\b` | 1/1 | 0/0 | 2/2 | 1/2 | 0/0 | 8/33 |
| Disc particle formats | `\b(JPAC\|JEFFjpa\|JPC\|JPA1\|BREFF\|BREFT)\b\|\.(jpc\|breff\|breft)\b` | 1/1 | 0/0 | 1/1 | 0/0 | 0/0 | 7/18 |
| Byte swaps | `bswap\|byteswap\|_byteswap_\|__builtin_bswap\|swap_endian\|byte_swap\|BigEndian\|big_endian\|be16\|be32\|\bswap16\b\|\bswap32\b` | 5/28 | 0/0 | 2/14 | 0/0 | 0/0 | 20/489 |
| 60 Hz constants | `\b60(\.0+)?f?\b\|\b1\.0f? ?/ ?60\|\b16\.6+\|\b0\.0166` | 46/101 | 1/6 | 232/454 | 3/5 | 7/13 | 22/48 |
| 4:3 and 480-line constants | `\b4\.0f? ?/ ?3\.0\|\b4 ?/ ?3\b\|\b1\.333\|\b640\b\|\b480\b\|\b448\b\|\b456\b\|\b608\b` | 14/25 | 2/5 | 24/59 | 3/7 | 3/3 | 5/12 |
| Tables sized by a constant | `\[(c\|k)Max[A-Z][A-Za-z]*\]\|\[MAX_[A-Z_]+\]\|\b(c\|k)Max[A-Z][A-Za-z]*(Num\|Count\|Size)\b` | 4/5 | 1/5 | 3/9 | 0/0 | 2/7 | 0/0 |

Second way (7.3.6, the SDK searches came back almost empty): plain substrings, `git grep -c -I -F` over the same
areas at a1f423dd8: `JKR`, `WPAD`, `OSReport`, `VIWait`, `DVDRead`, `revolution`, `PSMTX`, `PSVEC`, `JMap`,
`JMapInfo`, `GXColor` 0 lines; `J3D` 22 lines in 4 files, `nw4r` 1 and `JPA` 5 lines in 3 files, all in `crates` (the
importer's names for what it reads); `GX` 10 lines in 9 files (runtime 6, native/include 1, crates 3), `JUT` 2 lines and
`J2D` 4 lines (native/petari, native/include), all read: `GX` is in comments (a material's colours as the data has them;
the game's projection terms read into the host's reversed depth, `draw_walker.cpp:1680-1686`) and inside `GGX` and
`NGX`; `JUT` is a sound's name (`SE_OJ_POLTA_G_ROCK_JUT`); `J2D` names the 2D picture's shader
(`NEBULA_SHADER_J2D_PICTURE`): names, not the console's code. So the SDK's and the middleware's names are gone from the
game and the host; the console's shapes are not found by names, and pass two reads for them. Names the game kept from
the console that do hold its shapes: `ResTIMG` (the J3D texture header: 137 lines in 59 files of native/petari and
native/include, and 16 lines in 5 importer files; P-155), `Mtx44` (67 lines in 25 files: the game's 4 x 4 float matrix,
`nebula_math.h:40`, its projection math; the game's).

What the hits are, by search (each line read at a1f423dd8):

- Disc formats in `native/petari` and `runtime`: the names of files the game opens (`"%s.bcsv"`, `"Message.bmg"`,
  `"%s.brlyt"`, `"MessageFont26.brfnt"`, ...). What is read at run time is the importer's cooked layout, checked by
  its magic at load: tables `NTBL` (`native/petari/src/Game/Util/Table.cpp:17`, panics on another), messages `NMSG`
  (`native/petari/include/Game/System/MessageHolder.hpp:11`, `MessageHolder.cpp:140-141`), fonts `NFNT`
  (`crates/nebula-import/src/cook/native_data.rs:244`), movies `NMOV` (`native/petari/src/Game/Screen/MovieStream.cpp:58`),
  layouts `NLYT` and their animations `NLAN` (`native/petari/include/Game/Ui/UiData.hpp:9`; `Layout::Build`,
  `native/petari/src/Game/Ui/Layout.cpp:249-257`, refuses another magic or version). Class: the importer at setup, with
  the run time's check named above. The layout's material record still holds console fields: section 3.
- `RVL` in `runtime` and `platform`: the Wii Remote's product names (`RVL-CNT-01`) of the real-remote drivers
  (`wiimote_hid.h:20-21`, `WiimoteUsb.java:39-40`, `wiimote_protocol.cpp:114`): a device the player plugs in, not
  the console. Stays.
- `THP` in `runtime` and `native/include`: the movie decoder adapted from the SDK's (`movie_video.cpp:2`,
  `movie_video.h:18`) and the YUV shader's coefficients: the game's own movies are cooked to `NMOV` with codec 0, the
  THP frames, decoded while the game runs by native SSE2 and FMA3 kernels (`movie_video_tests.cpp`). The frames are the
  game's content; the SDK's IDCT column swap (`movie_video.cpp:104`) is P-118, which the owner decided stays. Weighed:
  the frames are intra-coded JPEG pictures; a cook to a current codec would encode lossy pictures a second time and
  lose detail, while the native decoder already runs as SIMD kernels. Class: the game's content, decoded natively; not
  an item.

## 2. Pass two: the areas read for the console's shape

| Area | Files | Read |
| --- | --- | --- |
| The game's sound code (`Game/AudioLib`, `Game/GameAudio`, `Game/Sound`, `Game/Speaker`, `Game/RhythmLib`) | 170 | `Sound`'s engine and the host mixer read; all swept by pattern, every hit read; P-186 |
| Archives and the game's file access (`Game/System` archive and resource holders, `Game/Util/FileUtil`, the store) | 11 | the load path read: modern |
| Text (`MessageHolder`, the `NMSG` reader, the host's `message_utf8`) | not counted | the record and readers read; 16-bit codes open |
| Layouts and fonts (`Game/Ui`, `Game/Screen`'s layout code, `Game/System` font holders) | `Game/Ui` 51, `Game/Screen` 289 | `Layout::Build`, `Material`, `ResFont` read; P-184 |
| Input (the host's and the game's) | not counted | the game's record read: modern |
| The game's own files E14 did not reach (Agent 3's hand-over, `docs/plan/HANDOVER-AGENT-3.md` on `app/work`: Player 99, Sound 76, Enemy 109, Boss 166, Ride 19, NPC 76, MapObj 314, LiveActor 62) | 921 | swept by pattern (below), every hit read; not read whole |

What is read so far (at a1f423dd8):

- Tables. Reads by column name (`getCsvData*`, `getValue<T>(key)`, `findElement*`, `searchItemInfo`, `getColumnKind`):
  241 lines in 56 files of `native/petari/src`. By the function each is in (the scratch script `tablecalls.py`, its
  pattern recorded with the item): 15 sit in functions named like per-step code (`exe*`, `control`, `calc*`, `update*`,
  `draw*`); read one by one, 9 are `ValueControl::getValue` or a sound note's field (not a table) and 6 are
  `StageDataHolder::calcPlacementMtx` (`StageDataHolder.cpp:673-680`), which runs at placement. 99 lines are in free
  functions (`MR::`) whose callers are not yet placed. A second pass names the enclosing function of every read by
  a definition match (340 lines with `ValueControl` and the message helpers): `init*`, constructors of the demo
  keepers, `load*`, `create*FromCSV`, placement getters, story-event and race-time lookups, the light tables
  (`LightFunction.cpp:64-91`, read once for every light by `LightDataHolder.cpp:24` at load). No read by name was found in a step. The column search by
  name (`Table.cpp:33-48`) is the game's own data access (its JMap design), paid at load: the game, not the console;
  not an item.
- Layouts. Cooked `NLYT` (section 1); the record's material: P-184.
- Text. Cooked `NMSG` (`MessageHolder.hpp:8-32`): host-order counts and offsets, the text as `char16_t`, flow nodes
  and branches as records. Fonts: cooked `NFNT` (`native_data.rs:244-257`): host order, sheets as distance fields
  (`cook::distance`) or pictures, a glyph found through two 256-entry pages by the code's high and low byte. Both read
  characters as 16-bit codes (`CharStrmReader.cpp:10-33`: UTF-8 of up to three bytes, UTF-16 units, CP1252), the
  console's font code range: no character past U+FFFF can be shown, though the host's own reader of the same text
  joins surrogate pairs (`host_main.cpp:2159-2172`, `message_utf8`). Nothing shown today is past U+FFFF (the 14
  languages' texts and the host's are inside it); the limit is the console font format's: P-187. A glyph's sheet is still found by
  the address of its bytes (`ResFontBase.cpp:186-191`), as P-155's textures.
- Sound. `Game/Sound` is a native engine over the host's mixer (`SoundEngine.cpp`, `SoundVoices.cpp`, 9,062 lines in
  the folder): memory from the game's arena (`SoundMemory.cpp`), commands through a ring. The host mixer
  (`runtime/include/galaxy/audio_mixer.h:1-12`, `:74-77`) mixes in float at the device's rate; 32 kHz is the rate the
  sound data's filters and delays were made at (the content), carried to the output rate. The sound driver's own tick
  (`SoundEngine.cpp:148`, `cTicksPerSecond`; sequences by tempo and timebase, `SeqTrack.cpp:644-646`) is the game's
  sound step, as the 60 Hz step is its logic's: stays. The console's surround matrix (surround buses folded into left
  and right, the inverted right) is used only when the device has two channels: with surround asked and an
  extensible format the buses go to speakers of their own (`audio_mixer.h:90-95`, `audio_device_wasapi.cpp:222`).
  `AudioLib`, `GameAudio`, `RhythmLib`, `Speaker` (48 sources, 6,369 lines) swept as the actor folders (below): no CPU
  intrinsic, address, byte swap or pointer kept as an integer; 5 reads of cooked records by offset (name tables); 11
  tables of a literal size (two music players, mute nibbles, four effect lines: the game's). The remote speaker's
  sounds are mixed at 6 kHz (`SpeakerMixer.hpp:58-77`) because their waves are the disc's speaker waves
  (`native_data.rs:542-545`, `CSW`); the host resamples player 1's into the output (`host_main.cpp:4016-4025`,
  `wiimote_speaker.h`): the game's content. The volume ramp (`audio_mixer.cpp:669-680`, `make_ramp`) reaches its target
  65 samples of 32 kHz into each tick, at any output rate: how the game's sounds change volume, as made; stays.
- Archives and file access. The game's archive names (`.arc`, `.szs` in 18 runtime, 6 game and 10 header lines) are
  names only: the store drops the ending (`asset_store.h:118-119`, `asset_store.cpp:33`). The game reads the store
  (`game.pak`, `cook/store.rs`) through `AssetFolder` and `GameAssets::load` (`FileUtil.cpp:88`); `ResourceHolder`
  files a folder's entries into tables by the cooked kind (`ResourceHolder.hpp:22-29`, `NEBULA_ASSET_KIND_*`). No
  archive or compression is read while the game runs. Class: modern; nothing of the console's file system is left in
  this path. Not yet read: `SoundArchive`, `ParticleResourceHolder`, `ErrorMessageArchive`.
- Input. The game takes one record per player per read, whatever the device (`nebula_game_api.h:287-322`,
  `NebulaInputSample`): buttons by the game's own names, the stick and the pointer in -1..1 of the picture, tilt in g,
  spins counted between reads. No report of the console's controller reaches the game. Class: modern. The timing of
  the reads (once a step) is P-075, P-078 and P-163 (E6b). The host's drivers for real Wii Remotes read the remote's
  own reports: a device, not the console.
- The actor folders (the 921 files' folders: 285,511 lines with their headers). Swept by the scratch script
  `actors.py` for six shapes, each hit read: the console CPU's intrinsics and cache and paired-single instructions
  (`__cntlzw`, `__fsel`, `__frsqrte`, `psq_`, `__dcb*`, `OSRoundUp`...) 0; the console's addresses and memory sizes 0;
  pointers kept as integers 1 (`ClippingActorHolder.cpp:16`, a hash of an address: host code); big-endian or packed
  access 4 (a colour packed for a draw, a sound id's fields: the game's); data read through byte offsets 14 (cooked
  records: the ghost file `NGST`, sequences, sound tables, `MarioConst`'s fields by name for mods); tables of a literal
  size 138 (legs, joints, walls, curve tables, the sound driver's registers: the game's own shapes). No console item
  found in them by these shapes; a whole read is not done.
- Limits. Tables sized by a named constant (pass one, 26 lines): Windows' `MAX_PATH`, the game's rule of 999 carried
  star pieces, the mods' loader tables (not in this order), host buffers: none the console's. Capacities given when a
  holder is made (`init(n)`, `new XHolder(n)`, `new T[n]` with n of 10 or more: 37 lines in `native/petari/src`): the
  six read grow when full (`MR::Vector` and `AssignableArray::grow`, `Array.hpp:104-120`, `:291-312`;
  `SensorGroup::add`, `SensorHitChecker.cpp:65-72`; `NameObjHolder::add`, `NameObjHolder.cpp:22-30`;
  `NameObjExecuteHolder::registerActor`, `NameObjExecuteHolder.cpp:200-204`). Read after: the members made with
  `init(n)` are all `MR::Vector` (`DemoStartRequestHolder`, `StarPieceDirector`, `TalkDirector`,
  `GameDataGalaxyStorage`, `SpinDriverPathStorage`, `ShadowController`, `DemoSimpleCastHolder`); groups grow
  (`NameObjGroup::registerObj`, `NameObjGroup.cpp:15-23`, under `LiveActorGroup` and `MiiFacePartsHolder`), as do
  `MiiFaceIconHolder::registerIcon` (`MiiFaceIconHolder.cpp:27-34`), the clipping groups (`ClippingGroupHolder.cpp:23-24`,
  `room`) and the shadow's polygon lists (`MarioShadow.cpp:207`). Pools of actors made ahead (`DinoPackunVs2.cpp:34-35`:
  64 and 16 fires) are how many fires the boss has: the game's. Not read: `EffectKeeper`'s extra emitters
  (`MarioEffect.cpp:728`, `0xB8`). Inline arrays of
  16 or more outside the actor folders: the collision query's and the draw rows' are small-buffer rooms that move to the
  heap past their size (`CollisionParts.cpp:299-317`, `TypedDraw.hpp`), the rest are the game's tables and text
  buffers.
- Byte swaps (pass one: runtime 28 lines, native/petari 14), each read. The console's byte order read while the game
  runs: the sound sequences' data section (`SeqTrack.cpp:29-37` `be16`, `be24`, `be32`; envelope tables
  `setOscTable` `:706-724`, filter weights `setFIR` `:739-745`, the table-reading operations `:1505-1525`): P-186.
  The rest read data that is not the game's: the Wii Remote's own calibration report (`wiimote_input.cpp:149-158`), a
  console save or Mii file the player imports (`save_transfer.cpp`, `mii_formats.cpp`: the importer of the player's
  data, run when the player asks), a PNG header (`native_settings.cpp:2627-2632`), the update's signature
  (`update_crypto.cpp:39-42`).
- 60 Hz (pass one: 627 lines). Outside the game (runtime 101, native/host 6, native/include 5, platform 13), each read:
  the game step and what follows it (`NEBULA_STEPS_PER_SECOND`, `game_step.h:10`, the pacing and interpolation code
  that names the step's 60), the benchmark's "holds 60" verdict and graph lines (judged on the game's steps), the
  frame-rate setting's default (`render_config.h:52`, `settings_table.tsv:41`: the game's own frames, 60, while
  interpolation is off), and numbers that are not a rate (seconds, minutes, sizes, a touch opacity, table entries).
  A frame count as a time: the VRAM report once an hour as 216,000 game frames (`render_core.cpp:1862`: game frames
  are steps, so exact) and the Direct3D 12 timeline's clock pairing every 60 frames (`renderer_d3d12.cpp:3836-3841`,
  a debug timeline, its period immaterial). No 60 Hz assumption of the console's found outside the step. Inside the
  game outside the actor folders, every line naming a rate or a conversion was read: steps (`InputRumbleData`,
  `GameSystemRestartProcess`, `NebulaAchievements`), clock arithmetic, the settings menu's 60 and 30 FPS graph lines,
  and the old saves' times in the console's 60.75 MHz ticks, read and turned into seconds (`NebulaCalendar.hpp:11-15`:
  saves now hold seconds). The actor folders: E14's sweep ("no clock or display rate outside System/LumaTime.cpp").
- The 4:3 and 480-line hits (pass one, 111 lines), each read. The game's own: the 2D canvas of 456 lines and the
  layouts' 608 width (`NEBULA_AUTHORED_LINES`, `NebulaFrameClock.hpp:10-14`: units, "nothing renders at this size"),
  lines and points sized in lines of a 456-line picture (`FrameList.hpp:18`, `ParticleDraw.hpp:43`), gameplay numbers
  that happen to be 448, 456, 480, 608, 640 (class ids, steps, sizes, the Bezier surface's weights), text buffers of
  640 bytes. The host's: the 480-line floor of the resolution fit and the PERFORMANCE preset (`resolution_fit.h:64-80`,
  `render_config.h:395-398`), the window's first height (`render_config.h:143`): choices of this port, not the
  console's. Effect targets are sized in a 640 x 456 grid of authored texels (`NebulaFrameClock.hpp:15-17`,
  `GameTexture.cpp:66-72`); the one walker all four renderers share makes each target at the frame's real size, the
  authored size times the frame map's scale (`draw_walker.cpp:1435-1439`): units, drawn at the real resolution. The
  game's widescreen width of 832 units over its 640 (`display_aspect.h:21`) is likewise its 2D and camera units. The blur radii made 1.333 taller at 16:9
  (`BloomEffectSimple.cpp:28`, `ImageEffectLocalUtil.cpp:146`) are read: the radius is a fraction of the frame, so
  1.333 (16:9 over 4:3) keeps the console's shape on a 16:9 screen, but every wider shape gets 1.333 too: P-185.

File counts: `git ls-files native/petari/src native/petari/include` by folder at a1f423dd8: MapObj 633, Boss 333,
Screen 289, System 260, Enemy 218, Player 179, Map 164, Util 158, Camera 158, NPC 155, LiveActor 122, Sound 74,
AreaObj 72, Demo 63, AudioLib 58, Ui 51, Scene 44, Effect 43, Ride 38, Particle 35, Gravity 28, NameObj 25, Animation
22, Render 18, GameAudio 16, Speaker 12, RhythmLib 10, Mii 7.

## 3. Items

Each: where; what of it is the console's; the modern form; row; state; proof.

| Item | Where | The console's part | Modern form | Row | State | Proof |
| --- | --- | --- | --- | --- | --- | --- |
| The frame's rows | `FRAME-MODEL.md` 1.9 and 1.10 (pile B) | 1.9's parts | `FRAME-MODEL.md` part 2 | the rows 1.9 names, placed in steps by 2.6; P-022 among them | open; P-022 parts 1-2 written on `rows/p022` | not yet |
| A layout material's blend kept in the console's codes and turned into the renderer's at every draw | `native/petari/include/Game/Ui/UiData.hpp:163-182` (`mBlend`: type, factors, op as the console wrote them; `mAlphaCompare`: two compares packed in a byte; `mGenerators`: texture coordinate generators as type, source, matrix); `native/petari/src/Game/Ui/Material.cpp:702-714` (each draw maps the blend type, and the source factor's two destination-colour codes, to `NEBULA_BLEND_*`, `NEBULA_FACTOR_*`) | the console's blend and compare encodings in the cooked record | the cook writes the material's pass state word (`PassState`) and the compare in the renderer's terms; the draw copies it | P-184 | open | not yet |
| Blur radii stretched for one wide shape only | `BloomEffectSimple.cpp:28`, `ImageEffectLocalUtil.cpp:146` (`isScreen16Per9() ? 1.333f : 1.0f`) | the console's two screen shapes, 4:3 and 16:9 | the vertical factor from the frame's aspect over 4:3, so the blur keeps its shape on every screen | P-185 | open | not yet |
| Sound sequences' data kept in the console's byte order | `native/petari/src/Game/Sound/SeqTrack.cpp:29-37`, `:706-745`, `:1505-1525` | the sequence file's data section as the console stored it (big-endian), swapped at every read on the sound thread | the cook, which already decodes the operations, writes each table they read in host order and typed (envelope points, filter weights, 8-, 16-, 24-, 32-bit tables); the run time reads records | P-186 | open | not yet |
| Characters as 16-bit codes | `native/petari/src/Game/Ui/CharStrmReader.cpp:10-33`, the font's two 256-entry pages (`native_data.rs:244-257`), `TagProcessorBase` (`u16 code`) | the console font format's code range (a character past U+FFFF cannot be drawn) | code points as 32-bit values through the readers, the tag processors and the cooked font (`NFNT` version 2: a code table by range) | P-187 | open | not yet |
| Texture headers in the console's form | `ResTIMG`, 137 lines in 59 files | the J3D texture header and its filter codes | a host id and a sampler resolved at load (E3 step 2) | P-155 | open (F4) | not yet |
