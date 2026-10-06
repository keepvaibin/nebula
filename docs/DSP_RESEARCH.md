# DSP Research Report

Status: Phase 0 research plus early decoder/lowerer/runtime scaffolding. This
is not a completion claim.

This document records the research baseline for replacing the current
DSP/AI/audio boundary with a native, install-time recompiled Wii DSP path for
RMGE01. It deliberately contains no extracted microcode bytes, no DSP ROM
bytes, and no copied opcode tables from GPL projects. Opcode and microcode
sources are cited as references; the implementation must be independently
authored and verified by tests.

## Non-Negotiable Boundary

Nebula is an install-time static recompiler. The DSP work must preserve
the same rule as the PowerPC path: unknown input is a fatal install/build error.
Runtime DSP instruction interpretation, a runtime DSP JIT, or handwritten AX /
JAudio host mixing is not acceptable.

Local project references read for this phase:

- `docs/ARCHITECTURE.md`
- `docs/CONTENT_POLICY.md`
- `docs/NO_HLE_MIGRATION.md`
- `tools/audit_no_hle.ps1`
- `crates/nebula-recomp-core/src/translate.rs`
- `runtime/include/galaxy/native_api.h`
- `runtime/src/ppc_float.cpp`
- `runtime/src/native_host.cpp`
- `runtime/include/galaxy/native_host.h`
- `runtime/src/native_runtime.cpp`
- `runtime/src/native_host_tests.cpp`

Current local inventory classifies DSP/AI/audio as an active native boundary
with DSP-shaped behavior. `native_host.cpp` currently models mailbox replies,
JAudio command groups, AI DMA timing, ADPCM/source handling, and sample mixing.
That is the target to replace with recompiled DSP microcode.

## Public Sources

- Duddie, "GameCube DSP (GCDSP) User's Manual":
  https://www.emulation64.com/files/getfile/483/
- Dolphin DSP opcode metadata:
  https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/Core/DSP/DSPTables.cpp
- Dolphin DSP register, status, and memory definitions:
  https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/Core/DSP/DSPCore.h
- Dolphin DSP memory map:
  https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/Core/DSP/DSPMemoryMap.cpp
- Dolphin DSP hardware interface:
  https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/Core/DSP/DSPHWInterface.cpp
- Dolphin DSP interpreter semantics:
  https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/Core/DSP/Interpreter/DSPIntArithmetic.cpp
  https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/Core/DSP/Interpreter/DSPIntMultiplier.cpp
  https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/Core/DSP/Interpreter/DSPIntExtOps.cpp
  https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/Core/DSP/Interpreter/DSPIntLoadStore.cpp
  https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/Core/DSP/Interpreter/DSPIntUtil.h
- Dolphin DSP LLE/HLE description:
  https://github.com/dolphin-emu/dolphin/blob/master/Readme.md
- Dolphin legal free DSP IROM/coef replacement notes:
  https://github.com/dolphin-emu/dolphin/blob/master/docs/DSP/free_dsp_rom/dsp_rom_readme.txt
- DSPSpy in Dolphin:
  https://github.com/dolphin-emu/dolphin/tree/master/Source/DSPSpy
- DSPTool in Dolphin:
  https://github.com/dolphin-emu/dolphin/blob/master/Source/DSPTool/DSPTool.cpp
- delroth, "Reverse Engineering DSP Code" slides:
  https://delroth.net/about/slides/re-emu-dsp.pdf
- delroth, "Emulating the Gamecube audio processing in Dolphin":
  https://delroth.net/posts/emulating-gamecube-audio-dolphin/
- WiiBrew DSP hardware register page:
  https://wiibrew.org/wiki/Hardware/DSP
- WiiBrew Audio Interface page:
  https://wiibrew.org/wiki/Hardware/Audio_Interface
- devkitPro libogc DSP control definitions:
  https://github.com/devkitPro/libogc/blob/master/libogc/system.c
- libogc DSP API reference:
  https://bot.libretro.com/doxygen/a06614.html
- Petari JAudio/RVL DSP references:
  https://github.com/SMGCommunity/Petari/blob/master/src/JSystem/JAudio2/dsptask.cpp
  https://github.com/SMGCommunity/Petari/blob/master/src/JSystem/JAudio2/dspproc.cpp
  https://github.com/SMGCommunity/Petari/blob/master/src/RVL_SDK/dsp/dsp.c
  https://github.com/SMGCommunity/Petari/blob/master/src/RVL_SDK/dsp/dsp_task.c
