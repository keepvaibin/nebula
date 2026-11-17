"""audit_fix5.py REPORT_DIR: the 11-17 measurements into file F."""
import pathlib
import sys

R = pathlib.Path(sys.argv[1])
fp = R / "F-interpolation-and-frame-unlock.md"
f = fp.read_text(encoding="utf8")


def rep(old, new):
    global f
    assert f.count(old) == 1, (old[:70], f.count(old))
    f = f.replace(old, new)


rep("**What this rests on.** File E is the as-built map: one reader read the pacing and blending code and cited every claim (16 findings, I-001 to I-016). I read the scheduler and the blending file myself (`render_core.cpp:730-1319` and `2579-2668`, `frame_interpolation.cpp` whole) and checked the map's main claims against the code. Nothing was run for this file; where a number is derived from the code and not measured, it says so.",
    "**What this rests on.** File E is the as-built map: one reader read the pacing and blending code and cited every claim (16 findings, I-001 to I-016). I read the scheduler and the blending file myself (`render_core.cpp:730-1319` and `2579-2668`, `frame_interpolation.cpp` whole) and checked the map's main claims against the code. After you allowed it, I ran the current build in real time with a visible window: ten runs on the PC and three on the tablet. Section 1A has what they showed, and the gaps below carry the measured figures.")
rep("| SUSPECTED | Needs a run; the run is named |",
    "| SUSPECTED | Needs a run; the run is named |\n| MEASURED | From the 11-17 real-time runs (section 1A) |")

measured = """
---

## 1A. What the measurements showed (added 11-17)

**How it was measured.** The build under test, in real time, visible, borderless on the 240 Hz monitor (Odyssey G8, 3840x2160), no sound device. A small mod takes the game from the title into Honeyhive Galaxy; scripted stick and jumps move Mario for about 45 seconds; the first 30 seconds of each run are left out. The figures come from the game's own pacing statistics and its frame timeline (every step, every picture's moment and `t`, every present call, the swap chain's own count of refreshes). Scripts: `scripts\\audit_pace.sh`, `audit_pace_digest.py`, `audit_pace_chain.py`. Results: `D:\\NebulaWork\\build\\audit-pace\\digest.txt`.

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
| Does the picture count fall under load? | Not tested: this PC never came near its budget | still not known |

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

### Tablet

Three runs were made (SM-X810, Vulkan). Two things limited them:

- **The tablet's display stayed at 60 Hz.** The tablet is in power saving and its display is set to the standard rate, so Android allows only 30 to 60 Hz and the game's request for the fastest mode was answered with 60. In-between pictures therefore never started. To measure them on the tablet, power saving has to be off and the display set to its adaptive (120 Hz) mode. I did not change either setting.
- **My test mod was refused on the tablet** (I had listed only Windows in its manifest), so these runs stayed on the title screen.

What they still showed, at 60 Hz with VSync on:

| Measure | Result |
|---|---|
| Pictures shown at the display time the game asked for | 599 to 604 of every 600 to 604 |
| From a step's start to its picture on the display | 33.3 ms: two refreshes. The game cannot hold one refresh here, because Android itself needs a finished picture about 17.7 ms before the refresh that shows it |
| With VSync off | The newest-only queue is used and no display times are asked for (confirms file E) |
| First start | The boot screen lasted about 20 seconds with an empty cache, against about 1 second on the PC |

The test app was removed from the tablet again; the installed Nebula was not touched.

### Still not measured

A variable-refresh display (whether this monitor's is on was not checked), Linux, a Mac, the tablet at 120 Hz, and any case under load.
"""
rep("\n---\n\n## 2. How it works today", measured + "\n---\n\n## 2. How it works today")

# G1: the derived table gives way to the measured one.
start = f.index("**The chain, at 144 Hz with VSync on, on the PC** (DERIVED")
end = f.index("**What is inherent.** One step.")
g1 = """**Measured** (Direct3D 12, 240 Hz, VSync on; section 1A):

| Moment | Today, measured | After the fix, derived |
|---|---|---|
| Input is read (the step starts) | 0 ms | 0 ms |
| The frame list is ready | under 1 ms | under 1 ms |
| Its pictures begin | 18.3 ms later | at once |
| It is shown whole, at its picture's moment | 34.1 ms | about 18 ms |
| On the display | 42.4 ms | about 26 ms; about 22 with M2 fixed as well |

With in-between pictures off, the same frame's present call begins 12.9 ms after the step starts and returns at 29.3 ms (M1 in section 1A). I had written 5 to 8 ms for that path; the measurement corrects it.

"""
f = f[:start] + g1 + f[end:]
rep("**Mark.** The wait is CONFIRMED in code. The millisecond figures are DERIVED. Run to settle: the frame timeline at 60 and 144 Hz (file E, section F).",
    "**Mark.** The wait is CONFIRMED in code and MEASURED: 17 to 23 ms in all nine runs with in-between pictures on.")
