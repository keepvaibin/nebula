"""audit_fill.py REPORT_DIR TABLE.md: put the tablet and emulator tour results into the master report and appendix D."""
import pathlib
import re
import sys

R = pathlib.Path(sys.argv[1])
tab = pathlib.Path(sys.argv[2]).read_text(encoding="utf8")
table = tab[tab.index("| Stop |"):].strip()
table_head = tab[:tab.index("| Stop |")].strip()

m = (R / "00-MASTER-REPORT.md").read_text(encoding="utf8")
d = (R / "D-run-results.md").read_text(encoding="utf8")


def rep(text, old, new):
    assert text.count(old) == 1, (old, text.count(old))
    return text.replace(old, new)


m = rep(m, "TABLET_TOUR_RESULT",
        "101 of 102. The test runner printed 99: on two stops the game finished and quit cleanly, and the runner asked "
        "Android for the exit record before Android had written it (file D, D.8)")
m = rep(m, "EMULATOR_TOUR_RESULT",
        "61 of 102 stops run when this was written: 60 pass, counting one false failure of the same runner kind; the run "
        "is still going and this row is updated when it ends")
m = rep(m, "The one failing stop is the same everywhere:", "The one stop that really fails is the same everywhere:")

fig = """The tour on the tablet also gives the first per-galaxy figures from a real device. Conditions: SM-X810 (Adreno 740), Vulkan, 2800x1752 inside and out, fixed steps run as fast as the device goes, two pictures drawn per step, 600 frames of random play per stop. The device's heat state was not recorded. The full table, one row per stop, is in file D (D.11).

| Measure | Figure |
|---|---|
| Game steps a second, middle value over 101 stops | 164 |
| The stops' own middle values | 65 to 229 |
| Lowest single second in any stop | 60 (`OceanPhantomCaveGalaxy` 4) |
| Stops whose lowest second was under 120 steps | 26 of 101 |
| GPU time a picture, middle value over 86 stops | 5.13 ms |
| GPU time a picture, the five highest | 14.47 (`OceanPhantomCaveGalaxy` 4), 11.75 and 10.28 (`OceanRingGalaxy` 2 and 1), 9.72 (`KoopaBattleVs1Galaxy`), 9.30 (`KoopaBattleVs3Galaxy`) |
| Times the full-size scene is opened per picture | 2.4 to 8.9, middle 2.8; plus one full-size copy in every picture |
| Longest single picture on the GPU | up to 77 ms; over 16.7 ms at least once in 36 of 86 stops |

What these do and do not say:

- **On this tablet, 60 Hz is covered everywhere.** No stop fell under 60 steps a second while also drawing two pictures per step at full resolution.
- **120 pictures a second is not covered at full resolution.** That needs every picture under 8.3 ms, and five stops are over it on GPU time alone.
- **This is the top of the range.** The figures bound a weaker phone from above and say nothing more about it. A low-end phone has to be measured.
- **The scene is reopened far more than it needs to be.** In the three stops with 8 to 9 openings (`FlipPanelExGalaxy`, `DarkRoomGalaxy`, `KoopaBattleVs2Galaxy`) the log names the cause: a switch to another target and back, 3.4 to 4.4 times a picture, each one reopening the full-size scene. This is direct evidence for planning the passes (section 8.3).
- **Pass count alone does not predict GPU time here.** 8.8 openings cost 6.08 ms; 2.5 openings cost 5.12 ms. The earlier report's claim that GPU time follows the pass count stays unproven.
- **The long single pictures cannot be read as stutter.** Each stop takes two full-size pictures for the test itself, and the first pictures of a stage are in the same window. They are a reason to measure, not a measurement.

From the earlier loader run on the same tablet: at the title, one pass costs 2.0 to 2.4 ms of GPU time; "frame work" averaged 1.11 ms with one step of 18.68 ms in a 600-frame window. That long step is the one recorded as open on 11-15. Its cause is not known (file B, round 4, lists 13 candidates and the five measurements that separate them)."""
m = rep(m, "TABLET_GPU_SECTION", fig)

m = rep(m, """- Tablet, at the title: about 2 ms of GPU time for one pass at 2800x1752; "frame work" 0.8 to 3.6 ms; an 18.68 ms longest step in one window. From the device's log.""",
        """- Tablet, in play at 2800x1752: 65 to 229 game steps a second with two pictures per step; GPU time 5.13 ms a picture in the middle stop and 14.47 ms in the worst (section 3.5). MEASURED
- Tablet, at the title: about 2 ms of GPU time for one pass; an 18.68 ms longest step in one window. From the device's log.""")
m = rep(m, """- **GPU time per pass on a phone.** The game can record it; the Android test start hard-codes recording off (one line, `Harness.java:76`).""",
        """- **GPU time per pass on a phone.** The total per picture is now measured on the tablet (3.5). The split by pass is not: the game can record it, and the Android test start hard-codes recording off (one line, `Harness.java:76`).""")