- Nintendo mixing patent cited by delroth:
  https://patents.google.com/patent/US7369665B1/en

## Hardware And ISA

The GC/Wii DSP is a custom Macronix 16-bit-word-addressed audio DSP. Public
reverse-engineering references describe it as an 81 MHz fixed-point processor
with 4K words IRAM, 4K words DRAM, 4K words IROM, 8K words DROM/coef storage,
DMA access to main RAM/ARAM, and hardware sample acceleration for PCM8, PCM16,
and ADPCM. See delroth's slides, "Specs", "Registers", "Subregisters", and
"More peculiarities", and Dolphin `DSPCore.h` / `DSPMemoryMap.cpp`.

### Memory Map

Instruction memory:

- `0x0xxx`: IRAM, 0x1000 16-bit words.
- `0x8xxx`: IROM, 0x1000 16-bit words.
- Other instruction pages are invalid and must hard-fail in this project.

Data memory:

- `0x0xxx`: DRAM, 0x1000 16-bit words.
- `0x1xxx`: coefficient ROM, 0x800 16-bit words mirrored through mask 0x7ff.
- `0xfxxx`: IFX hardware registers.
- Other data pages are invalid and must hard-fail in this project.

The native runtime must not commit official DSP IROM/DROM bytes. If RMGE01 uses
IROM or coefficient functions, the legal choices are either a clean-room
replacement with proven behavior or installer-supplied DSP ROM data kept local
and untracked. Dolphin's `docs/DSP/free_dsp_rom/dsp_rom_readme.txt` is a useful
precedent for clean-room DSP ROM replacement, but not a source to copy.

### Registers

Architectural registers:

- Address registers: `ar0`-`ar3`.
- Index/increment registers: `ix0`-`ix3`.
- Wrap/modulo registers: `wr0`-`wr3`; conventionally `0xffff` when unused.
- Stack registers: `st0`-`st3` for call, data, loop address, and loop counter.
- Config/status: `cr`, `sr`.
- Product: `prod.l`, `prod.m`, `prod.h`, `prod.m2`; modeled as a 40-bit
  product plus overflow/control bits.
- Operand registers: `ax0`, `ax1`, each with `.l` and `.h` 16-bit halves.
- Accumulators: `ac0`, `ac1`, each with `.l`, `.m`, and 8-bit `.h`; full
  accumulator reads sign-extend the 40-bit value to host width.

Status bits named by Dolphin:

- `0x0001`: carry.
- `0x0002`: overflow.
- `0x0004`: arithmetic zero.
- `0x0008`: sign.
- `0x0010`: over signed 32-bit range.
- `0x0020`: top two bits equal.
- `0x0040`: logic zero.
- `0x0080`: sticky overflow.
- `0x0200`: interrupt enable.
- `0x0800`: external interrupt enable.
- `0x2000`: multiply modifier, used by M0/M2 style multiply-by-two behavior.
- `0x4000`: 16/40 mode, the sign-extension/saturation mode relevant to SXM
  style accumulator views.
- `0x8000`: unsigned multiply mode for MULX-family low-half operands.

Duddie terminology and Dolphin terminology are not perfectly identical. The
decoder and ALU tests must therefore name the bit-level behavior, not only the
mnemonic: sign extension of accumulator high/mid views, unsigned low-half
multiply behavior, and multiply modification.

### Arithmetic Semantics

The bit-exact minefield:

- Full accumulator arithmetic is 40-bit signed. Host helpers must sign-extend
  a 40-bit value after every architectural write where the hardware truncates.
- Full accumulator rounding zeros the low 16 bits after adding a bias based on
  bit 16. Dolphin documents this in `DSPIntUtil.h`; the implementation here
  should encode the behavior independently and cover it with exhaustive edge
  tests around `0x7fff`, `0x8000`, `0xffff`, carry, and negative values.
- `acX.h` is an 8-bit accumulator extension. Reads may sign-extend or
  zero-extend depending on the status/mode path.
- The product register is not just a 32-bit multiply result. It has low/mid/high
  pieces and move/load instructions that either preserve, round, negate, add to
  an accumulator, or clear low accumulator bits.
