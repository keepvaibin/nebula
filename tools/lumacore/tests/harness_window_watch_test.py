"""harness_window_watch_test.py: the watch of a visible run's window (P-328, T7a): which looks of the desktop are "minimized",
"active", "pointer" and "covered", and the file window-watch.txt that says so. No game, no desktop: a fake look.

python -X utf8 -I tools/lumacore/tests/harness_window_watch_test.py [--work <folder>]

A window is "covered" when less than half of the game's window is visible: the visible windows above it (not minimized, not
cloaked, not a click-through overlay), each subtracted as a rectangle. The test command line carries --no-focus 1 for every
game the runner starts.
Exit 0 when every case holds.
"""
import pathlib
import sys
import tempfile
import threading

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import luma_test  # noqa: E402

failed = []


def check(ok, what):
    if not ok:
        failed.append(what)


def win(pid, rect, visible=True, minimized=False, cloaked=False, through=False):
    return {"pid": pid, "visible": visible, "minimized": minimized, "cloaked": cloaked, "rect": rect, "through": through}


class Desktop:
    def __init__(self, windows, foreground=0, pointer=0):
        self.list, self.fg, self.pt = windows, foreground, pointer

    def foreground_pid(self):
        return self.fg

    def pointer_pid(self):
        return self.pt

    def windows(self):
        return self.list


ME = {100}
GAME = (100, 100, 1100, 700)  # 1000 x 600

# ---- The visible share: rectangles subtracted ----
check(luma_test.visible_share(GAME, []) == 1.0, "nothing above: all visible")
check(luma_test.visible_share(GAME, [(0, 0, 50, 50)]) == 1.0, "a rectangle that does not touch it")
check(luma_test.visible_share(GAME, [(100, 100, 600, 700)]) == 0.5, "the left half covered: half")
check(luma_test.visible_share(GAME, [(0, 0, 2000, 2000)]) == 0.0, "all of it covered: none")
check(abs(luma_test.visible_share(GAME, [(100, 100, 600, 700), (100, 100, 1100, 400)]) - 0.25) < 1e-12,
      "two overlapping rectangles are subtracted once where they overlap: a quarter is left")
check(abs(luma_test.visible_share(GAME, [(100, 100, 600, 400), (600, 400, 1100, 700)]) - 0.5) < 1e-12,
      "two corners: half")
check(abs(luma_test.visible_share(GAME, [(350, 250, 850, 550)]) - (1 - 500 * 300 / 600000)) < 1e-12,
      "a hole in the middle")
check(luma_test.visible_share((5, 5, 5, 5), [(0, 0, 9, 9)]) == 1.0, "a window with no area is not covered")

# ---- The kinds of one look ----
look = lambda ws, fg=0, pt=0: luma_test.window_kinds(Desktop(ws, fg, pt), ME)  # noqa: E731
check(look([win(100, GAME)]) == [], "alone on the desktop: nothing")
check(look([win(100, GAME)], fg=100) == ["active"], "the foreground window is the run's: active")
check(look([win(100, GAME)], pt=100) == ["pointer"], "the pointer over it: pointer")
check(look([win(100, GAME, minimized=True)]) == ["minimized"], "minimized")
check(look([win(7, (0, 0, 2000, 2000)), win(100, GAME)]) == ["covered"], "a window over all of it: covered")
# 100..700 x 100..700 covers 600 x 600 of 1000 x 600 = 0.6: 0.4 left, under half: covered.
check(look([win(7, (100, 100, 700, 700)), win(100, GAME)]) == ["covered"], "six tenths covered: covered")
check(look([win(7, (100, 100, 600, 700)), win(100, GAME)]) == [], "exactly half visible is not covered (less than half is)")
check(look([win(7, (100, 100, 599, 700)), win(100, GAME)]) == [], "a little under half covered: not covered")
check(look([win(7, (100, 100, 601, 700)), win(100, GAME)]) == ["covered"], "a little over half covered: covered")
# Windows that hide nothing do not count.
big = (0, 0, 2000, 2000)
check(look([win(7, big, visible=False), win(100, GAME)]) == [], "an invisible window above it hides nothing")
check(look([win(7, big, minimized=True), win(100, GAME)]) == [], "a minimized window above it hides nothing")
check(look([win(7, big, cloaked=True), win(100, GAME)]) == [], "a cloaked window above it hides nothing")
check(look([win(7, big, through=True), win(100, GAME)]) == [], "a click-through overlay above it hides nothing")
check(look([win(7, (0, 0, 0, 0)), win(100, GAME)]) == [], "a window with no area hides nothing")
# Only the windows above it (in front of it in the order) count; the ones behind do not.
check(look([win(100, GAME), win(7, big)]) == [], "a window behind it hides nothing")
# Two windows each under half, together over half.
check(look([win(7, (100, 100, 500, 700)), win(8, (500, 100, 700, 700)), win(100, GAME)]) == ["covered"],
      "two windows together cover more than half")
