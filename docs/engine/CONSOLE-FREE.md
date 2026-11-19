# Console free: the inventory

What of Nebula is still the console's implementation, each part with its modern form, its row and its proof. The
100% statement of `D:/NebulaWork/plan/08-CONSOLE-FREE-ORDER.md` (section 6) is made from this file and from nothing
else. Its test (that file, section 3): code, a data format, a limit or a timing that is there because of the console's
hardware or SDK, not because of what the game does. The game's own rules, the fixed 60 Hz game step, its content and
the importer reading the disc once at setup stay.

State (2026-11-18, the code of a1f423dd8): pass one run; pass two has read each area of `FRAME-MODEL.md` 1.10 for
the console's shape, the 921 actor files by a pattern sweep with every hit read. Not read yet: the 921 files whole.
Items open: the frame's rows (FRAME-MODEL 1.9), P-155, P-184 to P-194. Section 4 applies file 08's
test (section 3) and the owner's answers of 18:18 (file 08 5.H). Not 100%.

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
  this path. `SoundArchive` maps the cooked sound archive from the store (`SoundArchive.hpp:11-13`);
  `ErrorMessageArchive.hpp` reads the cooked game data tables; `ParticleResourceHolder` (`ParticleResourceHolder.cpp:9-34`)
  reads the cooked particles and their name table, and keeps a particle's index as 16 bits: P-188.
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
  64 and 16 fires) are how many fires the boss has: the game's. `EffectKeeper`'s emitters (`MarioEffect.cpp:728`,
  `0xB8` extra) are an `MR::Vector` sized to start at the model's effects and the extra (`EffectKeeper.cpp:60-62`,
  `EffectKeeper.hpp:60`), so they grow too. Inline arrays of
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
| Particle indices of 16 bits | `ParticleResourceHolder.cpp:24`, `:45-47` (rows past 65,534 are not named; `0xFFFF` is none), `SingleEmitter.hpp:32` (`u16 mResourceId`), `EffectSystemUtil.hpp:30`, `:54`, `MultiEmitter.cpp:379-391` | the console particle library's 16-bit resource index | a 32-bit index from the name table to the emitter | P-188 | open | not yet |
| Table fields found by column name | `native/petari/src/Game/Util/Table.cpp:33-48` (the column search), 241 reads in 56 files (section 2) | a table kept as the disc's column list, each field found by its name's hash when read | the cook writes each table kind as a typed record with its columns resolved; the game reads fields by index, the name search gone | P-189 | open (E) | not yet |
| A normal map remade on the CPU and lit through a gradient texture | `native/petari/src/Game/MapObj/NormalMapBase.cpp:114`, `:119`, `:483-523` (`createGradTexture`: an LA8 gradient built on the CPU), `:525` on (`swapColorGB`: each RGBA8 texel's normal decoded and written back as 0, g, b, r in a copy), its own draw settings (`drawMaterialSetting`, `setStageForObject`) | the console's way of lighting a normal map by a texture lookup (read from the code; the shader's side not read yet) | the normal map used as it is cooked, lit by the renderer's lighting in the shader; nothing remade on the CPU | P-190 | open (E3) | not yet |
| What the game runs follows the screen's shape | `native/petari/src/Game/LiveActor/ClippingJudge.cpp:20-21`, `:33`, `:42` (`MR::getAspect()` in the volumes that decide which actors run); 42 lines of `native/petari/src` read an actor's clipped state (`isClipped`, `isInvalidClipping`, `isClippingInvalid`: `grep -rn ... --include=*.cpp`, 42 hits) | the console's culling of actors' runs by the one screen it had | (c) first: every actor of the loaded scene runs in every step, drawing culled by the real view; (b) if (c) misses a measure: one run volume the same on every screen | P-191 | open, after D.6 (file 08 5.H.2) | not yet |
| Shared counts and widths that can cap what a mod adds | `MR::Vector`, holders, registries, groups (section 2, Limits); id and index widths: `Sound.hpp:26-27` (`SoundID`: section 8 bits, group 8 bits, wave 16 bits; `SoundVoices.cpp:70`), P-188's particle index; the 138 literal tables and the pools, each read | a width or a size chosen for the console's content | every shared holder grows and every id or index is 32 bits wide or more; an actor's own count stays its number | P-192 | open (E; the tables' classing in the whole read) | not yet |
| Surround output by a written law | `runtime/src/audio_mixer.cpp:244-269`, `:704-729`; `runtime/src/native_audio.cpp:95-131`; `runtime/src/audio_device_wasapi.cpp:135-155`, `:207-230`; `platform/sdl/audio_device_sdl.cpp:33`; `native/petari/src/Game/Sound/SoundVoices.cpp:665-786` (section 4 says what each does) | none kept: the two-channel output stays as made (owner, 5.H.5); the surround law is new | a law from a sound's pan and surround amounts to each reported layout's speaker gains, written here before the code, each property asserted by a test, a render-to-file test through the real mixer | P-193 | open (E, with the sound work) | not yet |
| Actors that draw for themselves | 100 files of `native/petari/src` define `draw() const` (`grep -rlE "void \w+::draw\(\) const" native/petari/src`), 55 of them name a draw helper (`Material`, `TDDraw`, `setStage`, `RenderTexture`, `NebulaPass`, `drawFillBox`, `GameTexture`) | not read yet; `FRAME-MODEL.md` does not name them | each read; those with the console's shape placed in E3 or E4's steps | P-194 | open (the whole read, 5.H.1) | not yet |
| Texture headers in the console's form | `ResTIMG`, 137 lines in 59 files | the J3D texture header and its filter codes | a host id and a sampler resolved at load (E3 step 2) | P-155 | open (F4) | not yet |

## 4. Section 3's test, and the owner's answers of 2026-11-18 18:18

File 08 section 3 asks of every line this file put in "the game itself": can the code take its modern form with the
replay identical? Yes: an item. No: listed with the reason, a question for the owner. Applied at a1f423dd8:

- Table reads by column name (section 2, Tables): handling, the replay unchanged: P-189.
- The actor folders' packed and offset reads (section 2): `MarioSearchLight.cpp:297` packs an RGBA8 word for a 2D
  box, the renderer's own format: not the console's shape. `NormalMapBase.cpp:538` is part of P-190. `SoundID`'s
  widths (`Sound.hpp:26-27`, `SoundVoices.cpp:70`): P-192. The 14 reads by offset (`GhostPlayer.cpp:165`,
  `MarioConst.cpp:1061-1067`, `ChordInfo.cpp:18`, `SeqTrack.cpp:278-282`, `SoundArchive.cpp:11`, `:101`, `:147`,
  `SoundTable.cpp:5`, `:14`) walk cooked host-order records where they lie: the modern form already, except the
  sequences' big-endian data (P-186).
- The 138 tables of a literal size and the pools made ahead: P-192 by the owner's rule, each classed by reading the
  code that fills it, in the whole read. An actor's own count (`DinoPackunVs2.cpp:34-35`, 64 and 16 fires) stays,
  the owner's words.
- Units of the console's picture (the 456-line canvas, the 608 and 832 widths, lines sized in 456-line units, the
  640 x 456 grid of effect targets): coordinates of the content, drawn at the real resolution by every renderer
  (section 2): no handling of the console's shape. Not items.