- Multiply instructions depend on signed/unsigned mode and the multiply
  modifier bit. The MULX family is especially sensitive because low halves can
  become unsigned under `sr` bit 15.
- Store/load of accumulator mid and high pieces is mode-dependent. SET16/SET40
  and saturating stores must be tested against hardware traces or a reference
  core.
- Flags are instruction-specific. Some operations set arithmetic flags over a
  64-bit host expression of the 40-bit value; some set 16-bit flags; some update
  sticky overflow differently from overflow. Copying a single "update flags"
  helper across all opcodes will be wrong.

### Instruction Set Shape

The DSP has primary instructions and extended/parallel operations. Dolphin's
`DSPTables.cpp` currently lists 230 primary opcode templates plus a separate
extended-operation table; Duddie's manual documents the instruction encoding.
For legal and licensing reasons this repository should not commit a verbatim
copy of Dolphin's GPL opcode matrix.

The clean-room decoder must cover these categories:

- Address-register operations: decrement/increment address registers, add/sub
  index registers, modulo wrapping through `wr0`-`wr3`.
- Control flow: returns, returns from interrupt, conditional calls, conditional
  if/skip, jumps, loops, block loops, and stack operations. Conditional variants
  must use the DSP condition-code matrix, not PPC CR logic.
- Register moves and immediate loads: register-to-register, immediate-to-reg,
  high/low/subregister moves, stack register access, and status/config writes.
- Data memory loads/stores: direct, indirect, immediate, incrementing,
  indexing, modulo, accumulator stores, AX stores, product stores, and IFX
  access.
- ALU: add/sub families, compare/test families, accumulator clear/move/round,
  logic ops, shifts, saturation/modification, and status-bit set/clear.
- Multiplier/product: clear/test/move product, multiply, multiply-accumulate,
  multiply-move, multiply-round, negative product moves, and the MULX/MULC
  operand-selection families.
- Extended operations: parallel loads, stores, register moves, and address
  updates paired with arithmetic or multiplier operations. delroth's examples
  include `MULAX'LD` and `MADD'LDN`; these demonstrate that the decoder must
  emit one semantic bundle per 16-bit word, not two unrelated instructions.

Representative encodings that should become initial decoder tests:

- `0x0000` masked by `0xfffc`: `NOP`.
- `0x0004` masked by `0xfffc`: address-register decrement.
- `0x0008` masked by `0xfffc`: address-register increment.
- `0x000c` masked by `0xfffc`: subtract index from address register.
- `0x0010` masked by `0xfff0`: add index to address register.
- `0x0021`: `HALT`.
- `0xbcf0`: extended-operation example from delroth, multiply plus dual load.
- `0xf2e7`: extended-operation example from delroth, product add plus load and
  indexed address update.

Completion criterion for Phase 1: a Rust opcode table authored from a clean-room
spec, with a test proving that every primary and extended encoding either
decodes to a known semantic form or hard-fails. Unknown opcodes must never
lower to a placeholder.

## Microcode For RMGE01

This workspace does not contain a user dump or extracted DSP ucode, so the exact
RMGE01 DSP ucode hash cannot be computed in this phase. That is a hard blocker
for claiming the microcode is fully identified.

Local evidence points to the RMGE01 audio path being JAudio/JASDSP-driven:

- Runtime trace labels name `JASDSPChannel::start`, `alloc`, `allocForce`,
  `updateProc`, and `updateAll`.
- `native_host.cpp` currently handles JAudio-style command groups:
  `DsetupTable`, `DsetVARAM`, `DsyncFrame2ch/4ch`, and `DSPReleaseHalt2`.
- Petari's JAudio references define the same command shapes in
  `dsptask.cpp` and `dspproc.cpp`.

The brief uses "AX" as the mission label. Public Dolphin/delroth references
distinguish AX, AXWii, Zelda, and JAC/JAudio-style ucodes. Until extraction
proves the exact ucode, this project should call it "the RMGE01 DSP ucode" and
record the detected family as data:

- Ucode source address in guest RAM.
- IRAM destination and length.
- DRAM upload address/length, if any.
- SHA-1 and CRC32 of uploaded IRAM words.
- Entry vector and resume vector.
- Any later IRAM/DRAM overlays.

