# The guard: a failing mod is switched off, the game goes on

## More rules

### The guard

Headers: `native/include/luma_guard.h` (kernel and host; not for mods), `native/petari/include/Game/System/LumaGuard.hpp`
(kernel), `runtime/include/galaxy/luma_guard_host.h` (host). Code: `Game/System/LumaGuard.cpp`, `native/host/luma_guard.cpp`.
Branch f/1.6-guard. Reviewer: H, after it compiles and has run (R2).

### 1. Boundaries

Every place the kernel runs a mod's code calls it through `luma::guard::run(mod, kind, where, fn, a0..a3, &result)`,
which pushes a frame on the thread's stack of frames and calls `fn` through `luma_guard_call` (assembly per calling
convention: win-x64, sysv-x64, aarch64). It saves the registers a call keeps, then calls the mod's function itself, so
nothing of the kernel lies between the frame and the mod's code. The kinds and their `where`:

| kind | where | sites |
|---|---|---|
| BEFORE, AFTER, WRAP (wrap and replace), TELL | the hooked function's name | LumaHook run, proceed, tell |
| START | "begin", "start" | LumaLoader loadMod |
| STOP | "stop", "finish", "dependent_gone" | LumaLoader unloadMod, finishMod, tellDependentGone |
| EVENT | "<service> event <kind>" | LumaServices kernelCall (every service's event and timer) |
| CLASS | the class's name; the overridden function's name | LumaClasses make (`made`), overrideHook |

Boundaries nest per thread (a mod's hook calls the game, whose function another mod hooks); the innermost names the
mod. The kernel's own hooks are not boundaries. With no mod, none of this runs (0 cost).

### 2. Resumed, or not

The host's handler (Windows: the first vectored handler; POSIX: `sigaction` with `SA_ONSTACK`, an alternate stack per
thread from its first boundary) resumes at the innermost frame only when all hold:
1. the fault is an access violation (SIGSEGV, SIGBUS), an illegal instruction (SIGILL) or an integer division fault
   (SIGFPE FPE_INTDIV/FPE_INTOVF);
2. the thread is inside a boundary and not handling an earlier fault (`LumaGuardThread.handling`);
3. the faulting instruction is inside the mod's own code (the executable part of its library's image);
4. between the fault's stack pointer and the boundary, no return address into the game or the host lies below one
   into the mod (the mod called the game, which called the mod again: those game frames would be skipped half done).
   A value that only looks like one makes the guard not resume: it errs towards ending the game, never towards
   resuming into a broken one.

Not resumed (a stack overflow, a fault outside a boundary, outside the mod's code, a second fault, or 4): Windows
passes it on (other handlers, then the crash reporter, whose report names the mod and carries the guard's sentence);
POSIX writes one line naming the mod on stderr and passes it to the previous handler (the default ends the process).
Never a silent crash, never a resume on a dead stack.

### 3. After a caught fault

On the faulting thread, the frame already popped: the mod is off for the session (`faulted`; no boundary of it runs
again, its stop and finish included, until the next start); the game's log gets "[mod X] GUARD: its code failed
(<boundary>: <where>): <fault> at <file>+0x<offset>, address 0x<a>. It is off until the next start; what it was changing
may be half done."; the mod log gets a GUARD error (code: the fault, 1 access, 2 illegal, 3 divide) with the row
`Guard.Caught` ({boundary} {where} {fault} {address}, all 14 languages), which the notice shows on screen; its take-out
is queued for the next frame (hooks out, the mods that use it out first, no stop). The caller goes on as if the code
did nothing: a before does not skip; a wrap, replace or override that had not proceeded proceeds; a start is refused
(`Validator.Lib.StartFaulted`); an event's call answers LUMA_E_CANCELLED.

### 3b. Code time

Each boundary also measures: two reads of the cheapest clock (x64 `rdtsc`; arm64 `cntvct_el0`; never a system call)
around `luma_guard_call`, charging the innermost boundary's mod its time less the boundaries nested in it (a
thread-local sum), into the mod's slot for this frame. Once a frame (LumaLoader `frame`) each live slot rolls into a
ring of its last 120 frames. The export `luma_guard_cost(id, &avg_ns, &worst_ns)` (`LumaGuardCost`, luma_guard.h)
reads a slot from any thread through a seqlock: 0 found, -1 no such mod this session or the clock not yet scaled.
Scale: arm64 from `cntfrq_el0` at install; x64 against the host's clock over the first quarter second with a mod (no
host clock: said in the log, never scaled). Slots: 256 mods a session (a reload of the same id reuses its slot; past
256 said in the log); a mod taken out keeps its last values. No mod: no slot is touched and nothing rolls. In a
harness run, the first time a slot's ring fills the kernel writes the fact `kernel.guard_time` ({mod, found, avg_ns,
worst_ns}, as the export gives it).

### 4. Proof

Each fault kind in each boundary kind; the nested case; the not-resumed cases ending with their message; the perf line
(no mod: 0; with a mod: the boundary's cost in ticks against a plain call, and of that the clock's pair of reads,
measured). Tests 1601-1631 and 1634 (code time: guard-nest-a's and the game half's `kernel.guard_time`, found 0)
(suite f/tests/guard), 1632-1633 (suite f/tests/guard-crash: the game must end).

### 5. Known limits

- A service's own code between the boundary and the mod's function (LumaTime's handlerTrampoline) lies above the
  mod's frames, so rule 4 lets the fault resume, and the resume skips the rest of that code. A service must therefore
  do nothing after its call of the mod's function that has to happen (the trampoline does nothing); tests 1619-1621
  prove the time case.
- The old interpreter's classes (LumaClass.cpp) have no boundaries; they go in 1.10.
- Code time is the boundaries' wall time on the thread: a mod's thread that is preempted inside a boundary is
  charged the time it was off the processor.
- x64 scales ticks to ns against the host's clock, which in a deterministic (harness) run is the frames' clock: there
  the ns are the frames' time, not the wall's, unless the run is paced at the frame rate. Ticks (kernel.guard_cost) are
  always the measure.
- Windows on ARM64: the host compiles with the guard off (install says so in the log); the game module has no call
  for it and stops its build with an error naming this limit.
