# F. In-between pictures and the frame-rate unlock: where they stand and what the best version is

Written 2026-11-17. This replaces section 8.6 of the master report.

**What this rests on.** File E is the as-built map: one reader read the pacing and blending code and cited every claim (16 findings, I-001 to I-016). I read the scheduler and the blending file myself (`render_core.cpp:730-1319` and `2579-2668`, `frame_interpolation.cpp` whole) and checked the map's main claims against the code. After you allowed it, I ran the current build in real time with a visible window: ten runs on the PC and three on the tablet. Section 1A has what they showed, and the gaps below carry the measured figures.

| Mark | Meaning |
|---|---|
| CONFIRMED | I read the lines myself |
| READ | The reader read them and cited them; I did not re-read |
| DERIVED | Arithmetic from confirmed rules; not measured |
| SUSPECTED | Needs a run; the run is named |
| MEASURED | From the 11-17 real-time runs (section 1A) |

---

## 1. The answer

**The foundation is sound, and parts of it are ahead of most shipped games. It is not yet the best modern implementation.** Nothing in it has to be thrown away. The scheduler and the blending rules stay; three things are rebuilt around them.

Earlier I told you two things that this closer look corrects:

- I said the scheduling was simply "modern in design". It is better than that: every in-between picture shows the game at the moment that picture reaches the display, at any display rate. Section 3 lists what is already right.
- I said a slow moment turns in-between pictures off for good. That is true only with a temporal upscaler on. Without one, the count drops and comes back by itself.

**The nine gaps, biggest first**

| # | Gap | What the player gets today | Size |
|---|---|---|---|
| G1 | A finished frame waits for the next game step before its in-between pictures start | About one extra step of delay on everything, pointer included, whenever in-between pictures are on | Large, and cheap to fix |
| G2 | Every in-between picture is a whole second frame on the CPU and the GPU, and with VSync on one more unseen frame is drawn per step | MEASURED on the tablet: it cannot hold 120. 105 pictures a second in a light galaxy, 72 in the heaviest, though the GPU's own time fits | Large |
| G3 | Only Android with Vulkan asks the display for a time. Linux has no display clock at all. Variable-refresh displays are not really handled | Uneven motion on Linux; untuned on Windows, Mac and VRR | Large on those platforms |
| G4 | The game's own speed follows the display when it is within 1% (Windows) or 3% (Android, Apple) of 60 | The whole game up to 2.5% fast or slow on some phone and Mac panels | Medium |
| G5 | Nothing is read fresh for a picture: the pointer and a mod's camera look are as old as the frame | The mouse pointer and first-person look trail the hand | Medium |
| G6 | Two governors do not know of each other and neither watches what is shown; rates are offered that the display cannot show evenly | MEASURED on the tablet: an uneven 72 where a steady 60 was possible; in-between pictures switching off on the GPU's clock speed and staying off | Large on phones |
| G7 | Camera cuts and teleports are mostly guessed from speed | A smear across some cuts; a fast flick treated as a cut | Small to medium |
| G8 | A few counters still count pictures | Timings that change with the display rate | Small |
| G9 | The scheduler has no tests and nothing measures display times in play | Nobody can say it is smooth; only that it should be | Blocks proof of all the rest |

---

## 1A. What the measurements showed (added 11-17)

**How it was measured.** The build under test, in real time, visible, borderless on the 240 Hz monitor (Odyssey G8, 3840x2160), no sound device. A small mod takes the game from the title into Honeyhive Galaxy; scripted stick and jumps move Mario for about 45 seconds; the first 30 seconds of each run are left out. The figures come from the game's own pacing statistics and its frame timeline (every step, every picture's moment and `t`, every present call, the swap chain's own count of refreshes). Scripts: `scripts\audit_pace.sh`, `audit_pace_digest.py`, `audit_pace_chain.py`. Results: `D:\NebulaWork\build\audit-pace\digest.txt`.

### PC results

| Run | Pictures a second | Evenness | A step's real frame was shown | Finished frame waits | Step start to that frame whole on the display |
|---|---|---|---|---|---|
| Direct3D 12, VSync on, match (240) | 239.99 | 10,565 of 10,565 presents exactly one refresh apart | 5 of 2,641 steps | 18.3 ms | **42.4 ms** |
| Direct3D 12, VSync on, target 120 | 120.00 | 5,290 of 5,292 exactly two refreshes apart | 6 of 2,646 | 17.8 ms | 38.2 ms |
| Direct3D 12, VSync on, target 144 | **120.00** | 5,286 of 5,291 | 5 of 2,646 | 23.0 ms | 38.3 ms |
| Direct3D 12, VSync off, 240 | 240 | tearing | 2,643 of 2,643 | 17.0 ms | 30.2 ms |
| Direct3D 12, VSync off, 144 | 144 | tearing | 5 of 2,648 | 20.6 ms | 34.4 ms |
| Direct3D 12, VSync off, unlimited | 3,640 | tearing | 21 of 2,641 | 17.1 ms | 34.3 ms |
| Vulkan, VSync on, match (240) | 239.99 | 10,551 of 10,552; 11 late moments | 13 of 2,641 | 17.6 ms | 38.4 ms |
| Vulkan, VSync off, 240 (newest-only queue) | 239.97 | 10,556 of 10,558; 13 late moments | 8 of 2,642 | 18.7 ms | 38.4 ms |
| Direct3D 11, VSync on, match (240) | 239.99 | 10,561 of 10,561 | 9 of 2,640 | 17.7 ms | 42.6 ms |

