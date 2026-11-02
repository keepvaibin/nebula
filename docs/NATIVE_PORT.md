# Native port

Super Mario Galaxy runs as native code compiled from the Petari decompilation. The user's disc supplies only the
game's files. This document records the audit and every architecture decision, with its evidence. **M**: measured
in this tree. **D**: read from the RMGE01 assembly or source. **H**: hypothesis, not yet verified.

## Game core

- Petari (SMGCommunity, CC0-1.0) is forked into `native/petari`: the files the module compiles and includes, credited (its README, THIRD-PARTY-NOTICES.md). Its history up to 92263da is a separate fork's (branch `nebula`); changes continue in this tree.
  - The fork is Nebula's own code: it is optimised for native speed and need not match the console's object code.
    It will live in Nebula's repository, credited to SMGCommunity. Nothing goes back upstream: Petari does not accept
    AI-generated changes. With every function decompiled there is nothing to track.
  - It becomes public with Nebula's repository; pushing is the owner's call.
- Version: RMGE01. The test disc's `main.dol` SHA1 is 9A71008AE1EE9010E267FA67D1F0B0D4F0E895DD, which equals Petari's `config/RMGE01/build.sha1` (M).
- 99.90% of RMGE01 functions match the DOL byte for byte (decomp.dev). Of the 42 that don't (each audited against the assembly):
  - one has no source: `StarPointerFunction::forceOutsideScreenEdge`, now written from the assembly (D);
  - two differ only in instruction order (D);
  - 39 are SDK code, gone.
- Kept code: `src/Game` (1,609 units), `src/JSystem` (169), `src/nw4r` (29) and `src/RVLFaceLib` (14). The RVL SDK, MSL, Runtime and MetroTRK are gone.

## Build of the game code

clang 22 on every host, with the same flags everywhere:

| flag | why |
|---|---|
| `-std=gnu++17`, C `-std=gnu11` | The code is C++03 for CodeWarrior. C++20 adds rewritten comparison candidates that can change which `operator==` runs, and makes clang's builtin `stddef.h` re-enter under `__has_feature(modules)` (M). |
| The host's C and C++ libraries, force-included `nebula_prelude.h` | `MR::sort` (`SortUtil.hpp`) keeps the order of equal elements the same on every host; the collision binder depends on it (D). Text is UTF-16, `char16_t` (`TextString.hpp`). |
| `-fno-strict-aliasing` | The decompiled code type-puns through pointer casts throughout. |
| `-fno-math-errno` | No kept code reads `errno` (M). With it, `sqrtf` is one instruction instead of a compare and a library call on the negative path (M, assembly). |
| `-fno-stack-protector` | Pinned off for every toolchain (some distribution compilers enable it by default), so the module performs the same everywhere. |
| `-fno-strict-float-cast-overflow` | Float-to-int casts saturate (`llvm.fpto[su]i.sat`, M). Unsigned conversions equal the console's `__cvt_fp2unsigned` (D). Signed ones differ only for NaN (0 here, 0x80000000 there). 8- and 16-bit targets saturate where the console wrapped after an int32 conversion: every such conversion goes through `s32` in the source (`(s16)(s32)(x)`, 205 sites; `native/tools/narrow_float_audit.py`). Degrees to binary angles (x 182.04) leave the 16-bit range past 180 degrees: saturated, every object placed rotated past 180 degrees was turned to 180 (the Observatory's Terrace and Fountain domes, M). |
| `-fno-rtti -fno-exceptions -fno-sized-deallocation -fno-aligned-allocation` | CodeWarrior's model. JKRHeap's `operator new`/`delete` take every allocation; no kept code uses `dynamic_cast` or `typeid` (M). |
| `-fno-ms-compatibility -fno-delayed-template-parsing`; `-Wno-everything` then, as errors, `return-type`, the pointer/int truncation casts, calls without a prototype, int/pointer conversions, mismatched function pointer types, returned addresses of locals and constant out-of-bounds indexes | Windows reports the errors every other host reports. Strict mode found 15 lookup/goto sites hidden by the MSVC-compatible defaults (M). `-w` would also silence the `-Werror=` checks (M). Each error class is one the audits found silently miscompiled natively (see Audits). No function falls off its end (M). |
| `-ffp-contract=off` | The same results on every CPU, with or without FMA. |

