# Native port

Super Mario Galaxy runs as native code compiled from the Petari decompilation. The user's disc supplies only the
game's files. This document records the audit and every architecture decision, with its evidence. **M**: measured
in this tree. **D**: read from the RMGE01 assembly or source. **H**: hypothesis, not yet verified.

## Game core

- Petari (SMGCommunity, CC0-1.0) is forked into `native/petari`: the files the module compiles and includes, credited (its README, THIRD-PARTY-NOTICES.md). Its history up to 92263da is a separate fork's (branch `nebula`); changes continue in this tree.
  - The fork is Nebula's own code: it is optimised for native speed and need not match the console's object code.
    It will live in Nebula's repository, credited to SMGCommunity. Nothing goes back upstream: Petari does not accept
    AI-generated changes. With every function decompiled there is nothing to track.
  - Changes stay behind `TARGET_PC` where the console build should stay buildable.
  - It becomes public with Nebula's repository; pushing is the owner's call.
- Version: RMGE01. The test disc's `main.dol` SHA1 is 9A71008AE1EE9010E267FA67D1F0B0D4F0E895DD, which equals Petari's `config/RMGE01/build.sha1` (M).
- 99.90% of RMGE01 functions match the DOL byte for byte (decomp.dev). Of the 42 that don't (each audited against the assembly):
  - one has no source: `StarPointerFunction::forceOutsideScreenEdge`, now written from the assembly (D);
  - two differ only in instruction order (D);
  - 39 are SDK code the platform layer replaces.
- Kept code: `src/Game` (1,609 units), `src/JSystem` (169), `src/nw4r` (29) and `src/RVLFaceLib` (14). The RVL SDK, MSL_C, Runtime and MetroTRK are replaced.

## Build of the game code

clang 22 on every host, with the same flags everywhere:

| flag | why |
|---|---|
| `-std=gnu++17`, C `-std=gnu11` | The code is C++03 for CodeWarrior. C++20 adds rewritten comparison candidates that can change which `operator==` runs, and makes clang's builtin `stddef.h` re-enter under `__has_feature(modules)` (M). |
| `-nostdinc++`, Petari's `MSL_C++` headers | The game's own C++ library, linked into the DOL. MSL's `std::sort` decides the order of equal elements; the collision binder sorts with it (D). Its `std::pair` has `a1`/`b1` members. |
| `native/msl/include`, force-included `nebula_prelude.h` | MSL's C interface: the host C library where the two behave the same, renames where they don't (see below). |
| `-fsigned-char`, `-fshort-wchar` | Console `char`; 16-bit `wchar_t` for the UTF-16 disc text and the game's wide strings. |
| `-fno-strict-aliasing` | The decompiled code type-puns through pointer casts throughout. |
| `-fno-math-errno` | No kept code reads `errno` (M). With it, `sqrtf` is one instruction instead of a compare and a library call on the negative path (M, assembly). |
| `-fno-stack-protector` | Pinned off for every toolchain (some distribution compilers enable it by default), so the module performs the same everywhere. |
| `-fno-strict-float-cast-overflow` | Float-to-int casts saturate (`llvm.fpto[su]i.sat`, M). Unsigned conversions equal the console's `__cvt_fp2unsigned` (D). Signed ones differ only for NaN (0 here, 0x80000000 there). 8- and 16-bit targets saturate where the console wrapped after an int32 conversion: every such conversion goes through `s32` in the source (`(s16)(s32)(x)`, 205 sites; `native/tools/narrow_float_audit.py`). Degrees to binary angles (x 182.04) leave the 16-bit range past 180 degrees: saturated, every object placed rotated past 180 degrees was turned to 180 (the Observatory's Terrace and Fountain domes, M). |
| `-fno-rtti -fno-exceptions -fno-sized-deallocation -fno-aligned-allocation` | CodeWarrior's model. JKRHeap's `operator new`/`delete` take every allocation; no kept code uses `dynamic_cast` or `typeid` (M). |
| `-fno-threadsafe-statics` | All game C++ runs on the one simulation thread, so a function-local static needs no lock (docs/NATIVE_PERFORMANCE.md). |
| `-fno-ms-compatibility -fno-delayed-template-parsing`; `-Wno-everything` then, as errors, `return-type`, the pointer/int truncation casts, calls without a prototype, int/pointer conversions, mismatched function pointer types, returned addresses of locals and constant out-of-bounds indexes | Windows reports the errors every other host reports. Strict mode found 15 lookup/goto sites hidden by the MSVC-compatible defaults (M). `-w` would also silence the `-Werror=` checks (M). Each error class is one the audits found silently miscompiled natively (see Audits). No function falls off its end (M). |
| floating-point contraction on (default) | CodeWarrior contracts to `fmadds`. Results aren't bit-exact with the console in any case. |