### Upload Path

Public libogc/RVL DSP task code and the current runtime agree on the boot shape:

- CPU writes to DSP mailbox in two 16-bit halves at `0xCC005000` and
  `0xCC005002`.
- The boot task sends command/value pairs with `0x80F3xxxx` commands.
- `0x80F3D001` with a start-vector value is the final boot/start command.
- The DSP replies with task-state mail such as `0xDCD10000` (`DSP_INIT`).
- RMGE01/JAudio then expects an additional ucode handshake mail observed locally
  as `0xF3551111`.

Install-time extraction should not pattern-match source files. It should run the
same DOL analysis already used for PPC translation and locate the ucode upload
from the game's own data and boot-task setup:

1. Find the translated DSP task initialization path and `DSPTaskInfo` fields.
2. Resolve `iram_mmem_addr`, `iram_length`, `iram_addr`, `dram_mem_addr`,
   `dram_len`, `dram_addr`, `dsp_vector`, and `dsp_res_vector`.
3. Extract only from the user's dump or generated local artifacts.
4. Hash the extracted ucode and compare against an allowlist created from
   locally proven RMGE01 data.
5. Hard-fail on unexpected length, entry vector, overlay write, or hash.

### Command Protocol

Observed JAudio command-group protocol:

- `DSPSendCommands2` sends a count mail first. A count of zero means one command
  word for fire-and-forget commands in the current runtime's model.
- The first command word's top byte selects the work class.
- `0x81`: setup table. Current runtime treats this as table setup and validates
  channel/resampler/ADPCM filter table addresses.
- `0x8e`: VARAM setup.
- `0x82`: sync frame. Current runtime interprets bits 16..23 as a subframe
  count, then consumes output buffer addresses for left, right, aux A, and aux B.
- Other `0x00xxxxxx` release mails correspond to `DSPReleaseHalt2`, used by
  `JASDSPChannel::updateAll` after CPU-side TChannel updates.
- DSP-to-CPU replies use two classes locally: `0xDCD1xxxx` task-state mails that
  raise a DSP interrupt, and `0xF355xxxx` payload mails that are polled by the
  callback path.

For comparison, public AX documentation from delroth describes AX command lists
as parameter-block driven with commands such as setup, PB address, process,
mix AUX A/B, upload L/R/S, output, more, and end. AX commonly runs a 5 ms frame
cadence. RMGE01 must be proven by hash and command traces rather than assumed
to be AX/AXWii solely from Wii platform expectations.

## CPU To DSP Boundary

Public WiiBrew/libogc register map:

- DSP mailbox in high/low: `0xCC005000`, `0xCC005002`.
- DSP mailbox out high/low: `0xCC005004`, `0xCC005006`.
- DSP control/status: `0xCC00500A`.
- DSP/ARAM DMA registers: `0xCC005020`-`0xCC00502A`.
- AI DMA registers in this block: `0xCC005030`, `0xCC005032`,
  `0xCC005036`, `0xCC00503A`.
- AI control/counter block: `0xCD006C00`-`0xCD006C0C`.

Current local runtime model:

- `GuestAddressSpace::write_device` intercepts DSP mailbox writes, clears the
  to-DSP ready bit immediately, and calls `on_dsp_mail`.
- `GuestAddressSpace::read_device` serves from-DSP mail reads and clears the
  ready bit when low half is read.
- `GuestAddressSpace::pump_dsp_from_mailbox` posts queued from-DSP mails and
  raises interrupt state for `0xDCD1xxxx`.
- `dispatch_dsp_interrupts` dispatches OS interrupt 7 through the translated
  interrupt table.
- `dispatch_ai_dma_interrupts` dispatches OS interrupt 5 for AI DMA completion.
- `render_dsp_sync_subframes` is a handwritten JAudio mixer and must be removed
  once the recompiled DSP produces the same output buffers.

RMGE01 audio-related addresses verified against the generated US DOL bodies:

