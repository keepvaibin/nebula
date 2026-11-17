# 02. Agent 2: the mod system

Read `00-READ-FIRST.md` first. This file is your order of work. `03-LEDGER.md` lists the findings each package closes (177 are yours); the finding's full text is in `A-findings-mods.md`.

**Your side of the goal:** a mod can do anything, and no mod can bring the game down, lose a player's data or break another mod. Every promise the documentation makes is true. Everything is proved by tests in the gate.

**The owner's rule for this side: mods must be able to do anything. That is the point of LumaKit.** Safety is never bought by taking power away. It comes from knowing when a call is safe, from containing what fails, and from each mod owning what it sets. When you build the six reference mods and something cannot be done, that is a gap in the engine or the kit to fill, never something to work around inside the mod.

## The order

| # | Package | Why here |
|---|---|---|
| 1 | **M0** The mod tests into the gate | Proof first. Starts while Agent 1 makes the baseline |
| 2 | **M5** After a crash | The safety net, on every platform |
| 3 | **M2** Call safety | The largest source of crashes a mod author meets |
| 4 | **M1** The kernel | The listed faults |
| 5 | **M6** Files from strangers | Early, so it is done before Agent 1 changes the cooked formats |
| 6 | **M3** Services | |
| 7 | **M4** Ownership | Two mods, one game |
| 8 | **M10** Camera, look and first person | The first reference mod |
| 9 | **M7** The compiler | |
| 10 | **M8** LumaLink | |
| 11 | **M9** The mod manager | |
| 12 | **M12** Platforms for mods | |
| 13 | **M14** The six reference mods | The proof that a mod can do anything |
| 14 | **M11** Documentation, from the build | Last, so it describes what is true |
| 15 | **M13** Read what the audit did not, and the last check | |

Finish a package before starting the next, with two exceptions: a crash or a data-loss fault found at any time is fixed at once; and when a package waits on a long run or on Agent 1, start the next one.

