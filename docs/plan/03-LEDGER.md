# 03. The ledger: every finding, who closes it, and in which package

376 findings from the audit: M-001 to M-177 (file A), P-001 to P-183 (file B), I-001 to I-016 (file E). The gaps and measurements of file F (G1 to G9, M1 to M4, T1 to T7) are in package E6 and are listed in the Agent 1 plan.

**How it is used.** `ledger.tsv` beside this file goes into the repository on day one (`docs/plan/ledger.tsv`). Each row has a `state` and a `proof`. A row is closed only as one of:

| State | Meaning | What `proof` must name |
|---|---|---|
| `fixed` | The fault is gone | The commit, and the test in the gate that fails without the fix |
| `not-a-fault` | Checked again and the finding was wrong | The test or the lines that show it |
| `decided` | The owner chose to leave it | The owner's words (the note column, or file 00) |
| `out-of-scope` | Named as outside this plan in file 00 | The line of file 00 |

The work is finished only when no row is `open`. A row may not be closed as `fixed` without a test.

The title is the first line of the finding. The finding's full text, with file and line and the suggested fix, is in the audit file.

## Agent 1: engine and port

### E0 gate (12)

| Finding | First line | Owner's decision or note |
|---|---|---|
| P-014 | (maintainability) The Windows release links a stored PGO profile whose staleness warnings are switched off. |  |
| P-023 | see the finding's own text |  |
| P-024 | (untested, platform gap) Unit tests build only on Windows; the POSIX and Android branches of tested code are never tested. |  |
| P-043 | (untested) This extends P-023 with two facts it did not have. The three code checks are enforced only by one machine's local hook. The SDL and Apple b |  |
| P-044 | (untested) The engine's riskiest shared code has no unit test: the game arena, the frame-list writer, the draw walker, the display clocks and the Andr |  |
| P-068 | untested (a diagnostic that cannot answer its question). The SDL "[pacing] frame work … longest" line reports the largest CPU time of any registered t |  |
| P-089 | wrong behaviour: the benchmark's FPS, lows and "SMOOTH AT 60 FPS" verdict count Nebula's interpolated pictures, so a device that cannot run the game a |  |
| P-090 | wrong behaviour: the GPU times behind the recommendation average the interpolated pictures' cheap GPU frames in with the scene's, so the probe prices  |  |
| P-091 | wrong behaviour: the settings menu offers a benchmark's recommendation on a renderer other than the one it was measured on |  |
| P-124 | (untested): every verify script that starts the game goes through run.sh, so all of them depend on the refused plug-in. Each needs the plug-in for its |  |
| P-159 | (wrong measurement; platform gap): on a hidden window, Direct3D 12 and Direct3D 11 draw every picture but present none, while Vulkan acquires and pres |  |
| P-169 | A normal import does not prove the cooked collision octree reaches the same prisms |  |

### E1 data safety (16)

| Finding | First line | Owner's decision or note |
|---|---|---|
| P-018 | (wrong behaviour, rare) At exit or restart the host waits at most 5 s for pending saves, then ends the process anyway. |  |
| P-045 | (wrong behaviour, rare) After a dump request times out, the next request can take the previous dump's answer as its own. |  |
| P-067 | wrong behaviour. On Linux, macOS and Android the crash and die() paths flush achievements and stats inline with no bound, so a crash on a thread that  |  |
| P-079 | wrong behaviour (data loss after a downgrade). A Nebula.sav written by a newer Nebula (version above 4) is reported to the player as damaged. The play |  |
| P-080 | performance. SaveDataHandler::waitRequest spins the game thread with a pause/yield loop, without sleeping, while an earlier save request is in flight. |  |
| P-081 | wrong behaviour (rare data loss). A whole-save import moves the existing files aside and then moves the new ones in, one rename at a time, with no jou |  |
| P-082 | wrong behaviour (suspected). The in-game import's write is not ordered against the game's own save worker. The merged file is built from the save as t | Withdrawn by the reader. Confirm by test and close. |
| P-085 | performance. A Mii import holds the library mutex while it reads every chosen file (up to 64 MB each), copies each into the Miis folder, and writes th |  |
| P-092 | wrong behaviour: an install of a beta is never offered the final release that follows it, on Windows, Linux, macOS or Android |  |
| P-093 | (wrong behaviour) The launcher and the game write the same SharedPreferences file from two processes, so the launcher's stale copy undoes the game's c |  |
| P-094 | (wrong behaviour) Setting up again deletes the installed game before the new one is built, so a failure, a full device or Cancel leaves no game. |  |
| P-095 | (wrong behaviour) A full device during setup ends with an untranslated OS error, and setup killed midway can leave a 3.9 GB orphan that blocks the nex |  |
| P-096 | (wrong behaviour) Mod imports never check free space, show a raw ENOSPC text when the device fills, and their staging folders are never swept after a  |  |
| P-113 | (wrong behaviour, data loss): in Windows Setup, answering "No" to "Uninstall Nebula?" uninstalls Nebula and also deletes every save and setting. |  |
| P-114 | (wrong behaviour): the uninstall runs on Setup's window thread, so the window stops responding while it deletes. |  |
| P-119 | (wrong behaviour): save export and import carry the game's save files only, not achievements or stats. |  |

### E2 crashes and wrong behaviour (20)