In every run the game ran 60.000 steps a second. On Direct3D no step in any run was longer than 18 ms.

### What the measurements settle

| Question | Answer | Was |
|---|---|---|
| Is the motion even? | **Yes, and very.** At 240 Hz with VSync on, every one of 10,565 presents was exactly one refresh after the one before; the swap chain's own count agrees (10,807 presents over 10,807 refreshes). The game time each picture shows matched its display time within 0.03 ms for 99 of 100 pictures | not known |
| Does a finished frame wait a step before its pictures start (G1)? | **Yes: 17 to 23 ms** in every run | CONFIRMED in code, size derived |
| How long from input to display with in-between pictures on? | **38 to 42 ms with VSync on, 30 to 34 ms with it off.** The part up to the picture's own moment is 34.1 ms in every mode but one: two steps plus the small tracking delay, exactly as the code reads | derived as about 42 |
| Is the real frame almost never shown with VSync on (I-002)? | **Yes: 5 to 13 of about 2,640 steps.** Every other step drew one extra frame nobody saw. With VSync off at 240 the real frame was shown for all 2,643 steps | SUSPECTED |
| Does a rate the display cannot show evenly get offered (G6)? | **Yes.** A 144 target on this 240 Hz display gave 120 | derived |
| What does a picture cost on the PC? | About 0.3 to 0.45 ms of render-thread time and 0.14 to 0.16 ms of GPU time, in Honeyhive at 3840x2160. The game thread used 39 ms of CPU a second: 0.65 ms a step | SUSPECTED |
| Does the picture count fall under load? | Not on the PC, which never came near its budget. On the tablet it does not fall when it should, and falls when it should not (T3, T4 below) | not known |

### Four things the measurements found that the reading had not

**M1. With in-between pictures off and VSync on, a frame is held far longer than I had assumed.** I wrote that it reaches the display 5 to 8 ms after its step starts. Measured on the 240 Hz display at 60 pictures a second:

| Moment | After the step's start |
|---|---|
| The render thread makes the present call | 12.9 ms |
| The present call returns | 29.3 ms (it blocks for 16.4 ms) |

The render thread first waits about 12.5 ms for the swap chain to accept another frame, and the present call then blocks for nearly a whole step. The frame cannot be on the display before 12.9 ms and is most likely there at about 29 ms. The timeline does not record enough of the swap chain's statistics to place it exactly. Two things follow:

- The code that aims a step to finish just before a refresh (2.1) cannot be working here: it measures the time to the present call's return, and that is longer than a step.
- A 240 Hz display can show a finished frame at its next refresh, 4 ms away. Presenting at once, and letting the display repeat the frame, would bring this path to roughly 5 ms.

So the honest comparison is: about 29 ms with in-between pictures off, 38 to 42 ms with them on, on this display with VSync on. Both can come down a long way.

**M2. Direct3D shows a picture two refreshes after its moment at 240 Hz; Vulkan shows it after one.** Direct3D 12 and 11: 8.3 ms. Vulkan: 4.1 ms. On Direct3D the render thread spends about 4 ms of every picture blocked on the swap chain (4.11 ms of wall time per picture against 0.16 ms of GPU time). That is one refresh of delay with no purpose, and the reason the Direct3D rows above are 4 ms worse than Vulkan's.

**M3. One hitch on Vulkan, repeatable.** In both Vulkan runs, at the moment a restarted scene first showed (Mario had fallen), one picture reached the display 50 to 60 ms late and one game step ran long (18.4 ms and 24.6 ms). Direct3D 12 went through the same restart with no late step. The cause is not traced; it is on the render thread and outside the frame walk.

**M4. The render thread spins while it waits.** Its CPU use was 163 ms a second at 120 pictures a second and 99 ms a second at 240 with VSync on, where it sleeps inside the present call instead. The wait before each picture's moment ends in a busy loop of up to 2 ms (`precise_wait.h`). On a PC this costs nothing that matters. On a phone it is battery and heat, and it has to be measured there.

### Tablet (SM-X810, Adreno 740, Vulkan, 2800x1752), at 120 Hz and at 60 Hz

After you set the tablet to its 120 Hz mode with power saving off, I ran seven runs: a light galaxy (Honeyhive) and the heaviest stop of the tour (`OceanPhantomCaveGalaxy` 4), each at 120 Hz with in-between pictures and at 60 Hz without. Mario stands still in these runs: scripted input is not available on Android outside the fixed-step harness. The battery stayed at 24 C and the system reported no heat limit. Results: `D:\NebulaWork\build\audit-pace-tablet\digest.txt`. The test app is removed again; your install was not touched. You can put the tablet back to power saving.