- `0x804878BC`: audio interleave helper boundary.
- `0x804945CC`: audio ring output helper boundary.
- `0x80495C3C`: `JASDsp::TChannel::setBusConnect` helper boundary.
- `0x80496A60`: DSP running check helper boundary.
- `0x804965A0`: local comments identify this as `__DSPHandler`.
- `0x80492A08`, `0x80492A78`: `JASChannel::play` and `playForce`.
- `0x80495038`: `JASDSPChannel::start`.
- `0x80495150`, `0x804951BC`: `JASDSPChannel::alloc` and `allocForce`.
- `0x80495354`, `0x8049559C`: `JASDSPChannel::updateProc` and `updateAll`.
- `0x804958E0`, `0x80495900`: `JASDsp::TChannel::init` and `playStart`.
- `0x8049599C`: `JASDsp::TChannel::setWaveInfo`.

The `0x8049727C` through `0x804977E0` JAudio labels used by an earlier trace
belong to the RMGK01 regional layout (the corresponding cluster is `+0x2244`).
Those addresses name unrelated functions in RMGE01 and must never be used as
RMGE01 activation evidence.

These labels are local/trace names, not a substitute for symbol-proof. The
function map in `metadata/rmge01/functions.csv` only stores ranges, so any
future report should tie labels to disassembly evidence or Petari-equivalent
function matching.

## Recompilation Design

The viable strategy is ahead-of-time static lowering of the extracted DSP ucode
to native C++ compiled into the generated module or a sibling generated native
DSP module.

Install-time flow:

1. Verify RMGE01 game ID and `main.dol` SHA-1 as today.
2. Extract DSP ucode upload payload from the user's dump/DOL.
3. Hash and classify the ucode revision.
4. Decode every IRAM word into a DSP IR.
5. Build a control-flow graph from known entry/resume/interrupt vectors.
6. Reject self-modifying IRAM, unknown computed branch targets, unsupported
   IFX behavior, unknown overlays, or writes into executable IRAM after compile.
7. Emit C++20 for the DSP module and compile with the same MSVC warning policy.

Runtime flow:

- A `NativeDspServices`/coprocessor state owns DSP registers, IRAM/DRAM images,
  mailbox state, IFX registers, DMA callbacks, and cycle/timing counters.
- Runtime never decodes DSP instructions. The generated module exposes native
  entry functions for known DSP PCs and dispatches only by precompiled function
  pointers.
- A DSP thread waits on mailbox/DMA events and executes generated code until
  halt, mailbox wait, DMA wait, interrupt, or frame completion.
- DSP-generated writes to output buffers feed the existing AI DMA path. AI stays
  the final RAM-to-host-audio hardware boundary.
- DSP-to-CPU mail and DIRQ writes become the only source of DSP interrupt 7.
- Unknown IFX register use hard-fails with the DSP PC and register number.

ABI impact:

- If DSP services are exposed through `NativeServicesV1`, or if generated PPC
  code can call DSP helpers through `native_api.h`, bump `kNativeAbiVersion`.
- Prefer isolating the DSP ABI behind runtime-owned generated DSP module exports
  to avoid changing `PpcContext`.

## Implementation Progress As Of 2026-06-24

Current in-tree work has started the static DSP path without committing any
game-derived bytes:

- `crates/nebula-recomp-core/src/dsp.rs` contains a clean-room Rust decoder,
  audit path, and C++ lowering path for the currently modeled DSP instruction
  surface. Lowering hard-fails on unknown opcodes, missing static control-flow
  labels, a selected entry vector outside the decoded upload image, unsupported
  fallthrough, static coefficient writes, and unmapped DSP memory.
- The lowerer models static labels, compiled-label dispatch for register
  jumps/calls/returns, DSP call/loop stacks, hardware loop checkpoints,
  instruction-memory loads from installer-supplied IRAM, DRAM/coefficient/IFX
  data-memory routing, and parallel-extension load/store/move forms currently
  covered by tests.
- `runtime/include/galaxy/dsp_context.h` and `runtime/src/dsp_alu.cpp` contain
  the native register file, DRAM/IRAM/coefficient storage, IFX register storage,
  mailbox high/low busy-bit semantics, `DIRQ` interrupt callback plumbing, and
  `DSBL`-triggered byte DMA between external host memory and DSP IRAM/DRAM.
  The DSP accelerator now owns
  byte-backed PCM16, 4-bit ADPCM, raw read/write, current/start/end address
  masking, predictor/history registers, and accelerator exception signaling.
- `runtime/include/galaxy/dsp_guest_bridge.h` connects a `DspContext` to the
  runtime `GuestMemoryV1` API for external DSP byte reads/writes, DIRQ
  interrupts, and accelerator exceptions. It is runtime-owned scaffolding, not
  a game-module ABI change.