| Finding | First line | Owner's decision or note |
|---|---|---|
| P-005 | (wrong behaviour, formally; ARM) The game side reuses a frame list's store after reading `released` with no acquire. |  |
| P-008 | (crash, macOS/iOS) A Metal command-buffer error is never treated as a recoverable device loss; it ends the renderer. |  |
| P-012 | (wrong behaviour, latent) The game's global `operator new`/`delete` are not thread-safe and nothing enforces the "game thread only" rule. |  |
| P-047 | (maintainability) A pointer into `walk_lerped_` is valid only until the next walk_material_rows call. walk_same_material carries a comment-enforced wo |  |
| P-051 | (wrong behaviour, diagnostics only) The scene capture decodes with the frame size at read time, not at copy time. |  |
| P-062 | crash: three D3D11 in-frame allocations throw std::runtime_error on failure |  |
| P-122 | (wrong behaviour, suspected soft lock): FactoryGalaxy scenario 4 (the fast-foe comet) never gives control to the player after its opening flight. | The owner checks it in play later; do not wait: find the cause by run and fix it. |
| P-123 | (maintainability): the comet time limit is converted between seconds and steps three times, and the default is right only because two conversions canc |  |
| P-153 | (wrong behaviour when the option changes; maintainability): which light record a "shadow" light set returns depends on the ray-tracing option, but the |  |
| P-158 | (wrong behaviour on slow devices): a cutscene skip stops after 15 seconds of wall time even if the cutscene has not ended. On a phone, where fewer ext |  |
| P-161 | maintainability (a latent dead end; it would strand the player): if the menu could not make a row or page button while it is open, the game would stay |  |
| P-164 | wrong behaviour (late unlock): the Prankster Comet, Daredevil, Cosmic, Speedy, purple coin and hidden-star achievements do not unlock with the star th |  |
| P-170 | A starter without a rail strands the player in space instead of failing |  |
| P-171 | The port loads layer folders past LayerP that the console never loads |  |
| P-172 | The engine reads a glowing or water material's live rows at positions taken from the unlit form, so with pixel lighting it can read the wrong row |  |
| P-177 | A view-group cube with a negative group number writes before the start of the group table |  |
| P-178 | A flat-model shadow hands the renderer joint matrices it never wrote |  |
| P-179 | Effects marked as lit by their area never take the area's light |  |
| P-182 | View groups special-case one galaxy by name |  |
| P-183 | The creator index is built lazily with no lock, and NameObjAdaptor leaks a replaced functor |  |

### E3 render data path (14)

| Finding | First line | Owner's decision or note |
|---|---|---|
| P-046 | (performance, small) Ring-run merging patches matrix numbers and indices in place in write-combined upload memory, the access pattern the walker avoid |  |
| P-142 | (performance, small; console-era remnant): lights are loaded per object draw in the console's eight-register model, with each world light moved into v |  |
| P-143 | (performance, small; console-era remnant): each shadow volume is drawn twice, front faces then back faces, with the shape's vertices rebuilt and emitt |  |
| P-144 | (performance, small): each animated model's joint matrices are copied twice per frame after the skeleton is evaluated: into the model's pose, then int |  |
| P-145 | (performance, small): the scenario-select and mod mesh draw buffers merge same-material entries by scanning every entry already in the buffer, so entr |  |
| P-149 | (performance, medium on phones; console-era remnant): every particle is built as a matrix in eye space on the CPU, multiplied back to world space, and |  |
| P-150 | (console-era remnant, small): the particle material copies GX alpha-compare, combiner-stage and indirect-matrix settings into a constant array on ever |  |
| P-151 | (console-era remnant, performance on phones): the material path keeps the console's per-draw register model: texture matrices, the two ambient registe |  |
| P-154 | (console-era remnant, performance on phones): the "site" draws (effects, shadows, 2D, gauges, glows) still work as the console's immediate mode: each  |  |
| P-155 | (console-era remnant, small; performance on phones): a texture is still known by the address of its bytes and described by the console's texture heade |  |
| P-156 | (performance on high frame rates and phones; console-era remnant): every presented picture recomputes, on the CPU, the eye-space position and normal m |  |
| P-157 | (console-era remnant, maintainability): the imported material format still stores the console's register layout (combiner colour registers as signed 1 |  |
| P-173 | A material touched once never returns to its registered slot |  |
| P-181 | Shadow shapes are rebuilt on the CPU every draw, and the oval surface shadow draws its model three times |  |

### E4 the frame (9)

| Finding | First line | Owner's decision or note |
|---|---|---|
| P-006 | (performance, Android/tiled GPUs) Every Vulkan scene pass stores depth and stencil, and the multisampled colour, at its end; no pass variant discards  |  |
| P-007 | (performance, Android/tiled GPUs) Vulkan parallel recording splits the scene pass into several render pass instances. |  |
| P-021 | (console-era remnant) A frame copy asked before the frame's first draw returns the previous shown picture, "as the console's frame buffer still held i |  |
| P-025 | (performance, D3D12 with MSAA) With MSAA, bloom and scene AA resolve the scene and then copy the resolved picture again, a second full-frame write and |  |
| P-027 | (performance, D3D12 HDR output) Opening the HDR 2D layer takes two full-size conversion draws from the same source, and closing it takes a third, ever |  |
| P-031 | (performance, Apple silicon and iOS) The Metal scene pass always stores depth and stencil, MSAA stores the multisampled colour as well as resolving it |  |
| P-053 | (performance, MSAA) A resolve is recorded for each consumer even when nothing was drawn since the last resolve. |  |
| P-055 | see the finding's own text | Same finding as P-006. |
| P-063 | performance (D3D11 parity): D3D11 scene AA always copies the scene; D3D12 and Vulkan trade the image instead |  |

### E5 hitches and GPU waits (23)