m = rep(m, """- Unit tests build on Windows only, so the Linux, Mac and Android branches of tested code are never tested (P-024).""",
        """- Unit tests build on Windows only, so the Linux, Mac and Android branches of tested code are never tested (P-024).
- The Android test runner reports a false failure in about 2 runs in 100. RUN: it reads the exit record 40 to 60 ms before Android writes it; the game's own log shows a clean exit each time.""")

d = rep(d, "TABLET_TOUR_ROW", "101 of 102 (the runner printed 99; two are false failures, D.8)")
d = rep(d, "EMULATOR_TOUR_ROW",
        "61 of 102 run when this was written: 60 pass (one of them a false failure of the same kind); still running")
d = rep(d, "TOUR_DEVICE_SECTION", """**Tablet (Vulkan, Adreno 740): 101 of 102.** The runner printed 99 pass and 3 not:

| Stop | Runner's verdict | What happened |
|---|---|---|
| `030-FactoryGalaxy-4` | setup failed | The same stuck mission as on the PC |
| `024-EggStarGalaxy-2` | fail: "the game quit by itself with exit 0 (actual: null)" | False. The run reached its last frame, both pictures were taken, and the device log has "Process 12172 exited cleanly (0)". The runner asked for the exit record at 01:39:45.040; Android wrote it at 01:39:45.080 |
| `095-SurfingLv1Galaxy-1` | the same | False in the same way: asked at 02:04:30.452, written at 02:04:30.514 |

**Emulator (Vulkan, x86-64): 61 of 102 stops run when this was written.** 59 pass by the runner; `030-FactoryGalaxy-4` is the stuck mission; `040-HeavensDoorGalaxy-1` is a false failure of the same kind (the log has "Process 4141 exited cleanly (0)"). The emulator takes about two minutes a stop. The run continues.""")
d = rep(d, "TABLET_FIGURES_SECTION", """Figures from the tablet's tour are in D.11. From the loader run: at the title one pass costs 2.0 to 2.4 ms of GPU time at 2800x1752; in the suite's scenes 2.2 to 2.6 passes and one copy cost 6.6 to 7.0 ms; "frame work 1.11 ms average, 18.68 ms longest"; "600 frames, 600 on the display's vsync (60.0 Hz)". On the emulator the game's work averaged 17 to 20 ms a frame; that is the emulator's CPU, not a device figure.

The test app was removed from the tablet when its runs ended (`pm uninstall --user 0 dev.nebula.galaxy.test`); the installed Nebula is still there. The emulator still has it while its tour runs.""")
d = rep(d, "| The Android suite runner | ANDROID_RUNNER_NOTE | |",
        "| The Android suite runner (`agents\\luma\\d\\platforms\\android\\suite_android.py`) | Reports \"the game quit by itself "
        "with exit 0 (actual: null)\" on a run that ended cleanly: 2 of 102 on the tablet, 1 of 61 on the emulator | It reads "
        "`dumpsys activity exit-info` 40 to 60 ms before Android records the exit |")
d += """
## D.11 Tablet figures per stop

From the game's own log lines during the tablet tour (`scripts\\audit_tablet_figures.py`). SM-X810, Adreno 740, Vulkan, 2800x1752 inside and out. The harness runs fixed steps as fast as the device goes and the renderer draws two pictures per step, so "steps a second" is how fast the whole game and renderer ran, counted over the seconds that lie inside the 600 played frames. GPU figures are the renderer's last logged window of 600 pictures in that stop; a stop with no such line is blank. "Passes" is how many times the full-size scene was opened per picture; "copies" is full-size copies per picture.

The longest-picture column includes the two full-size pictures each stop takes for the test itself and the first pictures of a stage. It is not a stutter count.

""" + table_head + "\n\n" + table + "\n"

(R / "00-MASTER-REPORT.md").write_text(m, encoding="utf8")
(R / "D-run-results.md").write_text(d, encoding="utf8")
for name, text in (("master", m), ("D", d)):
    left = re.findall(r"\b[A-Z]+(?:_[A-Z]+)+_(?:RESULT|SECTION|ROW|NOTE)\b", text)
    print(name, "placeholders left:", left, "chars", len(text))