- `runtime/include/galaxy/native_dsp.h` and `runtime/src/native_dsp.cpp` add a
  runtime-owned `NativeDspCoprocessor` state wrapper. It owns `DspContext`,
  connects the guest-memory bridge, exposes CPU-visible mailbox operations, and
  loads installer-supplied IRAM/DRAM byte payloads plus coefficient word images
  with checked bounds. It also models the CPU-facing DSP MMIO halfword surface for
  CPU-to-DSP mailbox writes/readback, DSP-to-CPU mailbox reads/consume, and the
  halt/reset control bits. Reset rebuilds DSP state while preserving the
  completed control value with the hardware-owned reset bit cleared. The wrapper
  has a tested `run_native_entry` call point for already-compiled DSP functions;
  this is the runtime hook generated DSP C++ will use, not an interpreter.
  `NativeDspWorker` is an event-driven host thread scaffold that executes one
  compiled DSP entry per signal and records completions for synchronization
  tests; it is not yet the final audio-frame cadence model.
- `crates/nebula-recomp-core/src/dsp_extract.rs` contains a synthetic-tested
  scanner for initialized DOL data descriptors that look like DSP task upload
  metadata. It reports guest source addresses, DSP destinations, entry/resume
  vectors, descriptor format, and SHA-1 hashes without writing ucode bytes.
  It now recognizes both an expanded-vector descriptor form and the libogc
  `dsptask_t` upload field shape where `init_vec`/`resume_vec` are packed as
  two `u16` values after the six upload words. Candidates must use 32-byte
  aligned nonzero DMA extents to reduce sliding-window false positives. The
  scanner also runs the current clean-room DSP decoder audit over each IRAM
  candidate and records either an audit summary or the first decode error,
  without writing ucode bytes.
- `crates/nebula-recomp-cli/src/main.rs` exposes `dsp-audit`, `dsp-lower`, and
  `dsp-scan-ucode` developer subcommands. It also exposes `dsp-lower-ucode`,
  which selects a scanned upload candidate from a verified RMGE01 input and
  lowers the IRAM payload in memory using the descriptor base plus the selected
  init/resume/base entry. It writes generated C++ only, not extracted ucode
  bytes. These are tools for user-supplied or synthetic input, not checked-in
  game assets.

Proof currently available:

- `cargo test -p nebula-recomp-core dsp::` passes the decoder/lowering/audit
  tests for the modeled DSP surface, including explicit image-base versus
  entry-vector lowering and a hard-fail when the selected entry vector was not
  decoded.
- `galaxy_dsp_alu_tests` / CTest `dsp_alu_helpers` passes native ALU/context
  helper tests, including IFX mailbox, interrupt, host-to-DRAM DMA,
  DRAM-to-host DMA, host-to-IRAM DMA, coefficient memory, PCM16 accelerator
  reads, 4-bit ADPCM accelerator reads, pred-scale reloads, ADPCM clamp
  behavior, MMIO PCM no-increment reads, sample-end stopped-read behavior, raw
  accelerator reads/writes, accelerator exception signaling, deterministic
  40-bit accumulator add/sub and modulo-address self-consistency sweeps, and
  DSP DMA / accelerator byte traffic through `GuestMemoryV1`.
- `galaxy_native_dsp_tests` / CTest `native_dsp_coprocessor` passes runtime
  wrapper tests for IRAM/DRAM word and big-endian byte payload loading,
  coefficient loading, CPU-to-DSP and DSP-to-CPU mailbox semantics, DMA through
  `GuestMemoryV1`, DIRQ callback delivery, accelerator sample reads,
  accelerator exception delivery, and reset behavior.
  The same test now covers CPU-side DSP MMIO mailbox read/write behavior and
  halt/reset control state, plus executing a compiled DSP entry through the
  IFX/mailbox/interrupt surface. It also covers an event-driven worker thread
  executing signaled compiled-entry work without runtime instruction decoding.
- MSVC `/W4 /WX /permissive-` smoke compilation passes for generated DSP C++
  containing static IFX accesses.