rep("| One unseen walk per step | With pictures on refreshes, a list's real picture is almost never the one shown, so the walk at `t = 1` is made and thrown away. At 120 Hz that is 3 walks for 2 pictures | I-002. The rule is CONFIRMED; how often is SUSPECTED |",
    "| One unseen walk per step | With pictures on refreshes, a list's real picture is almost never the one shown, so the walk at `t = 1` is made and thrown away. At 120 Hz that is 3 walks for 2 pictures | I-002. CONFIRMED in code and MEASURED: the real picture was shown for 5 to 13 of about 2,640 steps with VSync on |")
rep("On the PC the measured render-thread cost is 0.2 to 0.4 ms a picture on Direct3D 12 and 0.3 to 0.7 ms on Vulkan and Direct3D 11, so the PC has room.",
    "On the PC the measured cost is about 0.3 to 0.45 ms of render-thread time a picture in Honeyhive at 3840x2160, so the PC has room.")
rep("| A rate the display cannot show evenly is offered | On a fixed 144 Hz display the list offers 75, 90, 100 and 120. A 120 target gives 72, because 120 does not divide into 144 and the next even rate down is 72 (`render_core.cpp:143-149`, CONFIRMED). The fallback is the right one; offering 120 is the fault |",
    "| A rate the display cannot show evenly is offered | On a fixed 144 Hz display the list offers 75, 90, 100 and 120. A 120 target gives 72, because 120 does not divide into 144 and the next even rate down is 72 (`render_core.cpp:143-149`, CONFIRMED). MEASURED on the 240 Hz display: a 144 target gave 120. The fallback is the right one; offering the rate is the fault |")
# the display timing table: Windows row gains M1 and M2
rep("Give Windows the adapting lead that Android has, so a picture is started as late as it safely can be instead of one whole refresh early |",
    "Give Windows the adapting lead that Android has, so a picture is started as late as it safely can be instead of one whole refresh early. MEASURED: at 240 Hz a picture is on the display two refreshes after its moment on Direct3D and one on Vulkan (M2); with in-between pictures off, a frame's present call blocks for 16.4 ms (M1) |")
# order of work and decisions
rep("| 2 | The four changes that depend on nothing: start the sequence when the list arrives (G1); the lead at multiples of 60 (G2); recovery after a suspend (G6); only even rates offered (G6) | Phase 4 |",
    "| 2 | The changes that depend on nothing: start the sequence when the list arrives (G1); the lead at multiples of 60 (G2); recovery after a suspend (G6); only even rates offered (G6); on Direct3D, stop blocking a refresh per picture (M2); with in-between pictures off on a fast display, present at once (M1) | Phase 4 |")
rep("Items 2 and 4 are where the player feels the difference first: about 15 to 20 ms less delay and a pointer that follows the hand.",
    "Items 2 and 4 are where the player feels the difference first. By the measurements: about 20 ms less delay with in-between pictures on (42 to about 22 ms on Direct3D at 240 Hz), about 20 ms less with them off on a fast display (29 to about 5 ms), and a pointer that follows the hand.")
rep("2. **May I make the visible-window runs** on the PC (and the tablet) to turn the six SUSPECTED items into measurements?",
    "2. **Done on the PC** (section 1A). **For the tablet:** to measure in-between pictures there, power saving has to be off and the display set to its 120 Hz mode. Will you set that, or may I?")
rep("| How often the real frame is shown with VSync on | The existing statistics at 120 and 144 Hz, visible window |\n", "")
rep("| The real added delay | The frame timeline at 60 and 144 Hz |\n", "")
rep("| The cost of a picture on the tablet | The same statistics on the device |",
    "| The cost of a picture on the tablet, and anything at 120 Hz there | The tablet out of power saving and in its 120 Hz mode |\n| Exactly when a frame reaches the display with in-between pictures off (M1) | The swap chain's full statistics in the timeline |\n| The cause of the Vulkan hitch at a scene restart (M3) | A trace of the render thread around it |\n| Behaviour under load: the governors, dynamic resolution | A heavier setting or a weaker machine |")
fp.write_text(f, encoding="utf8")
print("ok", len(f))
