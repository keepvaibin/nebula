"""audit_fix3.py REPORT_DIR: the frame and in-between-picture sections, and the tablet as the device result."""
import pathlib
import sys

R = pathlib.Path(sys.argv[1])
mp = R / "00-MASTER-REPORT.md"
dp = R / "D-run-results.md"
m = mp.read_text(encoding="utf8")
d = dp.read_text(encoding="utf8")


def rep(text, old, new):
    assert text.count(old) == 1, (old[:70], text.count(old))
    return text.replace(old, new)


# ---- master: the emulator row, the tablet as the device result
m = rep(m, "| Emulator (x86-64, Vulkan) | 61 of 102 stops run when this was written: 60 pass, counting one false failure of the same runner kind; the run is still going and this row is updated when it ends |",
        "| Emulator (x86-64, Vulkan) | Stopped at your request once the tablet's tour was complete; the tablet is the device result. Of the 61 stops the emulator ran cleanly, 60 passed (one misreported by the same runner fault) and one was the stuck mission. After that the emulator's own Android system hung and restarted, so its later stops are not valid |")
m = rep(m, "Run on three Windows renderers, the tablet and the emulator |", "Run on three Windows renderers and the tablet |")

# ---- master: 1.1 rework rows
m = rep(m, "**Rework (8)**", "**Rework (9)**")
m = rep(m, "| How a frame's passes are planned | Load, store and merging chosen before drawing | `GameScene::draw3D`; the Vulkan and Metal renderers. Section 8.3 |",
        "| The frame itself. This is an overhaul; it is listed here and not under rewrite only because the frame list's format and the four back ends survive it | The game states what each pass reads and writes; a planner orders and merges the passes and chooses load and store; nothing interrupts the main scene | `GameScene::draw3D` and where the game emits its target passes and frame copies; `draw_walker.cpp` (the pass handling, :1517-1630, 1860-2060); the Vulkan and Metal pass code. Section 8.3 |\n"
        "| In-between pictures (interpolation) | The design stays. The blending moves to the GPU; camera and pointer are read per picture; what does not change between two steps is not drawn again | `draw_walker.cpp:177-489`, `compose.cpp:170-228`, `frame_interpolation.cpp`. Section 8.6 |")

# ---- master: section 6 row
m = rep(m, "| Interpolation | Recently rewritten | Keep; fix | P-174 (one suspend turns it all off) |",
        "| In-between pictures (interpolation) | The design is the modern one; the implementation is not yet at modern standard, and nothing measures it in play | Keep the design; rework the implementation; fix the faults | Section 8.6 |")

# ---- master: the frame subsection, before 8.4
frame = """**The frame itself, and why it needs an overhaul**

*What it is today.* The game writes each frame as a list of passes in the order the console drew them: clear, draw the scene, copy the screen for effects that read it, draw more, bloom, then the 2D layer (`GameScene::draw3D`). Render-to-texture passes sit in that list wherever the console did them. The renderer plays the list as written. One console habit is still reproduced on purpose: a frame copy asked for before the frame's first draw returns the last shown picture, as the console's frame buffer would have (P-021).

*What that costs, by renderer (file B, round 3, read from the code).*

| Renderer | A typical frame with bloom, no MSAA | Waste the reader marked as changing no pixel |
|---|---|---|
| Direct3D 12 | 3 full-size draws, no copies | Little: it lends the scene image instead of copying |
| Direct3D 11 | 3 full-size draws | Little |
| Vulkan | 4 pass instances, plus 1 for each render-to-texture pass, plus 1 for each frame copy | Every pass stores everything; up to 3 depth stores nothing reads; with MSAA a resolve at every pass end |
| Metal | 4, on the same pattern | Depth and stencil stored the same way |

On a PC GPU an extra pass costs little. On a phone GPU each end and reopen of the scene is a full store and load of the picture.

*What the tablet measured (RUN).* The full-size scene was opened 2.4 to 8.9 times per picture, with one full-size copy in every picture. In the stages at 8 to 9, the log names the cause: the list leaves the scene for another target and comes back 3.4 to 4.4 times per picture. With in-between pictures on, all of it is done again for each one.

*What a modern engine does.* The game does not give an order; it states what each pass reads and writes. A planner then builds the frame:

| Rule | Today | After |
|---|---|---|
| Render-to-texture passes run before the main scene | They interrupt it, up to 4 times a picture | The main scene opens once |
| The scene is copied only when something visible reads it, once, at one agreed point | One copy in every picture on the tablet | No copy in most pictures |
| Load and store are chosen per pass from what is read later | Vulkan and Metal store everything | Nothing stored that nobody reads |
| MSAA resolves once | At every pass end on Vulkan | Once |
| Bloom's composite, the 2D layer and the final picture share one pass where no upscaler sits between | Separate | Merged on phone GPUs |
| "The last picture" is asked for by name | Whatever was shown last, which can be an in-between picture (P-021) | The last real game frame |

*What changes in the code.* `GameScene::draw3D` and the places where the game emits target passes and frame copies; the walker's pass handling (`draw_walker.cpp:1517-1630, 1860-2060`); the pass code of the Vulkan and Metal renderers. The frame list's 15 pass kinds mostly stay; each gains what it reads and writes.

*How it is proved.* The tour compares every stop's pictures before and after on each renderer, and the tablet reports GPU time per pass (one line to turn on, `Harness.java:76`). Without both, this change cannot be trusted. That is why it comes after phase 0.

"""
m = rep(m, "### 8.4 The game's own code, ranked (read, not timed)", frame + "### 8.4 The game's own code, ranked (read, not timed)")