Status:
- Without the cast checks, all 1,821 kept units compile at `-O2` for `x86_64-pc-windows-msvc` (M, `native/tools/census.py`).
- With them, 129 source lines cast pointers to or from 32-bit integers (M, `ptrcasts.txt`):
  - address arithmetic and alignment, mostly the JKR and MEM heaps: `uintptr_t`;
  - pointers kept in 32-bit IDs and fields (hash-table values, sound user data, DMA commands): the storage widens;
  - in-place relocation of loaded file data (AudChordInfo, SpkTable, J3DAnmLoader, ResFont, archives): part of the disc-data work;
  - console address-range tests (`>= 0x80000000` taken to mean a pointer in DrawSyncManager and lyt_texMap): native tests;
  - GX, DSP and ARAM addresses: the platform layer. No aarch64 or Linux/Android/macOS sysroot exists on this machine yet. Strict mode is what those builds would see; the units still have to be compiled there.

### Types

- The base types are the same fundamental types on every host (`types.h`): `s32` is `int`, `u32` is `unsigned int`, `s64` is `long long`. `int32_t` would be `long` on some hosts and change overload resolution.
- The console's `s32`/`u32` were `long`. The game passes `long` literals (`0L`, `1UL`) to s32/u32 overload sets. `NebulaLongCall` in `types.h` gives exactly those calls a forwarding template. Plain `long` overloads would have made `int` arguments ambiguous (M).
- Explicit `long` in kept code was respelled `s32`/`u32` (1,990 sites, `native/tools/long_sweep.py`). These are identical types on the console and 64-bit on LP64 hosts otherwise.

Math calls with float arguments take the host's float overloads (`<cmath>`).

## Console-only code

Every console-only construct was found by tool, not by reading:

- Paired-single assembly with no native alternative (`native/tools/find_console_only.py`): 31 functions became empty or returned garbage natively (M). Among them were `TVec3<f32>::operator=` (every vector assignment), `TVec3::dot`, `J3DCalcZValue`, `J3DTextureSRTInfo::operator=` and the Hermite interpolators.
- Each now has native code following the assembly's operations in their order (D). Quantized stores follow the Broadway rules: NaN to the maximum, saturate, truncate toward zero.
- Roots taken through `frsqrte` alone (`JMAFastSqrt`, `MR::fastSqrtf`, `JMASqrt`) are `sqrtss`. `__frsqrte` followed by the game's own Newton step is the host's estimate (`rsqrtss`; on AArch64 `frsqrte` plus one step). That has half the latency of `1/sqrtf` and the same accuracy after the step (`NATIVE_PERFORMANCE.md`).
- Every preprocessor branch on the compiler or target (180, `native/tools/compiler_branches.py`) was compared with the console's branch. Those whose native side did less (M):
  - `FAST_GPFLAGSET`/`FAST_FLAG_SET`: the field inserts behind all 756 `SC_*_SET_*` GX register macros were empty, so no GX register field was ever set. They are now `rlwimi` in C.
  - `ATTRIBUTE_ALIGN`/`ATTRIBUTE_PACKED` were empty, dropping the 32-byte alignment of 38 texture, display-list and DMA buffers. GX addresses those in 32-byte units.
  - Intrusive lists named their node's offset as 0 (nw4r layout panes, groups, animations, fonts; JUTConsole) or as the console's literal (`JASTrack` -0x248, `AudMeTrack` -384). They now use `offsetof`, through `T::nebulaListNodeOffset()` where the list is named inside an incomplete `T`.
  - Header-defined nerve singletons relied on weak symbols; they are inline variables (`ATTRIBUTE_WEAK_DEF`).
  - `NO_INLINE` forced 499 functions out of line for matching; it is empty natively.
- The GX SDK, compiled natively for the first time, read `GXColor`/`GXColorS10` as 32-bit words (TEV, channel, light and fog colours) and the TEV op tables' bitfields as words. On a little-endian host both reverse the register bytes. They are now built explicitly (`GX_COLOR_WORD`, shift macros). `GXInit` no longer points the register blocks at hardware addresses.
- Assembly-only `#else` declarations (link errors, not silent): the TVec3/JMathInlineVEC helpers are now native.
- Petari's audit flagged 23 of these independently; all are covered.
- GQR set-up (`OSInitFastCast`, `J3DSys`) is a no-op natively. Nothing executes quantized loads.
- Two shipped bugs are kept. `Sandstorm::exeStormEnd` and `MapPartsRailPosture::exeMove` test a member function's address (`__ptmf_test`), which is always true (D).
- Temporaries whose address the console returned or kept (`getClippingCenterOffset`, PlantGroup) now point at live objects.
- Game's `Overwrite.cpp`/`OverwriteJAudio.cpp` replace nine JSystem functions. Only the Game copies are in the DOL (D), so the JSystem copies are left out of native builds.