| Finding | First line | Owner's decision or note |
|---|---|---|
| P-002 | (performance) Every renderer's begin_frame stats config.ini twice a second on the render thread and can block on the settings UI mutex. |  |
| P-010 | (performance) A draw whose pipeline is not made yet compiles it on the render thread; only Vulkan with graphics pipeline libraries gets a quick link. |  |
| P-011 | (performance, D3D11) D3D11's `video_memory` queries the adapter on every walk, generated pictures included; D3D12 and Vulkan cache it for 100 ms. |  |
| P-026 | (performance, D3D12) The present pass recreates its shader views every frame, and the post-process and bloom helpers build every descriptor table fres |  |
| P-028 | (performance, D3D12) Several size changes drain the whole GPU in the middle of a frame (`wait_for_gpu`), while other size changes in the same renderer |  |
| P-029 | (memory, D3D12) A shader-visible descriptor heap that is replaced when it grows is kept until the renderer is re-initialised. |  |
| P-030 | (performance, Vulkan and Metal) The frame-generation and FSR 3 paths call `vkDeviceWaitIdle` from the render thread in the middle of a frame whenever  |  |
| P-048 | (performance) The D3D12 sampler heap is drained with a full GPU wait when it fills. Sampler keys include the dynamic-resolution line ratio and the ups |  |
| P-049 | (performance) DLSS is created on the render thread inside begin_frame, after a full GPU wait, while FSR 3 and XeSS are created on a worker. |  |
| P-050 | (performance) The FSR 3 frame-generation context is recreated mid-frame, with the command list open, after a full GPU wait. |  |
| P-052 | (performance, first use) Utility pipelines are compiled on the render thread at first use, outside the pipeline cache. |  |
| P-054 | (performance) Vulkan waits for the GPU in the middle of a frame whenever it makes a scene-sized target set. It records one barrier into a separate com |  |
| P-056 | (performance) When the sampler array fills, the Vulkan reset waits for the last submitted frame. On drivers with a 4000-sampler limit, it also destroy |  |
| P-057 | (performance) Vulkan creates DLSS synchronously on the render thread, as D3D12 does (P-049). SGSR 2 is made there too, by design (:995-997). |  |
| P-058 | (maintainability, small) VMA is told Vulkan 1.1 even when the device and instance are 1.2 or 1.3 without ray tracing. |  |
| P-059 | (performance, loading) Every texture record with authored-looking levels is analysed on the game thread when it is registered, and again on every budg |  |
| P-060 | performance: every recorded draw renames the root constant buffer with Map(WRITE_DISCARD) |  |
| P-061 | performance (and a limit that can fail): the D3D11 sampler cache is never trimmed, and dynamic resolution keeps adding keys |  |
| P-064 | performance: D3D11 upload_written scans every upload buffer on every flush, allocates 32 KB each time, and copies each written page into every video-m |  |
| P-065 | performance (hitches; Metal): the renderer's own shaders are compiled from MSL source on the render thread the first time each is used |  |
| P-066 | performance: Metal waits for the GPU whenever its sampler array nears full, and in bound-table mode its sampler cache never shrinks; dynamic resolutio |  |
| P-070 | performance. Every host log line does fprintf(stdout) plus fflush(stdout) on the calling thread, and the game thread is a caller (svc_log, context and |  |
| P-117 | (performance, suspected): AV1 movie decode is plain C and can stall the render thread. |  |

### E6 pacing and in-between pictures (28)

| Finding | First line | Owner's decision or note |
|---|---|---|
| P-001 | (maintainability) `frame_rate_decouple` is a dead setting with a live branch in the render loop. |  |
| P-003 | (wrong behaviour) The game's speed follows the display, not 60 Hz, whenever the display is within 3 % (Android) or 1 % (Windows) of a multiple of the  |  |
| P-004 | (performance, Android) The Choreographer thread re-posts its vsync callback forever, also while the app is in the background. |  |
| P-033 | (wrong behaviour, macOS and iOS) The Apple display clock locks the game to the display under the same loose 3 % tolerance as Android. |  |
| P-034 | (performance, macOS and iOS) The Apple display link is never stopped. |  |
| P-041 | (console-era remnant) Several host and renderer timers count frames or presents but are written and named as time, so they run 2-4x faster at 120-240  |  |
| P-042 | (console-era remnant; decided 11-22: stays 60) The default frame-rate setting is 60, the game step, on every platform. | Decided 11-22 (the owner: "should be 60fps."): the default stays 60 on every system and a player chooses a higher rate. |
| P-069 | performance. Android's performance-hint session is told the busiest thread's CPU time as the "actual work duration". The API asks for the wall time of |  |
| P-078 | wrong behaviour (suspected). The star pointer and the mod look are sampled once per game step. Above 60 Hz the pointer the player aims with moves at 6 |  |
| P-147 | (console-era remnant, small): the mod log notice counts its line lifetimes in game frames, not in time. |  |
| P-163 | performance (input latency, small): the menu reads the mouse, touch and wheel once per system step, so on a fast display the pointer's hover, drag and |  |
| P-174 | One slow stretch turns native interpolation off for the rest of the session when a temporal upscaler is on |  |
| I-001 | wrong behaviour (T1). The game step follows the display, not 60 Hz, whenever the display is near a multiple of 60. |  |
| I-002 | design gap (T3, T6). On vblanks, the real picture of a step is almost never shown, and every step costs one extra unseen walk. |  |
| I-003 | performance (T3). Each in-between picture is a full CPU walk and a full GPU frame. |  |
| I-004 | wrong behaviour (T4). Interpolation adds about one to two steps of latency, and neither camera nor pointer is sampled at present time. |  |
| I-005 | wrong behaviour (T2, T7). Linux has no display clock. |  |
| I-006 | wrong behaviour (T2), suspected. Metal drops a picture when `nextDrawable` waited more than 1.5 ms. |  |
| I-007 | wrong behaviour (T2), suspected. VRR detection and slot timing. |  |
| I-008 | wrong behaviour (T6). Rates fit to whole vblanks halve the rate instead of degrading gently. |  |
| I-009 | wrong behaviour (T6). A temporal-upscaler suspend turns interpolation off for the session. |  |
| I-010 | design gap (T6), suspected. Two governors act on the same GPU time without knowing of each other. |  |
| I-011 | console-era remnant (T5). Counts in presents and pictures remain. |  |
| I-012 | design gap (T7). Only Android Vulkan presents at a requested time; only Windows has frame telemetry and the timeline. |  |
| I-013 | wrong behaviour (minor), suspected. FramePhase measures the wrong work under interpolation. |  |
| I-014 | wrong behaviour (T2), suspected. Game-declared camera cuts come from two places only. |  |
| I-015 | maintainability. Stale or wrong comments and dead parts. |  |
| I-016 | gap in tests. The scheduler is not under test (J.8): the arrival clock, `vblanks_per_slot`, both governors, supersede, `interp_walk_time`, the platfor |  |

### E7 game step speed (23)

| Finding | First line | Owner's decision or note |
|---|---|---|
| P-013 | (performance, scales with object count) Every object construction and every `delete` searches all arenas linearly. |  |
| P-038 | (performance, scales with object count) Draw-buffer packet insertion, removal and light re-sort are linear per call, so clipping churn costs O(n) per  |  |
| P-039 | (performance, worse than linear) Sensor hits are found by testing every pair between groups, each step. |  |
| P-040 | (performance, structure) The step has no worker parallelism, and the shared state that blocks it is named here, so a plan can start from it. |  |
| P-088 | performance: the open settings menu rebuilds its page twice a frame on the game thread, and the CONTROLS page rebuilds its row list several times per  |  |
| P-127 | (performance, small): GameScene::draw does work every game frame that the shipping build never uses. |  |
| P-128 | (performance, small): each CategoryList call fetches the list executor through two pointer hops and the draw pass walks about 25 category lists per fr |  |
| P-129 | (performance, medium): every collision query walks every part in the global zone and in each overlapping zone; there is no spatial index over parts, s |  |
| P-130 | (performance, small): a moving part whose sphere touches its zone's box edge recomputes the whole zone's bounds every step. |  |
| P-131 | (performance, medium): once any ball or line query has filled 64 hits, every later query allocates three heap arrays per part tested, for the rest of  |  |
| P-132 | (performance, small): ball queries against a moving part invert the part's previous matrix and take the inverse matrix's scale (three square roots) on |  |
| P-133 | (performance, small): prism lists deduplicate by linear search over the hits found so far, and a prism that misses is retested in every block it appea |  |
| P-134 | (performance, small; console-era remnant): each collision prism stores one vertex plus normals, and the other two vertices are rebuilt at every use wi |  |
| P-135 | (performance, small): the clipping pass is linear and cheap, but it calls MR::getClippingJudge (a scene-object lookup) once per target actor per step, |  |
| P-136 | (performance, small): looking up a sensor by name hashes the string on every call, and actors ask by name in their per-step code. |  |
| P-137 | (performance, small): picking a free actor from a pool scans the pool from the start every time. |  |
| P-138 | (performance, small): a bound actor that does not move runs the same map query twice per step. |  |
| P-139 | (performance, medium): gravity is found by testing every registered gravity for every actor and every shadow every step; there is no spatial filter. |  |
| P-140 | (performance, medium): every moving actor asks for its shadow to be recomputed every step, and each recompute is a gravity search plus a map line quer |  |
| P-141 | (performance, small): the emitter pool is walked in full, empty slots included, up to three times per step. |  |
| P-146 | (maintainability, small): frame-list building, skeleton evaluation and collision results live in file-scope or member state that only one caller at a  |  |
| P-152 | (performance, small; same family as P-139): every visible actor scans every light area every step to find which one it stands in. |  |
| P-165 | performance (small): every NebulaText lookup is a linear scan of the 1,292-row table, and NebulaText::get with a growing buffer expands each text twic |  |

### E8 console limits and gameplay rules (6)

| Finding | First line | Owner's decision or note |
|---|---|---|
| P-118 | (console-era remnant): the THP movie decoder keeps the SDK's IDCT column swap on purpose. | Owner: leave as it is. |
| P-148 | (console-era remnant, wrong behaviour possible): a sensor's list of touching sensors is fixed at its group size and drops any more without a word. | Owner: let it grow, like every other console limit. |
| P-166 | Binder keeps every plane, so Mario's push-out and ground can differ from the console's | Owner: keep the port's behaviour. |
| P-167 | The hip drop no longer marks a digital jump when the actor's gravity is zero | Owner: restore the console's rule. |
| P-168 | Collision and movement arithmetic rounds differently from the console | Owner: a small difference is fine where it is faster. Keep unless a faster form is measured; replays must still match. |
| P-180 | A wire gravity has no point at the rail's end | Owner: leave as it is. |

### E9 upscalers, HDR and option parity (5)

| Finding | First line | Owner's decision or note |
|---|---|---|
| P-009 | (platform gap) Feature parity: D3D11 and Metal lack several features the other two have; three of them silently. | Owner keeps Direct3D 11: bring it to parity or hide what it cannot do. |
| P-032 | (platform gap) Neither Metal nor Direct3D 11 offers HDR output. | Owner keeps Direct3D 11. Where HDR cannot be offered, the option is not shown. |
| P-087 | platform gap (the P-032 pattern; a complete list for the pages read so far). These options are offered where the active platform or renderer cannot do |  |
| P-175 | FSR 3 runs with a moving render size but is never told the size moves |  |
| P-176 | SGSR 2 and MetalFX temporal get wide (HDR) scene colour with no HDR mode |  |

### E10 audio, input, menus, text (14)

| Finding | First line | Owner's decision or note |
|---|---|---|
| P-037 | (performance, Linux and Android) The SDL audio callback can allocate on the audio thread. |  |
| P-071 | wrong behaviour. On SDL hosts (Linux, Android) and Apple, a movie's picture runs ahead of its sound by the audio the device and SDL's stream still hol |  |
| P-072 | wrong behaviour (audible). The Windows output starts at one engine period and lowers the fill again after 10 quiet seconds. A machine that needs two p |  |
| P-073 | wrong behaviour. On Windows the left stick has a per-axis (square) dead zone for both XInput and HID pads, while the SDL hosts use a radial one. A sma |  |
| P-074 | wrong behaviour. A Wii Remote's Nunchuk stick takes its centre from the first report after connecting, not from the Nunchuk's calibration. A stick tha |  |
| P-075 | wrong behaviour. Short controller presses can be lost. Buttons are carried as levels, the newest sample replaces the last, and nothing latches a press |  |
| P-076 | platform gap (the host half of the mouse-held look). The look's mouse motion is unusable or differs between platforms: |  |
| P-077 | performance. The Windows HID worker registers a new thread-pool wait for every report: UnregisterWait, then RegisterWaitForSingleObject with WT_EXECUT |  |
| P-083 | performance (input stalls on Windows). The HID worker's own comment says it never stalls on a busy radio (controller_hid.cpp:1193-1196). Several paths |  |
| P-084 | wrong behaviour (it widens P-073 to every platform). On every host, every stick source goes through a second per-axis dead zone and per-axis clamp bef |  |
| P-086 | wrong behaviour. Some host texts shown in the settings menu have no NebulaText entry, so they appear in English capitals in every language. |  |
| P-106 | (wrong behaviour): the Mac app's own texts are English only, while the game ships 14 languages and NebulaText.tsv already holds most of these strings. |  |
| P-160 | (wrong behaviour: text shows in English): host texts are translated by building an id from the English words, so any change to a host string drops tha |  |
| P-162 | wrong behaviour (text fitting): long translations are squeezed past readability or run out of their boxes, with different floors in each place. |  |

### E11 Android app (8)

| Finding | First line | Owner's decision or note |
|---|---|---|
| P-019 | (platform gap, Android) Corrected in Round 2. The app gives the system a performance hint, but it never reads the thermal state, and its hint reports  | Corrected: the hint exists; its fault is P-069. |
| P-020 | (wrong behaviour, suspected) Android's restart starts the new activity in the `:game` process and kills that same process in the same UI-thread task. |  |
| P-097 | (wrong behaviour) In the Phone input mode a controller press hides the touch controls for good, because the field it checks is never set. |  |
| P-098 | (crash, suspected) GameActivity handles every configuration change but colorMode, touchscreen, mcc and mnc. One of those recreates it inside a process |  |
| P-099 | (wrong behaviour) The restart's CLEAR_TASK removes the launcher from the task, so after a restarted game ends the player lands on the home screen and  |  |
| P-100 | (wrong behaviour, suspected) A launcher opened by "Open with" or Share while the game runs can import mods into, or delete, files the running game is  |  |
| P-101 | (performance) The launcher's sky is redrawn every display frame for as long as the launcher is open, and it jumps every two minutes. |  |
| P-102 | (platform gap, suspected) On Android a mouse's buttons never reach the game: the touch overlay takes every click as a finger. |  |

### E12 Windows, Linux, Mac (20)

| Finding | First line | Owner's decision or note |
|---|---|---|
| P-015 | (platform gap) macOS and iOS write no crash report of Nebula's own. |  |
| P-016 | (platform gap, Linux) A Linux start from the app menu keeps no log file, so a crash report goes nowhere. |  |
| P-017 | (crash reporting) The SDL crash handler sets `SA_ONSTACK` but the render thread and game thread have no alternate stack of their own, so a stack overf |  |
| P-035 | (untested, macOS) The released Mac app runs only on Apple silicon, but the only Mac runs the tree records are on Intel Macs. The Intel-only code paths | Owner: must compile and be complete in code; the owner tests on Apple silicon later. |
| P-036 | (wrong behaviour, Linux) With no XDG desktop portal and no Zenity installed, every file pick (saves, mods, Miis) silently does nothing. |  |
| P-103 | (platform gap) On Linux the Display page offers Windowed or Borderless and a window size, but the SDL host never applies either. There is no full scre |  |
| P-104 | (platform gap): iOS and iPadOS have no host that can build. | Owner: out of scope until the game builds there; mods stop building for it (M-006). |
| P-105 | (wrong behaviour): full screen entered or left through the system is never written back to the display setting. |  |
| P-107 | (wrong behaviour): two screens with the same name cannot be told apart. |  |
| P-108 | (maintainability, suspected thread fault): settings_monitors reads NSScreen off the main thread. |  |
| P-109 | (wrong behaviour, suspected): rumble stops for good after a haptic-engine reset, and the reset handler races the main thread. |  |
| P-110 | (wrong behaviour): on the Direct3D 11 renderer, display changes made during play never reach the window, and neither do monitor changes. |  |
| P-111 | (wrong behaviour): after the player resizes the window by dragging its edge, or drags it onto a monitor with a different scale, the window jumps back  |  |
| P-112 | (wrong behaviour, small): the settings' window-size change uses the system DPI, not the window's. |  |
| P-115 | (wrong behaviour): Setup declares per-monitor DPI awareness but lays out everything at the system DPI, and never handles WM_DPICHANGED. |  |
| P-116 | (platform gap): HD replacement movies play only on Windows, and AV1 movies do not decode on macOS. |  |
| P-120 | (platform gap): Windows Setup refuses any install folder that is not on NTFS. |  |
| P-121 | (maintainability): small faults in the Windows Setup's helpers. |  |
| P-125 | (platform gap): the in-game update installs itself only on Windows. On macOS and Linux, as released, it can only name the new version. |  |
| P-126 | (wrong behaviour, small): the Linux install.sh and uninstall.sh have rough edges. |  |

### E13 tidy (1)

| Finding | First line | Owner's decision or note |
|---|---|---|
| P-022 | (maintainability) The same per-renderer logic is written four times. |  |

## Agent 2: mods

### M0 mod tests into the gate (8)

| Finding | First line | Owner's decision or note |
|---|---|---|
| M-019 | maintainability: the compiler's run-time checks (overflow, division, shifts, conversions) are tested as text only; the one test that runs generated C  |  |
| M-026 | maintainability: no recorded run covers this tree; PROVEN.md proves an older commit, and the two board files disagree on its count |  |
| M-035 | maintainability: the LumaLink test each_manifest_part_is_written_in_the_format_order_and_checked asks for lumakit >=1.2.0 while LumaKit is 1.0.0, so i |  |
| M-036 | missing feature: behaviours with no test in any suite |  |
| M-100 | docs wrong (tests and capability text stale): the safe-start notice moved from the Mods list to the LOG page; 6601 and 6602 still look for it on the l | Owner: the notice goes on the Mods list. |
| M-117 | maintainability (tests): language features that no in-game suite runs, and features no compiler test compiles |  |
| M-164 | wrong behaviour (low): `test` counts a test as passed when any one renderer left a pass, and one unreadable result loses every result |  |
| M-174 | missing feature (tests): behaviours no test proves, most likely to be hit first (extends M-036 and M-117) |  |

### M1 kernel (16)

| Finding | First line | Owner's decision or note |
|---|---|---|
| M-001 | crash: a fault inside a library mod's code (LumaKit) called by another mod is never resumed; the game ends |  |
| M-002 | wrong behaviour: the hook-chain grace covers hooks only; a mod with no published hook is freed at once, with no grace, while its service callbacks may |  |
| M-003 | wrong behaviour: a block header overwrite by a mod (buffer underflow) stops the whole game instead of turning the mod off |  |
| M-005 | maintainability: kernel entry points that take a mod-supplied `LumaFunction*` dereference it without checking it is a row |  |
| M-007 | crash: own_class, as_class, is_a, class_data, overrider and call_method read the object's vtable in the kernel; a stale or wrong object pointer ends t |  |
| M-008 | crash: an override whose call is still running on another thread when its mod is taken out panics the game in slotOriginal |  |
| M-009 | wrong behaviour: LumaClasses.modGone spins on the game thread until every make in progress on any thread ends |  |
| M-010 | maintainability: kMaxClasses (1024) class rows are never reused in a session; dev reloads use them up |  |
| M-013 | wrong behaviour: draw.native's take-out waits without limit for a mod function running on the render thread |  |
| M-018 | see the finding's own text |  |
| M-020 | maintainability: an off-thread hook's report reads the runtime's global caller without synchronisation |  |
| M-027 | wrong behaviour: every service event (guard kind 6) of a mod without unwind data is refused by the word scan, because luma_guard_call's own saved rbx  |  |
| M-028 | maintainability: time handlers and draw points are resumable only because the compiler turns the trampoline's call into a tail jump |  |
| M-110 | maintainability: a game class the header scanner misses gets class id 0 ("unknown") with no build error |  |
| M-112 | wrong behaviour (cost): an achievement on a frequent event does string work for every achievement on every raise, for the whole session |  |
| M-129 | maintainability (cost): every service event call formats a string and scans every mod record |  |

### M2 call safety (10)

| Finding | First line | Owner's decision or note |
|---|---|---|
| M-014 | crash: a game function a mod calls when its state is missing (no scene, no player) faults in game code and ends the game; the rows carry no preconditi | Run by the coordinator: the crash is at boot frames, not on the title. |
| M-015 | missing feature: the 29 NEBULA_MOD_EVENT_* events never reach mod code; only the host's achievement rows subscribe, so their "return to cancel / retur |  |
| M-038 | crash: kit.shop.open in a scene without a stage's playing result (the title, the file select, a mod's own menu before any stage) ends the game |  |
| M-045 | wrong behaviour: kit.area, kit.gravity and kit.player.form act in MarioActor hooks with no test that the actor is player 1; svc.player's extra players |  |
| M-049 | see the finding's own text |  |
| M-076 | save.game.export_file during the boot ends the game |  |
| M-077 | export_file and offer read the user files before the game has read them |  |
| M-111 | docs wrong / maintainability: the events' return-value contracts have no possible user, and the events page is stale |  |
| M-113 | maintainability: the only call lists the build keeps drop template callees, so a needs pass built on them would see no scene reach at all |  |
| M-173 | docs missing: no page warns about calls that crash on the first `on frame` |  |