Status:
- Without the cast checks, all 1,821 kept units compile at `-O2` for `x86_64-pc-windows-msvc` (M, `native/tools/census.py`).
- With them, 129 source lines cast pointers to or from 32-bit integers (M, `ptrcasts.txt`):
  - address arithmetic and alignment, mostly the JKR and MEM heaps: `uintptr_t`;
  - pointers kept in 32-bit IDs and fields (hash-table values, sound user data, DMA commands): the storage widens;
  - in-place relocation of loaded file data (AudChordInfo, SpkTable, J3DAnmLoader, ResFont, archives): part of the disc-data work;
  - console address-range tests (`>= 0x80000000` taken to mean a pointer in DrawSyncManager and lyt_texMap): native tests;
  - GX, DSP and ARAM addresses: the platform layer. No aarch64 or Linux/Android/macOS sysroot exists on this machine yet. Strict mode is what those builds would see; the units still have to be compiled there.

### Types

- Under `TARGET_PC` the base types are the same fundamental types on every host (`types.h`): `s32` is `int`, `u32` is `unsigned int`, `s64` is `long long`. `int32_t` would be `long` on some hosts and change overload resolution.
- The console's `s32`/`u32` were `long`. The game passes `long` literals (`0L`, `1UL`) to s32/u32 overload sets. `NebulaLongCall` in `types.h` gives exactly those calls a forwarding template. Plain `long` overloads would have made `int` arguments ambiguous (M).
- Explicit `long` in kept code was respelled `s32`/`u32` (1,990 sites, `native/tools/long_sweep.py`). These are identical types on the console and 64-bit on LP64 hosts otherwise.
- `nullptr` stays MSL's `0`: the code was written for it (`bool b = nullptr`, integer contexts).

### MSL interface (`native/msl`)

The host C library is used where it behaves like MSL. Where it doesn't, the prelude maps the name to MSL's behaviour:

| name | MSL behaviour kept |
|---|---|
| `abs` | `int abs(int)` only. UCRT adds `long`/`long long` overloads, so `abs(f32)` would no longer truncate as on the console (M). |
| `stricmp`, `strcasecmp` | "C"-locale lower case, signed char comparison, -1/0/1 (D). |
| `wcslen`, `wcscmp`, `wcsncpy`, `wcschr`, `swprintf`, `vswprintf` | 16-bit `wchar_t` and MSL's wide formatter: progress per call, -1 when the result doesn't fit, invalid conversions copied verbatim (D, `wprintf.c`). |
| `__fpclassifyf`/`d` | MSL's class codes: NaN is 1, as `MathUtil.cpp` tests (D). glibc's codes differ. |
| `cctype`, `locale.h` | "C" locale with MSL's results: the class bit, and 0 outside 0..0xFF instead of undefined behaviour (D). |
| `cmath` | MSL's float `M_PI` and degree macros (`DEG_TO_RAD` evaluates in float). |

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

- **Game module** (shared library): Petari's kept code, the MSL interface, and the SDK layer. It is built with the flags above and no host C++ library.
- **Nebula host**: renderer, audio, input, files, settings and window. It uses the host C++ library.
- Between the two sits a C function table.
- They are separate modules because MSL's `std::` templates and the host's mangle identically: one `std::sort` or `std::pair<int,int>` would silently replace the other in a shared image.
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
  - MSL's narrow formatter natively too: the host's printed "(null)" for a null string, which broke archive paths.
  - A layout name pointing into a buffer whose scope had ended (LayoutManager).
  - Interrupts that arrive while enabled are delivered before the next disable as well as at the next enable, as the console takes them when they arrive (M: 1.47-1.60 ns per disable/restore pair, unchanged).