# Kinds together.
check(look([win(7, big), win(100, GAME)], fg=100, pt=100) == ["active", "pointer", "covered"], "several kinds at once")
# The run's other process (the host's start in its place) counts as the run's.
check(luma_test.window_kinds(Desktop([win(101, GAME)], foreground=101), {100, 101}) == ["active"], "the restarted process")
# Another visible window of the run's own is not 'above' it: the run does not cover itself.
check(look([win(100, (0, 0, 2000, 2000)), win(100, GAME)]) == [], "its own windows do not cover each other")

# ---- window-watch.txt ----
work = pathlib.Path(sys.argv[sys.argv.index("--work") + 1]) if "--work" in sys.argv else pathlib.Path(tempfile.mkdtemp())
work.mkdir(parents=True, exist_ok=True)


class Ticker(threading.Event):
    """`done` that says no for `looks` waits and then yes, and a clock that moves a second a wait."""
    def __init__(self, looks):
        super().__init__()
        self.looks, self.t = looks, 0.0

    def wait(self, timeout=None):
        self.t += 1.0
        self.looks -= 1
        return self.looks < 0


def watched(folder, scripted, pointer=None):
    """The file the watch writes for a script of looks (each the windows' list); `pointer`: the pointer's position at each look
    and the process under it (default: still, over nothing of the run's)."""
    folder.mkdir(parents=True, exist_ok=True)
    ticker = Ticker(len(scripted))
    looks = iter(scripted)
    spots = iter(pointer or [((0, 0), 0)] * len(scripted))
    state = {}

    class Scripted:
        def foreground_pid(self):
            return 0

        def pointer_at(self):
            state["pos"], state["pid"] = next(spots)
            return state["pos"]

        def pointer_pid(self):
            return state["pid"]

        def windows(self):
            return next(looks)

    luma_test.watch_window(folder, 100, ticker, Scripted(), lambda: ME, now=lambda: ticker.t, every=0.25)
    path = folder / "window-watch.txt"
    return path.read_text(encoding="utf-8") if path.exists() else None


clear = [win(100, GAME)]
cover = [win(7, big), win(100, GAME)]
check(watched(work / "clear", [clear] * 8) is None, "a run nobody reached writes no file")
text = watched(work / "covered", [clear, clear, cover, cover, cover, clear])
check(text == "covered first at 3.0 s, 3 of the quarter seconds\n", "a covered window: the first time and the count: %r" % text)
check(watched(work / "minimized", [clear, [win(100, GAME, minimized=True)]]) == "minimized first at 2.0 s, 1 of the quarter seconds\n",
      "the old kinds still written in the same form")

# A pointer that rests under the window reaches nothing; one that moves over it does (the owner's pointer sits where the window
# comes up: all four renderers' first real runs were "not trusted" for it, 732 of 743 looks).
rest = [((1351, 1156), 100)] * 6
check(watched(work / "resting", [clear] * 6, rest) is None, "a pointer that does not move under the window: nothing written")
move = [((10, 10), 100), ((11, 10), 100), ((11, 10), 100), ((12, 11), 100), ((12, 11), 100), ((12, 11), 100)]
text = watched(work / "moving", [clear] * 6, move)
check(text == "pointer first at 2.0 s, 2 of the quarter seconds\n", "a pointer that moves over the window: written: %r" % text)
elsewhere = [((10, 10), 7), ((11, 10), 7), ((12, 10), 7), ((13, 10), 7), ((14, 10), 7), ((15, 10), 7)]
check(watched(work / "elsewhere", [clear] * 6, elsewhere) is None, "a pointer that moves over another program's window: nothing")
check(luma_test.window_kinds(Desktop([win(100, GAME)], pointer=100), ME, pointer_moved=False) == [], "window_kinds: a pointer that did not move")
check(luma_test.window_kinds(Desktop([win(100, GAME)], pointer=100), ME, pointer_moved=True) == ["pointer"], "window_kinds: one that did")

# ---- The command line of every game the runner starts carries --no-focus 1 ----
source = (HERE.parent / "luma_test.py").read_text(encoding="utf-8")
check('command += ["--no-focus", "1"]' in source and source.index('command += ["--no-focus", "1"]') <
      source.index('    hidden = {}\n    if test.get("visible")'), "the runner passes --no-focus 1 before it decides hidden or visible")

print("\n".join(failed) if failed else "window watch: every case holds")
sys.exit(1 if failed else 0)