Every package ends the same way: its ledger rows closed, its tests in the gate, the full gate green, main moved (and LumaLink's main, where it changed), a dated entry in `docs/plan/PROGRESS-AGENT-2.md`.

**Where you start.** Agent 1's first action is the baseline commit of the game repository. Until `MAIN-MOVED ... baseline` is on the board, work in the LumaLink repository: commit its pending `camera.lum` as its own baseline, fix its one failing test (M-035), and read file A whole. Then make your work tree `D:\NebulaWork\MarioGalaxyRecomp\Nebula-A2` on branch `mods/work` from the baseline, and a LumaLink work tree of your own.

---

## M0. The mod tests into the gate

**Findings:** M-019, M-026, M-035, M-036, M-100, M-117, M-164, M-174.

| Step | Work |
|---|---|
| M0.1 | Bring the 514 conformance tests and their test mods from `D:\NebulaWork\agents\luma\{f,fb,g,h,j,k}` into the repositories: engine tests into the game repository, LumaScript and LumaKit tests into LumaLink's. `scripts\audit_conf.sh` shows how the audit built and ran them |
| M0.2 | Fix the stale ones. The three guard tests (1619, 1620, 1621) use mods built without unwind data: build them as LumaLink builds mods, and add a separate test for a mod with no unwind data (M-027, M-028 in M1). The safe-start tests expect the notice on the Mods list: the owner has decided that is where it goes, so the tests are right and the code changes (M5). Classify and fix `h/mods 2241`, `modlog-host 3127` and `3129`, `refs 3138`, `startcheck 3141` |
| M0.3 | Every suite runs on Direct3D 12, Vulkan, Direct3D 11 and on Android through Agent 1's gate. The eight picture tests with no Direct3D 11 reference get one |
| M0.4 | `lumalink test` passes only when every renderer asked for left a pass (M-164) |
| M0.5 | The list of behaviours no test proves (M-036, M-117, M-174) becomes tests, as each package below reaches its subject. Five LumaKit modules have no proving test and two are used by no test mod: each gets one |

Done when: the gate runs every mod test on every renderer and on Android, and the count of tests and of passes is recorded as the starting line.

---

## M5. After a crash

**Findings:** M-031, M-032 (and M-100 from M0).

The aim is that crashes do not happen. This is the net for the ones that do.

1. **Every platform arms the safe start.** Today only the Windows crash handlers write the record. Tell Agent 1 on the board exactly what the Android, Linux and Mac handlers must write (Agent 1 owns those files and does it in E12; if you reach this first, make the smallest change yourself and note it).
2. **The record names the mod** when the fault was in a mod's code or under a mod's call, as the Windows report already does.
3. **The next start** runs with that mod off (not every mod), says so on the Mods list, and offers: keep it off, turn it on again, remove it. If no mod can be named, every mod's code is off for that start, as today.
4. A mod that crashes at start twice running stays off until the player turns it on.
5. Tests: a mod that faults at boot, in play, and at stop, on every platform the gate reaches; the next start's state and the notice are checked.

---

## M2. Call safety

**Findings:** M-014, M-015, M-038, M-045, M-049, M-076, M-077, M-111, M-113, M-173. **Evidence:** file A, round 7 (the event table and the design note); file D, D.5 (the probes: four of five calls crash the game on the first boot frame).

A mod can call any of 27,095 game functions at any moment. The game's code assumes its own calling order. This package makes "when is this safe" something the build knows.

1. **What each function needs.** The build already reads every game function with a compiler front end. Add the pass the design note describes: `instrument.py` collects what each function reads without a test (a scene, a player, a stage's playing state, a thread), `gen_types.py` runs it to a fixed point over the call graph, and each row of the game's description gains `needs`, `null_when`, `thread` and the reason. The existing call lists drop template callees, among them the call that reaches scene objects; the pass must add them (M-113). The four calls the audit's probes crashed on must come out marked.
2. **The checker uses it.** LumaScript refuses, at build time, a call whose need is not proved at that place, and says what would prove it ("inside `on stage frame`", "after `if let p = player()`"). A mod written in C gets the same facts in its header as comments and as a run-time check in debug builds.
3. **Safe handlers.** Add the handlers a mod author actually wants: a frame of play, any stage ready, a stage ended, the player ready. Each carries what it guarantees, so calls inside it are proved.
4. **The 29 events reach mod code** (M-015), each raised at a safe point. The audit classified them: 6 are safe to act in today, 1 mostly, 7 read-only, 15 not safe as raised. Move the raise of each unsafe one to a point where a mod may act, or deliver it deferred to the next safe point, and say in its contract which. The return-value contracts that no caller can use are either made real or removed (M-111).
5. **`on frame` and tasks during boot and loading.** They run today in every state. They keep running (a mod must be able to do anything), and the checker's proof rules make a call that needs a scene impossible to write there without a test.
6. **LumaKit is safe at any time.** Every public function either works or returns its documented "not now" value. No LumaKit call may crash on the first frame (M-038, M-045, M-049; `save.game.export_file` during boot, M-076, M-077).
7. **Tests:** the audit's probes (`scripts\audit_make_probes.py`) widened to every LumaKit function and a sample of raw game calls, each called on the first boot frame, during a load, at a stage's end and in play. None may end the game.

---

## M1. The kernel

**Findings:** M-001, M-002, M-003, M-005, M-007, M-008, M-009, M-010, M-013, M-018, M-020, M-027, M-028, M-110, M-112, M-129.

The kernel's design stays (binding by name and signature, hook chains, offsets resolved at load, a guard that recovers only where it is safe). Fix the listed faults:

- **A fault inside a library mod reached from another mod** ends the game, because the guard only recovers inside the calling mod's own code (M-001). Decided 2026-11-17 (board, Planner, on the owner's word): the guard resumes at the outermost boundary; a mod's call down into a library it needs has no boundary of its own. The mod turned off is found by need: start at the mod whose code faulted and move to its caller while that caller needs it. A library's call up into code a mod gave it (a callback) goes through a boundary of the giving mod, so the library goes on. Every mod left on with code on the cut stack is told, and the log, the notice and the crash record name the mod turned off, the mod whose code faulted and the place.
- **A mod that overwrites the header in front of its own memory block** stops the whole game (M-003): turn it into that mod's fault.
- **The timer path** recovers only because of how the compiler happens to arrange one call (M-027, M-028). Make it recoverable by construction, for mods with and without unwind data.
- **Class rows** are never reused in a session, so reloads use them up (M-010): reuse them. This is the owner's rule again: nothing has a fixed size.
- The unchecked pointers and vtable reads (M-005, M-007), the take-out races (M-002, M-008, M-009, M-013, M-018), the unsynchronised read (M-020), the class id 0 with no build error (M-110), and the per-event string work and scan (M-112, M-129).

Keep the measured cost: an unhooked game function costs one load and one branch. Measure before and after.

---

## M6. Files from strangers

**Findings:** M-143 to M-146, M-150 to M-152, M-156 to M-162, M-166 to M-169. **Evidence:** file A, rounds 11 to 14, with the closing table of files that break on hostile input.

A mod file can come from anyone. Today a dozen separate readers each trust a number written in the file. Replace that with one rule, applied everywhere:

1. **One budget object** passed to every reader: total bytes that may be produced, total items, nesting depth, time. Every allocation and every loop that takes its size from the file asks the budget first. Running out is a clean refusal that names the file and the limit.
2. **Archives** are checked for size, entry count and ratio as they are unpacked, against free space, not after 256 GiB (M-143, M-158).
3. **Already-prepared files are never passed through** (M-168). A file in Nebula's own prepared format is parsed and written again, or checked to the same standard as the game's reader assumes. `camera.rs:236-242` already does this correctly and is the model. The game's own readers of prepared data check their offsets.
4. **The checker fails closed** (M-144, M-150, M-161): if it cannot finish for any reason, the mod is refused and its code does not run, and the reason is shown. A failure with the game's description never falls back to checking without it.
5. **Matching and schemas** cannot take unbounded time or stack (M-160, M-167): no recursion per `*`, no exponential checking.
6. **Shaders** (M-152, M-162): compiled out of the game's process with a time limit on every renderer, as Direct3D 12 already does; a mod's shader is checked to end; a program registered at run time goes through the same check as one cooked.
7. **A corpus and fuzzing.** Build the corpus from the smallest input of every finding. It goes into the gate: every file refused cleanly, with bounded memory and time. Add a fuzz target for each reader, run for a fixed number of iterations in the gate and longer on demand.

Agent 1 changes the prepared formats in its package E3 and will tell you on the board before each change. Do this package before that, and keep the checks in step with each format change.

---

## M3. Services

**Findings:** M-011, M-029, M-030, M-055, M-072, M-073, M-075, M-078, M-079, M-081 to M-086, M-088, M-090 to M-099, M-109.

- **Every pointer a mod hands a service** is read where the guard does not apply (M-011; M-029 and M-082 list every one). Each service call reads its arguments inside the boundary, so a bad pointer is that mod's fault and not the game's end.
- **Ids with a dot** get no data folder and poison the ids record (M-083, M-093): one id rule, used everywhere.
- **Memory a mod can pin** through `fs` and `assets` (M-084, M-086), the walk of a whole folder per write (M-085), unbounded waits and spins (M-078), unlimited lookup threads (M-095), pongs without limit (M-055), the 4 MiB and 64-byte limits that contradict the headers (M-096, M-097): each bounded, each documented as built.
- **Extra players leak until the scene ends** (M-030).
- **Threads at exit** (M-092, M-098): joined, not detached.
- **The clipboard** decodes pictures from other programs inside the game (M-090): through the budget of M6.
- The rest as the findings say. Every service function gets a test for each of its documented errors.

The only cost a service has when no mod runs today is one buffer cleared per audio block. Keep it that way and measure.

---

## M4. Ownership

**Findings:** M-021, M-033, M-037, M-040 to M-044, M-046 to M-048, M-050 to M-054, M-059, M-065, M-070, M-071, M-080, M-087, M-089, M-135, M-136, M-138.

The kernel already does this well for hooks, numbers and tables. Above it, one value is kept where each mod needs its own.

1. **One pattern, written once and used everywhere:** a value a mod sets is recorded with its owner. The effective value is decided by a stated rule (the mod highest in the player's order wins, unless the value combines, like colours). When a mod stops, exactly its entries go and the next owner's value takes effect. Put the pattern in the kit's core and in the services' shared code; do not write it twenty times.
2. **Apply it** to every value in the master report's table (4.4): the camera's field of view, mirror and shake; the mouse look; loading tips; the talk line; the replay seed; galaxies, domes and kind names (namespaced by mod); stage and cutscene music; HUD handles (handles carry their owner); game sound slots; the shared pools behind per-mod limits; colours on one object; model parts.
3. **Libraries:** a library function that returns or takes its own struct checks the layout, not only the name (M-135, M-138). A library drops a dependent's callbacks when that mod stops (M-136).
4. **A UI callback** cannot run after its mod was taken out (M-051). The network helper says plainly that it authenticates no one, or gains a way to (M-052). Data schemas refuse what the code cannot take (M-053).
5. **Tests:** for every value in the table, two mods set it, one stops, and the other's value stands. This list is in the gate by name.

---

## M10. Camera, look and first person

**Findings:** M-056 to M-058, M-060 to M-064, M-066 to M-069, M-175 (with M-059 and M-065 from M4).

The first-person mod is the first real use of camera control from a mod and the first reference mod.

- **The body reappears** after any arrival or scene change: LumaKit hides it once and the game resets the flag when it sets up the player (M-175, confirmed). The rig holds the hidden state for as long as it holds, across every player set-up.
- **Mirrors show the mod's camera** (M-056). A carried object and the player are drawn or hidden correctly when a mod ends (M-064, M-065).
- **The look:** raw mouse counts on every platform, the cursor held inside the picture, the hold given back at the right moment (M-057, M-058, M-061, M-062, M-063); several controllers in one frame (M-060); no host work when no mod uses it (M-069).
- **A fast flick is a turn, not a cut** (M-068). Agent 1 builds stated cuts (its E6b, 6.6) and gives you the call; you add `camera.cut()` and stop inferring.
- **The look is read per picture.** Agent 1 builds the host side (its E6b, 6.5); you add the kit's mark for a free-look camera. Until then the look is as fresh as the step.
- **Test:** a recorded run of the first-person mod through an arrival, a death, a cutscene and a mirror stage, on PC and tablet, with pictures checked: the body never shows, the mirror shows the right view, the stick maps to the view.

---

## M7. The compiler

**Findings:** M-114, M-116, M-118, M-119, M-121 to M-128, M-130 to M-134, M-137, M-139, M-140.

- **Soundness first:** a script can write `none` into a game object's pointer field (M-123); `skip <text>` hands the game a pointer nothing keeps (M-126); an override keeps a data pointer across a base call that may destroy the object (M-133); an exported function runs script code on whatever thread calls it (M-140).
- **Memory:** texts and callbacks are kept until the mod unloads, so a per-frame use grows without limit (M-114, M-137). Give them the lifetime they need.
- **Versions:** a mod asks each service at the newest version its tools know, so a mod rebuilt with newer tools is refused by every older game even when it uses nothing new (M-116). Record which version introduced each function and ask for the lowest that has what the mod calls.
- **What the docs promise and the compiler refuses:** build `hook ... every override of`, `on layers`, `c struct` and the other constructs the back end refuses with S0800 (M-125, and the list in M-171), or remove them from the language and the docs. The owner's rule decides: a mod must be able to do anything, so build them unless one is truly meaningless.
- The checker's strictness (M-118, M-119), error positions in a chain (M-121), default values (M-122), `extern fn` against its header (M-124), and the rest.
- **Cost:** one `before` hook costs 2 clock reads and about 12 calls today; a live `ref` hooks every free in the game (M-132); each hook copies every text parameter (M-128). Reduce each and measure.
- Every run-time check gets an in-game test, not only a compile test (M-019, M-117).

## M8. LumaLink

**Findings:** M-023, M-141, M-142, M-147, M-148, M-149, M-153, M-163, M-165.

- `check`, `pack` and `build` agree on which game they check against, and the mod records it (M-147).
- **Uninstall cannot delete outside its own folder** (M-153, M-163): this is a data-loss fault, do it first.
- The linux-arm64 build is the same on every host (M-142). `pack` rebuilds when the tools changed (M-023). The rest as the findings say.
- The window shows every failure the command line shows (M-165).

## M9. The mod manager

**Findings:** M-101 to M-108, M-154, M-155.

- Android: a zip mod reported as imported runs (M-105).
- Importing an update of a mod in use does not replace files the game is reading (M-107). Importing an older version asks first (M-106).
- The Mods page and the start check do work that grows with the square of the number of mods, some of it per frame (M-101, M-103, M-104): linear, and not per frame. Test with 200 mods installed.
- A reload asked during another is queued (M-154). A mod that cannot be taken out is not loaded twice (M-155).
- M-102 was withdrawn: prove by test, close.

## M12. Platforms for mods

**Findings:** M-006, M-012, M-074.

- Stop building mods for iOS and Windows on ARM until the game builds there (the owner's decision). The tools, the manifest and the docs say which platforms a mod is built for and why.
- The microphone and camera services exist on Windows and Android only (M-012): build them on Linux and Mac, or have the service answer "not on this platform" in a way a mod can test, and say so.
- Metal accepts a mod's native draw and never calls it (M-074): make it work, with Agent 1, or refuse it with a reason.

---

## M14. The six reference mods

The mod system is proved by six mods, each built with the released tools from the public documentation only, each with tests in the gate that play it:

| # | Mod | It proves |
|---|---|---|
| 1 | First person | Camera, look, drawing rules for the player |
| 2 | A new object | A new class in the game's world: model, collision, behaviour, placement in a stage |
| 3 | A HUD change | Screens, layouts, text in every language |
| 4 | A ghost replay | Recording and replaying a run; identical steps on every platform |
| 5 | A stage edit | Changing and adding to a stage's data |
| 6 | A character swap | Models, animation, sound |

Rules:

- Write each as a mod author would. When something cannot be done, or needs knowledge the docs do not give, that is a finding: add it to the ledger and fix the engine, the kit, the tools or the docs.
- Run all six together. Then run them with one stopping at a time. Nothing breaks.
- Each mod's tests run on every renderer and on Android.
- They go into the documentation as the worked examples.

---

## M11. Documentation, from the build

**Findings:** M-004, M-016, M-017, M-022, M-024, M-025, M-034, M-039, M-115, M-120, M-170 to M-172, M-176, M-177 (M-173 and M-175 are closed by fixing the behaviour in M2 and M10).

1. **Reference pages are generated:** services from the headers, LumaKit from its sources, error codes from the compiler's table (80 codes are on no page today, M-172), the game's functions with their new `needs` marks.
2. **Every code sample is compiled in the gate.** A sample the compiler refuses fails the build (M-115, M-120, M-170).
3. **Every promise is checked against the code.** The audit read two pages sentence by sentence and both had false statements; there are 271. Read every page against the code. A promise the code does not keep is either made true or removed. The owner's rule: where a page says a mod cannot do something, make sure that is not a limit that should go.
4. **Permissions** (M-024): a mod is native code in the game's process. Permissions are consent that an honest mod asks for, and they are shown to the player; they are not a wall. Say that, in the docs and wherever the blog's wording is repeated. Do not narrow what a mod can do.
5. Every page in plain words.

---

## M13. Read what the audit did not, and the last check

The audit's master report, section 10, lists what was not read. On your side:

| Not read | Do |
|---|---|
| About half of LumaLink's internals: `new`, `add`, `test`, `convert`, `verify`, the editor server, the window, Setup | Read whole |
| The compiler's parser, lexer and parts of the game-description reader | Read whole |
| The importer and the checker line by line (the audit reviewed every allocation, loop and recursion site, not every line) | Read whole |
| The game's own readers of prepared files (M-168) | Read each against what the importer guarantees |
| The old tests beyond one per suite | Read what each asserts |
| `LumaDrawView` and how a mod's camera effects blend between steps | Read, with file F, section 9 |

Every fault becomes a ledger row and is closed like the rest.

Then the last check, before you may write the sentence in file 00, section 4.5:

1. The ledger has no open row of yours.
2. The full gate is green on main and on LumaLink's main, on clean checkouts.
3. The six reference mods pass together on every renderer and on Android.
4. The hostile corpus and the fuzz targets are clean.
5. Every page of the documentation has been checked against the code, and every sample compiles.
6. Read your own diff of the whole work once, start to end, as a reviewer who distrusts it.
