# Native port

Super Mario Galaxy runs as native code compiled from the Petari decompilation. The user's disc supplies only the
game's files. This document records the audit and every architecture decision, with its evidence. **M**: measured
in this tree. **D**: read from the RMGE01 assembly or source. **H**: hypothesis, not yet verified.

## Game core

- Petari (SMGCommunity, CC0-1.0) is forked at `D:/NebulaWork/petari`, branch `nebula`.
  - The fork is Nebula's own code: it is optimised for native speed and need not match the console's object code.
    It will live in Nebula's repository, credited to SMGCommunity. Nothing goes back upstream: Petari does not accept
    AI-generated changes. With every function decompiled there is nothing to track.
  - Changes stay behind `TARGET_PC` where the console build should stay buildable.
  - Publishing it is the owner's call.
- Version: RMGE01. The test disc's `main.dol` SHA1 is 9A71008AE1EE9010E267FA67D1F0B0D4F0E895DD, which equals Petari's `config/RMGE01/build.sha1` (M).
- 99.90% of RMGE01 functions match the DOL byte for byte (decomp.dev). Of the 42 that don't (`D:/NebulaWork/agents/petari-audit/RMGE01_UNMATCHED.md`):
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
| `-fno-strict-float-cast-overflow` | Float-to-int casts saturate (`llvm.fpto[su]i.sat`, M). Unsigned conversions equal the console's `__cvt_fp2unsigned` (D). Signed ones differ only for NaN (0 here, 0x80000000 there). 8- and 16-bit targets saturate where the console wrapped after an int32 conversion, which matters only for out-of-range values. |
| `-fno-rtti -fno-exceptions -fno-sized-deallocation -fno-aligned-allocation` | CodeWarrior's model. JKRHeap's `operator new`/`delete` take every allocation; no kept code uses `dynamic_cast` or `typeid` (M). |
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
- Diagnostics `-Wno-everything` hid (`native/tools/warnings.py`, M):
  - 13 C calls without a prototype; `GXGetTexObjAll` received a pointer through an implicit `int`.
  - Undefined behaviour: 6 returns of a local's address, 8 matrix reads past a row's end, a `va_list` copied by dereference (a `char*` natively; the message tags' numbers and strings, ReplaceTagProcessor), an array deleted through an incomplete type (DrawBufferHolder).
  - DrawSyncManager's stop message (0x10000) passed the native pointer test.
  - JKRDvdRipper tested `isErrorRetry`'s address instead of calling it.
  - 24 include paths differ in case from the file (case-sensitive file systems).
  - printf-style functions now carry format attributes: 47 mismatches, all pointers printed with `%x` in diagnostics. No class object passes through varargs (clang-query), so no `BE<T>` value reaches a format.
  - Reviewed, nothing native-specific: values left uninitialised on paths the original also left undefined (52), deletes through `void*` (4), `offsetof` on non-standard layouts, shifts into the sign bit, enum range compares (clang does not assume enum ranges without `-fstrict-enums`).
- The arena is aligned to its size (512 MB): an arena pointer's low 29 bits are its physical address, so values the game derives from an address's low bits (J3DDrawBuffer's hash slots) are the console's (M, `os_scheduler_tests`).

## Byte order

- Data whose layout the console's files or hardware define keeps the console's byte order in memory; CPU-only state is native. That covers structs laid over loaded files, saves, display lists, vertex arrays and textures. The disc-data audit (`agents/petari-audit/DISC_DATA.md`) found 246 overlay structs with 1,557 fields in 31 formats (M).
- Such fields are `BE<T>`, a byte swap per access, and 32-bit offsets are `BEOffset<T>` (`native/msl/include/nebula_be.hpp`). Dusklight ships this approach.
  - Slots the console overwrote with a pointer in place (RARC file entries, KCL, BRFNT, TPL, rhythm tables) hold `SelfRel<T>`: the target's distance from the slot, which fits in the slot's 4 bytes.
  - `NEBULA_BE(T)`/`NEBULA_OFFSET(T)` read as the plain types for CodeWarrior.
- Everything the GPU reads stays big-endian, as on the console. That includes the vertex arrays and textures the game builds at run time (ocean, flags, skinning buffers, the fur map), which are written big-endian. Nebula's FIFO parser and renderer then read every source alike.
- Saves keep the Wii format (big-endian, chunk payload at offset 12), so existing saves load.
- CPU-only file data may be converted once at load where every consumer is CPU code, such as BMG message text.

## Memory and the GPU's addresses

- The game's memory is one host arena in the console's physical layout: MEM1 at physical 0, MEM2 at 0x10000000, enlarged for 8-byte pointers.
- `OSCachedToPhysical` and `OSPhysicalToCached` map arena pointers to the same 32-bit physical addresses the console used. The game module's image gets a second mapped window, for the static display lists and arrays the game hands to GX.
- Game threads run on stacks allocated in the arena.
- Nebula's FIFO parser already resolves physical addresses through a region table (`GuestMemoryV1`). The native build gives it the arena and module regions, so the v1.0.1 renderer is reused unchanged.
- Petari's GX SDK writes the console's command stream as big-endian stores into the command ring (`native/sdk/gx/gx_fifo.c`). The ring is host memory mapped twice in a row, so every span is contiguous. At each flush the host gets a span and parses it in place, then releases it; no copy is made. Draw-sync tokens and draw done reach the host as markers at their stream position, with no pre-parse. Display lists are recorded into game memory as on the console.
- ARAM ("alternate ARAM" in MEM2 on the Wii): ARAM addresses stay 32-bit offsets from its base. The main-memory side of a transfer is a pointer.

## Platform boundary

- The native objects reference **488 symbols** they don't define (M, `native/tools/boundary.py`), and this is the whole surface:

  | area | symbols |
  |---|---|
  | GX | 126 |
  | OS | 64 |
  | C library | 38 |
  | GD | 29 |
  | MTX/VEC | 35 |
  | NAND | 24 |
  | VI | 16 |
  | WPAD | 16 |
  | DVD | 14 |
  | NWC24 | 13 |
  | ARC | 10 |
  | KPAD | 9 |
  | MEM | 7 |
  | RSO | 7 |
  | AI, AR, DSP | 6 each |
  | SC | 5 |
  | PPC | 4 |
  | LC, TPL | 2 each |
  | VF, WENC | 1 each |

- 2,480 of the DOL's 2,911 SDK functions are never reached and are not implemented (`droppable_functions.csv`). That includes all of AX, AXFX, MIX, SYN, SEQ, PAD, SI, EXI, BTE, WUD, IPC, ES, USB, NET and EUART.
- 19 externals are used only by code absent from the DOL (dead-only). They get stubs that stop with a message if ever reached.
- **GX**: the game's GX calls write the hardware's FIFO format, which J3D's display lists already use. Nebula's D3D12 backend (`runtime/src/gx`) parses it.
  - Addresses in commands (CP array bases, texture and TLUT images, display-list calls, copy destinations) are the arena's 32-bit physical addresses, as on the console.
  - GD (display-list building) is Petari's C code with one reservation per command.
- **Audio**: JAudio2 drives the DSP directly, not through AX (D). The DSP task layer maps onto Nebula's host mixer (`runtime/src/jaudio_host_mixer.cpp`).
- **Threads**: the console runs one thread at a time, by priority, and the game relies on it (`OSDisableInterrupts` used as a lock). All game threads run on one host thread with the SDK's own scheduler, and a thread switch is a stack switch (`native/sdk/os`). Parallelism lives in the host: renderer, audio mixer, input, disc.

## Open

| item | state |
|---|---|
| Disc data byte order and 64-bit pointers (archives, J3D, JPA, KCL, JMap, BMG, layouts, audio banks; `JKRMemArchive` writes pointers into 32-bit file fields, D) | done: every kept unit compiles and links (M) |
| 274 US data sections not yet compared byte for byte (`us_data_unverified.csv`) | H: symbol naming; verify against the matching build |
| Heap sizes: pointers double in size natively, so fixed JKRHeap sizes need a 64-bit allowance | open |
| `nw4r::ut::RomFont`: the console's system font comes from the IPL ROM, not the disc | open |
| THP movies (prologue, endings, final battle): Petari's decoder is paired-single assembly | video through the host's native decoder (`thp_video_decode`), audio in `native/sdk/thp` |
| Mii data (RFL) without a console NAND | open |
| aarch64 and LP64 sysroots for the compile census on Linux, Android and macOS | not on this machine |