| Run | Pictures a second | Pictures exactly one refresh after the one before | GPU a picture | Render thread, wall time a picture |
|---|---|---|---|---|
| Honeyhive, 120 Hz, VSync on (first run) | 105 | 83% | 5.6 ms | 7.2 ms |
| Honeyhive, 120 Hz, VSync on (second run) | **60**: in-between pictures went off at the loading screen and never returned | 67% two refreshes apart, the rest one then three (T5) | 8 to 13 ms | |
| Honeyhive, 120 Hz, VSync off | 107 | 85% | 5.6 ms | 7.9 ms |
| Heaviest stop, 120 Hz, VSync on | **72** | **42%** | 6.9 ms | 9.8 ms |
| Heaviest stop, 120 Hz, dynamic resolution on | 72 | 40% | 6.9 ms | 9.8 ms |
| Honeyhive, 60 Hz, no in-between | 60.0 | 600 of 600 at their time | 10.8 to 12.8 ms | |
| Heaviest stop, 60 Hz, no in-between | 60.0 | 604 of 604 at their time | 12.0 to 14.2 ms | |

**T1. The tablet does not hold 120.** In the light galaxy about one picture in six is a refresh late. In the heavy one it shows 72 pictures a second and fewer than half arrive on time; the gaps between pictures are a mix of 8, 17 and 25 ms. That is what stutter is.

**T2. The GPU's own time fits; the pipeline around it does not.** A picture has an 8.33 ms slot. The GPU's own time is 5.6 to 6.9 ms. But from one in-between picture to the next the render thread takes 7.2 to 9.8 ms of wall time, and once per step it also draws the unseen frame. Its CPU work per step is 5 to 9 ms at 120 Hz against 4 to 6 ms at 60. What the rest of that wall time is was not separated. In the heavy stop the figures (GPU 6.9 ms, wall 9.8 ms) fit the render thread and the GPU working one after the other on each picture instead of side by side; that needs a trace of the render thread to confirm. This is G2, measured on the device it matters for.

**T3. Nothing reacts.** The GPU governor sees two pictures fit its budget and keeps the count at 1. The cadence governor sees the game keep its steps and does nothing (0 drops). Dynamic resolution changed nothing: it never lowered the size, because GPU time per picture is inside its budget. So the game goes on trying for 120 and delivers an uneven 72, where a steady 60 would look better. No rule watches what is actually shown, although the game already counts it (late moments: 318 in that run).

**T4. The count of in-between pictures flips on the GPU's clock speed, and can stay flipped.** Two Honeyhive runs with identical settings behaved differently:

| | First run | Second run |
|---|---|---|
| On the title screen | 107 to 118 pictures a second | 107 to 118 pictures a second |
| After the stage's loading screen, GPU time per picture read | 10.7 ms | 10.5 ms |
| The governor's answer | in-between pictures off | in-between pictures off |
| Then | back on after 8 seconds | never back in the rest of the run, about a minute |

The mechanism is visible in the log. During the loading screen there is almost nothing to draw and the phone lowers the GPU's clock. The next full pictures, at that low clock, take 10 to 13 ms where they took 5. The governor concludes that two do not fit and turns in-between pictures off. At 60 pictures a second the GPU stays at its low clock, so the reading stays high, and they stay off. The same picture costs 5 ms when the GPU is busy and 11 to 13 ms when it idles between pictures (the 60 Hz rows above show the same).

**T5. With in-between pictures capped off on a 120 Hz display, the 60 pictures are placed unevenly.** At first every picture is two refreshes after the last (120 of 120 in a window). Twenty seconds later 24 of 120 are one refresh and 24 are three. The fallback state itself judders, and it gets worse the longer it lasts.

**T6. At 60 Hz the tablet is perfectly even and 50 ms deep.** Every picture appeared at the display time the game asked for. But the game asks for a display time three refreshes (50 ms) after the step starts, in both galaxies, and never comes down. One refresh of that is Android's own (it needs a finished picture about 17.7 ms before the refresh that shows it). One more is lost to the low GPU clock: at 11 to 14 ms a picture the frame misses the two-refresh slot it would make at 5 to 7 ms.

**T7. Bloom is a quarter of the GPU's time in the heavy stop:** scene 9.3 ms, bloom 3.1 ms, final picture 1.1 ms of 13.5 ms (at the low clock).

**What T2 to T6 mean for the design.**

- The arbiter (G6) has to judge by what reaches the display, not by GPU time alone: the share of pictures on time, already counted. When 120 cannot be held it should fall to a steady 60, and it should place those 60 pictures evenly.
- It must not read GPU time at a clock speed its own decision caused. Android's performance hint exists for this: the game tells the system how long a frame's work should take and the system holds the clock up. Today the hint is told CPU time only, and the wrong figure (P-069).
- The unseen frame per step, the CPU blending (G2), and whatever keeps the render thread and the GPU from working side by side are what stand between this tablet and 120 Hz. The trace named in T2 comes first.

### Still not measured

