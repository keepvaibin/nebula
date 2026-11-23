# Console free: the inventory

What of Nebula is still the console's implementation, each part with its modern form, its row and its proof. The
100% statement of `D:/NebulaWork/plan/08-CONSOLE-FREE-ORDER.md` (section 6) is made from this file and from nothing
else. Its test (that file, section 3): code, a data format, a limit or a timing that is there because of the console's
hardware or SDK, not because of what the game does. The game's own rules, the fixed 60 Hz game step, its content and
the importer reading the disc once at setup stay.

State (2026-11-19). Pass one ran at a1f423dd8 (rows/p022 before its restack; its code is 5526e1cf0's, where only
`native/tests/CMakeLists.txt` differs). Pass two has read each area of `FRAME-MODEL.md` 1.10 for the console's
shape, the 921 actor files by a pattern sweep with every hit read, then whole by the readall harness (its 49 area
reports folded 11-19, each cited line read at 241395b49); on 11-19 at 241395b49 the actor tables and pools were
classed and the 100 self-drawing actors placed. No area is left unread. Items open: the rows of section 3. Section
4 applies file 08's test (section 3) and the owner's answers of 18:18 (file 08 5.H). Not 100%.

## 1. Pass one: whole-tree searches

Run with `git grep -c -I -E <pattern> HEAD -- runtime native/host native/petari native/include native/math platform
crates ':(exclude)*_tests.cpp' ':(exclude)*_test.cpp' ':(exclude)*/tests/*' ':(exclude)third_party'` at a1f423dd8,
by hand (no script holds the searches: the patterns are these). Each cell is files/lines. `native/math` was searched too and had no hit in any search.

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
| The game's sound code (`Game/AudioLib`, `Game/GameAudio`, `Game/Sound`, `Game/Speaker`, `Game/RhythmLib`) | 170 | read whole by the readall harness at 0c98b11c2 (its Sound, AudioLib, GameAudio, Speaker and RhythmLib reports), every finding checked at 241395b49; P-186, P-192, P-193, P-221 to P-228 |
| Archives and the game's file access (`Game/System` archive and resource holders, `Game/Util/FileUtil`, the store) | 11 | the load path read: modern; `Game/System` read whole by the readall harness at 0c98b11c2 (199 files, 32,035 lines by its count), every finding checked at 241395b49; P-231 to P-236, lines to P-189, P-192, P-197, P-205 |
| Text (`MessageHolder` in `Game/System`, `CharStrmReader` in `Game/Ui`, `MessageUtil` and `TextString` in `Game/Util`, the tag processors in `Game/Screen`, the host's `message_utf8`, the cook's `message.rs`) | in their areas | read whole with their areas by the readall harness at 0c98b11c2, every finding checked at 241395b49: the 16-bit codes P-187 (with its 11-18 lines: the cooked text, the font's pages and byte metrics, the launcher's units), the inline tags P-202, the tag faults P-215, P-237, P-275, the settings menu's UTF-8 P-209 |
| Layouts and fonts (`Game/Ui`, `Game/System` font holders) | `Game/Ui` 51 | read whole by the readall harness at 0c98b11c2, every finding checked at 241395b49; P-184, P-229, P-230, lines to P-146, P-151, P-197, P-209, P-218 |
| The 2D screens, the image effects, the pointer, the movies (`Game/Screen`) | 289 (144 sources, 145 headers; 44,869 lines) | read whole at 6577392b5, and by the readall harness at 0c98b11c2 (checked at 241395b49); P-197 to P-220, P-184's 2D colours |
| Input (the host's and the game's) | in their areas | read whole with `Game/System`, `runtime/src` and `runtime/src/input` by the readall harness at 0c98b11c2, every finding checked at 241395b49: the game's record keeps the Wii Remote's shape, P-282 (a question for the owner); P-234, P-236, P-288, lines to P-074, P-075, P-084 |
| The game's actors (`Game/Player` 179, `Game/Enemy` 218, `Game/Boss` 333, `Game/Ride` 38, `Game/NPC` 155, `Game/MapObj` 633, `Game/LiveActor` 122; the 921 sources E14 did not reach are among them) | 1,678 | swept by pattern (below), every hit read; then read whole by the readall harness at 0c98b11c2 (its Player, Enemy, Boss, Ride, NPC, MapObj and LiveActor reports), every finding checked at 241395b49; P-243 to P-253, P-264, lines to P-084, P-136, P-146, P-154, P-155, P-189, P-190, P-192, P-214 |
| The rest of the game (`Game/Util` 158, `Game/Map` 164, `Game/Render` 18, `Game/Camera` 158, `Game/Effect` 43, `Game/Demo` 63, `Game/Scene` 44, `Game/Particle` 35, `Game/NameObj` 25, `Game/AreaObj` 72, `Game/Animation` 22, `Game/Mii` 7, `Game/Gravity` 28, `native/petari/src/game` 12, the three headers at `native/petari/include`) | 852 | read whole by the readall harness at 0c98b11c2, every finding checked at 241395b49; P-237 to P-242, P-254 to P-263, P-265 to P-268, lines to P-010, P-021, P-134, P-142, P-144, P-146, P-149 to P-151, P-154 to P-157, P-168, P-173, P-187 to P-192, P-197, P-218, P-219 |
| The engine below the game (`native/math` 9, `native/include` 17, `native/host` 8, the build files) | 34 and the build | read whole by the readall harness at 0c98b11c2, every finding checked at 241395b49; P-269 to P-275, lines to P-142, P-155, P-157, P-197, P-254 |
| The platforms (`platform/android` 37, `platform/sdl` 10) | 47 | read whole by the readall harness at 0c98b11c2, every finding checked at 241395b49; P-276 to P-281, lines to P-017, P-037, P-042, P-075, P-094, P-192, P-193, P-205 |
| The runtime (`runtime/src` 191, its input 36, render 121, render core 25, Direct3D 11 18, materials 48, Metal 32, Vulkan 42, menus 36, updater 11) | 560 | read whole by the readall harness at 0c98b11c2, every finding checked at 241395b49; P-282 to P-307, lines to P-021, P-028, P-074, P-075, P-082, P-084, P-088, P-142, P-151, P-155, P-157, P-187, P-192 to P-193, P-197, P-213, P-234, P-236, P-254 |
| The cooked GPU programs (`runtime/src/render/programs`: 1,965 pixel and 436 vertex programs, each the cook's output) | 2,401 | 160 read whole by the readall harness at 0c98b11c2 as the sample covering every feature; the whole set is read through its one writer, `crates/nebula-import/src/cook/programs.rs`, in the importer's row below; P-303, P-304, lines to P-142, P-151 |
| The importer and its tools (`crates/nebula-import` 88, `tools/dev` 3) | 91 | read whole by the readall harness at 0c98b11c2, every finding checked at 241395b49; P-308 to P-310, lines to P-134, P-151, P-157, P-187, P-188, P-192, P-202, P-292 |

What is read so far (at a1f423dd8):

- Tables. Reads by column name (`getCsvData*`, `getValue<T>(key)`, `findElement*`, `searchItemInfo`, `getColumnKind`):
  241 lines in 56 files of `native/petari/src`. By the function each is in (the scratch script `tablecalls.py`, its
  pattern recorded with the item): 15 sit in functions named like per-step code (`exe*`, `control`, `calc*`, `update*`,
  `draw*`); read one by one, 9 are `ValueControl::getValue` or a sound note's field (not a table) and 6 are
  `StageDataHolder::calcPlacementMtx` (`StageDataHolder.cpp:673-680`), which runs at placement. 99 lines are in free
  functions (`MR::`) whose callers are not yet placed. A second pass names the enclosing function of every read by
  a definition match (340 lines with `ValueControl` and the message helpers): `init*`, constructors of the demo
  keepers, `load*`, `create*FromCSV`, placement getters, story-event and race-time lookups, the light tables
  (`LightFunction.cpp:64-91`, read once for every light by `LightDataHolder.cpp:24` at load). That pass missed the reads inside the
  message and story helpers: the readall harness's System report (checked at 241395b49) shows reads by name in
  steps. A message's index is found by a binary search on its name at each fetch
  (`MessageData::findMessageIndex`, `MessageHolder.cpp:149-159`), and a story event's progress by a scan of the map
  table by name at each ask (`GameDataHolder::isPassedStoryEvent`, `GameDataHolder.cpp:282-288`; from
  `GameEventFlagChecker.cpp:43`, `EventUtil.cpp:157`, `:161`), a name not in the table reading as passed. The
  column search (`Table.cpp:33-48`) is the game's own data access (its JMap design); the names are resolved to
  ids at load in its modern form (P-189's 11-18 line).
- Layouts. Cooked `NLYT` (section 1); the record's material: P-184, with the 2D material's colours in the console's
  signed register form (`Ui::Material::mRegisterColors`, `native/petari/include/Game/Ui/Material.hpp:111-134`).
- Screen (`Game/Screen`, every file read whole at 6577392b5, 11-18; the verdict of each file kept with the
  inventory's working notes). Timing: the screens count game steps (`GameSystem::frameLoop` runs `update()` once a
  frame and the extra world steps of a mod's time rate do not, `GameSystem.cpp:182-205`; Nebula's own screens run
  from `GameSystem::update`, `:258-290`), drawn between steps by the layouts' interpolation
  (`LayoutActor.cpp:42`, `draw_walker` `:618-665`): the game's step, not the console's. Glides and edge scrolls of
  the settings menu go by the host clock. The console's shape found:
  - the layouts' anamorphic 16:9 canvas, a layout unit 4/3 wider than tall at 16:9 and flagged panes narrowed by
    0.75 (`LayoutManager.cpp:200-211`, `Pane.cpp:185-187`), with Nebula's screens undoing it again
    (`NebulaUi.cpp:568-578`, `NebulaAchievementToast.cpp:388`, `NebulaAchievementBoard.cpp:53`, `NebulaPaneKit.cpp:91-92`): P-197;
  - the depth of field's 8-bit indirect depth lookup through a ramp texture (`DepthOfFieldBlur.cpp:99-141`): P-198;
  - `IndDummy.tpl` found by its bytes' address in the archive (`LayoutManager.cpp:597-642`): P-199;
  - message tags as in-text binary records, string arguments as 4-byte tokens, rewritten byte by byte
    (`MessageTagSkipTagProcessor.cpp:5-77`, `ReplaceTagProcessor.cpp`; walked also by `NebulaUi.cpp:133-139`,
    `StaffRoll.cpp:646-657`, `TitleSequenceProduct.cpp:76-93`): P-202, beside the 16-bit codes (P-187);
  - movie subtitles and rumble started on equality with the shown frame (`MoviePlayingSequence.cpp:335-350`,
    `DemoPadRumbler.cpp:23-30`): P-203; the staff roll timed by steps beside its music (`StaffRoll.cpp:387-395`): P-206;
  - effect and sound names matched by 16-bit hashes (`MultiEmitter.cpp:243-245`, `AudSeKeeper.cpp:58`, `:81`): P-204;
  - the save in the console's `GameData.bin` layout (`BinaryDataContentAccessor.cpp:13-20`, `:55-64` and the
    storages): P-205;
  - the underwater warp in screen texels, smaller as the resolution grows (`WaterCameraFilter.cpp:43-44`): P-207;
  - the A button's pictures found by texture size and IA4 format (`NebulaIcons.cpp:2368-2390`): P-210.
  Caps on what a mod adds (5.H.3): language panes in a `BitFlag<128>` (P-200), eight sound tags a message (P-201),
  small fixed counts in the 2D screens (P-208), a mod's ABOUT picture over 1024 dropped (P-211). Faults found on the
  way: the settings menu's UTF-8 decoder (P-209), a bound of 4 over a 3-entry array (P-212). The rest is the game's
  own (its layouts, counters, menus and their step counts) or modern (the holders grow: `InlineArray::grow`,
  `Array.hpp:217-232`; pools reuse with renewed draw identity; text buffers grow to the text). Not settled by reading
  and kept with the E4 work of fed-back and camera-bound draws: the full-screen blur's feedback
  (`FullScreenBlur.cpp:38-46`), the heat haze's board placed at the step's camera (`HeatHazeEffect.cpp:19-30`), the
  lens flare; and two run checks: whether the picture font's sheet takes the icons' sharp copy (`NebulaIcons.cpp:1804-1813`),
  and Nebula's screens stepping faster while a cutscene skip runs extra `update()` calls (`GameSystem.cpp:199-202`).
  The readall harness's second whole read (at 0c98b11c2; each line it cites read again at 241395b49) added: the
  image effects' constants at the registers' 8-bit precision (P-213); draws that do step work, while the frame draws
  once and may step more (`GameSystem.cpp:182-205`): the galaxy map's capture, the pointer's blur, a message's sound
  tag, the movie's frame pick (P-214); two reads past a message by a tag's claimed length (P-215); the icons' sharp
  sheets handed over from one reused record while the store keeps its pointer for a device restart
  (`texture_store.cpp:593`, `:335-337`; P-216); a row button written at index -1 (P-217); the cooked layout's
  console field widths, 16-bit pane links and 16-character names (P-218); the movie volume as 0..127 and the
  pointer unprojected through the GX projection and viewport vectors (P-219); game behaviour chosen by
  `__ANDROID__` (5 of the 30 platform branches in `native/petari`; P-220); and the pointer's hit tests and the
  HOME menu on P-197's two-shape switch. Six of its claims fell on reading (kept with the inventory's notes), for
  example the staff roll following the display (`MR::getScreenHeight` is the constant 456, `ScreenUtil.hpp:18-20`).
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
  The readall harness read the 170 files whole (at 0c98b11c2; each line it cites read again at 241395b49). The
  console's shape it found: the Me phrases' operands and the jump tables still big-endian, the rhythm look-ahead
  over raw bytes (P-186's 11-18 line); banks, effect categories, wave groups, resource numbers, talk voices,
  limited sounds, speaker and chord ids at the console's widths or counts (P-192's two 11-18 lines); a note's sends
  as the driver's packed mix-config words decoded each update (P-221); effect, filter, switch and format values in
  the DSP's and the table's encodings (P-222, the values themselves as made); and the step learning a music
  stream's end from the audio device (P-226; the beat itself is read only by the audio thread's sequence clock,
  `RhythmSystem.cpp:100-110`). Faults on the way: linear lookups on the sound paths (P-223), cooked and program
  values trusted (P-224), streams played from the mapped store inside the mix (P-225), handles left pointed to by
  the sound they were taken from (P-227), music tables read with no bound (P-228). Kept: the driver's 7 ticks a step
  in its own conversions (`SeqTrack.cpp:676`, `AudSystem.cpp:363`), the categories' limits, an actor's own handles,
  the remix's 16 note ids (as made, 5.H.5); the 4096 host voices grow with P-192.