- `cargo test -p nebula-recomp-core dsp_extract::` passes synthetic DOL scanner
  tests for expanded-vector and packed-vector upload descriptors,
  unmapped/odd-length rejection, 32-byte DMA-shape filtering, and zero-filled
  IRAM rejection. It also covers in-memory selected-candidate payload recovery
  and reporting a decoder audit error while keeping the hash-only candidate
  report.

What this does not prove yet:

- The exact RMGE01 DSP ucode hash and revision are still unknown without running
  `dsp-scan-ucode` against a user dump or extraction artifact and auditing the
  reported candidate.
- The DSP opcode/extended-operation matrix is not claimed complete until the
  audit proves full coverage over the extracted RMGE01 ucode and every unknown
  opcode hard-fails with a useful diagnostic.
- DMA and accelerator byte traffic can now use `GuestMemoryV1`, and the
  `NativeDspCoprocessor` wrapper is wired into `GuestAddressSpace` behind
  `GALAXY_DSP_NATIVE`. Release launch scripts now set that flag so a compiled
  generated RMGE01 DSP module owns the product DSP path.
- The accelerator still needs broader differential coverage, especially
  loop-edge cases, remaining gain-scale modes, MMIO PCM increment mode, and
  exact exception/cadence behavior under real RMGE01 ucode.
- The old handwritten audio/JAudio mixer path still exists for transitional
  runs without `GALAXY_DSP_NATIVE`, but requested native-DSP launches now
  hard-fail if the generated ucode is missing or cannot take over.
- No end-to-end audio correctness proof exists yet.

## Test Plan

Required test layers:

- Decoder golden tests for primary and extended opcode encodings.
- Decoder coverage test proving every known opcode template has a test case.
- ALU unit tests for accumulator sign extension, saturation, status flags,
  product moves, rounding, signed/unsigned multiply, M0/M2 behavior, and
  SET16/SET40 behavior.
- Differential ALU tests against hardware traces or a legally usable reference
  oracle over randomized inputs. Dolphin can guide expected behavior, but do not
  copy code.
- Lowering tests that compile small DSP blocks and compare register/memory
  state after execution to reference traces.
- Install-time extraction tests using synthetic local ucode fixtures only.
- Runtime protocol tests for mailbox ready/consume behavior, `0xDCD1` interrupt
  raising, `0xF355` polled replies, DMA to/from IRAM/DRAM, and AI output.
- End-to-end controlled run with a user dump: boot to observatory with
  `GALAXY_DSP_NATIVE=1`, nonzero PCM, no transitional mixer path, no fake
  DSP-ready path, and captured before/after audio.

## Risks And Open Questions

- Exact RMGE01 DSP ucode hash is not known in this workspace. A user dump or a
  locally generated extraction artifact is required.
- The brief says AX, while local evidence is JAudio/JASDSP-shaped. Hash and
  command traces must settle whether this is AXWii, JAudio/JDSP, or a variant.
- Full opcode matrix must be clean-room. Committing a copied Dolphin opcode table
  is not acceptable.
- Bit-exact arithmetic is the largest correctness risk. Audio can sound "mostly"
  right while still clicking or drifting due to one wrong saturation or rounding
  bit.
- Coefficient ROM behavior must be legal and bit-exact. Clean-room coefficient
  replacement may be enough only if RMGE01 never depends on unavailable official
  ROM behavior, or if tests prove equivalence.
- Self-modifying or overlayed ucode would complicate static recompilation. The
  extractor must explicitly detect and reject unsupported overlays until they
  are statically compiled as separate revisions.
- Timing cannot be "complete immediately". The DSP thread must preserve mailbox,
  DMA, interrupt, and per-frame cadence well enough for translated game code.

## Current Conclusion

The work is feasible only as a second static recompiler: Rust decoder and CFG
analysis at install time, generated native C++ for the DSP ucode, and a runtime
coprocessor service that supplies mailboxes, IFX/DMA, interrupts, and timing.
The current host mixer provides useful observations and tests, but it is exactly
the HLE-style behavior this track must delete.

Nothing in this report proves audio correctness yet. The next concrete phase is
to finish the remaining opcode/IFX/accelerator/DMA surface against the extracted
RMGE01 ucode, add a legal installer-side ucode extractor that computes the
ucode hash from the user's dump, and replace the current handwritten audio path
only after the recompiled DSP produces bit-exact buffers under tests.