- The sound driver's tick, the filters made for 32 kHz, the volume ramp, the speaker's 6 kHz waves, the two-channel
  fold: kept, the owner's answer ("Sound stays as it was made"). The surround output: P-193.
- Movies, the disc's frames: kept, the owner's answer ("Keep them"). No item.
- The 60 Hz game step: the owner's decision (file 08 section 3).
- New by this test: actors that draw for themselves (P-194), a class `FRAME-MODEL.md` does not name.

The whole read of the 921 actor files comes after the code items of D and E or after the weekly reset (file 08
5.H.1); the pattern sweep closes no area.

What P-193 starts from (read at a1f423dd8 for 5.H.5; the law itself is written in this file when P-193 starts, before
its code):

- The game: the player's sound setting picks the sound engine's output mode, 0 mono, 1 stereo, 2 surround
  (`AudSystem.cpp:333-345`). In mode 2 a note has a pan, an effect send and a rear amount (`dolby`), each the sum of
  the sound's, the effect's and the note's (`SoundVoices.cpp:665-688`). A note in its surround form goes to the
  panner with pan and rear (`:689-698`); any other note sets the volumes of up to six buses from pan, send and rear,
  each by a quarter sine or by the matrix's rear curve `sin((x * 0.34776 + 0.32612) * pi / 2)` (`:704-786`, `:772`). The
  buses include surround left and right and the inverted right (`SoundEngine.cpp:16-21`).
- The panner (`audio_mixer.cpp:704-729`): front left, front right, rear left, rear right get
  `cos(pi p / 2) cos(pi r / 2)`, `sin(pi p / 2) cos(pi r / 2)`, `cos(pi p / 2) sin(pi r / 2)`,
  `sin(pi p / 2) sin(pi r / 2)` (`pan_gain`, `:47-49`): the squares sum to 1 at every pan p and rear r.
- Two channels (kept as made): the surround buses, 2.5 ms late (one subframe, `:154-155`, `:248-249`), are
  matrix-encoded into left and right, the right's part inverted, the center at -3 dB, the inverted right subtracted
  (`:262-267`).
- Speakers of their own (`set_discrete`, chosen when the device gives more than two channels, `native_audio.cpp:120-131`):
  - the surround buses still come 2.5 ms after the front (`audio_mixer.cpp:248-260`): the fold's delay on real
    speakers;
  - the inverted right bus is still subtracted from the right speaker (`:255`): an inverted signal on a speaker;
  - a sound's rear part is split equally over sides and backs when both exist (`native_audio.cpp:99`, `kHalfPower`),
    whatever the speakers' angles; there is no low-frequency channel; a centre with no speaker goes to left and
    right at -3 dB (`:98`).
- The device: Windows takes the system mix format's channel mask when surround is asked and the mask has rear
  speakers (`audio_device_wasapi.cpp:207-230`), its channels in the mask's order (`:135-155`); Android (SDL) always
  opens two channels (`audio_device_sdl.cpp:33`), so a phone or tablet with more speakers, or a receiver behind it,
  gets the two-channel matrix.
