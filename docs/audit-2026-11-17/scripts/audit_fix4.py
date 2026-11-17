"""audit_fix4.py REPORT_DIR: point the master report at files E and F and replace section 8.6 with their summary."""
import pathlib
import sys

R = pathlib.Path(sys.argv[1])
mp = R / "00-MASTER-REPORT.md"
m = mp.read_text(encoding="utf8")


def rep(text, old, new):
    assert text.count(old) == 1, (old[:70], text.count(old))
    return text.replace(old, new)


m = rep(m, "| `D-run-results.md` | What was run, the results, and where the logs and pictures are |",
        "| `D-run-results.md` | What was run, the results, and where the logs and pictures are |\n"
        "| `E-pacing-interpolation-as-built.md` | How the frame-rate unlock and the in-between pictures are built today, with file and line for every claim (I-001 to I-016) |\n"
        "| `F-interpolation-and-frame-unlock.md` | My judgement of both against the modern standard, the nine gaps, the target design and how to prove it |")

start = m.index("### 8.6 In-between pictures (interpolation): is it at modern standard?")
end = m.index("\n---\n\n## 9. What changed in gameplay behaviour (your decisions)")
new86 = """### 8.6 In-between pictures (interpolation) and the frame-rate unlock

**Files E and F hold the full treatment; this is the summary.** An earlier version of this section, written before the code was read closely, undersold the scheduling and overstated one fault. File F states both corrections.

*What is already at modern standard (keep).* A fixed 60-step game with drawn in-between pictures. Each picture shows the game at the moment it reaches the display, at any display rate. Pictures land on real display refreshes. A stall skips what it missed. Two governors lower the picture count under load and bring it back. Rotations are blended as rotations, still objects stay still under a moving camera, teleports are caught. Android asks the display for a time per picture.

*The nine gaps (file F, section 4).*

| # | Gap | Class |
|---|---|---|
| G1 | A finished frame waits for the next game step before its pictures start: about one step of delay that is not needed (`render_core.cpp:2615-2631`, CONFIRMED). Derived figures at 144 Hz: about 42 ms from input to display today, about 23 ms after | Fix, small |
| G2 | Every picture is a whole second frame on the CPU and GPU, and with VSync on one more unseen frame is drawn per step (I-002, I-003) | Rework, with the render data path rewrite and the frame overhaul |
| G3 | Only Android with Vulkan asks the display for a time; Linux has no display clock at all; variable-refresh displays are not really handled (I-005, I-007, I-012) | Rework, per platform |
| G4 | The game's speed follows the display within 1% (Windows) or 3% (Android, Apple) of 60 (I-001) | Fix |
| G5 | The pointer and a mod's camera look are as old as the frame; nothing is read at picture time (I-004) | Rework |
| G6 | Two governors that do not know of each other; rates offered that the display cannot show evenly, so 120 on a 144 Hz display gives 72; off for good after one slow stretch with a temporal upscaler (I-008 to I-010) | Fix |
| G7 | Camera cuts and teleports are mostly guessed from speed; the game states cuts from two places only (I-014) | Fix |
| G8 | A few counters count pictures (file E, section H) | Fix, small |
| G9 | The scheduler has no unit tests and nothing measures display times in play (I-016) | Phase 0 |

*Verdict.* Keep the scheduler and the blending rules. Rebuild three things around them: where the per-picture work is done (G2), how each platform is asked to show a picture (G3), and what is read fresh (G5). Fix the rest. File F, section 5 has the target design and the code each change touches; section 6 has the measures that prove it.
"""
m = m[:start] + new86 + m[end:]

m = rep(m, "| In-between pictures (interpolation) | The design stays. The blending moves to the GPU; camera and pointer are read per picture; what does not change between two steps is not drawn again | `draw_walker.cpp:177-489`, `compose.cpp:170-228`, `frame_interpolation.cpp`. Section 8.6 |",
        "| In-between pictures (interpolation) and the frame-rate unlock | The scheduler and the blending rules stay. Three things change around them: the per-picture work moves to the GPU, each platform is asked for a display time, and the pointer and free-look are read per picture | `draw_walker.cpp:177-489`, `compose.cpp:170-228`, `render_core.cpp:730-1319, 2579-2668`, each renderer's present code. Section 8.6 and file F |")
m = rep(m, "| In-between pictures (interpolation) | The design is the modern one; the implementation is not yet at modern standard, and nothing measures it in play | Keep the design; rework the implementation; fix the faults | Section 8.6 |",
        "| In-between pictures and the frame-rate unlock | The scheduler and blending are at modern standard; the delay, the per-picture cost, the display timing outside Android and the proof are not | Keep the scheduler; rework three parts; fix the rest | Section 8.6, files E and F |")
m = rep(m, "then the in-between pictures rework (8.6), then 8.4's list.",
        "then the in-between pictures rework (8.6 and file F), then 8.4's list.")
m = rep(m, "Section 8.5, measured with phase 0's real-time mode, fixed in the order the measurements give.",
        "Section 8.5, measured with phase 0's real-time mode, fixed in the order the measurements give. File F, section 7 gives the order for in-between pictures and the frame-rate unlock; its first four changes depend on nothing else.")
mp.write_text(m, encoding="utf8")
print("ok", len(m))