## Module layout

- **Game module** (shared library): Petari's kept code, built with the flags above against the host's C and C++ libraries.
- **Nebula host**: renderer, audio, input, files, settings and window. It uses the host C++ library.
- Between the two sits a C function table.
- They are separate modules so the game's `operator new` (JKRHeap's) stays out of the host.
- JKRHeap's global `operator new` stays module-local: a DLL's own definition on Windows, hidden visibility on ELF and Mach-O.

## Audits

Complete over every compiled unit (1,874), with the compiler as the checker. An audit build sets `NEBULA_AUDIT_FLAGS` in place of `-Wno-everything` (`port/tools/audit_build.ps1`: `-Weverything` minus style-only diagnostics).

- 64-to-32-bit narrowing (`-Wshorten-64-to-32`, `native/tools/narrowing.py`): 1,187 warnings at 186 sites (M). Truncated addresses, fixed:
  - the gravity requester (an actor's own gravity was never excluded);
  - a particle emitter's user work (EffectSystemUtil keeps a pointer there);
  - the DSP's ARAM-in-MEM2 base;
  - the Mii database's callback argument;
  - `ComparePtr`'s sign;
  - material IDs (`mDiffFlag`), now the console's address values.
  The rest are sizes and differences inside one allocation, hashes and report output.
- Pointer/32-bit casts the cast errors miss (`reinterpret_cast<T*>(u32)`, `(u32)(uintptr_t)p`; clang-query, `native/tools/query_sites.py`): 52 sites (M). Sentinels compared with themselves, small message values, ARAM offsets. JUTException's crash screen walks PowerPC frames and reads module tables at console addresses; natively a panic is `OSPanic`.
- Diagnostics `-Wno-everything` hid (`native/tools/diagnostics.py`, M):
  - 13 C calls without a prototype; `GXGetTexObjAll` received a pointer through an implicit `int`.
  - Undefined behaviour: 6 returns of a local's address, 8 matrix reads past a row's end, a `va_list` copied by dereference (a `char*` natively; the message tags' numbers and strings, ReplaceTagProcessor), an array deleted through an incomplete type (DrawBufferHolder).
  - DrawSyncManager's stop message (0x10000) passed the native pointer test.
  - JKRDvdRipper tested `isErrorRetry`'s address instead of calling it.
  - 24 include paths differ in case from the file (case-sensitive file systems).
  - printf-style functions now carry format attributes: 47 mismatches, all pointers printed with `%x` in diagnostics. No class object passes through varargs (clang-query), so no `BE<T>` value reaches a format.
  - Reviewed, nothing native-specific: values left uninitialised on paths the original also left undefined (52), deletes through `void*` (4), `offsetof` on non-standard layouts, shifts into the sign bit, enum range compares (clang does not assume enum ranges without `-fstrict-enums`).
- Every `TARGET_PC` block of the fork, both branches (324, `native/tools/target_blocks.py`), read against the console code, paired-single assembly included (D). Fixed:
  - KCL conversion left the last position's and the last normal's y and z big-endian; all 1,016 collision files on the disc use both (M, `port/tools/kcl_check.py`).
  - A message tag's string argument lived in a side table keyed by the slot's address, lost when the text was copied; it is now the string's console address in the slot, the console's bytes.
- First native runs (`native/host`, headless, the installed game.pak): the game boots to the Logo scene and runs its frame loop with audio pacing (M, 2026-10-18). What the runs found, all fixed:
  - Object sizes the game fixed for 32-bit pointers: the root heap's header (0x90), fixed heap sizes (JKRNativeHeapSize gives twice the room where the parent has it), FunctionAsyncExecutor's unit heap units, copies and messages sized by console struct sizes (wave archive and stream tasks, oscillator copies, movie audio buffers), GXTexObj too small for GX's record (now 40 bytes; every GX record asserted to fit).
  - operator new memory 16-byte aligned (the compiler zeroes new objects with aligned SSE stores).
  - The unit heap's allocation table read as little-endian words (two objects in one unit).
  - A null string in a format prints nothing (`nebula_snprintf`): the host's C library prints "(null)", which broke archive paths.
  - A layout name pointing into a buffer whose scope had ended (LayoutManager).
  - Interrupts that arrive while enabled are delivered before the next disable as well as at the next enable, as the console takes them when they arrive (M: 1.47-1.60 ns per disable/restore pair, unchanged).
- AddressSanitizer (`-DNEBULA_SANITIZE=address`, `port/tools/build_asan.ps1`): stack switches are fiber switches it is told of; the JKR heaps, built uninstrumented, keep their free space and block headers poisoned and unpoison each block they hand out, so overflows and uses after free inside the game's own heaps are reported too. Boot to the title screen and 60 s of its loop run clean (M, 2026-10-18). Found and fixed:
  - `OSClearStack` cleared below its own frame through a `memset` call whose frame was in the cleared range (volatile stores now).
  - `JKRThread`'s message buffers were `count * 4` bytes for 8-byte messages: every JKR thread's queue overran the next allocation.
  - The game's sort (`MR::sort`) placed its pivots at `len / sizeof(iterator)`: 8 natively put the second pivot past the end of short ranges, and its swaps overwrote the next heap header (the stationed heap lost its 10.7 MB tail). Now the console's 4.
  - A 4x4 projection constructed from a 3x4 identity read 16 bytes past it (StarPointerDirector).
  - Layout animations' file names: the string table's offsets read in host order (`GetStrTableStr`).
  - A buffer read past its block (FileRipper's header) and a single-character string read one past its end by the text writer's look-ahead (picture glyphs).
- Float-to-integer conversions into 8- and 16-bit types (`native/tools/narrow_float_audit.py`, clang-query over every unit; `-DNEBULA_AUDIT_FLOAT_CASTS=ON` runs it after each link): 205 sites in 69 files now convert through `s32` as the console's `fctiwz` and narrow store do, wrapping (M, 2026-10-19). Found by the Observatory's domes turned to 180 degrees: `MR::makeMtxRotate` took 296 and 330 degrees to the s16 limit. Also the JMath degree and radian sine tables' indexes, JPA emitter rotations, Ocean Ring and Water Road normals, snow and flag colours. Left saturating: the `OSf32to*` stand-ins for the quantized paired-single stores, which saturate on the console too.
- Whole-program audits with libclang (`native/tools/*_audit.py`, by symbol, every unit; each lists its sites for review; M, all reviewed):
  - Addresses of locals kept past their lifetime, through stores, returns and calls by parameter position, virtual overrides and `this` (`escape_audit.py`, 38,697 functions; `scope_escape.query` for block scopes): 74 sites; one real (Tamakoro's up vector, read past its block). The rest are the console's own patterns (GD display-list cursors, synchronous DVD/ARAM requests, interrupt contexts).
  - Sizes written as numbers for types larger natively (`size_audit.py`: allocations, copies, clears, byte-offset casts): JPA's seven function-pointer tables (`count * 4`), JASBasicBank's instrument table cleared by half, JASTaskThread's call-stack headers.
  - Raw memory viewed as host-order numbers (`endian_audit.py`, 191 casts) and objects reached through `void*` then read at console offsets (`void_flow_audit.py`, flow to a fixpoint over 2,318 slots): XanimeResource read a `J3DAnmBase`'s attribute and frame count at the console's byte offsets 4 and 6, inside the 8-byte vtable pointer.
  - 3x4 matrices given 4x4 parameters (`mtx_rows_audit.py`, by declared type): none left.
  - `sizeof` of a pointer used as a number, and `int` shifts widened to 64 bits (bit masks past bit 31).
- The Microsoft ABI sizes a member pointer by its class's inheritance where the class is complete: SystemUtil defined `callMethodAllSceneNameObj` with `NameObj` incomplete, a different size from its callers. `-fcomplete-member-pointers` now makes any such site an error (one existed).
- Game threads run on native stacks from a pool at the top of MEM2 (256 KB each, 1 MB under ASan; guarded): the buffers the game gives are sized for PowerPC frames. The deepest measured, boot to the title screen, is 13.5 KB (M).
- `nebula_game_debug_dump` reports every game thread with the module addresses on its stack and how deep the stack has been (`native/tools/symbolize_dump.py` names them). An out-of-memory panic lists the heap's largest blocks, children and uncovered spans.
- The arena is aligned to its size (512 MB): an arena pointer's low 29 bits are its physical address, so values the game derives from an address's low bits (J3DDrawBuffer's hash slots) are the console's (M, `kernel_tests`).
- Memory a device reads, by the type the CPU wrote it as (`gpu_data_audit.py`: every pointer handed to GX, GD, J3DGD, the DSP, the AI or a cache operation; 371 sites, M). Fixed: `clearZTexData`, the Z24X8 texture that clears depth each frame, was a `u32` array, so the GPU read far as near and all gameplay 3D failed the depth test; NormalMapBase's IA8 gradient texels; JUTDirectPrint's frame-buffer pixels.
- Bitfields whose words code also reads (`bitfield_audit.py`: casts, unions, byte copies; 457 sites, M). CodeWarrior allocates from the most significant bit, clang from the least. Fixed: J3DModelX's flags (directDraw picks display list *i* by bit *i*: Mario's shadow silhouette drew black into the frame), Mario's movement and draw states (word masks), RushEndInfo's damage type (shifts), JASChannel's mix config (`0x150` gave bus 0x50), Binder's collision flags (byte masks).
- Host pointers tested against the console's memory map (`console_address_audit.py`, every line built natively; it runs before each link of the module and fails it). Fixed: `J3DMaterial::getMaterialAnm` returned null for any pointer at or above 0xC0000000, which every host heap address is, so no material animation ran: texture patterns (BTP; Good Egg's Dino Piranha egg drew its dark default shell texture, not the white Vs1 one), TEV colours (BRK), texture scrolling (BTK: clouds, water) and material colours (BPK) (M). `OSIsMEM1Region` and `OSIsMEM2Region` are console-only; NW4R's debug pointer assertion checks for null. Truncating a pointer to 32 bits is a compile error (`-Werror=pointer-to-int-cast`, `-Werror=void-pointer-to-int-cast`; audit build with both and `-Wmicrosoft-cast`: 0 sites in 1,879 units, M).
- SDK answers the game turns into logic: `VIGetDimmingCount` returned 0, the console's "screen dimmed", so AudSystem lowered the music to a third from boot. The VI now counts idle fields as the console does (below).
- Galaxy tour (`GALAXY_TEST_GALAXY_TOUR`, `port/tools/tour_enterable.txt`, both hosts, ASan headless; M, 2026-10-18):
  - PowerPC's `divw`/`divwu` never trap; x86's do (CannonFleet 2's FlipPanel divided by a frame count of 0; FallOutFieldDraw in DarkRoomGalaxy passes 0 divisions). Every division is now safe in source (next bullet).
  - `DynamicCollisionObj` built its one-leaf octree as the console's two u16 halves; the native KCL code reads host-order entries (HeavenlyBeach 3 crashed). It is the only octree, or disc format, the game builds raw; the others go through big-endian field types.
  - `JASTrack::setIIR` copies 8 values from an `sIIRCutoff` row: row 127 reads the next 8 bytes of the console's `.rodata`, which the native table now carries.
  - Hidden scenarios (a hidden star's entry, `PlacedHiddenStarScenarioTable`) are not enterable by number: the console sizes per-scenario storage without them. Six stops removed from the list.
  - The headless host now connects an idle remote: without one the game pauses on "connect the Wii Remote".
  - A thread's stack slot was freed by OSExitThread while the exiting thread still ran the reschedule, the idle wait and the interrupts it delivered on it; a thread created meanwhile got the same stack. Freed now by the switch that leaves it. Two sanitizer tours had crashed in FlipPanelEx at stop 33; none has since.
  - Waits that spin on another thread's work: on the console the interrupt that completes the work, and the thread it wakes, preempt the spin; natively a loop that makes no OS call never takes it. GameScene's wait for the scenario's sound waves hung when the disc was slower than the scene's set-up (Linux under WSL; Windows with every read delayed 4 ms by `GALAXY_TEST_DISC_DELAY_US`: EggStar 1, the game thread at 540 ms/s and no frames; M). Every loop of the game, JSystem, nw4r, the SDK and RFL with an empty or OS-free body was reviewed (`native/tools/spin_audit.py`, 26 sites listed, plus loops on clock reads); the waits on other threads now call `__NebulaPoll` (deliver what is pending, else sleep until a host thread raises something): the wave wait, the player-archive swap, RFL's async wait, the triple-buffer swap, `JASDriver::waitSubFrame` (was a busy `OSYieldThread` loop) and the NAND error freeze. JUTException's and JUTConsole's retrace waits run only on its crash screen, which is `OSPanic` natively; the DSP loops wait on calls that finish synchronously natively. With the fix the 4 ms-per-read tour runs all stops at 60 retraces/s (M).
  - Results (M): ASan and ASan+UBSan headless, all 102 stops, 0 reports; NebulaNative at 21:9, EFB 2x, 81 stops (time limit), no crash; story stages at 16:10 (prologue garden, Observatory 1-5, domes 1-6, storybook); PrologueA through the game's movie sequence (video and sound); misses to game over. Linux headless (WSL, clang 18): all 102 stops. NebulaNative at 4:3 (1280x960) and 32:9 (1920x540): Battlerock and Gusty Garden, the picture fitted vertically and horizontally. The final Grand Star (`grandstar@`): the ending movies, the staff roll with its gameplay inserts, the storybook's new chapter and the save prompt. Luigi (`luigi@`, then a miss): the stage reloads with Luigi's archive, model and HUD.
  - Forcing a stage change while a story sequence waits for its demo to end reads the scene that is being replaced (null): a state the game never enters, as on the console. Story stops run from a fresh boot instead.
- Integer divisions whose divisor is not a nonzero constant (`native/tools/divide_audit.py`, clang-query over every
  unit of the module, headers once): 287 sites, 57 of them in Windows SDK headers the host-facing units include (M,
  2026-10-29). Each is shown unable to divide by zero (D: counts, constants, values checked before; 126 sites), or
  states the console's result in its own terms where its divisor comes from data (48 sites: placement arguments,
  animation frame counts, particle and sound data, interval steps; a zero divisor gives a quotient of 0, or -1 for a
  negative signed dividend, and the dividend as the remainder). Fonts with no sheet rows, lines or size are rejected
  when they load (ResFont::SetResource), so glyph lookups never divide by zero. The x86 trap handler that finished
  traps with PowerPC's result is gone; the galaxy tour runs all 102 stops (M, 2026-10-29). Verdicts per site:
  the audit's classification file in the program's tracker data.
- Block-scope lifetimes again, with a scanner that follows assignments (`native/tools/scope_escape_audit.py`, every unit): 28 sites, 2 real (LiveActorGroupArray's default group name, missed by `scope_escape.query`, and Tamakoro's). The other 26 point a parameter or global at a local used inside the same function, as the console does.
- Undefined-behaviour diagnostics (audit build with `-Wuninitialized`, `-Wsometimes-uninitialized`, `-Wconditional-uninitialized`, `-Wtautological-undefined-compare`, `-Wtautological-pointer-compare`, `-Wundefined-reinterpret-cast`, `-Warray-bounds-pointer-arithmetic` and the rest of `-Wall`): 53 uninitialised uses, all over closed value sets or never-called matching stubs; `this != nullptr` (SurfRay) and `&member != nullptr` (SoundUtil) are true on the console too.
- Parity with v1.0.1 on the same input route (`port/tools/route_star_festival.txt`, v1.0.1 = the Native47 package): Star Festival frames match after the fixes above; the title music, aligned by cross-correlation, matches in RMS (1895.6 against 1890.6) and per band within 3 % from 20 Hz to 16 kHz except 4-8 kHz (effects timing), correlation 0.93 (M, 2026-10-18).

## Byte order

- Everything is host order. The importer turns the game's files once (`crates/nebula-import/src/cook`): archives, J3D, JPA, KCL, JMap, BMG, layouts, audio banks, waves, ghosts. Nothing swaps at access; `nebula_be.hpp` is gone.
- Offsets the console overwrote with a pointer in place (RARC file entries, KCL, BRFNT, TPL, rhythm tables) hold the target's distance from the slot, which fits the slot's 4 bytes.
- Bitfields: where code also reads a bitfield's word, the native declaration lists the fields from the least significant value bit up, with explicit padding.
- Saves are written in host order at the size they always had; a console-order save (Wii) and a Switch save read through `SaveBinary` and are written back in host order. Their table keeps its system entry, empty, so older builds read a new save.

## Memory and the GPU's addresses

- The game's memory is one block of 385 MB (`NebulaMemory.c`), which the root heap divides: heaps sized for 64-bit data, no console arena, no MEM1 or MEM2. Untouched pages cost address space and commit, not memory: about 80 MB is in memory at a stop. docs/NATIVE_PERFORMANCE.md "Memory" has the table.
- The store is mapped read-only for the process's life (`AssetStore::map`, `asset_map`). Waves and streams play from the mapping: no copy. Other files are read into the game's heaps.
- The sound memory holds no sound: 1 MB, each wave archive's place in its section, so the archive goes with the section. A voice carries its source's start and end.
- Images and shapes reach the renderer as records: the game hands each over where it loads or rewrites it, and a heap's going tells the renderer to forget what lay in it (`shape_forget`).
- The game draws through typed records (`nebula_game_api.h`; docs/GRAPHICS_ARCHITECTURE.md). No game code calls GX.
- The lens flare's visibility test and the star pointer's and talk balloons' depth are screen probes: the game names its points, the renderer reads those pixels where the drawing has reached, the answers come back a frame or two later (`screen_probe`, `screen_probe_results`).
- Each game thread is a host thread with its own stack, at least 256 KB, guarded by the OS.

## Platform boundary

- The game's objects reference only the C and C++ runtimes and the host's services in `nebula_game_api.h` (`tools/progress/census.py`, `native/tools/boundary.py`).
- **GX**: gone. The game's native layer (`J3DNativeShape`, `J3DNativeMaterial`, `J3DNativeDraw`, `J3DNativeState`, `J3DNativeView`, `J3DNativeTexture`) makes the records.
- **Audio**: JAudio2's 64 voices and four effect lines are records the host's mixer reads (`voices_setup`, `voices_mix`, `voices_frame`): the game updates them each subframe and the mixer (`runtime/src/audio_mixer.cpp`) mixes them in float at the output device's rate (resampling, filters, sends, panner, effect delays, surround matrix, volume categories). All sources are PCM16 from the importer. The host's clock asks for a frame every 17.5 ms, paced by the device's queue; the frame, with the movie player's samples, goes to WASAPI on Windows (`runtime/src/audio_device_wasapi.cpp`) with the master volume. `runtime/src/native_audio.cpp` also writes the tests' WAV dump, 15 s unless `GALAXY_AUDIO_WAV_DUMP_SECONDS` says more.
- **Frame timing**: the game waits for its frame on the host's clock (`frame_wait`, `Game/System/NebulaFrameClock`). The remotes run on their own host timers. Screen dimming is counted as on the console (idle time since the last input, with the system's screen saver setting); the picture is not darkened, as a PC display dims itself.
- **Controls**: the game reads typed input samples (`input_read`, `NebulaInputSample`: buttons, the Nunchuk stick, the pointer with its roll and distance, gravity in g for the remote and the Nunchuk, shake), one every 10 ms and one when it reads, into its own WPad layer (`WPadHolder.cpp`: button edges and repeat, the acceleration filter the game sets, the pointer held through samples without one); no Wii Remote report is built or decoded and KPAD is gone. Nebula's live controls (`runtime/src/input/host_input.cpp`) make the samples; a real remote's driver reads its own reports into them (`runtime/src/input/wiimote_input.cpp`: calibration from its EEPROM, its camera's two lights as the pointer with the game's 0.03/0.5 smoothing). What v1.0.1 read from the console's memory for its control mapping the native game reports as it changes (`game_context`): StarPointerOnOffController's mode (menu or play mapping, rumble held in menus) and the ride Mario is bound to (Star Ball or Ray: tilt steering, and no pointer while the remote points up or down). The game's motor goes to the controller in use (`input_control`).
- **HOME Menu**: the HOME button library is a PowerPC module on the disc; natively the game's entry points open Nebula's panel over the frame (return to the game, Wii Menu, reset), driven by keyboard, controllers and mouse. Nintendo's own menu natively would need its library (wii-ipl's CC0 decompilation is a later version than SMG's) and a substitute for the IPL ROM font.
- **Power**: the game asks the host to close or start again (`app_exit`, `app_restart`). The game's own reset goes back to its boot sequence in place.
- **Movies**: the importer turns THP files into frames the host decodes (`movie_video_decode`) with PCM16 sound; the game feeds them to the player as it needs them.
- **Display shape**: as v1.0.1's dynamic aspect, the host latches the output's shape at each retrace once the frame is presented (`display_extent`), and the game draws for it (`MR::getDisplay`): 4:3 to 32:9; `MR::getScreenWidth` (832 at 16:9, even units), `CameraContext::getAspect`, the NW4R canvas (`setupDrawForNW4RLayout`, both screen/layout conversions; height fit under 16:9), the star-select sky and perspective. Layouts follow v1.0.1's repairs (`LayoutAspect`: full-screen backings, HUD panes on the canvas edges, cinema bars), plus the pause menu's counters (they moved in from the HUD's corners on pausing) and the spin, ball and tilt guidance under the names the game creates them by (v1.0.1's rules named their "Replace" archives) (`native/tools/layout_edge_audit.py` over `GALAXY_TEST_DUMP_LAYOUTS=1` logs: the 38 layouts the tour and menus reach; what it lists is window parts and anchors); UI keeps its 16:9 size against the height when wider and fits the width when narrower, CollisionShadow's capture keeps 832, and the strap screen's white reaches the borders (`scene_surround`). These were site hooks on recompiled code in v1.0.1; natively they are the game's source.
- **Options**: text speed (TalkBalloon, Rosalina's storybook) and the language setting, as v1.0.1.
- **Crash reports**: a fatal exception in the module or the host, or the game's fatal call, writes a minidump (stacks and what they point at, about 150 KB) from a thread kept for it, and logs the module offset and the game's threads (`GALAXY_CRASH_DUMP_DIR`; else `%LOCALAPPDATA%\Nebula\crashes` for NebulaNative and `crashes\` for the headless host; `GALAXY_CRASH_TEST=1` checks the path). Every exception in the process passes the exception handlers, so theirs are small, uninstrumented frames.
- **Tests**: `GALAXY_TEST_GALAXY_TOUR` and `GALAXY_TEST_TELEPORT` as in v1.0.1, and `GALAXY_TEST_EVENTS=kill@frame/...` (a miss through `MR::forceKillPlayerByAbyss`; five reach game over); the host decides, the game acts through `MR::requestChangeStageInGameMoving` and `MR::setPlayerPos` once per scene frame (`test_scene_frame`). `GALAXY_TEST_HOME_ROW` picks a HOME Menu row by its keys. Other events: `star@`, `grandstar@` (taken once the scene plays), `movieN@`, `luigi@`/`mario@` (the file's player from the next scene), `defeat:<model>@` (that model's live actors killed), `wait@`; frames count from the last tour stop. `GALAXY_TEST_DISC_DELAY_US` makes every disc read take that long, `GALAXY_TEST_NAND_DELAY_US` every NAND operation (saves); `GALAXY_TEST_CPU_AFFINITY` (hex mask) limits the processors; `GALAXY_TRACE_THREAD_CPU=1` logs each thread's CPU time per second by name. `GALAXY_TEST_SHADER_KEY_CAPTURE=<file>` appends every shader key the session resolves, for `galaxy_vk_shader_tests <file>`. `GALAXY_TEST_BLOOM_ROUTE=0|1|2` forces the game's bloom route (0 its own chains as the console's, 1 NATIVE, 2 ENHANCED); `GALAXY_TEST_RT_EFFECT_AB=reflections|water|sun_rays|lens_bloom` turns that ray-traced effect off in frames that end in an odd present (at 60 fps every other frame). `GALAXY_TEST_CURVED_AB=1` draws curved surfaces flat in the same frames.
- **Threads**: every game thread is a host thread (`Game/System/GameThreads`). One at a time changes the game world, under the world lock: the main thread first when it wants to, the scene and archive loaders otherwise, first come first served; a thread lets go of it when it blocks. File loaders and the audio task threads work beside it under their own mutexes. Sound runs on the host's audio clock thread: `excludeAudio` keeps it out of a section. A frame's work runs on a thread of its own, under the world lock. Test mode keeps every thread under the world lock and moves the clock only when no thread can run, so two runs take the same steps.

## Open

| item | state |
|---|---|
| Ghost recordings, the last format swapped at read | cooked to host order; the reader only reads |
| Mii data (RFL) without a console NAND | the Mii library is a native file, started once from a NAND's RFL_DB.dat when it has one; Mii import adds any console's Miis; the running game reads them as typed records |
| Volume categories | natively per voice (`voices_mix`, the game reports each channel's category) |
| The remote speaker's sound | through `speaker_samples` |
| The release's launcher and single-instance lease | P7 |
| Other platforms' toolchains and the compile census there | section S |
