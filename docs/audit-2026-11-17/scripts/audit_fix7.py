"""audit_fix7.py REPORT_DIR: the tablet's 120 Hz measurements into file F and the master report."""
import pathlib
import sys

R = pathlib.Path(sys.argv[1])
fp = R / "F-interpolation-and-frame-unlock.md"
f = fp.read_text(encoding="utf8")


def rep(text, old, new):
    assert text.count(old) == 1, (old[:70], text.count(old))
    return text.replace(old, new)


start = f.index("### Tablet\n")
end = f.index("### Still not measured")
tablet = """### Tablet (SM-X810, Adreno 740, Vulkan, 2800x1752), at 120 Hz and at 60 Hz

After you set the tablet to its 120 Hz mode with power saving off, I ran seven runs: a light galaxy (Honeyhive) and the heaviest stop of the tour (`OceanPhantomCaveGalaxy` 4), each at 120 Hz with in-between pictures and at 60 Hz without. Mario stands still in these runs: scripted input is not available on Android outside the fixed-step harness. The battery stayed at 24 C and the system reported no heat limit. Results: `D:\\NebulaWork\\build\\audit-pace-tablet\\digest.txt`. The test app is removed again; your install was not touched. You can put the tablet back to power saving.

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

"""
f = f[:start] + tablet + f[end:]
f = rep(f, "A variable-refresh display (whether this monitor's is on was not checked), Linux, a Mac, the tablet at 120 Hz, and any case under load.",
        "A variable-refresh display (whether this monitor's is on was not checked), Linux, a Mac, a low-end phone, and the tablet with Mario moving.")
f = rep(f, "| Does the picture count fall under load? | Not tested: this PC never came near its budget | still not known |",
        "| Does the picture count fall under load? | Not on the PC, which never came near its budget. On the tablet it does not fall when it should, and falls when it should not (T3, T4 below) | not known |")
f = rep(f, "Three runs were made (SM-X810, Vulkan). Two things limited them:", "Three runs were made (SM-X810, Vulkan). Two things limited them:") if "Three runs were made (SM-X810, Vulkan). Two things limited them:" in f else f
# gap table in section 1: sizes by measurement
f = rep(f, "| G2 | Every in-between picture is a whole second frame on the CPU and the GPU, and with VSync on one more unseen frame is drawn per step | Lower rates than the hardware can give; phones first | Large |",
        "| G2 | Every in-between picture is a whole second frame on the CPU and the GPU, and with VSync on one more unseen frame is drawn per step | MEASURED on the tablet: it cannot hold 120. 105 pictures a second in a light galaxy, 72 in the heaviest, though the GPU's own time fits | Large |")
f = rep(f, "| G6 | Two governors do not know of each other; rates are offered that the display cannot show evenly | A 120 target on a 144 Hz display silently gives 72 | Medium |",
        "| G6 | Two governors do not know of each other and neither watches what is shown; rates are offered that the display cannot show evenly | MEASURED on the tablet: an uneven 72 where a steady 60 was possible; in-between pictures switching off on the GPU's clock speed and staying off | Large on phones |")
# G6 table rows
f = rep(f, "| No heat rule on phones | file E, I.2 | Feed the system's thermal headroom to the arbiter |",
        "| No heat rule on phones | file E, I.2 | Feed the system's thermal headroom to the arbiter |\n"
        "| Nothing watches what is shown | MEASURED on the tablet (T3): 72 uneven pictures a second in the heavy stop, with no governor acting and dynamic resolution idle | The arbiter judges by the share of pictures on time, and falls to a steady 60 when 120 cannot be held |\n"
        "| The GPU reading follows the GPU's clock speed | MEASURED (T4): the same picture reads 5 ms or 11 to 13 ms, so in-between pictures switch off after a loading screen and may never return | Hold the clock with the system's performance hint; judge a return by trying it, as the cadence governor already does |\n"
        "| The capped state is uneven | MEASURED (T5): 60 pictures on the 120 Hz display drift to one-then-three refreshes | Place the capped pictures on every second refresh, by the same display clock |")
f = rep(f, "| Android, Vulkan | `VK_GOOGLE_display_timing`, the system's vsync timeline, a frame-rate vote. Good |",
        "| Android, Vulkan | `VK_GOOGLE_display_timing`, the system's vsync timeline, a frame-rate vote. Good: MEASURED, at 60 Hz every picture appeared at the time asked. But the lead it settles on is three refreshes, 50 ms (T6) |")
f = rep(f, "2. **Done on the PC** (section 1A). **For the tablet:** to measure in-between pictures there, power saving has to be off and the display set to its 120 Hz mode. Will you set that, or may I?",
        "2. **Done** on the PC and on the tablet at 120 Hz (section 1A). You can put the tablet back to power saving.")
f = rep(f, "| The cost of a picture on the tablet, and anything at 120 Hz there | The tablet out of power saving and in its 120 Hz mode |\n", "")
f = rep(f, "| Behaviour under load: the governors, dynamic resolution | A heavier setting or a weaker machine |",
        "| The tablet with Mario moving, and a low-end phone | Scripted input on Android outside the fixed-step harness; a second device |")
fp.write_text(f, encoding="utf8")

mp = R / "00-MASTER-REPORT.md"
m = mp.read_text(encoding="utf8")
m = rep(m, "The tablet stayed at 60 Hz (power saving, standard display rate), so in-between pictures were not measured there.",
        "On the tablet at 120 Hz (seven runs): it does not hold 120. It showed 105 pictures a second in a light galaxy with one in six a refresh late, and an uneven 72 in the heaviest stop. The GPU's own time fits (5.6 to 6.9 ms); the time from one picture to the next on the render thread does not (7.2 to 9.8 ms against an 8.33 ms slot). No governor reacts; dynamic resolution did nothing; and in-between pictures switch off after a loading screen because the GPU's clock had dropped, sometimes for good. At 60 Hz the tablet is perfectly even but shows each frame 50 ms after its step starts.")
m = rep(m, "Not measured: other galaxies, play under load, any phone at 120 Hz, a low-end device.",
        "On the tablet at 120 Hz the same kit measured real unevenness (file F, section 1A, T1 to T6). Not measured: other galaxies, a low-end device, the tablet with Mario moving.")
mp.write_text(m, encoding="utf8")
print("ok", len(f), len(m))