- Archives and file access. The game's archive names (`.arc`, `.szs` in 18 runtime, 6 game and 10 header lines) are
  names only: the store drops the ending (`asset_store.h:118-119`, `asset_store.cpp:33`). The game reads the store
  (`game.pak`, `crates/nebula-import/src/cook/store.rs`) through `AssetFolder` and `GameAssets::load` (`FileUtil.cpp:88`); `ResourceHolder`
  files a folder's entries into tables by the cooked kind (`ResourceHolder.hpp:22-29`, `NEBULA_ASSET_KIND_*`). No
  archive or compression is read while the game runs. Class: modern; nothing of the console's file system is left in
  this path. `SoundArchive` maps the cooked sound archive from the store (`SoundArchive.hpp:11-13`);
  `ErrorMessageArchive.hpp` reads the cooked game data tables; `ParticleResourceHolder` (`ParticleResourceHolder.cpp:9-34`)
  reads the cooked particles and their name table, and keeps a particle's index as 16 bits: P-188.
- Input. The game takes one record per player per read, whatever the device (`nebula_game_api.h:287-322`,
  `NebulaInputSample`): buttons by the game's own names, the stick and the pointer in -1..1 of the picture, tilt in g,
  spins counted between reads. No report of the console's controller reaches the game. Class: not modern, corrected
  11-19 from the readall harness's runtime/src and runtime/src/input reports (checked at 241395b49): the record keeps
  the Wii Remote's shape, tilt as gravity in the remote's axes, the pointer's roll as the sensor bar would show it and
  its distance from the screen (`nebula_game_api.h:304-316`); every native device is rewritten into it (a pad's tilt
  as a remote's gravity by the section the game names, `tilt.cpp:67-112`; a virtual remote's distance,
  `native_input.h:50-52`; the pointer off when a remote would not face the bar, `:80-82`, `controls_engine.cpp:10-23`),
  and the game reads those fields as a remote's (`PlayerInputs.cpp:123-131`, `StarPointerLayout.cpp:688`,
  `PlayerInputUtil.cpp:64`): P-282, a question for the owner. The timing of the reads (once a step) is P-075, P-078
  and P-163 (E6b). The host's drivers for real Wii Remotes read the remote's own reports: a device, not the console.