# ---- master: 8.6, before section 9
interp = """
### 8.6 In-between pictures (interpolation): is it at modern standard?

**The design is. The implementation is not yet, and nothing has measured it in play.** When I wrote "keep" I meant the design. This section replaces that one word.

*What it does.* The game steps 60 times a second and writes one frame list per step. For a display faster than 60 Hz the host draws extra pictures between the two newest steps: it pairs each object in the two lists by numbers the game writes, blends positions, poses and material values at the wanted moment, and draws the whole scene again. No game code runs twice (file B, R8).

*What is at modern standard.*

| Part | Why |
|---|---|
| Fixed simulation step with drawn in-between pictures | This is how modern engines separate game speed from display rate. Every in-between picture is a real drawing of the scene, so it has none of the smearing that picture-space frame generation has |
| Objects paired by identity, not guessed from the picture | The game writes slot and generation numbers (`Model.cpp:637-638`, `Material.cpp:990-993`) |
| A slow moment skips missed slots instead of bursting | Read whole and found correct (`frame_interpolation.cpp`) |
| Camera cuts are detected so two unrelated views are not blended | Present; its thresholds are a fault below |

*What is not.*

| Gap | What a modern engine does | Finding |
|---|---|---|
| The blending is done on the CPU, per draw, for every picture shown. Its cost grows with the display rate; on the PC the render thread is already the named limit | Send the previous and the current step's matrices and poses once per step; blend in the vertex shader with one number per picture | P-156; step 5 of the render data path rewrite |
| Every in-between picture repeats every pass: render-to-texture passes, bloom, all of it | Draw again only what changes between two steps; reuse the rest | File B, round 3 (`draw_walker.cpp:1616-1630, 1860-2060`). How much can be reused has to be measured per pass |
| It shows the world one step late (about 17 ms) by design, and the pointer, the mod camera look and the menu are read once per step on top of that | Read the camera and the pointer for each picture, just before it is drawn | P-078, P-163 |
| One slow stretch with a temporal upscaler on turns in-between pictures off until a setting changes | Come back when the cost fits again | P-174 |
| A fast flick of a mod camera is treated as a cut (the tests are 30 and 60 degrees in one step) | Cuts are stated by the game or the mod, not inferred from speed | M-068 |
| A frame copy taken before the frame's first draw can be an in-between picture, so the result differs with the display rate | The copy is the last real game frame | P-021 |
| Several timers count pictures, so they run 2 to 4 times too often at 120 to 240 Hz | Count time or steps | P-041 |
| The benchmark counts in-between pictures as frames | Report steps and shown pictures apart | P-089, P-090 |

*What is not known.* Whether every kind of draw blends correctly in play (particles, 2D layers, mirrors, mod draws) and whether pictures reach the display evenly. The code that does it was read completely. Nobody has measured it on a visible display in a stage: the one script that compared pictures with it on and off (`interp_real.sh`) is among the dead tools, and the harness runs fixed steps only.

*Verdict.* Keep the design. Rework the implementation in three parts: the blending moves to the GPU with the render data path rewrite (8.3, step 5); camera and pointer are read per picture; passes that do not change between steps are not drawn again. Fix the five faults. Prove it with phase 0's real-time mode and picture comparison.
"""
m = rep(m, "\n---\n\n## 9. What changed in gameplay behaviour (your decisions)", interp + "\n---\n\n## 9. What changed in gameplay behaviour (your decisions)")

# ---- master: phases
m = rep(m, "Section 8.3's five steps, then the pass planning, then 8.4's list.",
        "Section 8.3's five steps, then the frame overhaul (8.3), then the in-between pictures rework (8.6), then 8.4's list.")

# ---- D
d = rep(d, "61 of 102 run when this was written: 60 pass (one of them a false failure of the same kind); still running",
        "Stopped at the owner's request after the tablet's tour: 61 valid stops, 60 pass (one misreported by the runner), 1 the stuck mission")
old_emu = d[d.index("**Emulator (Vulkan, x86-64): 61 of 102 stops run when this was written.**"):d.index("**The stuck stop, by probe.**")]
d = rep(d, old_emu, """**Emulator (Vulkan, x86-64): stopped at the owner's request; the tablet is the device result.** Of the first 61 stops, 59 pass by the runner; `030-FactoryGalaxy-4` is the stuck mission; `040-HeavensDoorGalaxy-1` is a false failure of the same kind (the log has "Process 4141 exited cleanly (0)"). At stop 62 the game's log stops right after the Vulkan device is made, the run timed out at 240 s, and 26 s later Android's own watchdog restarted the emulator's system (02:04:15). The stops after that failed at setup while the system was coming back and are not results. The same stop passes on the tablet and on all three PC renderers.

""")
d = rep(d, "The emulator still has it while its tour runs.", "It was removed from the emulator as well.")
d = rep(d, "- **The emulator had too little room**",
        "- **The emulator's Android system hung and restarted** at 02:04 during its tour (above). Whether the game's renderer start set it off is not known; the emulator had been up for 39 hours.\n- **The emulator had too little room**")

mp.write_text(m, encoding="utf8")
dp.write_text(d, encoding="utf8")
print("ok", len(m), len(d))
