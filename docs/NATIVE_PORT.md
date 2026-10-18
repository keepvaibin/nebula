# Native port

Super Mario Galaxy runs as native code compiled from the Petari decompilation. The user's disc supplies only the
game's files. This document records the audit and every architecture decision, with its evidence. **M**: measured
in this tree. **D**: read from the RMGE01 assembly or source. **H**: hypothesis, not yet verified.

## Game core

- Petari (SMGCommunity, CC0-1.0) is forked at `D:/NebulaWork/petari`, branch `nebula`, with remote `upstream`.
  - Every change is either behind `TARGET_PC` or reads identically to CodeWarrior, so upstream merges stay clean.
  - The fork is local only. Publishing it to GitHub is the owner's call.
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
| `-fno-strict-float-cast-overflow` | Float-to-int casts saturate (`llvm.fpto[su]i.sat`, M). Unsigned conversions equal the console's `__cvt_fp2unsigned` (D). Signed ones differ only for NaN (0 here, 0x80000000 there). 8- and 16-bit targets saturate where the console wrapped after an int32 conversion, which matters only for out-of-range values. |
| `-fno-rtti -fno-exceptions -fno-sized-deallocation -fno-aligned-allocation` | CodeWarrior's model. JKRHeap's `operator new`/`delete` take every allocation; no kept code uses `dynamic_cast` or `typeid` (M). |
| `-fno-ms-compatibility -fno-delayed-template-parsing`; `-Wno-everything` then `-Werror=return-type` and the pointer/int truncation casts as errors | Windows reports the errors every other host reports. Strict mode found 15 lookup/goto sites hidden by the MSVC-compatible defaults (M). `-w` would also silence the `-Werror=` checks (M); with them on, 129 lines cast pointers to or from 32-bit integers (M), see below. No function falls off its end (M). |
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
- Roots taken through `frsqrte` (one refinement step, or none) are now the root itself.
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
  - Registers that carry addresses use 64-bit extension commands: CP array bases, texture and TLUT images, display-list calls, copy destinations.
  - GD (display-list building) is Petari's C code, unchanged.
- **Audio**: JAudio2 drives the DSP directly, not through AX (D). The DSP task layer maps onto Nebula's host mixer (`runtime/src/jaudio_host_mixer.cpp`).
- **Threads**: the console runs one thread at a time, by priority, and the game relies on it (`OSDisableInterrupts` used as a lock). The first implementation is host threads with one runnable at a time; true parallelism comes only where shown safe.

## Open

| item | state |
|---|---|
| Disc data byte order and 64-bit pointers (archives, J3D, JPA, KCL, JMap, BMG, layouts, audio banks; `JKRMemArchive` writes pointers into 32-bit file fields, D) | audit in progress |
| 274 US data sections not yet compared byte for byte (`us_data_unverified.csv`) | H: symbol naming; verify against the matching build |
| Heap sizes: pointers double in size natively, so fixed JKRHeap sizes need a 64-bit allowance | open |
| `nw4r::ut::RomFont`: the console's system font comes from the IPL ROM, not the disc | open |
| THP movies (prologue, endings, final battle): Petari's decoder is paired-single assembly; Nebula has a native THP decoder | planned |
| Mii data (RFL) without a console NAND | open |
| aarch64 and LP64 sysroots for the compile census on Linux, Android and macOS | not on this machine |