- The actor folders (the 921 files' folders: 285,511 lines with their headers). Swept by the scratch script
  `actors.py` for six shapes, each hit read: the console CPU's intrinsics and cache and paired-single instructions
  (`__cntlzw`, `__fsel`, `__frsqrte`, `psq_`, `__dcb*`, `OSRoundUp`...) 0; the console's addresses and memory sizes 0;
  pointers kept as integers 1 (`ClippingActorHolder.cpp:16`, a hash of an address: host code); big-endian or packed
  access 4 (a colour packed for a draw, a sound id's fields: the game's); data read through byte offsets 14 (cooked
  records: the ghost file `NGST`, sequences, sound tables, `MarioConst`'s fields by name for mods); tables of a literal
  size 138 (legs, joints, walls, curve tables, the sound driver's registers: the game's own shapes). No console item
  found in them by these shapes. The readall harness then read them whole (the actors' row of the table above): the
  console's shapes it found there are P-243 to P-253 and P-264, each line checked at 241395b49.
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
| A normal map remade on the CPU and lit through a gradient texture | `native/petari/src/Game/MapObj/NormalMapBase.cpp:114`, `:119`, `:483-523` (`createGradTexture`: an LA8 gradient built on the CPU), `:525` on (`swapColorGB`: each RGBA8 texel's normal decoded and written back as 0, g, b, r in a copy), its own draw settings (`drawMaterialSetting`, `setStageForObject`) | the console's texture stages and light channels, restated (read at 5526e1cf0). The CPU: a 256 x 256 LA8 ramp made for each actor at its start, intensity by column and alpha by row (`:483-523`), keeping the console's tile conversion's scramble in rows 0-3 (`:505-510`); a matrix from the two light directions made each draw (`updateLightMtx`, `:151-215`); each draw finds the normal-mapped materials by name (`strstr` in `isNormalMapMaterial`, `:217-223`, over meshes, joints and materials, `shapeAnalyzeDraw`, `:237-292`) and remakes the joint's matrix from its angles. The shader (`runtime/src/render/materials/normal_map.hlsl`): the light channels per vertex in 0-255 units (`:48-59`); the indirect stage's normal as 8 bits less 128 (`:75`), moving the ramp's centre (texel 128) by at most 0.504 x 128 x sqrt(3), 112 texels, so rows 0-3 are never sampled (by this arithmetic, not run); four stages each clamped, the last doubled (`:109`), the alpha the console's unselected constant (`KASEL_1`) | the normal map final at the cook (the `Zormal` swap done there); the materials found once at load; the ramp evaluated per pixel from the normal and the two lights, its levels, curve and colours as the material's constants, no texture made on the CPU (the unreachable scramble goes with it); the lights from the step's light table (E3 step 6); one material program in the shared draw path, the stages written as what they compute | P-190 | open (E3) | not yet |
| What the game runs follows the screen's shape | `native/petari/src/Game/LiveActor/ClippingJudge.cpp:20-21`, `:33`, `:42` (`MR::getAspect()` in the volumes that decide which actors run); 42 lines of `native/petari/src` read an actor's clipped state (`isClipped`, `isInvalidClipping`, `isClippingInvalid`: `grep -rn ... --include=*.cpp`, 42 hits) | the console's culling of actors' runs by the one screen it had | (c) first: every actor of the loaded scene runs in every step, drawing culled by the real view; (b) if (c) misses a measure: one run volume the same on every screen | P-191 | open, after D.6 (file 08 5.H.2) | not yet |
| Shared counts and widths that can cap what a mod adds | `MR::Vector`, holders, registries, groups (section 2, Limits); id and index widths: `Sound.hpp:26-27` (`SoundID`: section 8 bits, group 8 bits, wave 16 bits; `SoundVoices.cpp:70`), P-188's particle index; the 138 literal tables and the pools, each read (section 4: none grows past P-192's list) | a width or a size chosen for the console's content | every shared holder grows and every id or index is 32 bits wide or more; an actor's own count stays its number | P-192 | open (E) | not yet |
| Surround output by a written law | `runtime/src/audio_mixer.cpp:244-269`, `:704-729`; `runtime/src/native_audio.cpp:95-131`; `runtime/src/audio_device_wasapi.cpp:135-155`, `:207-230`; `platform/sdl/audio_device_sdl.cpp:33`; `native/petari/src/Game/Sound/SoundVoices.cpp:665-786` (section 4 says what each does) | none kept: the two-channel output stays as made (owner, 5.H.5); the surround law is new | a law from a sound's pan and surround amounts to each reported layout's speaker gains, written here before the code, each property asserted by a test, a render-to-file test through the real mixer | P-193 | open (step G6.7, order 29 of the native plan, after the tablet's pacing steps; the player's Speakers choice with it) | not yet |
| Actors that draw for themselves | 100 files of `native/petari/src` define `draw() const` (`git grep -l -E 'void \w+::draw\(\) const' 241395b49 -- native/petari/src`: 100), 55 of them name a draw helper | read 11-19 at 241395b49, every body whole, each file classed once by `D:/NebulaWork/plan/a1/inv/draw_class.py`: 28 sites written at each draw, their own vertices or through `DirectDraw`, `BezierSurface` and the shadow shapes (P-154's sites, FRAME-MODEL 2.12.1); 7 kept meshes (2.12's nine files, P-195); 5 shadows and volumes counted in the stencil (P-143, P-181); 8 models through the material path (one shape per point, `KirairaChain.cpp:98-107`, `Plant.cpp:698-701`, instanced by the walk, FRAME-MODEL 1.7 item 7); 16 work on the whole frame (the frame's copies: `NEBULA_PASS_COPY_FRAME` and `TAKE_FRAME`, `FrameList.cpp:1062-1072`, given out as a `ResTIMG`, `ScreenUtil.cpp:122-124`: E4's target passes and P-155; the depth of field P-198; the bloom P-185 and P-213; the blurs P-213; the fall-out field P-198; fills and fades plain; the movie kept); 17 layouts (P-197); 19 empty or calling other draws. New: WaterCameraFilter passes the screen's size through `u16` for its quad (`WaterCameraFilter.cpp:145-150`), the console's 16-bit screen positions (a line of P-154); the name `drawGD` (the console's display lists) is left on two kept meshes (`WaterRoad.cpp:250-260`, `OceanRingDrawer.cpp:263`) | placed: each draw in E3's step or E4's pass named, the console's shape in each held by the rows named; nothing of its own is left to P-194 | P-194 | read (11-19); closes with the rows it names | the read and its script |
| The layouts' anamorphic canvas | `LayoutManager.cpp:200-211`, `:477-482`; `Pane.cpp:69`, `:185-187`; `LayoutUtil.cpp:669-678`; `DrawUtil.cpp:113-126`; undone again in `NebulaUi.cpp:568-578`, `NebulaAchievementToast.cpp:388`, `NebulaAchievementBoard.cpp:53`, `WipeRing.cpp:209`; empty `isScreen16Per9` branches `StarPointerCommandStream.cpp:91-92`, `StarPointerLayout.cpp:708-710` | the console's 16:9 picture as a 4:3 canvas stretched (a layout unit 4/3 wide), flagged panes narrowed by 0.75 | square layout units: the canvas 608 * 4/3 wide at 16:9, unflagged panes given the 4/3 width, flagged panes nothing; the 0.75 code goes | P-197 | open | not yet |
| The depth of field's 8-bit indirect lookup | `DepthOfFieldBlur.cpp:99-141`, `frame_depth.hlsl`, `dof_final.hlsl`, `ImageEffectLocalUtil.cpp:113-124` | the depth copied to 8 bits and moved through an indirect matrix into a 128x2 ramp texture | the final pass reads the depth at full precision and weighs the blur from the two distances | P-198 | open | not yet |
| `IndDummy.tpl` found by address | `LayoutManager.cpp:597-642` | the archive as a blob of bytes, a resource known by where it lies | the layout's texture list names it; the swap goes by that entry | P-199 | open | not yet |
| Language panes in 128 bits | `LayoutManager.cpp:34-130`, `:645-705` | a fixed `BitFlag<128>` for a layout's panes | marks sized to the pane's children | P-200 | open | not yet |
| Eight sound tags a message | `CustomTagProcessor.cpp:483-499` | a u8 mask of played sound tags | a mask or list sized to the message's tags | P-201 | open | not yet |
| Message tags as in-text records | `MessageTagSkipTagProcessor.cpp:5-77`; `ReplaceTagProcessor.cpp:49-72`, `:139-161`, `:239-321`; `NebulaUi.cpp:133-139`; `StaffRoll.cpp:646-657`; `TitleSequenceProduct.cpp:76-93`; `NebulaSettingsMenu.cpp:889-901` | the message editor's binary records (0x1A, a length byte, 16-bit parameters, 4-byte string tokens) inside the text | a message parsed once into text runs and typed tag records, each holding its own string | P-202 | open | not yet |
| Movie cues on one frame | `MoviePlayingSequence.cpp:335-350`, `DemoPadRumbler.cpp:23-30`, `MovieStream.cpp:194-217`, `MoviePlayerSimple.cpp:72-74` | a cue fires only when the shown frame equals its start; a hitch passes it | a cue fires when the movie's place crosses its start, read from the movie's position | P-203 | open | not yet |
| Names by 16-bit hashes | `PaneEffectKeeper.cpp:107-116`, `MultiEmitter.cpp:243-245`, `AudSeKeeper.cpp:58`, `:81` | names compared by the low 16 bits of a hash | by name, as `MultiSceneEffectKeeper.cpp:77-80` does | P-204 | open | not yet |
| The save in `GameData.bin`'s layout | `BinaryDataContentAccessor.cpp:13-20`, `:55-64`; `GameDataGalaxyStorage.cpp:184`; `BinaryDataChunkHolder.cpp:46-49`; `SpinDriverPathStorage.cpp:55`; `GameEventValueChecker.cpp:48-53` | 16-bit name hashes and offsets, counts in a byte, 6-bit ids, u16 values | named, sized records with a version; an old file read once by the importer | P-205 | open | not yet |
| The staff roll timed by steps | `StaffRoll.cpp:49-56`, `:178-187`, `:387-395`, `:552-554` | pictures and the end placed by counting steps beside the music | placed from the stream's play position | P-206 | open | not yet |
| The underwater warp in pixels | `WaterCameraFilter.cpp:43-44`, `water_camera.hlsl` | a warp of 0.05 screen texels a step: about 6.4 pixels at any resolution | the warp as a share of the picture | P-207 | open | not yet |
| Small fixed counts in the 2D screens | `PictureBookLayout.cpp:61-73`, `:425-443`; `PowerStarList.cpp:113-123`, `:573-579`; `StarPieceCounter.cpp:138-156`; `WipeHolderBase.cpp:7-10`, `:64-73`; `StaffRoll.cpp:27-28`, `:283-288` | counts fixed for the disc's content | probe to the first missing name, lists sized to the content, counters laid out from the number's width | P-208 | open | not yet |
| The settings menu's UTF-8 decoder | `NebulaSettingsMenu.cpp:538-555` (callers `:776`, `:2386`, `:2483`, `:2575`, `:3229`) | none (a fault): 4-byte sequences and stray bytes mis-read | one decoder for the module: `MR::utf8ToText16` beside `MR::Text16` (`StringUtil`), the achievements' decoder with longer forms refused | P-209 | written, lands by the chain after P-212 | `rows/p209` 5aec3a567: native_formatted_name_tests 16 of 16 (the old decoder 11 of 16 wrong); `git grep -n "fromUtf8(char16_t" -- native` 1 before, 0 after |
| Two more lax UTF-8 decoders | `NebulaText.cpp:110-133` (NebulaText.txt); `nebula_text_tags.h:57-71` (the mod surface's tag writer) | none (a fault): stray, cut-short and longer forms stored as read | the one decoder's rules in a header every side includes, `MR::utf8ToText16` built on it; the font's reader is P-187's | P-320 | open | not yet |
| The A button's pictures by size and format | `NebulaIcons.cpp:2368-2390` | a picture known by its 48 or 40 texel IA4 texture | by the texture's name, the whole tree walked | P-210 | open | not yet |
| A mod's ABOUT picture over 1024 | `NebulaSettingsMenu.cpp:321`, `:4996-5001` | none of the console's: a fixed copy size | the texture made at the picture's size, never dropped | P-211 | open | not yet |
| A bound of 4 over 3 | `NebulaSettingsMenu.cpp:5521`, `:5719` | none (a fault) | bound by the array's size | P-212 | written, lands by the chain after docs/model5 | `rows/p212` 8f095d863: the index bound by the array's count, a static_assert ties the array to `NEBULA_BENCHMARK_PART_AGAIN + 1` |
| Image-effect constants as bytes | `ImageEffectState.cpp:103-113`, `BloomEffect.cpp:61-65`, `BloomEffectSimple.cpp:26-35`, `ScreenBlurEffect.cpp:75`; unpacked by every renderer | the TEV registers' 8-bit colours | float constants written once a step | P-213 | open | not yet |
| Draws that do step work | `GalaxyMapController.cpp:161-163`, `:168-183`; `StarPointerLayout.cpp:545-581`, `:844`; `CustomTagProcessor.cpp:478-499`; `MoviePlayerSimple.cpp:73`, `:128` | draw and step one to one under the retrace | the step decides and stores; draw() only records | P-214 | fixed (S0.2.1) | the map's capture prepared in `GalaxyMapController::calcAnim` (`prepareCapture`), recorded by `draw` (`recordCapture`); the blur's hide flags set by `StarPointerLayout::updateBlurShown` from `StarPointerDirector::update`; the sound tags started by `TalkTextFormer`'s step walk (`TextBox::PrintSilent`), never by a draw; the movie's frame picked in `MoviePlayerSimple::exePlaying` and `exeOpen` |
| Reads past a message by a tag's length | `CustomTagProcessor.cpp:644-654`, `NebulaUi.cpp:154-160` | none (a fault) | bound by the text's length | P-215 | open | not yet |
| One record for every sharp sheet | `NebulaIcons.cpp:1826-1857`, `:2078`, `:2490`; `texture_store.cpp:593`, `:335-337` | none (a fault) | a record per sheet kept while handed over; retire at the frame fence | P-216 | open | not yet |
| A row button at index -1 | `NebulaSettingsMenu.cpp:4591-4597` | none (a fault) | no button when none can be made | P-217 | open | not yet |
| The cooked layout's console widths | `UiData.hpp:46-59`, `Pane.hpp:154-155`, `LayoutManager.hpp:79-81` | BRLYT's 16-bit links, 16-character names, 8-byte user data | 32-bit links, whole names resolved to ids, length-prefixed user data | P-218 | open | not yet |
| Console parameter forms at the host | `MoviePlayerSimple.cpp:141`, `MovieStream.cpp:235-242`; `StarPointerDirector.hpp:20-21` | THP's 0..127 volume; GX's projection and viewport vectors | an f32 gain and ramp; the inverse view-projection in the host's terms | P-219 | open | not yet |
| Behaviour by operating system | `TitleSequenceProduct.cpp:72-93`, `:234-238`; `StarPointerGuidance.cpp:61-66`, `:84-88`, `:150-154` | none (the port's own) | by the input in use and the device's capabilities | P-220 | open | not yet |
| A note's sends as mix-config words | `SoundVoices.hpp:123`, `SoundVoices.cpp:564-579`, `:704-786`; `StreamPlayer.hpp:101` | the sound driver's packed bus and curve words | typed send records; a bus map per output mode made once | P-221 | open | not yet |
| Sound settings in DSP and table encodings | `SoundEngine.cpp:596-606`, `SoundVoices.hpp:214-234`, `SoundAudibleParam.hpp:11-40`, `SoundVoices.cpp:128-132` | Q15 integers, a filter mode word, packed switch bits, the disc's format code | floats of the same values, named flags, a playable flag and category | P-222 | open | not yet |
| Linear lookups on the sound paths | `SoundArchive.cpp:131-137`, `SoundBank.cpp:70-98` | tables small enough to walk | maps made at load; per-id group lists | P-223 | open | not yet |
| Cooked sound values trusted | `StreamPlayer.cpp:33-36`, `:177-221`; `SeqTrack.cpp:1412-1426`; `EffectManager.cpp:162-168`; `PlayerLink.cpp:128-133`; `EffectSound.cpp:134`, `:152` | none (a fault) | every count, index and offset checked at load; ports bounded by the cook | P-224 | open | not yet |
| Streams played from the mapped store | `StreamPlayer.cpp:207-208`, `StreamSound.cpp:105` | none (the port's own) | a reader thread fills a ring ahead of the play position | P-225 | open | not yet |
| A stream's end told by the device | `StreamPlayer.hpp:26-28`, `StreamSound.cpp:122-124`, `:263-266`, `AudBgm.hpp:76-82`, `SoundUtil.cpp:378-440` | sound and picture from one clock | the end known by the step clock; the audio follows | P-226 | open | not yet |
| Sound handles taken over | `AudMePlayer.cpp:76-91`, `:186-194`, `:235-244`; `SpkSound.cpp:34-35`, `:74-90`, `:151-166` | none (a fault) | a handle and its sound detach as a pair | P-227 | open | not yet |
| Music tables read with no bound | `AudStageBgmWrap.cpp:110-112`, `:137-139`; `AudBgmSetting.cpp:283-299`; `AudTrackController.cpp:78` | tables sized to the disc's music | each number checked; settings as records keyed by sound id | P-228 | open | not yet |
| Layout images as TPL containers | `TexMap.cpp:43-64`, `Animation.cpp:150-158`; `Font.hpp:20`, `CharWriter.cpp:83-95` | the TPL's offsets and texel format codes, walked at run time | store textures by id; the sheet's kind in the renderer's terms | P-229 | open | not yet |
| Cooked layouts trusted | `Animation.cpp:155-157`, `:232`, `:256`; `Layout.cpp:275`; `Group.cpp:10-12`; `TexMap.cpp:53-60`; `UiCommon.cpp:122-131` | none (a fault) | every index, count and offset checked at load | P-230 | open | not yet |
| Story steps walked as an array | `StorySequenceExecutor.cpp:855-870`, `:958-965`, `:233-265`, `:368`, `:483` | none (a fault) | a built sequence's steps in one array, walked by its count | P-231 | open | not yet |
| Hashes taken for names | `GameEventValueChecker.cpp:75-91`; `GameEventFlagTable.cpp:336-349`, `:586`; `ResourceHolderManager.cpp:144-154` | 16- and 15-bit name hashes as keys | names compared after a hash; flag ids checked unique at load | P-232 | open | not yet |
| A resource's size by a tree walk | `AssetFolder.cpp:115-133`, `LayoutHolder.cpp:12-35` | the archive API's getResSize, kinds by four-byte tags | the handle carries size and kind | P-233 | open | not yet |
| Pad connection and language in the console's shape | `PlayerInputs.cpp:61-87`, `Language.cpp:14-71` | the remote SDK's callbacks; the ten system-language slots | the device's state; the language named by the host | P-234 | open | not yet |
| Fixed wall-time budgets and waits on the game thread | `NebulaCutsceneSkip.cpp:46-47`, `:384-399`; `SaveDataHandler.cpp:120-122` | none (the port's own; a 16.7 ms frame assumed) | a share of the measured frame interval; requests queued | P-235 | open | not yet |
| Rumble through the motor-callback registry | `InputRumble.cpp:10`, `:81-88`, `:116-125`; `PlayerInput.cpp:37-38`; `InputInfoChecker.cpp:7`, `:29-39` | the motor callback's registry; the info request every 300 s | each pad steps the rumble in use once; the battery read when shown | P-236 | open | not yet |
| Render values made on the CPU in the console's units | `DrawUtil.cpp:141-163` (the stencil cleared by a full-screen quad), `ModelUtil.cpp:163-183` (fog range from the one global view), `FurDrawer.cpp:85-87`, `:148-152` (shell values cut to bytes, strands in the 4 x 4 tile order) | the console's clears by drawing, its one view, its byte registers and tiled texture layout | the pass's load action; fog per view on the GPU; float values; the strands in the texture's own order | P-239 | open | not yet |
| Game rules that read the screen's shape or size | `BasaBasa.cpp:658-685`, `Gesso.cpp:417-433`, `Petari.cpp:458-466`, `BombBird.cpp:126-132` (the frustum); `StarPointerController.cpp:68`, `:96-99`, `FireBall.cpp:225-228` (the pointer's speed in screen pixels); `CameraViewInterpolator.cpp:192-197` (the camera's wall ball from the field of view and aspect) | the console's two screens, where the frustum and the pixel width were fixed | the rule reads a volume or a speed in the world's or the design's units, the same on every screen | P-243, P-244, P-256 | open; questions for the owner | P-191's four-screen proof |
| Work spread over steps for the console's CPU | `PunchBox.cpp:115-128` (the shadow recomputed once every 128 steps) | the console's CPU budget | computed when it changes, or on the GPU | P-248 | open | not yet |
| The views and draw order in the GX calls' shapes | `FrameList.hpp:84-90` (projection as a type and six terms, viewport, z scale, scissor latched and read back); `Model.cpp:317-331` (translucent order by the one global view); `TypedDraw.cpp:131-141` (a site's camera found by comparing matrix bytes) | GX's latched projection and viewport, one camera per frame | a view record per picture; order per view; the camera named by the draw | P-254 | open | not yet |
| Miis known by the console format's ids and flags | `MiiCharacters.cpp:22-24`, `:156-182`, `:202`, `:221-224` | the 8-byte create ID and its flag bits (temporary, special, local-only) | the host library's own ids; which Miis are offered by the library's rules | P-268 | open; question for the owner | not yet |
| The input record in the Wii Remote's shape | `nebula_game_api.h:304-316`; `tilt.cpp:67-112`; `native_input.h:50-52`, `:80-82`; `controls_engine.cpp:10-23`; read at `PlayerInputs.cpp:123-131`, `StarPointerLayout.cpp:688`, `PlayerInputUtil.cpp:64` | tilt as a remote's gravity, the sensor bar's roll, a remote's distance | the device's own motion and a pointer in the picture's terms, each rule reading what it means | P-282 | open; question for the owner | not yet |
| The game's bloom around a copy and write-back of the frame | `game_bloom.cpp:207-233`, `:345-351`, `:564-579`; `game_bloom.hlsl:5-6`, `:64-71`; the same in the other three renderers | the console's frame copy and write-back around the glow pass, the scene clamped to 8-bit range | the glow draws into its own target, the composite only adds | P-289 | open; question for the owner (the clamp on HDR) | not yet |
| Clears and frame copies in the console's copy forms | `renderer_d3d12.cpp:6436-6475`, `renderer_common.cpp:228-235`, `renderer_common.h:41-47`, `renderer_d3d11.h:426-430`, `renderer_vk_frame.cpp:606-613`, `:2845-2851`, `renderer_mtl_scene.mm:592-600`, `:1196-1200` | the copy-with-clear's enables, rectangle and pixel-format flag; the copy's channel forms made again by a conversion draw | load actions stated per pass; each copy's format known at load, read through a swizzle | P-290, P-296 | open | not yet |
| Depth and masks through 8-bit colour copies | `frame_depth.hlsl:24-33`, `dof_final.hlsl:18-23`, `screen_blur.hlsl:17-20`, `clip_fill.hlsl:18-25` | the console's depth read back through a colour copy | the passes read the depth target by name; masks in their own target | P-300 | open | not yet |
| The console's texture compression and movie container | `block4_astc.h:4-10`, `texture_pack.h:44-47` (CMPR's 5/8 + 3/8 mixes); `movie_playback.cpp:737-782`, `crates/nebula-import/src/cook/thp.rs:167-174` (the THP frame chain) | CMPR's palette mixes; THP's streaming chain | each texture cooked in the forms devices sample; a frame table in the header | P-291, P-292 | open | not yet |
| The game's picture read back for a step | `StarPointerDirector.cpp:155-177`, `StarPointerController.cpp:31-43`, `TalkDirector.cpp:100-124`, `LensFlare.cpp:230` | the console's peek at its frame buffer | a scene query in the step (decided 11-19, the owner's "compute it from the world"; written here before its code, S0.2.2). **Inputs:** the step's camera (its view matrix, `MR::getStarPointerViewMtx` for the pointer and `MR::getCameraViewMtx` for the balloon, and its perspective: field of view, aspect, near and far, `CameraContext`) and a screen point (the pointer's `PointerInfo::mPos`; the balloon's `TalkPeekZ::mScreenPos`). **Test:** the ray from the camera through that point, against the map's collision (`MR::getFirstPolyOnLineToMap`) and the pointer targets' spheres (`StarPointerTarget`); the nearest hit along the ray is the answer. **Result:** the world point (`StarPointerController::mWorldPos`, `TalkPeekZ::mWorldPos`) and the distance along the view, the view-space depth of that point as `StarPointerController::calcViewDistanceZ` gives it today (`PointerInfo::mViewDistZ`). **No hit:** the point where the ray meets the camera's far distance (`CameraContext::mFarZ`), where the picture's cleared depth put it; a pointer outside the screen keeps its last point, as today (`mDrawReady` false). **Called** from `StarPointerController::movement` in `StarPointerDirector::update` (S0.2.3) and for the talk balloon's point (S0.2.4): no picture read back, no frame of delay, the same at every screen shape and resolution. **The flare:** its visibility computed on the graphics card in the frame and never returned to the game (S0.2.5). Its samples are `BrightObjBase`'s 17 points (`BrightObjBase::calcSamples`): the centre, 8 at 0.4 of the radius and 8 at 0.7, each with the depth in front of the sphere's surface at it; the extra mark entry the old `FrameList::probeScreen` added to each ask was the asker's tag, not a sample (read at S0.2.5). After the scene, `runtime/src/render/materials/flare_sight.hlsl` counts the samples the scene's depth leaves seen and writes the share, moved toward it by at most 0.05 a picture, into a 1 by 1 target, one of two taken in turn; `runtime/src/render/materials/flare_blend.hlsl` then mixes the picture without the flare's models and with them by that share, exact because every flare material blends by its source alpha. The ring, glow and line are placed from the sun and the step's camera alone (`LensFlareDirector::checkBrightObj`). | P-294 (with N-1068) | fixed at S0.2.5; form decided 11-19 (S0.2.2); the pointer asks the scene since S0.2.3 (`MR::calcScenePointUnderScreen` in `StarPointerUtil.cpp`, its targets kept in `AllLiveActorGroup`); the balloon's read is deleted at S0.2.4 with no query in its place, as nothing read its world point; the flare decided on the graphics card since S0.2.5 (`flare_sight.hlsl`, `flare_blend.hlsl`): nothing of the game reads the picture; the asking path, its wait and the probe pass are deleted at S0.2.6 | P-191's four-screen proof; the step hashes of one recorded play equal on every renderer (S0.2.6) |
| Pixel stages in the console's combiner | the hand-written programs (`color_tex_alpha.hlsl:8-9`, `:30-32` and 18 more; `program.hlsli:206-210`); the cooked programs' writer (`crates/nebula-import/src/cook/programs.rs:436-441`, `:491-521`, `:627` on, `:806-830`) | stages, 8-bit wraps and compares, two-reference alpha tests, 8-bit indirect warps in texels, the console's coordinate generators and q = 0 rule | one float expression per named form; a float cutoff; a warp map in UV units; coordinates by source and kind | P-299, P-303, P-304 | open | the pictures per form group |
| Replacing passes for a hand list of models | `crates/nebula-import/src/cook/programs.rs:1769-1800` | none of the console's; a cap on what a mod adds (5.H.3) | each replacing pass a form declared by the content or the draw | P-309 | open | not yet |
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
- The 138 tables of a literal size and the pools made ahead, classed for P-192 by the owner's rule (file 08 5.H.3)
  on 11-19 at 241395b49, each by reading the code that fills it. An actor's own count (`DinoPackunVs2.cpp:34-35`, 64
  and 16 fires) stays, the owner's words.
  - Tables: `git grep -n -E '^\s*(static\s+)?(const\s+|constexpr\s+)?[A-Za-z_][A-Za-z0-9_:<>, ]*[ *&]+m?[A-Za-z_][A-Za-z0-9_]*\s*\[\s*[0-9]+\s*\]' 241395b49 -- native/petari/{src,include}/Game/{Player,Enemy,Boss,Ride,NPC,MapObj,LiveActor,Sound}`:
    149 hits, 11 of them an index inside an expression (`MarioActorMorph.cpp:25-28`, `MarioActorParts.cpp:245`,
    `SkeletalFishBossRailHolder.cpp:29`, `HitSensorKeeper.cpp:42`, `:105`, `ShadowController.cpp:70`,
    `SoundVoices.hpp:111`, `RhythmSystem.cpp:361`), so 138 tables. Each hit is classed once, none left, by
    `D:/NebulaWork/plan/a1/inv/tables_actors_class.py` over `D:/NebulaWork/plan/a1/inv/out/tables_actors.txt`: a function's working array 33; a
    field of a cooked record or a triangle, fixed by its format, 14; the game's own values 36 (`MarioAnimatorData`,
    `MarioConst`, the sound driver's tables, `SoundEmitter`'s 33 sounds, an argument past them silent by design,
    `SoundEmitter.cpp:35-36`); the parts an actor is made of 38 (a boss's legs, walls and joints; BigBubble's six
    neighbours, one per axis, `BigBubble.cpp:1140-1147`; ArrowSwitchMulti's four targets, `ArrowSwitchMulti.cpp:72-77`,
    `:137`; RingBeam's 32 points, `RingBeam.cpp:49-58`; SkeletalFishBossBattleDirector's four guard models, which play
    the power-up demos' fixed two and four animations, `SkeletalFishBossBattleDirector.cpp:57-73`); the sound
    driver's state as made 17 (5.H.5). Two that looked shared are not caps: GroupCheckManager's two groups are each a
    `NameIndex`, which grows past the 32 and 8 it is made with (`GroupChecker.cpp:28-29`, `NameIndex.hpp:80-82`);
    StarPieceDirector's two shooters are one per controller port (`StarPieceDirector.cpp:49-51`, `:332`). No table
    grows that P-192 does not already list.
  - Pools: actors taken from a group when dead (`git grep -n -E 'getDeadActor\(\)|getDeadMember|getDeadObj|findDeadActor|getDeadWeakestActor' 241395b49 -- native/petari/src`,
    28 lines) and free slots found by a loop (`git grep -n -E 'isDead\(m[A-Za-z0-9_]+\[[a-z]+\]\)|isDead\([a-zA-Z_>.-]*m[A-Za-z0-9_]+\[[a-z]+\]\)' 241395b49 -- native/petari/src`,
    40 lines), each read. Made again when empty: star pieces (`StarPieceDirector.cpp:257-263`), coins
    (`CoinHolder.cpp:131-152`), air bubbles (`AirBubbleHolder.cpp:14-26`), big bubbles (`BigBubbleHolder.cpp:65-76`);
    their making inside a step is P-247. Scene pools shared by every instance that answer none when full: Kabokuri
    fires, Kameck beams, fire balls and turtles, TakoHei ink (16 each), clip-area drops (32), the four short talk
    balloons, NoteFairy's and RockCreator's 1024: each grows, in P-192 already. The rest are one actor's own, and
    stay: a boss's bombs, fires, rocks and rings (`BossStinkBug.cpp:408`, `DinoPackunBattleVs2Lv1.cpp:366`, `:390`,
    `KoopaParts.cpp:59-116`, `PoltaFunction.cpp:150-220`, `Syati.cpp:606`, `:664`); a launcher's or group's members
    sized by its placement (`BegomanLauncher.cpp:50`, `MeteorStrikeLauncher.cpp:58`, `PlantGroup.cpp:120`,
    `StarPieceGroup.cpp:90-132`); fixed beams, bullets, ice and hills (`BallBeamer.cpp:53`, `RingBeamer.cpp:50`,
    `WaterBazooka.cpp:913`, `IceMerameraKing.cpp:112`, `WormEatenPlanet.cpp:132`); a Kameck's eight summoned actors,
    a rule it checks before it attacks (`ActiveActorList(8)`, `Kameck.cpp:51`, `:397`; `BossKameck.cpp:36`,
    `BossKameckStateBattle.cpp:169`). The screens' slots are pages or content: eight Mii icons a page
    (`MiiSelect.hpp:115`, `MiiSelect.cpp:499`, `mBaseIndex + i`), the file select's fellow models
    (`FileSelectItem.cpp:98`, `:391`), the staff roll's lines (P-208).
- Units of the console's picture (the 456-line canvas, the 608 and 832 widths, lines sized in 456-line units, the
  640 x 456 grid of effect targets): coordinates of the content, drawn at the real resolution by every renderer
  (section 2): no handling of the console's shape. Not items.
- The sound driver's tick, the filters made for 32 kHz, the volume ramp, the speaker's 6 kHz waves, the two-channel
  fold: kept, the owner's answer ("Sound stays as it was made"). The surround output: P-193.
- Movies, the disc's frames: kept, the owner's answer ("Keep them"). No item.
- The 60 Hz game step: the owner's decision (file 08 section 3).
- New by this test: actors that draw for themselves (P-194), a class `FRAME-MODEL.md` did not name. Read 11-19: each placed in E3 or E4, its console shape in a row already open (section 3's row).

The 921 actor files were read whole by the readall harness (file 08 5.H.1; section 2, the actors' row), its
findings checked line by line at 241395b49 and filed as P-243 to P-253 and P-264; the tables and pools above were
classed by reading after it.

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
- Read again at 697535a86 (11-19), two more faults of the speakers-of-their-own path:
  - the panner's rear effect sends are matrixed into effect lines 2 and 3 with the cross term (`kUnit * left -
    kMatrix * right`, `audio_mixer.cpp:806-816`) whatever the output, so with rear speakers the rear echo still
    comes matrix-encoded through the front lines;
  - power: on two channels a sound on one surround bus reaches the speakers at `kFold^2 (kUnit^2 + kMatrix^2)` =
    0.8602 of its power (-0.654 dB, `:16-19`, `:262-264`); on its own speaker it is at 1, so the same sound is 0.65 dB
    louder with rear speakers than on two channels.

The law (written 11-19, before P-193's code; each line a property its test asserts):

1. Two channels: today's matrix, bit for bit (the owner: as made). The game's mono mode (0) is the game's own and
   stays; what it does in the mixer was not checked here.
2. More speakers: the layout is the device's report, never assumed: Windows the mix format's channel mask; Android
   the output's channel mask where the system reports one (else two channels); Linux and Mac what their device
   reports. Speakers the game has no content for (low frequency, height, front wide) stay silent.
3. A note in surround form, and every note when the output has rear speakers, is placed by its pan p and rear r
   with the panner's constant-power law (front left `cos(pi p/2) cos(pi r/2)`, and so on); the six-bus note path
   with its matrix curve (`SoundVoices.cpp:704-786`) and the inverted right are the two-channel encoding and are not
   used. Which sounds and effect lines send to the inverted right or the surround buses is read first; an effect
   line's return to them becomes a rear return of the same power.
4. Rear gain: a sound's power on rear speakers equals its power on two channels: rear amounts are scaled by
   `kFold sqrt(kUnit^2 + kMatrix^2)` = 0.92747; with sides and backs both present each gets 0.92747 / sqrt 2 =
   0.65582 of it. Front and centre at 1; a centre with no speaker of its own to left and right at 1 / sqrt 2.
5. No delay, no inversion and no matrix on a speaker of its own: the surround buses' 2.5 ms and the rear effect sends'
   matrix belong to the two-channel encoding only; with rear speakers the rear effect sends feed rear echo lines
   unmatrixed.
6. Left and right mirror: swapping p for 1 - p swaps every left speaker's gain with its right twin's, at every r.
7. No jump: every gain is continuous in p and r over [0, 1]; a change of p, r or layout ramps as volumes do (10 ms).
8. Channel order: the device's (the mask's bit order on Windows, the reported order elsewhere), each channel named
   from its bit, never by position.
9. Tests: the properties over p and r on a 101 by 101 grid for each layout (2.0, 3.0, 4.0, 5.1 side, 5.1 back, 7.1,
   7.1.4); a render of known sounds through the real mixer to a file for each layout, each channel's energy and
   phase checked against 1 to 8. Windows and Android in the gate; Linux and Mac built with it. The owner's listening
   comes later and is not a gate.

Added 11-22 (the owner: 3.1, 5.1 and 7.1 in the game's options; the same words as step G6.7's card in
`docs/plan/native/05-NATIVE-LANE-G.md`):

10. The player's choice. One setting, Speakers: Auto, Mono, Stereo, Encoded stereo, 3.1, 5.1, 7.1 (stored as
    `audio_speakers`; the old `audio_sound_output` is read once and not written). Mono, Stereo and Encoded stereo
    (today's Surround on two channels) are the two-channel paths of point 1, on the device's front pair, every other
    channel silent. Auto is the device's report (point 2); on two channels it is Stereo, bit for bit. 3.1 is left, right
    and centre; 5.1 adds one rear pair (the device's sides, else its backs); 7.1 adds both pairs. The low-frequency and
    height channels are silent in every layout (point 2). The game never plays more speakers than the device reports:
    a layout the device lacks speakers for is not offered, and a stored one it cannot play plays as Auto. A device
    change (a headset plugged in) resolves the choice again and the gains ramp as point 7 says. The game's output mode
    is 0 for Mono, 1 for Stereo, 2 for Encoded stereo, and for the rest 2 when the device has more than two channels,
    else 1. Test: each choice against each layout of point 9, the channels used and the silent ones, the power equal
    to two channels' (the two-channel choices bit for bit), and a device change in the middle of a render.
11. Fewer speakers than the rear needs (the fold that point 4 leaves open: a layout with no rear speaker, 3.0 and 3.1
    chosen on a larger device, and a rear pair of one kind, 5.1 chosen on 7.1): the rear part of a note goes to the
    speakers that play at its two-channel power: the rear amount is scaled by 0.92747 as in point 4 and, with no rear
    speaker, joins the front gain of its own side as a power sum, `sqrt(front gain^2 + (0.92747 * rear gain)^2)`; with
    one rear pair only that pair carries it at 0.92747 in place of the 1 / sqrt 2 split of point 4. Power equal to two
    channels', mirror and no jump hold (points 4, 6, 7); the 101 by 101 grid of point 9 asserts it for 3.0 and for the
    5.1 and 3.1 choices on a 7.1 device.

What P-202 and P-187 start from (read at 697535a86, 11-19; the form written before their code, file 08 7.4):

- The cooked message file (`NMSG`, `crates/nebula-import/src/cook/message.rs:1-15`, read by `MessageData`,
  `MessageHolder.hpp:8-62`) keeps the disc's text as it was: UTF-16 units with the message editor's tags inside the
  text (a 0x1A unit, then the tag's length in bytes and group, then its id and parameters in units).
- Every reader finds the tags by scanning for 0x1A: `git grep -n -E "0x1A|0x001A" 697535a86 -- native/petari/src`
  gives 26 hits, 17 of them message tags: `CustomTagProcessor.cpp:408`, `:429`; `MessageTagSkipTagProcessor.cpp:95`,
  `:103`; `ReplaceTagProcessor.cpp:53`, `:87`, `:117`, `:158`, `:308`; `NebulaUi.cpp:131-134`;
  `NebulaSettingsMenu.cpp:890`; `StaffRoll.cpp:647`; `TitleSequenceProduct.cpp:90`; `NebulaAchievementToast.cpp:54-64`
  (writes them). The other 9 are other things (a sound id, effect floors, a timer, a colour, filter tables). The mod
  surface writes the same records (`nebula_text_tags.h`, P-320), and the tag processors take one 16-bit code a
  character (`TagProcessorBase.hpp:37-38`), so a character past U+FFFF cannot pass (P-187).
- The form: the cook parses each message once into runs: text runs of code points (32-bit) and typed tag records
  (kind, parameters, and a string of its own where the tag names one), with the message's run list and its plain text
  apart. Readers walk runs; nothing scans for 0x1A or reads a length from inside the text (P-215 goes with it). The
  tag processors take a code point; the cooked font maps code points by range (`NFNT` version 2, P-187). Mods and
  Nebula's own text (`NebulaText.txt`) build runs through the same builder, decoded by the one UTF-8 decoder (P-209,
  P-320).
- Landings: (1) `NMSG` version 2 cooked beside version 1, the reader's run API, and a test that every message of
  the three discs (RMGE01, RMGP01, RMGJ01) in every language gives the same characters and tags from both versions; (2) the
  tag processors on runs; (3) `NebulaUi`, `NebulaSettingsMenu`, `NebulaAchievementToast`, `StaffRoll`,
  `TitleSequenceProduct` and the mod surface on runs; (4) code points through the font (P-187); (5) version 1 and the
  in-text format deleted, the search above then giving only the 9 other hits.
- Proof: the text-layout test (every message's glyph sequence and pen positions, old path against new, every
  language and region) identical through landings 1 to 4; the gate's pictures; a fuzz test of malformed tags against
  the cook's parser; the search's count after landing 5.