- AddressSanitizer (`-DNEBULA_SANITIZE=address`, `port/tools/build_asan.ps1`): stack switches are fiber switches it is told of; the JKR heaps, built uninstrumented, keep their free space and block headers poisoned and unpoison each block they hand out, so overflows and uses after free inside the game's own heaps are reported too. Boot to the title screen and 60 s of its loop run clean (M, 2026-10-18). Found and fixed:
  - `OSClearStack` cleared below its own frame through a `memset` call whose frame was in the cleared range (volatile stores now).
  - `JKRThread`'s message buffers were `count * 4` bytes for 8-byte messages: every JKR thread's queue overran the next allocation.
  - MSL's `std::sort` placed its pivots at `len / sizeof(iterator)`: 8 natively put the second pivot past the end of short ranges, and its swaps overwrote the next heap header (the stationed heap lost its 10.7 MB tail). Now the console's 4.
  - A 4x4 projection constructed from a 3x4 identity read 16 bytes past it (StarPointerDirector).
  - Layout animations' file names: the string table's offsets read in host order (`GetStrTableStr`).
  - A buffer read past its block (FileRipper's header) and a single-character string read one past its end by the text writer's look-ahead (picture glyphs).
- Float-to-integer conversions into 8- and 16-bit types (`native/tools/narrow_float_audit.py`, clang-query over every unit; `-DNEBULA_AUDIT_FLOAT_CASTS=ON` runs it after each link): 205 sites in 69 files now convert through `s32` as the console's `fctiwz` and narrow store do, wrapping (M, 2026-10-19). Found by the Observatory's domes turned to 180 degrees: `MR::makeMtxRotate` took 296 and 330 degrees to the s16 limit. Also the JMath degree and radian sine tables' indexes, JPA emitter rotations, Ocean Ring and Water Road normals, snow and flag colours. Left saturating: the `OSf32to*` stand-ins for the quantized paired-single stores, which saturate on the console too.
- Whole-program audits with libclang (`native/tools/*_audit.py`, by symbol, every unit; each lists its sites for review; M, all reviewed):
  - Addresses of locals kept past their lifetime, through stores, returns and calls by parameter position, virtual overrides and `this` (`escape_audit.py`, 38,697 functions; `scope_escape.query` for block scopes): 74 sites; one real (Tamakoro's up vector, read past its block). The rest are the console's own patterns (GD display-list cursors, synchronous DVD/ARAM requests, interrupt contexts).
  - Sizes written as numbers for types larger natively (`size_audit.py`: allocations, copies, clears, byte-offset casts): JPA's seven function-pointer tables (`count * 4`), JASBasicBank's instrument table cleared by half, JASTaskThread's call-stack headers.
  - Raw memory viewed as host-order numbers (`endian_audit.py`, 191 casts) and objects reached through `void*` then read at console offsets (`void_flow_audit.py`, flow to a fixpoint over 2,318 slots): XanimeResource read a `J3DAnmBase`'s attribute and frame count at the console's byte offsets 4 and 6, inside the 8-byte vtable pointer.
  - 3x4 matrices given 4x4 parameters (`mtx_rows_audit.py`, by declared type): none left.
  - `sizeof` of a pointer used as a number, and `int` shifts widened to 64 bits: MSL's `bitset` masks (`1 << n` for bits 32-63).
- The Microsoft ABI sizes a member pointer by its class's inheritance where the class is complete: SystemUtil defined `callMethodAllSceneNameObj` with `NameObj` incomplete, a different size from its callers. `-fcomplete-member-pointers` now makes any such site an error (one existed).
- Game threads run on native stacks from a pool at the top of MEM2 (256 KB each, 1 MB under ASan; guarded): the buffers the game gives are sized for PowerPC frames. The deepest measured, boot to the title screen, is 13.5 KB (M).
- `nebula_game_debug_dump` reports every game thread with the module addresses on its stack and how deep the stack has been (`native/tools/symbolize_dump.py` names them). An out-of-memory panic lists the heap's largest blocks, children and uncovered spans.
- The arena is aligned to its size (512 MB): an arena pointer's low 29 bits are its physical address, so values the game derives from an address's low bits (J3DDrawBuffer's hash slots) are the console's (M, `os_scheduler_tests`).
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

- Data whose layout the console's files or hardware define keeps the console's byte order in memory; CPU-only state is native. That covers structs laid over loaded files, saves, display lists, vertex arrays and textures. The disc-data audit (`agents/petari-audit/DISC_DATA.md`) found 246 overlay structs with 1,557 fields in 31 formats (M).
- Such fields are `BE<T>`, a byte swap per access, and 32-bit offsets are `BEOffset<T>` (`native/msl/include/nebula_be.hpp`). Dusklight ships this approach.
  - Slots the console overwrote with a pointer in place (RARC file entries, KCL, BRFNT, TPL, rhythm tables) hold `SelfRel<T>`: the target's distance from the slot, which fits in the slot's 4 bytes.
  - `NEBULA_BE(T)`/`NEBULA_OFFSET(T)` read as the plain types for CodeWarrior.
- Everything the GPU reads stays big-endian, as on the console. That includes the vertex arrays and textures the game builds at run time (ocean, flags, skinning buffers, the fur map, the depth-clear texture), which are written big-endian. Nebula's FIFO parser and renderer then read every source alike. One exception: the arrays indexed loads read (position, normal and texture matrices) are CPU state the game also reads, so they stay in host order and NebulaNative tells the parser (`set_indexed_load_host_order`).
- The JAudio DSP program's memory is device memory too: CH_BUF's channel records, FX_BUF, DSPRES_FILTER, the output planes and the AI's DAC buffers are in the console's byte order and layout (FX_BUF's delay-line slot holds the 32-bit physical address), so v1.0.1's host DSP program reads them unchanged.
- Bitfields: where code also reads a bitfield's word, the native declaration lists the fields from the least significant value bit up, with explicit padding, so each field has the console's bit.
- Saves keep the Wii format (big-endian, chunk payload at offset 12), so existing saves load.
- CPU-only file data may be converted once at load where every consumer is CPU code, such as BMG message text.

## Memory and the GPU's addresses

- The game's memory is one host arena in the console's physical layout: MEM1 at physical 0, MEM2 at 0x10000000, enlarged for 8-byte pointers.
- `OSCachedToPhysical` and `OSPhysicalToCached` map arena pointers to the same 32-bit physical addresses the console used. The game module's image is a second window at physical 0x08000000 (between MEM1 and MEM2, up to 128 MB), for the static display lists, arrays and textures the game hands to GX: every GPU address stays below 512 MB, as GX's 29-bit array bases and 24-bit texture bases need.
- Images reach the renderer as records: the game hands each texture image (and palette) over where it loads or rewrites it (`texture_image`, API 42), and the renderer decodes from its copy. What remains of the console's coherency (file loads' and a few writers' flushes, DMA, disc reads through `gpu_coherent`) only covers images not handed over yet.
- Game threads run on stacks allocated in the arena.
- The game draws through typed records (`nebula_game_api.h`; docs/GRAPHICS_ARCHITECTURE.md "Native record stream"): J3D shapes, matrices and materials, the geometry it builds or sends as it draws, drawing state, lights, EFB copies and texture images. No game code calls GX's state, vertex or display-list functions any more; the GX command ring (`native/sdk/gx/gx_fifo.c`, host memory mapped twice in a row, parsed in place at each flush) carries only GXInit's defaults, Nebula's markers and the draw-sync tokens and draw done at their stream position.
- EFB peeks run in draw-sync token callbacks: the lens flare's visibility test (the sun sphere's alpha) and the star pointer's and talk balloons' depth. The console's GPU waited at a breakpoint after the token while the callback peeked. Natively each peek carries its token (API 7); the render thread records a 640x528 field into the frame's command list where it parses that token, on one of four channels bound to a kind and token, and the peek reads that token's previous field (one frame old; the lens flare alternates two tokens, two frames). Before (M): fields were taken at whichever chunk end followed a peek, and a colour field held texel (0,0) in every pixel (conversion shader; `galaxy_efb_conversion_tests` covers it now), so the lens flare never saw the sun. Now the Good Egg intro's flare matches v1.0.1; D3D12 and Vulkan identical (M).
- ARAM ("alternate ARAM" in MEM2 on the Wii): ARAM addresses stay 32-bit offsets from its base. The main-memory side of a transfer is a pointer.

## Platform boundary

- The native objects reference **407 symbols** they don't define (M, `native/tools/boundary.py`, 2026-10-29), and this is the whole surface:

  | area | symbols |
  |---|---|
  | GX | 71 |
  | OS | 55 |
  | C library | 76 |
  | C++ runtime | 18 |
  | other | 38 |
  | NAND | 24 |
  | VI, WPAD, DVD | 14 each |
  | NWC24 | 13 |
  | ARC | 10 |
  | KPAD | 9 |
  | MSL (Nebula) | 9 |
  | MEM | 7 |
  | MTX/VEC/QUAT | 13 |
  | AI | 6 |
  | SC, AR | 5 each |
  | PPC | 3 |
  | TPL | 2 |
  | WENC | 1 |

- 2,480 of the DOL's 2,911 SDK functions are never reached and are not implemented (`droppable_functions.csv`). That includes all of AX, AXFX, MIX, SYN, SEQ, PAD, SI, EXI, BTE, WUD, IPC, ES, USB, NET and EUART.
- 19 externals are used only by code absent from the DOL (dead-only). They get stubs that stop with a message if ever reached.
- **GX**: the game's drawing is typed records (above); its native layer (`J3DNativeShape`, `J3DNativeMaterial`, `J3DNativeDraw`, `J3DNativeState`, `J3DNativeView`, `J3DNativeTexture`) replaced the GX and GD calls. Image identities and copy destinations are still the arena's 32-bit physical addresses.
- **Audio**: JAudio2 drives the DSP directly, not through AX (D). NebulaNative runs v1.0.1's host version of the JAudio DSP program (`runtime/src/jaudio_host_mixer.cpp`) on the command words (`galaxy::host::NativeGameAudio`) and plays each AI block through v1.0.1's sink (XAudio2, master volume, WAV dump). The output device's clock paces the AI: the next block is taken once fewer than two wait there.
- **Video timing**: the retrace (60 Hz, as v1.0.1 paces the game; the console's 59.94 would repeat a frame every 17 s on a 60 Hz display), the AI and the remotes run on high-resolution waitable timers. Screen dimming is counted as on the console (idle fields since WPAD's last input or connection, 18000 with SYSCONF's screen saver on); the picture is not darkened, as a PC display dims itself.
- **Controls**: Nebula's live controls build the remote's reports (v1.0.1's snapshot builder); the mouse is the pointer through v1.0.1's input providers. What v1.0.1 read from the console's memory for its control mapping the native game reports as it changes (`game_context`): StarPointerOnOffController's mode (menu or play mapping, rumble held in menus) and the ride Mario is bound to (Star Ball or Ray: tilt steering). The game's motor bit drives the controller in use. The pointer is exact: each report carries the position its IR dots stand for, and KPAD takes it as the position (distance and roll still from the dots). A real remote's camera needs KPAD's 0.03 play radius and halved steps (`KPADSetPosParam`, WPadPointer); a mouse does not: they left the pointer 0.03 + 0.02*v units behind a moving cursor (v in screen units per second; about 35 ms at one screen width a second) and quantized it to 1/226 unit.
- **HOME Menu**: the HOME button library is a PowerPC module on the disc; natively the game's entry points open Nebula's panel over the frame (return to the game, Wii Menu, reset), driven by keyboard, controllers and mouse. Nintendo's own menu natively would need its library (wii-ipl's CC0 decompilation is a later version than SMG's) and a substitute for the IPL ROM font.
- **Power**: OSShutdownSystem, OSRestart, OSReturnToMenu and OSRebootSystem reach the host (`power`), which closes the game or starts it again. The game's own reset (HOME Menu, reset button) goes back to its boot sequence in place.
- **Movies**: each THP frame's video component goes to v1.0.1's decoder with its size from the frame header (THP's entropy data is not byte-stuffed, so nothing else bounds a frame).
- **Display shape**: as v1.0.1's dynamic aspect, the host latches the output's shape at each retrace once the frame is presented (`display_extent`), and the game draws for it (`native/sdk/vi/display_native.c`, `MR::getDisplay`): 4:3 to 32:9; `MR::getScreenWidth` (832 at 16:9, even units), `CameraContext::getAspect`, the NW4R canvas (`setupDrawForNW4RLayout`, both screen/layout conversions; height fit under 16:9), the star-select sky and perspective. Layouts follow v1.0.1's repairs (`LayoutAspect`: full-screen backings, HUD panes on the canvas edges, cinema bars), plus the pause menu's counters (they moved in from the HUD's corners on pausing) and the spin, ball and tilt guidance under the names the game creates them by (v1.0.1's rules named their "Replace" archives) (`native/tools/layout_edge_audit.py` over `GALAXY_TEST_DUMP_LAYOUTS=1` logs: the 38 layouts the tour and menus reach; what it lists is window parts and anchors); UI keeps its 16:9 size against the height when wider and fits the width when narrower, CollisionShadow's capture keeps 832, and the strap screen's white reaches the borders (`scene_surround`). These were site hooks on recompiled code in v1.0.1; natively they are the game's source.
- **Options**: text speed (TalkBalloon, Rosalina's storybook) and the language setting, as v1.0.1.
- **Crash reports**: a fatal exception in the module or the host, or the game's fatal call, writes a minidump (stacks and what they point at, about 150 KB) from a thread kept for it, and logs the module offset and the game's threads (`GALAXY_CRASH_DUMP_DIR`; else `%LOCALAPPDATA%\Nebula\crashes` for NebulaNative and `crashes\` for the headless host; `GALAXY_CRASH_TEST=1` checks the path). Every exception in the process passes the exception handlers, so theirs are small, uninstrumented frames.
- **Thread stacks**: an exiting thread's stack slot is freed by the switch that leaves it (it still runs the reschedule and idle interrupts on it); a thread within 8 KB of its stack's end is logged at a switch. The switch keeps the TIB's stack fields with each stack (`context_switch.c`).
- **Tests**: `GALAXY_TEST_GALAXY_TOUR` and `GALAXY_TEST_TELEPORT` as in v1.0.1, and `GALAXY_TEST_EVENTS=kill@frame/...` (a miss through `MR::forceKillPlayerByAbyss`; five reach game over); the host decides, the game acts through `MR::requestChangeStageInGameMoving` and `MR::setPlayerPos` once per scene frame (`test_scene_frame`). `GALAXY_TEST_HOME_ROW` picks a HOME Menu row by its keys. Other events: `star@`, `grandstar@` (taken once the scene plays), `movieN@`, `luigi@`/`mario@` (the file's player from the next scene), `defeat:<model>@` (that model's live actors killed), `wait@`; frames count from the last tour stop. `GALAXY_TEST_DISC_DELAY_US` makes every disc read take that long, `GALAXY_TEST_NAND_DELAY_US` every NAND operation (saves); `GALAXY_TEST_CPU_AFFINITY` (hex mask) limits the processors; `GALAXY_TRACE_THREAD_CPU=1` logs each thread's CPU time per second by name. `GALAXY_TEST_SHADER_KEY_CAPTURE=<file>` appends every shader key the session resolves, for `galaxy_vk_shader_tests <file>`. `GALAXY_TEST_BLOOM_ROUTE=0|1|2` forces the game's bloom route (0 its own chains as the console's, 1 NATIVE, 2 ENHANCED); `GALAXY_TEST_RT_EFFECT_AB=reflections|water|sun_rays|lens_bloom` turns that ray-traced effect off in frames that end in an odd present (at 60 fps every other frame). `GALAXY_TEST_CURVED_AB=1` draws curved surfaces flat in the same frames.
- **Threads**: the console runs one thread at a time, by priority, and the game relies on it (`OSDisableInterrupts` used as a lock). All game threads run on one host thread with the SDK's own scheduler, and a thread switch is a stack switch (`native/sdk/os`). Parallelism lives in the host: renderer, audio mixer, input, disc.

## Open

| item | state |
|---|---|
| Disc data byte order and 64-bit pointers (archives, J3D, JPA, KCL, JMap, BMG, layouts, audio banks; `JKRMemArchive` writes pointers into 32-bit file fields, D) | done: every kept unit compiles and links (M) |
| 274 US data sections not yet compared byte for byte (`us_data_unverified.csv`) | H: symbol naming; verify against the matching build |
| Heap sizes: pointers double in size natively, so fixed JKRHeap sizes need a 64-bit allowance | open |
| `nw4r::ut::RomFont`: the console's system font comes from the IPL ROM, not the disc | open |
| THP movies (prologue, endings, final battle): Petari's decoder is paired-single assembly | video through v1.0.1's decoder with each frame's size, audio in `native/sdk/thp`; a route that reaches a movie is still to run |
| Mii data (RFL) without a console NAND | an empty database (RFL_DB.dat) is created, as v1.0.1 does; Mii import (runtime/src/mii_import.cpp, mii_formats.cpp) adds any console's Miis to it and to the running game's (nebula_game_mii_prepare/install) |
| Volume categories (music, effects, voices) natively | open: v1.0.1 classifies channels from game memory; the native game will report each channel's category |
| The remote speaker's sound | not in v1.0.1 (a v2 feature on wt-motion); the game's speaker data reaches the host and is dropped |
| Crash dumps, the single-instance lease and the launcher for NebulaNative | open (release, P7) |
| aarch64 and LP64 sysroots for the compile census on Linux, Android and macOS | not on this machine |