### M3 services (27)

| Finding | First line | Owner's decision or note |
|---|---|---|
| M-011 | crash: every pointer a mod gives a service is read in kernel or host code; a bad pointer ends the game, not the mod |  |
| M-029 | crash: the full list for M-011 - every service function whose pointer argument is read or written by kernel or host code (a bad one ends the game, not |  |
| M-030 | wrong behaviour: player.make and player.remove leak each extra player's memory until the scene ends; a mod that makes and removes in a loop ends the g |  |
| M-055 | wrong behaviour: a WebSocket server can make the game queue pongs without limit; pongs bypass the send budget every other send is held to |  |
| M-072 | draw.mesh answers LUMA_E_LIMIT "32" when no scene runs |  |
| M-073 | draw.light: radius and CASTS are checked and then dropped; the header describes traced lights |  |
| M-075 | draw.post.program takes a store path from the root, undocumented |  |
| M-078 | save.global waits for the host's file thread with an unbounded spin on the game thread |  |
| M-079 | luma_save.h leaves out answers and a limit the code gives |  |
| M-081 | A replaced music stream is read from the store mapping on the audio thread, under the mixer lock |  |
| M-082 | see the finding's own text |  |
| M-083 | fs refuses a data folder to every mod whose id has a '.' |  |
| M-084 | One mod can pin gigabytes of host memory through fs and assets.cook |  |
| M-085 | Every fs write walks the whole folder; one worker serves every mod |  |
| M-086 | assets.provide's "256 MiB over a session" is per run of the mod |  |
| M-088 | A mod's cooked table (.subs, .events) is read with no bounds check on its cells; the comment says otherwise |  |
| M-090 | A picture another program put on the clipboard is decoded by stb_image inside the game |  |
| M-091 | system.display is never filled on iOS, and the comment that says it will be is wrong |  |
| M-092 | Device threads are detached and may still run while the process tears down its statics |  |
| M-093 | A mod whose id has a '.' makes the whole mods.ids record unreadable at the next start, and its data can never be forgotten |  |
| M-094 | mods.option for a "key" option always gives the manifest's default, never the player's binding |  |
| M-095 | wrong behaviour: name lookups have no limit; a mod or a slow DNS server can start threads without end |  |
| M-096 | wrong behaviour and docs wrong: ws_send cannot send a message above 4 MiB, though the header gives 16 MiB "either way"; http's 4 MiB body limit is not |  |
| M-097 | wrong behaviour: a mod's own endpoint may be up to 100 bytes, but ipc_connect accepts only 64, so another mod cannot reach it; Apple's socket path can |  |
| M-098 | crash at exit (suspected): the net I/O thread and the lookup threads are detached and use static objects that exit destroys |  |
| M-099 | missing feature: IPv6 link-local addresses pass the local rule but cannot connect |  |
| M-109 | docs wrong: luma_modlog.h says an entry's count is at most LUMA_MODLOG_ARGS, but the host sends ARGS + 1 on purpose |  |

### M4 ownership (26)

| Finding | First line | Owner's decision or note |
|---|---|---|
| M-021 | wrong behaviour: kit.camera's field of view, mirror and shake are one value each; with two mods the newer one's end resets the game's default instead  |  |
| M-033 | see the finding's own text |  |
| M-037 | wrong behaviour: kit.gravity's timed overrides count down once per MarioActor, so with svc.player's extra players a `frames` override ends early |  |
| M-040 | maintainability: kit.projectile's slot lists only grow; a slot is reused only while its object is alive in the current stage, so each new stage, and e |  |
| M-041 | wrong behaviour: kit.talk says one line at a time for all mods with no hand-over; a second `say` while a line shows takes the globals, and whichever e |  |
| M-042 | wrong behaviour: kit.hud's handles are bare slot numbers checked against no owner; a mod's handle to an element that went (removed, timed out, or its  |  |
| M-043 | wrong behaviour: kit.area, kit.gravity and kit.ride give the game bare names for their kinds; two mods with one name clash and the second's kind silen |  |
| M-044 | wrong behaviour: kit.menu moves the game's Back and Quit only when the pause menu opens; an entry added, removed or stopped while the menu is open lea |  |
| M-046 | wrong behaviour: when a mod stops while the player rides one of its mounts, kit.ride calls that stopped mod's on_dismount |  |
| M-047 | wrong behaviour: kit.stage galaxies and domes are one per name for all mods; a second mod's definition takes the first's, and when it stops the galaxy |  |
| M-048 | wrong behaviour: kit.stage.build's gravity fields never pull; each load of a built stage defines a new gravity shape and alias that nothing places or  |  |
| M-050 | wrong behaviour: kit.replay's seed is one value for every mod; a second mod's seed replaces the first's, and when that mod stops the game's own random |  |
| M-051 | crash: kit.ui.kit runs a button's or slider's callback one frame after the press; if its mod is taken out in between, LumaKit calls into the freed mod |  |
| M-052 | wrong behaviour: kit.net.kit trusts what any peer says: a player can tell the host that any id joined or left, any machine on the network can give a w |  |
| M-053 | wrong behaviour: the kit data schemas accept values the kit cannot take; a data-only mod that passes the validator can stop LumaKit's data read or han |  |
| M-054 | wrong behaviour: kit.cutscene and kit.stage share svc.audio's one music without knowing of each other; a cutscene with music in a galaxy that has a mo |  |
| M-059 | wrong behaviour: one look for all mods: a second mod's on silently replaces the first, its off or end ends both, and its scale reaches every reader |  |
| M-065 | wrong behaviour: set_drawn's four slots fill with removed extra players; a later hide is not recorded and outlives its mod |  |
| M-070 | draw.inst: two mods on one actor compound each other's colours (and scale) every frame |  |
| M-071 | Shared pools behind per-mod limits: draw textures and fonts, draw.mesh, draw.post programs and slots |  |
| M-080 | audio.game_play takes another mod's ended slot; that mod's stop then answers HANDLE |  |
| M-087 | anim: the play slots and the event pool are shared by every mod behind a per-mod limit, and a lost END is silent |  |
| M-089 | content.model: a reused part keeps the colours an earlier handle set, from the same mod or another |  |
| M-135 | crash: a library function that returns its own struct or enum writes its own size into the caller's value; a struct that gained a field overruns the o |  |
| M-136 | crash: a library keeps a dependent's closure past that mod's stop; the generated dependent_gone drops nothing |  |
| M-138 | wrong behaviour: a struct a caller passes to a newer library gets zero, not the defaults, in the fields the caller does not have |  |

### M5 after a crash (2)

| Finding | First line | Owner's decision or note |
|---|---|---|
| M-031 | crash: on Linux, macOS and Android a fault with mods on never makes the next start safe; a mod whose code crashes the game at start crashes it on ever |  |
| M-032 | missing feature: after a crash that names a mod, nothing turns that mod off or offers to; the next start turns off every mod's code once, and a restar |  |

### M6 files from strangers (18)

| Finding | First line | Owner's decision or note |
|---|---|---|
| M-143 | crash (disk and memory exhaustion): an archive added to the game is unpacked up to 256 GiB before the validator sees it, and an asset up to 4 GiB is r |  |
| M-144 | wrong behaviour (fails open): when the validator cannot run, import accepts the mod and the start records no refusal |  |
| M-145 | crash (memory exhaustion): a small glTF can make the cook allocate gigabytes; only each accessor is limited |  |
| M-146 | hang: a BRSTM, BCSTM or BFSTM header with a block size of 0 and a large block count loops for billions of iterations |  |
| M-150 | wrong behaviour (fails open, verified): a package the validator cannot finish is started with its code |  |
| M-151 | crash (memory exhaustion): FLAC and OGG are decoded whole before the length limit is checked; FLAC can reach 1 GiB a channel |  |
| M-152 | crash (device lost) or hang: a mod's shader may loop without bound; neither the compiler nor the game limits it |  |
| M-156 | crash (memory exhaustion): a mod's sound archive can ask for 64 GiB with one 12-byte command |  |
| M-157 | hang and memory exhaustion: a layout animation (BRLAN) can repeat one key curve billions of times |  |
| M-158 | crash or hang: nested game archives multiply the 4 GiB member limit |  |
| M-159 | crash (memory exhaustion), corrects M-151: an Ogg stream given in place of a game stream is decoded with no limit |  |
| M-160 | hang: a library mod's data schema can make the check take exponential time, in LumaLink and at every game start |  |
| M-161 | wrong behaviour (fails open, partly): a mod that makes the validator fail only when the game's self-description is given is checked again without it |  |
| M-162 | wrong behaviour, revises M-152: a post program a mod's code registers while running skips the cook's checks; three of four renderers compile it in the |  |
| M-166 | crash (suspected): a texture record's width times height is computed in 32 bits before anything is read | Withdrawn by the reader. Confirm by test and close. |
| M-167 | crash (stack overflow) and hang: a patch selector's `glob` recurses once per `*` and backtracks without limit, in the validator at every start and in  |  |
| M-168 | memory safety (game process): the cook passes already-cooked files through unchecked, and the game reads them in place with only a magic check |  |
| M-169 | crash (memory exhaustion): a mod's cooked table reserves rows and columns from its header before reading any |  |

### M7 compiler (20)

| Finding | First line | Owner's decision or note |
|---|---|---|
| M-114 | wrong behaviour: texts given to the game and closures given to services are kept until the mod stops, one per distinct value; a mod that does either e |  |
| M-116 | wrong behaviour (compatibility): a mod asks each service at the SDK's newest minor version, not the lowest one that has the functions it calls, so a m |  |
| M-118 | the checker refuses a `T?` that the game takes. This is too strict, not unsound. |  |
| M-119 | S0067 says "test it first", but only optional services narrow on a test. For a value, `if x != none` |  |
| M-121 | wrong behaviour (diagnostics): an error on any part of a postfix chain is placed at the chain's first word |  |
| M-122 | missing feature: no zero or default value for the built-in value types, though the docs promise one |  |
| M-123 | the checker lets a `none` reach a game pointer field the game may read without a test. |  |
| M-124 | maintainability (C and scripts): an `extern fn` is never compared with the C header it names |  |
| M-125 | docs wrong: `hook ... every override of` is documented and checked, but the back end refuses it |  |
| M-126 | crash: `skip <text>` in a before hook of a `const char *` or `const char16_t *` function hands the game the runtime's block, not characters |  |
| M-127 | wrong behaviour (C does not compile): reading `result`, or an after hook's named result, of a `const char *` function |  |
| M-128 | maintainability (cost): each hook body copies every named `const char *` parameter on every call |  |
| M-130 | wrong behaviour: JSON of a map keyed by a game enum writes a bare number as an object key |  |
| M-131 | wrong behaviour: `clamp(v, lo, hi)` with lo > hi gives lo for every v below hi, with no report |  |
| M-132 | maintainability (cost): a mod holding any live `ref` keeps two before hooks on every game free and every NameObj destruction |  |
| M-133 | crash: a class override keeps the object's data pointer across `base.f()`; if the base call destroys the object, later field reads and writes use free |  |
| M-134 | wrong behaviour: tasks started by `on stop` have their way-outs run inside lumart_end, after luma_finish |  |
| M-137 | maintainability (memory, cost): every distinct closure given to a service is kept until unload, and each keep scans the whole table |  |
| M-139 | docs wrong (low): `keep()` logs "more than 1 MiB as json" when the core refused the value for another reason |  |
| M-140 | crash (low): an `export c fn` runs script code on whatever thread the mod's C calls it from |  |

### M8 LumaLink (9)

| Finding | First line | Owner's decision or note |
|---|---|---|
| M-023 | maintainability: `pack` rebuilds code only when a source file is newer than the oldest code file; a LumaLink update or a new game self-description pac |  |
| M-141 | maintainability (low): `pack` leaves `<id>-<version>.luma.part` beside the folder when writing or the second check fails with an error (a full disk) |  |
| M-142 | wrong behaviour: linux-arm64 is built with outline atomics on a Linux host that has GCC 9.3.1 or newer, so its link fails, and the output depends on t |  |
| M-147 | docs wrong (usability): `check` and `pack` check against the installed game, `build` against what `--names` or LUMA_INCLUDE gave, and nothing records  |  |
| M-148 | maintainability (low): `build` rewrites luma.json in place, and silently does not record a platform for a luma.json with a byte order mark |  |
| M-149 | maintainability (low): an SDK path that contains a comma breaks every ELF link |  |
| M-153 | wrong behaviour (data loss): uninstall deletes the whole LUMALINK_DATA folder |  |
| M-163 | maintainability (low): uninstall deletes every path the install manifest lists, including absolute ones |  |
| M-165 | wrong behaviour (low): failures that LumaLink's window does not show |  |

### M9 mod manager (10)

| Finding | First line | Owner's decision or note |
|---|---|---|
| M-101 | wrong behaviour: the Mods list's overlap count is quadratic in mods and in files, recomputed on every change of the list |  |
| M-102 | WITHDRAWN after reading native_settings.cpp: texture-only and movie-only mods apply live (apply_live_mods, native_settings.cpp:2666-2670, called by sa | Withdrawn by the reader. Confirm by test and close. |
| M-103 | wrong behaviour: the start's cross-check validates every package again whenever any one package changes, and rounds can repeat it once per refused pac |  |
| M-104 | wrong behaviour: the Mods page does work that grows with the square of the number of mods, several times per frame, while the page shows |  |
| M-105 | wrong behaviour: on Android, a Riivolution or files mod in a .zip is reported as imported, then never runs |  |
| M-106 | wrong behaviour: importing an older version of an installed mod replaces the newer one, and the result says UPDATED | Corrected: the message says OLDER VERSION; the replacement still happens. |
| M-107 | wrong behaviour: on Linux, macOS and Android, importing an update of a mod this session is using replaces its folder while the game still reads it |  |
| M-108 | docs wrong (translation): two strings on a mod's page have no translation |  |
| M-154 | wrong behaviour: a reload asked while another runs is dropped, though the log says it will be asked again |  |
| M-155 | wrong behaviour: a mod that cannot be taken out is loaded a second time beside its running copy |  |

### M10 camera, look and first person (13)

| Finding | First line | Owner's decision or note |
|---|---|---|
| M-056 | wrong behaviour: the game's mirrors reflect the game's own camera, not the mod's main camera |  |
| M-057 | wrong behaviour: on macOS the look turns by the system's accelerated pointer motion, not the mouse's counts |  |
| M-058 | wrong behaviour: on macOS the held cursor is frozen where it was, not moved into the picture |  |
| M-060 | wrong behaviour: with more than one controller read in a frame, look() keeps only the last read's turn |  |
| M-061 | wrong behaviour (Windows): the mouse clip is given back only by the next controller read |  |
| M-062 | wrong behaviour (Linux): where SDL cannot hold the mouse the look gets no mouse motion and asks again every pump |  |
| M-063 | wrong behaviour (Android): a mouse click on the picture drops the mouse hold |  |
| M-064 | see the finding's own text |  |
| M-066 | wrong behaviour: a JOINT rig on Mario reads the skeleton without Mario's own body scale and look offset |  |
| M-067 | docs wrong: luma_input.h's first comment names input 1.3 |  |
| M-068 | wrong behaviour: a fast turn of the look counts as a camera cut for the player and for the frame interpolation |  |
| M-069 | maintainability: the look's host work runs on every controller read with no mod |  |
| M-175 | docs wrong (misleading): kit.camera promises the player's body stays hidden while a first-person rig holds the camera; it is hidden once and comes bac |  |

### M11 documentation (15)

| Finding | First line | Owner's decision or note |
|---|---|---|
| M-004 | docs wrong: luma.h says an out-of-memory `new` "stops the game"; inside any boundary the trap is a mod fault the guard resumes |  |
| M-016 | docs wrong: events.md links docs/modding/EVENTS.md, which does not exist |  |
| M-017 | docs wrong: detailed-rules.md promises a build warning for hooking a function that runs off the game thread, from a self-description mark that does no |  |
| M-022 | docs wrong: "A call from another thread is a script error" for LumaKit; the library's export wrappers have no thread test |  |
| M-024 | see the finding's own text | Owner: mods must be able to do anything. Correct the wording about permissions; do not narrow what a mod can do. |
| M-025 | docs wrong: concepts/lumascript.md says a script cannot read a game object after it is gone, and errors.md that a script error cannot damage memory; a |  |
| M-034 | docs wrong: three LumaKit doc comments lost words, and the reference pages print the gap |  |
| M-039 | docs wrong: kit.shop's header says coins are taken with MR::incCoin and a negative count, and that the shop closes when the keeper goes; the code uses |  |
| M-115 | docs wrong: detailed-rules.md tells modders to write `if let p = MarioAccess::getPlayerActor { ... }`, which the compiler refuses |  |
| M-120 | docs wrong: the reference writes calls without `()` and uses `vec3` alone as a zero vector; the compiler refuses both |  |
| M-170 | docs wrong: code blocks the compiler refuses |  |
| M-171 | docs wrong: features documented as working that the compiler refuses with S0800 |  |
| M-172 | docs missing: 80 of the compiler's build-time codes appear on no page, including S0012 and S0800 |  |
| M-176 | docs wrong: three service pages tell C modders to import an older version than the header provides |  |
| M-177 | docs wrong (low): anim.md's limits table gives three limits one description |  |

### M12 platforms for mods (3)

| Finding | First line | Owner's decision or note |
|---|---|---|
| M-006 | missing feature: Windows on ARM64 has no guard in the game module and no build; mods still ship windows-arm64 code | Owner: stop building mods for iOS and Windows on ARM until the game builds there. |
| M-012 | missing feature: the microphone and camera services have backends only on Windows and Android |  |
| M-074 | draw.native on macOS and iOS: at() succeeds, the function never runs, backend() answers 0 |  |