A variable-refresh display (whether this monitor's is on was not checked), Linux, a Mac, a low-end phone, and the tablet with Mario moving.

---

## 2. How it works today

### 2.1 The game step

The game loop is: wait for the step's moment, read input and update, pose, ask for the previous frame to be shown, then write this step's frame list (`GameSystem.cpp:182-206`). CONFIRMED

What the step's clock follows:

| Platform | Rule | Result |
|---|---|---|
| Windows | Follows the display when it is within 1% of a multiple of the step and the frame's work fits; otherwise the host's own 60 Hz clock (`frame_phase.cpp:9-47`) | Locked at 59.94, 60, 119.88, 120, 240, 360 Hz. Exactly 60 at 72, 75, 90, 100, 144, 165 |
| Android | Follows the display's vsync when within 3% (`display_clock.cpp:119-160`) | A 58.5 to 61.5 Hz panel sets the game's speed |
| Mac, iOS | Same 3% rule on the display link | Same |
| Linux | No display clock exists (`display_clock.cpp:193-211`) CONFIRMED | Always exactly 60 |

### 2.2 From a step to the screen, with in-between pictures on

1. Step N writes list N and hands it to the render thread.
2. The render thread **holds it** (`render_core.cpp:2615-2631`). CONFIRMED
3. Step N+1 wakes, updates, poses, then asks for a picture. Only now does list N's sequence start (`run_pending_interp_start`, `:730-741`). CONFIRMED
4. The sequence's start time is step N+1's moment plus a small delay. The delay tracks how late frames usually arrive (the 97th percentile, at most half a step) and moves slowly so the motion's speed never jumps (`:809-833`). CONFIRMED
5. Each picture is given a time `t` between list N-1 (0) and list N (1): `t = (the picture's moment - the start) x 60` (`:1212`). CONFIRMED
6. Where pictures can only appear on a display refresh (VSync on, or a queue that cannot tear), each picture's moment is a real refresh of the display, taken from a clock that tracks the display (`:749-766`). Elsewhere the moments run on a timer.
7. A picture is a full walk of the frame list with everything blended to `t`, then a present.
8. When list N+1's sequence starts, list N's is replaced. If its real picture (`t = 1`) was never shown, the list is walked once more at `t = 1` **without being shown**, so that later frames can read what it wrote (`:1160-1171`). CONFIRMED

### 2.3 What is blended

| Blended | Held or snapped |
|---|---|
| Cameras: rotation as a rotation, eye position in a line | A camera cut shows the newer frame |
| Every joint: rotation, scale and position separately, so a turning object does not shrink | A teleport (moved more than half its distance from the eye plus 200 units, or scaled more than 8 times) shows the newer frame |
| Objects under a moving camera are blended in the world, so a still object stays still | A turn over 150 degrees keeps the newer rotation |
| Lights, projection, sky direction | Textures (a flipbook shows the newer) |
| Material values that change, with scrolling textures unwrapped | Materials in a registered slot |
| Per-frame vertices when the shape matches | Particles; anything new this step |
| Render-to-texture passes, bloom, the 2D layer: all drawn again for the picture's time | |

The pointer, the HUD and the menus are ordinary items in the list. Nothing is read from a device at picture time (file E, section E; searched). READ

### 2.4 Showing a picture, per renderer

| | Direct3D 12 and 11 | Vulkan | Metal |
|---|---|---|---|
| Queue | Flip model, wait object, at most one frame queued, tearing allowed with VSync off | FIFO with VSync on; newest-only or tearing with it off | Up to 3 drawables |
| Asks the display for a time | No | Only on Android, only with VSync on (`VK_GOOGLE_display_timing`) | No |
| Learns when a picture was really shown | Only with diagnostics on | Android only | Only with diagnostics on |
| When a picture is late | Shown at the next refresh | Same; on Android the lead time adapts | **Dropped** if the drawable waited over 1.5 ms |

### 2.5 Under load

- **GPU governor.** In-between pictures may use nine tenths of a step of GPU time. Over that, the count drops at once and returns after a calm stretch (`:878-933`). CONFIRMED
- **Cadence governor.** If the game itself starts missing steps because the render thread is busy, the count drops; it tries again after 2 to 64 seconds, doubling after each failed try (`:964-1105`). CONFIRMED
- **Dynamic resolution** runs separately, aiming each picture at its own budget. The two do not know of each other (I-010).
- **With a temporal upscaler on,** a picture that does not fit turns in-between pictures off until a setting changes, and the check that would bring them back can never run (I-009, P-174). READ

---

## 3. What is already at the modern standard

Keep all of this. It is the hard part and it is done.

| Part | Why it is right | Evidence |
|---|---|---|
| A fixed 60-step game with drawn in-between pictures | The standard way to separate game speed from display rate. Each picture is a real drawing, so none of the smearing that picture-space frame generation has | section 2 |
| Each picture shows the game at its own display moment | Motion is even at any rate. At 144 Hz `t` advances 0.4167 per refresh; no rate is special | `render_core.cpp:781-792`, CONFIRMED |
| Pictures land on real display refreshes | Avoids two pictures in one refresh and a repeated refresh | `:749-766`, `present_slots.h:34-40`, CONFIRMED |
| The start time follows the step clock with a slow, bounded correction | The speed of motion changes by at most 2% of a step per step | `:809-833`, CONFIRMED |
| A stall skips the moments it missed | No burst of old pictures after a hitch | `:1218-1223`, CONFIRMED |
| Two governors that tell the game's own misses from the pictures' | In-between pictures never cost the game its steps for long | `:935-1105`, CONFIRMED |
| Blending rules (2.3) | Rotations blended as rotations; still objects stay still; teleports caught | `frame_interpolation.cpp`, CONFIRMED |
| Android presentation | Vsync timeline from the system, a requested display time per picture with a lead that adapts, a frame-rate vote to the system. This is what Google's guidance asks for | file E, section C; tested on a simulated display at 17 rates |
| Windows swap chain | Flip model, wait object, one frame queued, tearing; a dedicated thread tracks the display's refreshes | file E, section C |
| The pure maths and the pairing have unit tests | | file E, section J |

---

## 4. The gaps, one by one

### G1. One step of delay that is not needed

**Today.** A finished frame waits for the next step before its pictures begin (2.2, steps 2 and 3). The wait is nearly a whole step, because a step's work is short and the rest of the step is sleep.

**Measured** (Direct3D 12, 240 Hz, VSync on; section 1A):

| Moment | Today, measured | After the fix, derived |
|---|---|---|
| Input is read (the step starts) | 0 ms | 0 ms |
| The frame list is ready | under 1 ms | under 1 ms |
| Its pictures begin | 18.3 ms later | at once |
| It is shown whole, at its picture's moment | 34.1 ms | about 18 ms |
| On the display | 42.4 ms | about 26 ms; about 22 with M2 fixed as well |

With in-between pictures off, the same frame's present call begins 12.9 ms after the step starts and returns at 29.3 ms (M1 in section 1A). I had written 5 to 8 ms for that path; the measurement corrects it.

**What is inherent.** One step. A picture between two frames cannot be drawn until the second frame exists. That 16.7 ms is the price of in-between pictures without prediction, in any engine.

**What is not inherent.** The wait for the next step (about 15 ms), and most of the refresh between a picture's moment and its display (G3).

**The fix.** Start a list's sequence when the list arrives. The start time becomes this step's moment plus the usual time a step takes to produce its list. The same slow, bounded tracking that exists today applies; only the reference changes. The code that holds the list (`render_core.cpp:2615-2631`) and the present-only message from the game stop gating the start.

**Why it was built this way.** The console showed a frame at the start of the next one, and the port kept that order for the present. With in-between pictures off the port already shows a frame at the end of its own walk; the path with them on still waits.

**Mark.** The wait is CONFIRMED in code and MEASURED: 17 to 23 ms in all nine runs with in-between pictures on.

### G2. The cost of a picture

**Today.**

| Cost | Detail | Mark |
|---|---|---|
| CPU, per picture | Every draw's matrices, skin sums and changing material values are computed again at `t`; every per-frame vertex is blended; every pass is recorded again | P-156, I-003, READ |
| GPU, per picture | The whole frame again: render-to-texture passes, frame copies, bloom, the scene, the 2D layer | I-003, READ |
| One unseen walk per step | With pictures on refreshes, a list's real picture is almost never the one shown, so the walk at `t = 1` is made and thrown away. At 120 Hz that is 3 walks for 2 pictures | I-002. CONFIRMED in code and MEASURED: the real picture was shown for 5 to 13 of about 2,640 steps with VSync on |

On the PC the measured cost is about 0.3 to 0.45 ms of render-thread time a picture in Honeyhive at 3840x2160, so the PC has room. The phone does not have that room, and the cost grows with the number of draws.

**The modern form, in four parts.**

1. **No unseen walk.**
   - At rates that are multiples of 60 (120, 240, 360), give the sequence a lead so that one refresh lands exactly on `t = 1`. The real frame is then shown and nothing is thrown away. On Windows the step is already locked to the display at those rates, so the phase holds. This is small.
   - At other rates (144, 165), the unseen walk exists only because later frames read what a frame wrote. Once passes state what they read (the frame overhaul, master report 8.3), the engine keeps "the last real frame" by name and the unseen walk goes.
2. **Blend on the GPU.** Once per step, send the older and the newer pose of everything (position, rotation, scale; joint sets; changing material values). Per picture, send one number. A small GPU pass makes that picture's matrices. The CPU cost per picture stops depending on how many draws there are. This is step 5 of the render data path rewrite.
3. **Record once per step, replay per picture.** With all per-picture data in buffers, the draw commands for a step do not change between its pictures. Where the API allows a recorded list to be replayed, replay it; where it does not, recording without any per-draw maths is still cheap. Which APIs allow it has to be checked against their documentation before it is designed in.
4. **Do not redraw what does not depend on `t`.** Measure each pass first. Bloom and the scene do depend on it. Some render-to-texture passes may not.

**The teleport and cut tests stay on the CPU, once per step,** where they are today. Their answer is baked into what is sent ("older pose = newer pose" for a teleport).

### G3. Asking the display for a time, per platform

**Today.** Only Android with Vulkan and VSync on gives each picture a requested display time and learns when it was really shown. Everywhere else the render thread waits for the picture's moment, draws, and presents as soon as it can.

| Platform | Today | Best available now |
|---|---|---|
| Android, Vulkan | `VK_GOOGLE_display_timing`, the system's vsync timeline, a frame-rate vote. Good: MEASURED, at 60 Hz every picture appeared at the time asked. But the lead it settles on is three refreshes, 50 ms (T6) | Add `VK_EXT_present_timing`, which Google's documentation prefers on Android 17 and later, with the present path kept as the fallback. Report wall time, not CPU time, to the performance hint (P-069). No thermal rule exists (file E, I.2) |
| Windows, Direct3D 12 and 11 | A thread tracks refreshes; the picture is drawn at its refresh moment and shown at the next one. The display's real times are read only with diagnostics on | Read the swap chain's frame statistics all the time, not only in diagnostics. Use them to learn the real display time and to count missed refreshes. Give Windows the adapting lead that Android has, so a picture is started as late as it safely can be instead of one whole refresh early. MEASURED: at 240 Hz a picture is on the display two refreshes after its moment on Direct3D and one on Vulkan (M2); with in-between pictures off, a frame's present call blocks for 16.4 ms (M1) |
| Windows and Linux, Vulkan | The same wait, then FIFO | Use `VK_EXT_present_timing` where the driver offers it, checked at start. Elsewhere, as Direct3D |
| Linux | **No display clock, no refresh grid, no display times** (I-005, CONFIRMED). Pictures on a free timer go into a queue that shows them on refreshes, so two can land in one refresh and the next refresh repeats | `VK_EXT_present_timing` where present. Otherwise build the refresh grid from when presents complete. Until one of those works, in-between pictures on Linux should say they are unsynchronised |
| Mac, iOS | A display link gives the refresh grid. Presents are "never at a time asked for" (`renderer_mtl.mm:768-769`). A picture whose drawable waited over 1.5 ms is dropped (I-006). iOS asks for the maximum rate only | The newer Metal display link, which gives each frame its target display time; present at that time; ask for a rate range, not a fixed maximum; do not drop in-between pictures |
| Variable refresh (G-Sync, FreeSync, VRR TVs) | Detected only when the measured rate falls below 0.8 of the mode's rate, which in-between pictures near the full rate prevent. When detected, the picture clock still follows a refresh grid that, on these displays, follows the presents (I-007) | Its own path. On a variable-refresh display the display shows a picture when it is presented, so the engine sets the rhythm: evenly spaced presents by a precise timer, each drawn for its own moment, capped a little under the display's maximum. Any target rate is then even, including 120 on a 144 Hz display |

Marks: the table's "today" column is READ (file E, section C). I-006 and I-007 are SUSPECTED and each names its run.

### G4. The game's speed

**Today.** Section 2.1. On a 59.94 Hz display the game runs 59.94 steps a second on Windows, Android and Mac. On an Android or Mac panel anywhere within 3% of 60 the game follows it, up to 2.5% off.

**Judgement.** Locking at 59.94 is right when in-between pictures are off: the console itself ran at 59.94, the difference is one part in a thousand, and the alternative is a doubled or dropped picture every 17 seconds. 3% is not right. A game 2.5% fast is a different game, and the music runs on its own clock.

**The modern rule.**

| Case | Rule |
|---|---|
| In-between pictures off, display within 0.2% of 60 or a multiple | Lock the step to the display |
| In-between pictures off, display further off (a 61 Hz phone panel, 50, 75 Hz) | Keep exactly 60 and show blended pictures at the display's rate, as at 144 Hz |
| In-between pictures on | Exactly 60. At exact multiples, also keep the step's phase tied to the display so one refresh lands on `t = 1` (G2) |

One smaller fault belongs here. With in-between pictures on at 120 or 240 Hz on Windows, the code that aims the step just before a refresh measures the wrong thing, because it reads the time of the last real picture and real pictures are rarely shown (I-013, SUSPECTED).

**Not known.** How the music and sound follow a locked step that is not exactly 60. The reader did not read the audio side of this. It has to be read before the tolerance is chosen.

### G5. Fresh input

**Today.** Input is read once per step. The pointer is drawn from the frame list like any object. With in-between pictures on, the mouse pointer on the screen is as late as everything else in G1's table.

**The modern form.** Two classes of thing can be read at picture time without changing the game:

| Thing | How |
|---|---|
| The pointer, and the settings menu's cursor | Draw it from the newest device sample at the picture's moment. The game's own logic still uses the per-step value |
| A free-look camera owned by a mod (first person) | The mod marks its camera as free-look; the engine applies the newest look rotation to the picture's view. The game's movement still uses the per-step view |

The game's own camera follows Mario and has nothing fresher to read, so it stays blended.

This removes the whole chain's delay for what the hand controls directly, which is where delay is felt.

### G6. Governors, and the rates on offer

| Fault | Today | Fix |
|---|---|---|
| A rate the display cannot show evenly is offered | On a fixed 144 Hz display the list offers 75, 90, 100 and 120. A 120 target gives 72, because 120 does not divide into 144 and the next even rate down is 72 (`render_core.cpp:143-149`, CONFIRMED). MEASURED on the 240 Hz display: a 144 target gave 120. The fallback is the right one; offering the rate is the fault | On a fixed display offer only the rates it shows evenly (144 and 72). On a variable-refresh display offer any |
| A GPU cap halves the rate | The same rule: capped from 2 pictures to 1 at 144 Hz gives 72 | Lower the resolution first, then the picture count. Halving the rate is the last step |
| Two governors, no order | Dynamic resolution and the picture governor each read their own GPU average (I-010, SUSPECTED) | One arbiter with one budget and a stated order: resolution, then picture count, then back up in the reverse order |
| Temporal upscaler: off until a setting changes | I-009, READ | Come back by the same back-off the other governor uses |
| No heat rule on phones | file E, I.2 | Feed the system's thermal headroom to the arbiter |
| Nothing watches what is shown | MEASURED on the tablet (T3): 72 uneven pictures a second in the heavy stop, with no governor acting and dynamic resolution idle | The arbiter judges by the share of pictures on time, and falls to a steady 60 when 120 cannot be held |
| The GPU reading follows the GPU's clock speed | MEASURED (T4): the same picture reads 5 ms or 11 to 13 ms, so in-between pictures switch off after a loading screen and may never return | Hold the clock with the system's performance hint; judge a return by trying it, as the cadence governor already does |
| The capped state is uneven | MEASURED (T5): 60 pictures on the 120 Hz display drift to one-then-three refreshes | Place the capped pictures on every second refresh, by the same display clock |

### G7. Cuts and teleports are guessed

**Today.** The game tells the renderer about a camera cut from two places (a forced camera change, and cut frames in camera animations). Every other cut is caught by a threshold: a turn over 60 degrees in one step, or a jump over 1,500 units that is also six times the previous move. A cut below those is blended for one step. A counter of cuts is written into every frame list and read by nothing (I-014, CONFIRMED). Teleports of objects are all inferred.

**The modern form.** The game states them. This is a native build of the game's own source: the camera code knows when it changes camera, and the code that warps or respawns an object knows it did. Each calls one function. Mods get the same call. The thresholds stay as a net and are no longer the rule, so a fast first-person flick is a turn and not a cut.

### G8. Counted in pictures

Small. The lead-time controller counts presents (60, and 300 to 4,800); dynamic resolution counts pictures with a time floor; the Android timing read is every fourth present (file E, section H). Each becomes time or steps. One statement in the earlier findings was wrong and is corrected: the Windows display query runs twice a second at any display rate (P-041).

### G9. Proof

**Today.**

- The scheduler has no unit test: the start time, the picture moments, both governors, the replacement of a sequence (I-016). The simulated-display test covers only Android's lead-time controller.
- No test compares an in-between picture with what it should show. The one script that touched this compared only the real frames, and it is among the dead tools.
- The frame timeline and the telemetry exist on Windows only.
- The harness runs fixed steps and cannot measure pacing.

Section 6 says what proof looks like.

---

## 5. The target, in one place

**Five rules.**

1. The game runs exactly 60 steps a second. The one exception is a display within 0.2% of a multiple of 60 with in-between pictures off.
2. Every picture shows the game at the moment the picture reaches the display, less one constant delay. That delay is one step plus the smallest lead the device can hold.
3. The CPU cost of a picture does not depend on how many objects are drawn.
4. What the hand controls directly is read at picture time.
5. Every platform either does the same or says why not, on screen.

**The path of a frame.**

| Stage | Once per step | Once per picture |
|---|---|---|
| Game | Step, write the list, state cuts and teleports | nothing |
| Render thread | Pair the two lists, run the teleport tests, send both poses of everything, record the draws | Pick `t` from the display time the picture will have; send `t`, the fresh pointer and look; replay |
| GPU | | One small pass makes the matrices for `t`; then the frame |
| Display model | | Predict the display time; after the present, read when it was really shown; adjust the lead |
| Arbiter | Read GPU time, CPU time, heat; set resolution and picture count | |

**What changes in the code.**

| Change | Where | Depends on |
|---|---|---|
| Start a sequence when its list arrives | `render_core.cpp:2615-2631, 730-871` | nothing |
| Lead at multiples of 60 on refreshes, so the real frame is shown | `render_core.cpp:797-798, 848` | nothing |
| Recovery after a temporal-upscaler suspend | `render_core.cpp:903-924`; the renderers' `interpolation_mode` | nothing |
| Offer only even rates on fixed displays | `settings_model.cpp:84-168` | nothing |
| Lock tolerance 0.2%, and blended pictures on off-rate displays | `frame_phase.cpp`, `display_clock.cpp`, `display_clock_apple.mm` | the audio question in G4 |
| Scheduler pulled out of `render_core.cpp` into its own unit with a simulated display under test | new file; `present_slots_tests.cpp` is the model | nothing |
| A display model per platform: predicted and actual display times | the four renderers' present code; a Linux clock that does not exist yet | the scheduler unit |
| Variable-refresh path | `renderer_common.cpp:290-399` and the scheduler | the display model |
| One arbiter | `resolution_governor.h`, `render_core.cpp:878-1105` | the scheduler unit |
| Fresh pointer and free-look | `draw_walker.cpp`, the input services, LumaKit `camera` | nothing |
| Stated cuts and teleports | `MotionSignal.cpp`, the camera code, `FrameList.cpp:527`, LumaKit | nothing |
| Blend on the GPU; record once, replay | `draw_walker.cpp:177-489`, `compose.cpp` | the render data path rewrite |
| No unseen walk at 144 and 165 Hz | `render_core.cpp:1160-1171` | the frame overhaul |

---

## 6. What proof looks like

**Measures.** Each is recorded by the game itself, on every platform, in play.

| Measure | What it is | Target I propose |
|---|---|---|
| Step rate | Steps a second against the clock | 60, within 0.01%, outside the lock case |
| Animation error | For each shown picture: the game time it shows, against the time it really reached the display, less the constant delay. This is the measure Intel's PresentMon added for exactly this question | 99 of 100 pictures within 1 ms on a fixed-refresh display |
| Refreshes missed or doubled | From the display's own statistics | None in ten minutes of steady play |
| Added delay | Input read to display, with in-between pictures on, less the same with them off | One step plus one refresh or less |
| Cost per picture | Render-thread time against the number of draws | Flat from 500 to 5,000 draws |
| Speed after a hitch | How long until pictures are even again | Within half a second |

**Tools to build** (all are in phase 0 of the master plan, or extend it).

1. A real-time mode for the test harness, so a stage can be played at a set display rate.
2. Display-time recording always on, not only in diagnostics: frame statistics on Direct3D, presentation timing on Vulkan, the presented time on Metal.
3. The scheduler under a simulated display, at every rate in the table in file E (A.5), with steady, varying and spiking load. The Android controller's test already does this and is the model.
4. A blending test scene: objects on known paths. An in-between picture at `t` must show each object where its path says, and the pictures at `t = 0` and `t = 1` must equal the two real frames exactly.
5. The frame timeline on every platform, not only Windows.

**One measurement I can make now, if you allow it.** Six of the SUSPECTED items in file E are settled by a two-minute run with a visible window at your monitor's rate, using diagnostics that already exist. My standing rule is hidden windows, so I have not done it.

---

## 7. Order of work

This fits inside the master plan. Nothing here needs a new phase.

| Order | Work | Where in the master plan |
|---|---|---|
| 1 | Measure what is there: the visible-window runs above, on the PC and the tablet | Phase 0 |
| 2 | The changes that depend on nothing: start the sequence when the list arrives (G1); the lead at multiples of 60 (G2); recovery after a suspend (G6); only even rates offered (G6); on Direct3D, stop blocking a refresh per picture (M2); with in-between pictures off on a fast display, present at once (M1) | Phase 4 |
| 3 | The scheduler as its own unit, under a simulated display | Phase 0 |
| 4 | Stated cuts and teleports; fresh pointer and free-look | Phase 4, with the mods agent for the LumaKit side |
| 5 | The display model per platform; the Linux clock; the variable-refresh path | Phase 4 and 5 |
| 6 | One arbiter, with heat | Phase 4 |
| 7 | The lock rule | Phase 4, after the audio question is read |
| 8 | Blend on the GPU; replay; no unseen walk | Phase 3, with the render data path rewrite and the frame overhaul |

Items 2 and 4 are where the player feels the difference first. By the measurements: about 20 ms less delay with in-between pictures on (42 to about 22 ms on Direct3D at 240 Hz), about 20 ms less with them off on a fast display (29 to about 5 ms), and a pointer that follows the hand.

---

## 8. Decisions I need from you

1. **The lock at 59.94 Hz.** Keep it (the game 0.1% slower than 60, as the console was, and no added delay), or hold exactly 60 everywhere and blend. I recommend keeping it, at 0.2% and no wider.
2. **Done** on the PC and on the tablet at 120 Hz (section 1A). You can put the tablet back to power saving.
3. **Prediction.** A low-delay mode could show the game slightly ahead by guessing motion, removing the inherent step of delay. It produces visible errors when the guess is wrong. I recommend not building it; fresh pointer and look cover what the hand feels.
4. **The proposed targets in section 6.** Are they the standard you want held in the gate?
5. **Displays to test on.** Do you have a variable-refresh monitor, a 144 or 165 Hz monitor, a Mac and a Linux machine available? Three of the gaps cannot be closed without each.
6. **Default rate.** Today a new install starts at 60 with in-between pictures off on every display (P-042). Once the measures hold, should a 120 Hz display start at 120?

---

## 9. What is not known

| Question | What settles it |
|---|---|
| Exactly when a frame reaches the display with in-between pictures off (M1) | The swap chain's full statistics in the timeline |
| The cause of the Vulkan hitch at a scene restart (M3) | A trace of the render thread around it |
| The tablet with Mario moving, and a low-end phone | Scripted input on Android outside the fixed-step harness; a second device |
| Whether variable refresh is detected with in-between pictures on | A variable-refresh monitor |
| Visible judder on Linux | A Linux machine at 144 Hz |
| How often Metal drops an in-between picture | A Mac |
| How audio follows a locked step | Reading the audio clock code |
| Whether a mod's camera effects blend correctly | Reading `LumaDrawView`, then the blending test scene |
| Parts of `draw_walker.cpp` (809-1439, 1640-1889) and two renderers' `interpolation_mode` | Not re-read for this file |

Sources for the platform guidance in G3 and section 6:
- https://developer.android.com/games/develop/vulkan/frame-pacing-extensions
- https://developer.android.com/games/sdk/frame-pacing
- https://developer.android.com/games/optimize/display-refresh-rate-change
- https://developer.apple.com/documentation/quartzcore/cametaldisplaylink
- https://github.com/GameTechDev/PresentMon/releases
