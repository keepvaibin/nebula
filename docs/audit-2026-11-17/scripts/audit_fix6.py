import pathlib, sys
p = pathlib.Path(sys.argv[1]) / "00-MASTER-REPORT.md"; m = p.read_text(encoding="utf8")
def rep(old, new):
    global m
    assert m.count(old) == 1, (old[:60], m.count(old))
    m = m.replace(old, new)
rep("Derived figures at 144 Hz: about 42 ms from input to display today, about 23 ms after | Fix, small |",
    "MEASURED at 240 Hz on Direct3D 12 with VSync on: the wait is 18.3 ms and a frame is whole on the display 42.4 ms after its step starts; about 22 to 26 ms after the fix | Fix, small |")
rep("*Verdict.* Keep the scheduler and the blending rules.",
    "*Measured on 11-17* (file F, section 1A; ten real-time runs on the PC with a visible window, three on the tablet). The motion is even: at 240 Hz every one of 10,565 presents was exactly one refresh after the one before, and the game ran 60.000 steps a second with no step over 18 ms on Direct3D. The measurements also found four things the reading had not: with in-between pictures off and VSync on, a frame's present call blocks for 16.4 ms on a fast display (M1); Direct3D shows a picture two refreshes after its moment where Vulkan takes one (M2); Vulkan had one repeatable 50 to 60 ms hitch when a restarted scene first showed (M3); the render thread spins while it waits (M4). The tablet stayed at 60 Hz (power saving, standard display rate), so in-between pictures were not measured there.\n\n*Verdict.* Keep the scheduler and the blending rules.")
rep("- **Stutter in play.** No tool can measure capped pacing in a stage (4.1). Until one exists, the only capture is you playing with recording on.",
    "- **Stutter in play, beyond one stage on one PC.** No tool in the repositories can measure pacing in a stage (4.1). For this audit I built a kit that can (`scripts\audit_pace.sh`) and measured Honeyhive on the PC: even pacing, no long step on Direct3D, one repeatable hitch on Vulkan (file F, section 1A). Not measured: other galaxies, play under load, any phone at 120 Hz, a low-end device.")
rep("| `F-interpolation-and-frame-unlock.md` | My judgement of both against the modern standard, the nine gaps, the target design and how to prove it |",
    "| `F-interpolation-and-frame-unlock.md` | My judgement of both against the modern standard, the nine gaps, the target design and how to prove it, with the 11-17 real-time measurements |\n| `G-pacing-runs-digest.txt` | The raw figures of those ten PC runs |")
p.write_text(m, encoding="utf8"); print("ok")
